# BPM Auto-Detection Build Plan (L6) — port TiXL BpmDetection.cs

> HEAD 2a6f45a. First batch of the post-particle-field arc — BREADTH into L6 audio analysis. Machine-verifiable without 柏為 (deterministic float DSP, closed-form synthetic-signal golden + independent intermediate re-derivation). File-disjoint from cook-core spine.

## Deliverable
Headless `BpmDetection` class ported line-for-line from `external/tixl/Editor/Gui/Interaction/Timing/BpmDetection.cs` (182 lines, pure deterministic float arithmetic: sliding sample buffer → SmoothBuffer boxcar subtract → MeasureEnergyDifference autocorrelation → ComputeFocusFactor fall-off → argmin over BPM range). No GPU/device/UI/frame-loop/randomness. Plus `--selftest-bpm-detect` golden.

## Files (all new or append-only; owner-lock vs spine = NONE)
- **NEW** `app/src/runtime/bpm_detection.h` — `class BpmDetection`: `addFftSample(const float* fft, int n)`, `computeBpmRate()`, `hasSufficientSampleData()`; tunables sampleDurationInSec=25, bpmRangeMin=80/Max=180, normalizedFrequencyRange{0,0.2}, lockInFactor=0.001. Declare `int runBpmDetectionSelfTest(bool injectBug)`.
- **NEW** `app/src/runtime/bpm_detection.cpp` — line-for-line port of BpmDetection.cs:18-182. State: `std::vector<float> sampleBuffer_/smoothedSampleBuffer_`, currentBpm_=66, searchOffsets_={-0.3,-0.1,0,0.1,0.3}. Use `sw::clampf` (`anim_math.h:89`) for .Clamp.
- **NEW** `app/src/runtime/bpm_detection_selftest.cpp` (or fold into .cpp, match how spectrum_analyzer carries runSpectrumSelfTest).
- **EDIT append-only** `app/src/selftests_core.cpp:12` — add `{"bpm-detect", runBpmDetectionSelfTest},` to REGISTER_SELFTESTS (audio cluster near `{"spectrum",...}` :27). ONLY edit to an existing file; same mechanism every field golden uses.
- **EDIT** `app/src/selftests_decls.h` — fwd-decl.
- **EDIT** `app/CMakeLists.txt` — add the new .cpp(s).
- **DO NOT** wire into frame_cook/cook-core. If surfacing to transport.bpm, do it at the existing app/main render-thread `audio_monitor::spectrum()` consumer, GATED/opt-in (engine lands machine-verified; live-driving transport is a deferred follow-up — see fork below).

## Input contract (already exists)
`app/src/runtime/spectrum_analyzer.h:24-31` publishes `SpectrumSnapshot.fftNormalized[1024]` + `fftGain[1024]`, consumed via `app/src/app/audio_monitor.h:24` `spectrum()`. TiXL reads `fftBuffer[lowerBorder..upperBorder]`, borders = NormalizedFrequencyRange·FftResolution (`BpmDetection.cs:92-93`) — a RATIO, so sw's 1024-bin buffer drops in cleanly.

## Golden design (verified vs TiXL WITHOUT 柏為)
1. **Closed-form recovery (primary):** synthesize FFT stream — for N=sampleDurationInSec·60=1500 frames, fake fft[1024] with a band-0..0.2 energy spike height 1.0 every `period=round(240/bpmTarget·60/4)` frames, ~0 else. bpmTarget=120 → addFftSample 1500× → computeBpmRate() within ±1 of 120 (argmin is integer-BPM, BpmDetection.cs:43). Repeat for 90 (prove not hard-coded).
2. **Intermediate-buffer equivalence (refuter make-or-break):** for a small fixed sampleBuffer, hand-compute SmoothBuffer (boxcar smoothSteps=5, :120-138) + MeasureEnergyDifference(bpm) (:146-168) from the .cs, assert sw matches to 5 decimals. Source of truth = the .cs (not self-consistency).
3. **-bug RED (injectBug):** (a) drop the SmoothBuffer energy-subtract (max(0,sample-avg)→sample) so DC/volume drift swamps autocorrelation, OR (b) flip `smoothedSampleBuffer[i-offset]`→`[i+offset]` → wrong periodicity → recovered ≠ target → golden RED. Pick the discriminating one.
4. **hasSufficientSampleData guard:** before SampleBufferSize samples, false (:13).

## TiXL source
`external/tixl/Editor/Gui/Interaction/Timing/BpmDetection.cs:18-182` (READ-ONLY) — :43-69 argmin+searchOffsets, :77-82 ComputeFocusFactor fall-off, :86-112 UpdateSampleBuffer band sum, :120-138 SmoothBuffer, :146-168 MeasureEnergyDifference (load-bearing), :170-181 constants (FramesPerSecond=60, FftResolution=512, currentBpm=66). `external/tixl/Core/IO/BpmProvider.cs` (transport surfacing contract). Port VERBATIM; forks named below.

## Forks (name up front)
- **fork-bpm-fft-resolution**: sw kFftBins=1024 (`spectrum_analyzer.h:19`) vs TiXL FftResolution=512. Borders ratio-derived → output identical for default 0..0.2 range. Faithful, named.
- **fork-bpm-not-live-driving-transport**: engine lands machine-verified; auto-writing transport.bpm every frame gated/deferred (mirrors L1 "engine ≠ wiring" split). Live-drive + the don't-call-every-frame throttle (:29-31) = follow-up.
- Note: `_searchOffsets` hard-range 70..170 (:58) vs configurable BpmRangeMin/Max — port verbatim, note asymmetry.

## Refuter focus
- MeasureEnergyDifference + SmoothBuffer independently re-derived from BpmDetection.cs (the make-or-break — confirm sw matches the .cs math, not just self-consistent).
- Closed-form recovery is REAL (planted 120/90 recovered ±1, not hard-coded); -bug genuinely lands on wrong BPM (RED).
- FFT-resolution rescale faithful (borders ratio-correct for 1024 vs 512).
- No cook-core touch; selftests_core.cpp edit append-only; clampf reused not reimplemented.

## Risk: R1-R2, MERGE-SAFE-likely. Self-contained deterministic DSP, fixed C# source of truth, zero spine overlap. No metallib rebuild (pure C++).

## Critical files
- external/tixl/Editor/Gui/Interaction/Timing/BpmDetection.cs (port target)
- app/src/runtime/spectrum_analyzer.h (input), app/src/app/audio_monitor.h (spectrum())
- app/src/selftests_core.cpp (append selftest row), app/src/runtime/transport.cpp (opt-in bpm sink)
- app/src/runtime/anim_math.h:89 (clampf)
