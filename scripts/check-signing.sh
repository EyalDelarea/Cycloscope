#!/usr/bin/env bash
# Signing readiness "doctor". Run this once your Apple Developer account + certs are set
# up: it auto-detects the Developer ID identities, finds their exact strings for you, and
# reports exactly what's still missing. Safe to run anytime (read-only).
#
#   scripts/check-signing.sh
set -uo pipefail

ok()   { printf "  \033[32m✓\033[0m %s\n" "$1"; }
miss() { printf "  \033[31m✗\033[0m %s\n" "$1"; }
info() { printf "    %s\n" "$1"; }

ready=1

echo "Cycloscope — signing readiness"
echo

echo "Apple Developer certificates:"
APP_ID=$(security find-identity -v -p codesigning 2>/dev/null | grep -o '"Developer ID Application:[^"]*"' | head -1 | tr -d '"')
INST_ID=$(security find-identity -v 2>/dev/null | grep -o '"Developer ID Installer:[^"]*"' | head -1 | tr -d '"')
if [ -n "$APP_ID" ]; then ok "Developer ID Application found"; info "export APP_SIGN_ID='$APP_ID'";
  else miss "Developer ID Application — not found (signs the .vst3/.component/.app)"; ready=0; fi
if [ -n "$INST_ID" ]; then ok "Developer ID Installer found"; info "export INSTALLER_SIGN_ID='$INST_ID'";
  else miss "Developer ID Installer — not found (signs the .pkg)"; ready=0; fi

echo
echo "Notarization credential:"
if [ -n "${NOTARY_PROFILE:-}" ]; then ok "NOTARY_PROFILE set ($NOTARY_PROFILE)"
elif [ -n "${APPLE_ID:-}" ] && [ -n "${TEAM_ID:-}" ] && [ -n "${NOTARY_PASSWORD:-}" ]; then ok "APPLE_ID + TEAM_ID + NOTARY_PASSWORD set"
else
  miss "No notary credential in env"
  info "either: xcrun notarytool store-credentials Cycloscope-notary --apple-id … --team-id … --password …"
  info "then:   export NOTARY_PROFILE=Cycloscope-notary"
  ready=0
fi

echo
echo "Tooling:"
command -v xcrun >/dev/null && xcrun --find notarytool >/dev/null 2>&1 && ok "notarytool available" || { miss "notarytool missing (install Xcode CLT)"; ready=0; }
command -v productbuild >/dev/null && ok "productbuild available" || { miss "productbuild missing"; ready=0; }

echo
if [ "$ready" = 1 ]; then
  printf "\033[32mReady to sign.\033[0m  Build, then run:  scripts/sign-and-notarize.sh\n"
else
  printf "\033[33mNot ready yet.\033[0m  See RELEASING.md for the one-time Apple setup.\n"
fi
