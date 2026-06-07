#!/usr/bin/env bash
# Full release signing for macOS: codesign the bundles -> build a signed .pkg ->
# notarize -> staple. Each stage is gated on its credentials being present, so this
# is safe to run before you have an Apple Developer account (it just tells you what's
# missing). Run AFTER `cmake --build build -j`.
#
# Required env once you have an Apple Developer Program membership:
#   APP_SIGN_ID        "Developer ID Application: Name (TEAMID)"   -> signs bundles
#   INSTALLER_SIGN_ID  "Developer ID Installer: Name (TEAMID)"     -> signs the .pkg
# Notarization — EITHER a stored profile:
#   NOTARY_PROFILE     name created via: xcrun notarytool store-credentials
# OR explicit credentials:
#   APPLE_ID  TEAM_ID  NOTARY_PASSWORD   (app-specific password)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ART="$ROOT/build/Cycloscope_artefacts"
ENTITLEMENTS="$ROOT/scripts/entitlements.plist"

# Locate bundles dynamically (path gains a /Release/ segment under CMAKE_BUILD_TYPE).
find_bundle() { find "$ART" -maxdepth 3 -name "$1" -print -quit 2>/dev/null; }
BUNDLES=(
  "$(find_bundle 'Cycloscope.vst3')"
  "$(find_bundle 'Cycloscope.component')"
  "$(find_bundle 'Cycloscope.app')"
)

# 1) Codesign bundles (hardened runtime + secure timestamp) -----------------------
if [ -n "${APP_SIGN_ID:-}" ]; then
  echo "==> Codesigning bundles as: $APP_SIGN_ID"
  for b in "${BUNDLES[@]}"; do
    [ -e "$b" ] || continue
    codesign --force --options runtime --timestamp \
             --entitlements "$ENTITLEMENTS" \
             --sign "$APP_SIGN_ID" "$b"
    codesign --verify --strict --verbose=2 "$b"
    echo "    signed $(basename "$b")"
  done
else
  echo "==> SKIP codesign: set APP_SIGN_ID (Developer ID Application) to enable."
fi

# 2) Build the installer (signs it if INSTALLER_SIGN_ID is set) --------------------
echo "==> Building installer"
"$ROOT/scripts/package.sh"
VERSION="${VERSION:-$(sed -n 's/^project(Cycloscope VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")}"
PKG="$ROOT/dist/Cycloscope-${VERSION:-0.1.0}.pkg"

# 3) Notarize + staple -------------------------------------------------------------
NOTARY_ARGS=()
if   [ -n "${NOTARY_PROFILE:-}" ]; then
  NOTARY_ARGS=(--keychain-profile "$NOTARY_PROFILE")
elif [ -n "${APPLE_ID:-}" ] && [ -n "${TEAM_ID:-}" ] && [ -n "${NOTARY_PASSWORD:-}" ]; then
  NOTARY_ARGS=(--apple-id "$APPLE_ID" --team-id "$TEAM_ID" --password "$NOTARY_PASSWORD")
fi

if [ "${#NOTARY_ARGS[@]}" -gt 0 ] && [ -n "${INSTALLER_SIGN_ID:-}" ]; then
  echo "==> Notarizing $PKG (this can take a few minutes)"
  xcrun notarytool submit "$PKG" ${NOTARY_ARGS[@]+"${NOTARY_ARGS[@]}"} --wait
  echo "==> Stapling"
  xcrun stapler staple "$PKG"
  xcrun stapler validate "$PKG"
  echo "==> Done: notarized + stapled $PKG"
else
  echo "==> SKIP notarize: needs a signed pkg (INSTALLER_SIGN_ID) + notary creds"
  echo "    (NOTARY_PROFILE, or APPLE_ID + TEAM_ID + NOTARY_PASSWORD)."
  echo "    Unsigned installer is at: $PKG"
fi
