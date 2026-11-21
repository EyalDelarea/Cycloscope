# Changelog

All notable changes to Cycloscope are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed
- **Freeze restores consistently:** a host can send the Freeze switch an in-between value (e.g.
  0.37); restoring a saved state could then leave it reporting that raw value or the switched one,
  depending on what it held before. It now always comes back as on or off (pluginval 2
  "Discrete parameter state restoration").
- **VST3 passes the Steinberg validator:** the plugin's single program had an empty name, which the
  VST3 SDK validator (run by pluginval) rejects as "Program 000 has no name"; it is now "Default".
- **Base Shape no longer hogs the host's UI thread:** pitch detection ran a direct
  O(N·lag) correlation every frame (≈10–30 ms of CPU per frame at 44.1–192 kHz, i.e. more than
  the 60 fps budget, under the host's message lock). It now uses an FFT (≈1 ms per frame),
  with identical results to the old detector; the cycle phase search is ~2× faster too.
- **Bad input no longer breaks the display:** a single block of NaN / Inf / absurdly loud
  samples from an upstream plugin left the spectrum stuck (its ballistics held NaN forever).
  The copy the analyzer reads is now sanitised; the audio itself still passes through untouched.
- **Saved-state robustness:** a host handing back another plugin's or a corrupted chunk is
  ignored instead of replacing the parameter tree; out-of-range stored window / stereo-panel
  sizes are clamped. Valid sessions (including v0.1.0 ones) load exactly as before.
- **Small windows:** the stereo panel now yields space on narrow windows instead of squeezing
  the main display to a sliver (it keeps at least half the width); your dragged panel width is
  kept, and windows wide enough for it look exactly as before.
- **Closing the window with a file dialog open** (Export / Save / Load preset) could call back
  into the deleted editor; the dialog is now dismissed with the window.
- **Preset loading** ignores XML files that are not Cycloscope presets.
- **Scope buffer thread safety:** the samples shared between the audio thread and the display
  were plain floats read and written concurrently (a data race); they are now lock-free atomics,
  with no cost or allocation on the audio thread.

### Development
- The DSP test suites' `assert()` checks were compiled out in Release (as CI builds), so they
  checked nothing; they are live again. New JUCE-level plugin tests (`-DCYCLOSCOPE_PLUGIN_TESTS=ON`,
  on in CI) cover state restore, the audio-callback contract (bit-exact passthrough, no
  allocation), hostile input and the editor lifecycle, plus `--render` / `--bench` tools.
  `-DCYCLOSCOPE_SANITIZER=address|thread|undefined` instruments the test targets.

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
