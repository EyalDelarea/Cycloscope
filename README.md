# Cycloscope

[![CI](https://github.com/EyalDelarea/Cycloscope/actions/workflows/ci.yml/badge.svg)](https://github.com/EyalDelarea/Cycloscope/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/EyalDelarea/Cycloscope?display_name=tag&sort=semver)](https://github.com/EyalDelarea/Cycloscope/releases)
[![Downloads](https://img.shields.io/github/downloads/EyalDelarea/Cycloscope/total)](https://github.com/EyalDelarea/Cycloscope/releases)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows-lightgrey.svg)

**An audio plugin that lets you *see* your sound** — waveform, single cycle, spectrum, and stereo
image. Its signature trick, **Base Shape**, locks a periodic source (saw, sine, supersaw) into one
**stationary single cycle** so you read its true shape instead of a scrolling blur.

VST3 · AU · Standalone · universal (Apple Silicon + Intel) · Windows VST3 · built in JUCE 8.

![Cycloscope](media/demo.gif)

## What it shows

| Mode | What you get |
|---|---|
| **Live** | Real-time triggered waveform — Free/Rising/Falling trigger, sweep modes, tempo sync, time/amplitude zoom, live Vpp + RMS. |
| **Base Shape** | Auto-detected pitch; many cycles phase-locked into one clean, still cycle. A/B compare + export as a wavetable WAV. |
| **Spectrum** | 2048-point FFT, log-frequency axis, dB grid. |

Plus an always-on **stereo goniometer** (Lissajous + correlation + L/R meter), **presets**,
**freeze**, full host automation, and a dark UI with glowing-arc knobs.

## Install

> Downloads are **unsigned** — macOS Gatekeeper / Windows SmartScreen will warn about an
> "unknown developer." That's expected (signing needs paid certificates). Verify any download
> against the published `SHA256SUMS`, or build it yourself below.

<details>
<summary><b>macOS — ZIP (recommended)</b></summary>

1. Download `Cycloscope-<version>-macOS.zip` from [Releases](../../releases).
2. Unzip and copy:
   - `Cycloscope.vst3` → `~/Library/Audio/Plug-Ins/VST3/`
   - `Cycloscope.component` → `~/Library/Audio/Plug-Ins/Components/`
3. Clear the quarantine flag (**required**, or your DAW silently skips it):
   ```bash
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Cycloscope.vst3
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Cycloscope.component
   ```
4. Rescan plugins in your DAW.
</details>

<details>
<summary><b>macOS — PKG installer</b></summary>

1. Download `Cycloscope-<version>-macOS.pkg` and double-click it.
2. macOS blocks it. Open **System Settings → Privacy & Security**, scroll down, click
   **Open Anyway**, then run it again. If it still refuses:
   `xattr -dr com.apple.quarantine ~/Downloads/Cycloscope-*.pkg`
</details>

<details>
<summary><b>Windows — ZIP</b> (VST3 + Standalone)</summary>

1. Download `Cycloscope-<version>-Windows.zip` and unzip it.
2. Copy `Cycloscope.vst3` → `C:\Program Files\Common Files\VST3\`
3. Rescan in your DAW. (AU is macOS-only.)
</details>

## Build from source

Requires macOS or Windows, CMake ≥ 3.22, and a C++ toolchain. JUCE 8 is fetched by CMake.

```bash
cmake -B build
cmake --build build -j8        # Standalone + VST3 (+ AU on macOS)
ctest --test-dir build         # 8 headless DSP test suites
```

See [`CONTRIBUTING.md`](CONTRIBUTING.md) to develop and [`RELEASING.md`](RELEASING.md) to package.

## How it works

- **Audio thread** writes L/R into lock-free ring buffers and passes audio through unchanged (it's
  an analyzer), reading host BPM from the playhead.
- **GUI thread** runs the DSP at 60 fps — trigger search, MPM pitch detection, phase-locked cycle
  averaging, FFT, and the Lissajous — with cached grid/phosphor layers.
- All DSP is **header-only and JUCE-free** (`Source/*.h`), so it's unit-tested independently.

## Tips

- It's a transparent passthrough — place it **after** any effect whose output you want to see
  (e.g. after a stereo widener to read its image).
- Base Shape shows the **post-filter** audio, so open your synth's filter to see the raw harmonics.

## Contributing & license

Issues and PRs welcome — see [`CONTRIBUTING.md`](CONTRIBUTING.md),
[`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md), [`SECURITY.md`](SECURITY.md).

**GPL-3.0** ([`LICENSE`](LICENSE)) — required by JUCE's free tier. Third-party credits in
[`NOTICE.md`](NOTICE.md) (JUCE; Inter typeface — SIL OFL 1.1).
