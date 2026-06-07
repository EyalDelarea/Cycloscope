#!/usr/bin/env bash
# Build a macOS installer (.pkg) that places Cycloscope's VST3, AU and Standalone
# into the standard system locations. Works unsigned for local testing; signs the
# product archive when INSTALLER_SIGN_ID is set (a "Developer ID Installer" identity).
#
# Usage:   scripts/package.sh
# Env:     VERSION             override version (default: from CMake project, 0.1.0)
#          INSTALLER_SIGN_ID   "Developer ID Installer: Name (TEAMID)"  -> signs the pkg
#
# Output:  dist/Cycloscope-<version>.pkg
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ART="$ROOT/build/Cycloscope_artefacts"
VERSION="${VERSION:-$(sed -n 's/^project(Cycloscope VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")}"
VERSION="${VERSION:-0.1.0}"
IDENT="com.EyalDelarea.Cycloscope"
OUT="$ROOT/dist/Cycloscope-$VERSION.pkg"

# Locate bundles dynamically: their path gains a /Release/ segment when built with
# CMAKE_BUILD_TYPE set, so don't hard-code it.
find_bundle() { find "$ART" -maxdepth 3 -name "$1" -print -quit 2>/dev/null; }
VST3="$(find_bundle 'Cycloscope.vst3')"
AU="$(find_bundle 'Cycloscope.component')"
APP="$(find_bundle 'Cycloscope.app')"

for b in "$VST3" "$AU"; do
  [ -e "$b" ] || { echo "ERROR: missing artifact: $b  (build first: cmake --build build -j)"; exit 1; }
done

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
mkdir -p "$ROOT/dist" "$WORK/pkgs"

# One component pkg per format, each rooted at its real install location.
stage_component() {  # name  src  install-dir
  local name="$1" src="$2" dir="$3"
  [ -e "$src" ] || return 0
  local root="$WORK/root-$name"; mkdir -p "$root$dir"
  ditto "$src" "$root$dir/$(basename "$src")"   # ditto copies bundles cleanly
  xattr -cr "$root"                              # strip xattrs so pkgbuild emits no ._ AppleDouble entries
  pkgbuild --quiet --root "$root" --install-location "/" \
           --identifier "$IDENT.$name" --version "$VERSION" \
           "$WORK/pkgs/$name.pkg"
  echo "  staged $name"
}

echo "Packaging Cycloscope $VERSION..."
stage_component vst3       "$VST3" "/Library/Audio/Plug-Ins/VST3"
stage_component au         "$AU"   "/Library/Audio/Plug-Ins/Components"
stage_component standalone "$APP"  "/Applications"

# Distribution wrapper combining the components into one installer.
cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>Cycloscope $VERSION</title>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <choices-outline>
    <line choice="vst3"/><line choice="au"/><line choice="standalone"/>
  </choices-outline>
  <choice id="vst3"       title="VST3 plug-in"        visible="true"><pkg-ref id="$IDENT.vst3"/></choice>
  <choice id="au"         title="Audio Unit (AU)"     visible="true"><pkg-ref id="$IDENT.au"/></choice>
  <choice id="standalone" title="Standalone app"      visible="true"><pkg-ref id="$IDENT.standalone"/></choice>
  <pkg-ref id="$IDENT.vst3"       version="$VERSION">pkgs/vst3.pkg</pkg-ref>
  <pkg-ref id="$IDENT.au"         version="$VERSION">pkgs/au.pkg</pkg-ref>
  <pkg-ref id="$IDENT.standalone" version="$VERSION">pkgs/standalone.pkg</pkg-ref>
</installer-gui-script>
XML

SIGN_ARGS=()
if [ -n "${INSTALLER_SIGN_ID:-}" ]; then
  SIGN_ARGS=(--sign "$INSTALLER_SIGN_ID")
  echo "  signing installer as: $INSTALLER_SIGN_ID"
else
  echo "  (unsigned — set INSTALLER_SIGN_ID to sign)"
fi

productbuild --quiet --distribution "$WORK/distribution.xml" \
             --package-path "$WORK/pkgs" ${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"} "$OUT"

echo "Built: $OUT"
