# Third-party notices

Cycloscope is licensed under the **GNU General Public License v3.0** (see `LICENSE`).
It builds on the following third-party components:

## JUCE 8
- License: GPLv3 (used here under the free/personal tier) — https://juce.com
- Fetched automatically at configure time via CMake `FetchContent`; not vendored.
- Cycloscope's GPLv3 licensing follows from linking JUCE under its free tier.

## Inter typeface
- © 2016 The Inter Project Authors — https://github.com/rsms/inter
- License: SIL Open Font License 1.1 (see `Assets/Inter-LICENSE.txt`)
- Vendored in `Assets/` and embedded in the plugin binary.

## Inspiration
Cycloscope's "hold a single cycle still" concept is inspired by classic single-cycle
oscilloscope plugins. No third-party plugin code is used — it is an independent JUCE 8
implementation.
