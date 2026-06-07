# Releasing Cycloscope

Releases are **unsigned** (no Apple Developer account). Cutting a release is one command;
the rest is automated.

## Cut a release

```bash
git tag v0.1.0
git push origin v0.1.0
```

`.github/workflows/release.yml` then builds a universal Release, runs the tests, and publishes
a GitHub Release with:

- `Cycloscope-<ver>-macOS.zip` — the `.vst3` + `.component` (+ Standalone `.app`)
- `Cycloscope-<ver>-macOS.pkg` — a double-click installer
- `SHA256SUMS.txt` — checksums users verify with `shasum -a 256 -c SHA256SUMS.txt`

Re-pushing the same tag updates the existing release. Auto-generated notes are grouped via
`.github/release.yml` (label your PRs `enhancement` / `bug`).

## Build the artifacts locally

```bash
cmake --build build -j8
scripts/package.sh          # -> dist/Cycloscope-<ver>.pkg
```

An unsigned `.pkg` installs after the **Privacy & Security → Open Anyway** step (or
`xattr -dr com.apple.quarantine <file>`). See the README's Installation section — that's the
user-facing guidance.

---

## Optional: code signing + notarization (only if you get an Apple Developer account)

Everything below is already scripted and is a no-op until the credentials exist. Run
`scripts/check-signing.sh` to see what's missing.

1. Apple Developer Program ($99/yr); create **Developer ID Application** + **Developer ID
   Installer** certs; make an app-specific password + note your Team ID.
2. Locally:
   ```bash
   export APP_SIGN_ID="Developer ID Application: Your Name (TEAMID)"
   export INSTALLER_SIGN_ID="Developer ID Installer: Your Name (TEAMID)"
   export NOTARY_PROFILE="cycloscope-notary"   # or APPLE_ID + TEAM_ID + NOTARY_PASSWORD
   cmake --build build -j8 && scripts/sign-and-notarize.sh
   spctl --assess --type install -vv dist/Cycloscope-*.pkg   # expect: accepted, Notarized
   ```
3. In CI, add repo secrets `MACOS_CERT_P12`, `P12_PASSWORD`, `APP_SIGN_ID`,
   `INSTALLER_SIGN_ID`, `APPLE_ID`, `TEAM_ID`, `NOTARY_PASSWORD`, and call
   `scripts/sign-and-notarize.sh` from the release workflow.
