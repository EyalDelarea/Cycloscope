# Spectrum analyzer responsiveness — design

**Issue:** [#3](https://github.com/EyalDelarea/Cycloscope/issues/3) — Spectrum analyzer response time well behind FabFilter.
**Date:** 2026-06-08 · **Branch:** `feat/spectrum-responsive-analyzer`

## Goal
Make the **Spectrum** mode react like a premium analyzer (FabFilter Pro-Q feel): instant attack, smooth release, even resolution across the log axis, and no raster stutter on large/ultrawide windows. Fix all four validated root causes from issue #3 with the lowest-risk mechanism for each. Acceptance is a **live A/B in a DAW**, not offline.

## Non-goals (deliberately deferred — these are what got the prior attempt reverted)
- Multi-resolution FFT (low/high band split).
- A dedicated analysis **thread** (the 60 Hz timer is sufficient for a display — we always want the *latest* spectrum).
- A user-facing Speed control. Ballistics are hardcoded to a known-good spot.
- Any DSP in `processBlock`.

## Changes

### 1. Asymmetric, time-based ballistics (fixes PRIMARY cause)
Replace the symmetric EMA (`specMag*0.7 + db*0.3`, `ScopeComponent.cpp:397`) with per-bin peak ballistics:
- **Attack: instant** — if the new bin dB ≥ displayed, snap to it.
- **Release: slow, time-based** — else `displayed += (new - displayed) * releaseCoeff`, where
  `releaseCoeff = 1 - exp(-dt / tau)`, `tau ≈ 0.35 s`, and `dt` is the *actual* elapsed time since the last analysis tick.
- `dt` comes from a timestamp diff each tick, so the ballistic is **frame-rate independent** (CPU load / dropped ticks don't change the feel). First tick seeds `displayed = new`.

### 2. Move analysis off the paint path (fixes "FFT welded to paint thread")
- Add `analyse()` that does: snapshot latest `fftSize` → window → `performFrequencyOnlyForwardTransform` → per-bin dB → ballistics into `displayedDb[]`.
- Call `analyse()` from the **timer callback** (message thread), once per tick, *before* requesting a repaint — not from `paint()`.
- `paint()` (which under OpenGL is driven by the GL render thread) only **reads** a published snapshot of `displayedDb[]` and draws. Handoff is a double-buffer with an `std::atomic<int>` ready-index (analysis writes back buffer, publishes index; paint reads front). Display-only data, so a torn read is harmless but the atomic index keeps it clean.

### 3. OpenGL rendering (fixes "no GPU + heavy raster")
- `CMakeLists.txt`: link `juce::juce_opengl`.
- `PluginEditor`: own a `juce::OpenGLContext`; `attachTo(*this)` in the constructor, `detach()` in the destructor. Enable multisampling before `attachTo`. Keep JUCE component painting (no custom `OpenGLRenderer`) — the context just GPU-accelerates existing `paint`. Repaints stay driven by the scope's 60 Hz timer (no `setContinuousRepainting`).

### 4. Per-pixel log resampling + larger FFT (fixes jagged top / smeared bass)
- In `paintSpectrum`, replace the per-bin draw loop (`ScopeComponent.cpp:431-440`) with **one vertex per pixel column**: for each x, compute its frequency span `[f(x), f(x+1)]`, take the **max** displayed dB over the bins in that span (peak-preserving), and `lineTo`. Bounded by pixel width, not bin count → no overdraw at the top, no blockiness at the bottom.
- Bump FFT: `kFftOrder 11 → 12` (2048 → 4096) for better low-end resolution; affordable now that analysis is one-per-tick off the paint path.
- Keep the existing fill, but it reads the resampled curve.

## Affected files
- `Source/ScopeComponent.h` — `displayedDb` double-buffer + atomic index, `lastAnalyseTimeMs`, `analyse()` decl, `kFftOrder = 12`.
- `Source/ScopeComponent.cpp` — `analyse()` (ballistics), `timerCallback` calls `analyse()` then `repaint()`, `paintSpectrum` reads snapshot + per-pixel resampling.
- `Source/PluginEditor.h/.cpp` — `juce::OpenGLContext` member, attach/detach.
- `CMakeLists.txt` — `juce::juce_opengl`.

## Thread-safety notes
- `RingBuffer::copyLatest` is read-only and already tolerant of torn reads — safe to call from the timer while audio writes.
- `displayedDb` written only by the timer (message thread), read only by `paint` (GL thread): double-buffer + atomic index is sufficient.
- No locks on the audio thread; `processBlock` is unchanged.

## Risks / live-test checklist
- **OpenGL on the host:** some DAWs/older GPUs misbehave with attached GL contexts. Verify the editor opens and renders in the target DAW(s); confirm no flicker on resize and that the goniometer/waveform modes still draw.
- **Feel:** confirm transients hit instantly and release is smooth (tune `tau` if needed — it's a one-line constant).
- **Resolution:** confirm the top end is clean (no jaggies) and bass is detailed vs Pro-Q.
- **CPU:** confirm CPU is lower / no stutter on the ultrawide window.

## Acceptance
Builds locally; PR opened; **user validates live in DAW against Pro-Q**. Tune `tau` / FFT size from live feedback before merge.
