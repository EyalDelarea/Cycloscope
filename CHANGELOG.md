# Changelog

All notable changes to Cycloscope are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.1.0] — 2026-06-07

Initial public release.

### Added
- **Three display modes:** Live (triggered/scrolling waveform), Base Shape (auto-detects
  pitch and locks a periodic source into one clean, stationary cycle), and Spectrum (FFT).
- **Live mode:** Free/Rising/Falling trigger with threshold; sweep modes (Auto/Normal/Single);
  tempo sync (Off · 1/4 · 1/2 · 1 Bar); Time and Amplitude zoom; Vpp + RMS readout.
- **Base Shape:** McLeod/NSDF pitch detection, phase-locked cycle averaging, pitch/note/
  clarity readout, A/B cycle compare, and wavetable WAV export.
- **Always-on stereo goniometer:** X-Y Lissajous with peak scaling and phosphor persistence,
  correlation meter and L/R level readout with meter ballistics; resizable panel.
- **Channel source:** Mono / Left / Right / Side / Stereo (dual-trace).
- **Presets:** factory presets + save/load your own.
- **macOS:** VST3 + AU + Standalone, universal binary (Apple Silicon + Intel).
- **Windows:** VST3 + Standalone.
- Embedded Inter typeface; full host automation and state recall.
- Unsigned downloads (zip + macOS pkg) with `SHA256SUMS` published on each GitHub Release.

[Unreleased]: https://github.com/EyalDelarea/Cycloscope/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/EyalDelarea/Cycloscope/releases/tag/v0.1.0
