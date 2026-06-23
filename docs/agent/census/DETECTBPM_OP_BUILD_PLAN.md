# DetectBpm node op Build Plan — port the OLDER DetectBpm.cs operator (option A)

> HEAD a7d932e. PARITY DECISION (orchestrator self-decided per constitution "問 TiXL，會變行為=照 TiXL不分岔"): the TiXL OPERATOR named `DetectBpm` is `external/tixl/Operators/Lib/io/audio/_/DetectBpm.cs` (the OLDER algorithm). The DetectBpm NODE must match it, NOT the newer editor `Interaction.Timing.BpmDetection.cs` (which is what the landed `bpm_detection.{h,cpp}` engine ports). They emit different BPM → option A (faithful DetectBpm.cs), not option B (wrap the newer engine).

## Engine-consumer correction (record)
The landed `bpm_detection.{h,cpp}` (newer editor BpmDetection) is the EDITOR's timing class; its consumer is editor TRANSPORT via `external/tixl/Core/IO/BpmProvider.cs`, i.e. the deferred `transport.bpm` wire (fork-bpm-not-live-driving-transport). It does NOT back the DetectBpm operator. So the engine is not orphaned — it just has a different (deferred) consumer than the scout assumed. DetectBpm op is a SEPARATE algorithm port.

## DetectBpm.cs algorithm (port target — READ the .cs, these are the deltas vs the landed engine)
`external/tixl/Operators/Lib/io/audio/_/DetectBpm.cs` (READ-ONLY). Differs from the landed engine:
- `_searchOffsets = {-0.5,-0.1,0,0.1,0.5}` (engine: {-0.3,-0.1,0,0.1,0.3}).
- refinement hard-range `70..160` (engine: 70..170).
- `_currentBpm` seed `122` (engine: 66).
- `MeasureEnergyDifference` autocorrelates against a FIXED `60*60` buffer (engine: sliding buffer sized to duration).
- default `BufferDurationSec = 15` (engine: sampleDurationInSec 25).
- **Input border contract: INTEGER bin indices** `LowerLimit`/`UpperLimit` into `UpdateBuffer(fft, lower, upper)` — .t3 defaults `LowerLimit=2, UpperLimit=199` (engine: normalized 0..0.2 range, derives borders internally). The DetectBpm port takes integer-bin borders directly.
- `.t3` op defaults (confirm by reading DetectBpm.t3): ~BpmRangeMax=180/UpperLimit=199/LowerLimit=2/BufferDurationSec=15/currentBpm=122/threshold=0.0.
Port DetectBpm.cs's ComputeBpmRate/UpdateBuffer/SmoothBuffer/MeasureEnergyDifference VERBATIM (its own constants), like the engine port did for BpmDetection.cs. Do NOT reuse the landed engine's algorithm (different).

## Wiring — rides the AudioReaction stateful-audio-node pattern (scope-confirmed by the prior agent)
- **Registration:** stateful node = `MathOp` registrar with outputs-first ports + pinless param knobs + `nullptr` evaluate. Precedent `node_registry_math_anim.cpp:21-39` (AudioReaction). Mirror for DetectBpm: 1 float output (BPM), param knobs (LowerLimit/UpperLimit/BufferDuration/range per .t3), nullptr evaluate.
- **Cook:** `cookAudioReactionNodes` (`frame_cook.cpp:138-172`) loops resident nodes by opType, resolves params via `resolveResidentFloatInputs`, runs the stateful algo on `audio_monitor::spectrum()`, writes to `rn.extOut[]`. State = `std::map<path,State>` static keyed by resident path (survives rebuilds, per-instance). Add a DetectBpm branch (or a sibling cook fn) holding a per-path DetectBpm instance, fed `spectrum().fftGain`/`fftNormalized` each frame with the INTEGER borders LowerLimit/UpperLimit, writing computeBpmRate() to extOut[0].
- **Call site:** `frame_cook.cpp:328-332` (once per frame). Add the DetectBpm cook alongside AudioReaction.
- **Read-back:** `evalResidentFloat` returns `extOut[port index]` for nullptr-evaluate nodes (`resident_eval_graph.h:105-107`).
- **Throttle:** neither DetectBpm.cs nor the editor class has a real don't-call-every-frame throttle (the prior agent grepped — no consumer sets a cadence in TiXL). DetectBpm.Update() runs full ComputeBpmRate every eval. So run it every frame (faithful); do NOT invent a throttle.

## Golden — node-level (`--selftest-detectbpm`)
Drive the node the way main does: feed the synthetic FFT spike-train (planted BPM) over N frames through the node's cook path (DetectBpm instance + integer borders), assert extOut[0]/evalResidentFloat returns the recovered BPM (±1) against DetectBpm.cs's OWN math. Plant 2 targets (rule out hard-coding). + an intermediate re-derivation anchor from DetectBpm.cs (MeasureEnergyDifference against the fixed 60*60 buffer). -bug RED (node doesn't accumulate / wrong extOut wiring / lag-detune → wrong BPM). Confirm AudioReaction selftest + engine `--selftest-bpm-detect` stay green.

## Files (no cook-core point_graph touch beyond the existing AudioReaction cook hook)
- NEW `app/src/runtime/detect_bpm.{h,cpp}` (the DetectBpm.cs algorithm port — distinct from bpm_detection.*) OR fold into a node TU. Keep ≤400.
- EDIT `frame_cook.cpp` (add DetectBpm cook branch + call — this is the AudioReaction precedent's file; app-level frame cook, not point_graph eval-graph).
- EDIT `node_registry_math_anim.cpp` (register DetectBpm spec, mirror AudioReaction).
- EDIT selftests_core.cpp/decls + CMake.
- Confirm git diff touches NO point_graph.cpp/resident_eval_graph internals beyond the extOut read pattern.

## Forks
- Port DetectBpm.cs verbatim (its own constants) — no fork vs the operator.
- If DetectBpm.cs has a param the sw NodeSpec can't yet express (e.g. a threshold/peak output), name a deferred fork rather than inventing.

## Risk: R2. Second BPM-algorithm port (clean, fixed .cs source) + proven AudioReaction wiring. Refuter re-derives DetectBpm.cs's MeasureEnergyDifference independently + confirms the integer-border contract + node golden recovers vs DetectBpm.cs (not vs the landed engine).

## Critical files
- external/tixl/Operators/Lib/io/audio/_/DetectBpm.cs (+ DetectBpm.t3 for defaults) — port target
- app/src/runtime/frame_cook.cpp:138-172,328-332 (AudioReaction cook precedent + call site)
- app/src/runtime/node_registry_math_anim.cpp:21-39 (AudioReaction registrar precedent)
- app/src/runtime/resident_eval_graph.h:105-107 (extOut readback)
- app/src/app/audio_monitor.h:24 / spectrum_analyzer.h (spectrum input)
