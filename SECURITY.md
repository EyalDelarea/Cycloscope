# Security Policy

## Unsigned builds — by design

Cycloscope releases are **not code-signed or notarized** by Apple (the project does not pay
for an Apple Developer account). macOS will warn that "the developer cannot be verified."
**This is expected and is not a sign of compromise.** The full source is here and every
release ships a `SHA256SUMS.txt` so you can verify the download matches the published build:

```bash
shasum -a 256 -c SHA256SUMS.txt
```

You can always build from source yourself instead of using the prebuilt binaries.

## Reporting a vulnerability

Please **do not** open a public issue for security problems. Use GitHub's
**[Private vulnerability reporting](https://github.com/EyalDelarea/Cycloscope/security/advisories/new)**
(Security tab → "Report a vulnerability"). I'll acknowledge within a reasonable time and
coordinate a fix and disclosure.

## Supported versions

This is pre-1.0 software; only the latest release is supported.
