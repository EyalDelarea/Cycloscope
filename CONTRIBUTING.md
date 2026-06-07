# Contributing to Cycloscope

Thanks for your interest! Bug reports, ideas, and PRs are all welcome.

## Reporting bugs / requesting features

Use the issue templates (Issues → New issue). For "how do I install / use it" questions,
please use [Discussions](https://github.com/EyalDelarea/Cycloscope/discussions) instead.

## Building

Requirements: macOS, CMake ≥ 3.22, Apple Command Line Tools. JUCE 8 is fetched
automatically by CMake — nothing to install.

```bash
cmake -B build -G "Unix Makefiles"
cmake --build build -j8            # Standalone + VST3 + AU
ctest --test-dir build --output-on-failure
```

Artifacts land in `build/Cycloscope_artefacts/` and (via `COPY_PLUGIN_AFTER_BUILD`) install
to `~/Library/Audio/Plug-Ins/`. See `RELEASING.md` for packaging.

## Code style & design

- Match the surrounding code — its naming, spacing, and comment density.
- **DSP is header-only and JUCE-free** (`Source/*.h` like `RingBuffer`, `Trigger`,
  `PeriodDetector`, `StereoUtils`). Keep it that way so it stays unit-testable, and add a
  test under `Tests/` for any new DSP (wired up in `CMakeLists.txt`).
- The audio thread must stay allocation-free and lock-free; do GUI work on the timer.
- Keep PRs focused; update `CHANGELOG.md` under **Unreleased**.

## Pull requests

1. Branch off `main`.
2. Make sure `cmake --build` and `ctest` pass; test in at least one DAW.
3. Open a PR using the template and describe what you changed and how you tested it.

## Licensing of contributions

By submitting a contribution you agree to license it under the project's **GPL-3.0** license
(see `LICENSE`). This is required because Cycloscope links JUCE under its free/GPL tier.
