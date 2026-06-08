# Changelog

All notable changes to Cycloscope are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.2.0] — 2026-06-08

Spectrum analyzer overhaul (FabFilter Pro-Q-class response) and GPU rendering.

### Added
- **Collapsible stereo panel:** a **Stereo** toggle in the top bar shows/hides the
  goniometer; when hidden it renders nothing (saves CPU). State persists with the project.

### Changed
- **Spectrum analyzer rebuilt for premium responsiveness:** instant-attack / slow-release
  per-bin ballistics (frame-rate independent), FFT computed off the paint thread, 8192-point
  FFT, upward spectral tilt (~4.5 dB/oct), constant-Q frequency smoothing, per-pixel
  log-frequency resampling, and a 0…−90 dB analyzer scale with level calibration.
- **GPU rendering:** the editor now attaches an OpenGL context, keeping the analyzer smooth
  on large / ultrawide windows.
- **Goniometer:** fixed, tuned phosphor persistence (no smearing); lighter render (30 Hz,
  decimated trace).

### Removed
- **`Decay` parameter:** goniometer persistence is now a fixed internal value — the
  automatable `stereoDecay` parameter was removed. (Old projects automating it lose that lane.)

### Performance
- Spectrum analysis is decoupled from painting, and the always-on goniometer cost is cut
  substantially (and is zero when the panel is collapsed).

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

[Unreleased]: https://github.com/EyalDelarea/Cycloscope/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/EyalDelarea/Cycloscope/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/EyalDelarea/Cycloscope/releases/tag/v0.1.0
