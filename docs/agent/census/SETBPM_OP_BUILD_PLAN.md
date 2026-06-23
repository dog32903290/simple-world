# SetBpm node op Build Plan — the FAITHFUL transport-BPM path

> HEAD 010d2c4. Supersedes the non-parity "feed engine → transport.bpm" wire (a scope agent proved that has no TiXL contract). The real TiXL transport-BPM mechanism is the `[SetBpm]` operator + a triggered-pull BpmProvider.

## Source-grounded finding (the correction)
- `external/tixl/Core/IO/BpmProvider.cs:22-33` — `TryGetNewBpmRate(out bpm)` returns true ONLY when `SetBpmTriggered`; takes NO spectrum, runs NO detection. A triggered PULL.
- `external/tixl/Operators/Lib/numbers/anim/vj/SetBpm.cs:22,25,38-39` — the ONLY writer: the `[SetBpm]` operator, on a `TriggerUpdate` bool EDGE, writes a user-supplied `BpmRate` float (clamped 54..240) to `NewBpmRate` + sets `SetBpmTriggered`.
- `external/tixl/Editor/.../PlaybackUtils.cs:74-78` — per-frame consumer: `if (BpmProvider.TryGetNewBpmRate(out r)) Playback.Bpm = r;` (writes transport BPM only on the triggered edge).
- The editor `BpmDetection` class (which the landed `bpm_detection.{h,cpp}` engine ports) is DEAD CODE in TiXL (grep → class + 2 doc-comment `<see cref>` only). The engine is a faithful-but-unconsumed leaf, matching TiXL's own non-use. DetectBpm op (older operator) is the real audio-BPM node. Neither feeds transport.

## Deliverable
The `[SetBpm]` VJ node op: on a TriggerUpdate bool edge, write the clamped BpmRate to a BpmProvider-equivalent singleton; a per-frame pull in frame_cook writes `g_transport.bpm` (mirroring PlaybackUtils). Machine-verifiable, app-level, file-disjoint from point_graph.

## Files (app-level; NO point_graph touch)
- NEW `app/src/runtime/bpm_provider.{h,cpp}` (or fold the singleton into transport.* if cleaner) — BpmProvider-equivalent: `setNewBpmRate(float)` (sets NewBpmRate + SetBpmTriggered), `bool tryGetNewBpmRate(float& out)` (true + clears trigger only when set, else false + stale). Mirror BpmProvider.cs:22-33 exactly (triggered-pull semantics).
- NEW (or in an existing node TU) the SetBpm NodeSpec + cook: inputs BpmRate (float, clamp 54..240) + TriggerUpdate (bool); on the trigger EDGE (rising), call setNewBpmRate(clamp(BpmRate)). Edge-detection state per-resident-path (like AudioReaction/DetectBpm pattern, or however bool-trigger ops already do edges in sw — grep for an existing TriggerUpdate/edge op precedent first). Mirror SetBpm.cs:22-39.
- EDIT `frame_cook.cpp` — per-frame `if (bpmProvider.tryGetNewBpmRate(r)) g_transport.bpm = r;` pull (mirror PlaybackUtils:74-78). frame_cook is AT its 402 grandfather cap → PEEL a helper to stay ≤402, NO bump (gate-or-it-rots).
- EDIT transport.* if the bpm sink needs a setter; selftests + CMake.

## Golden `--selftest-setbpm` (machine-verifiable, no 柏為)
- Fire the SetBpm node with BpmRate=128 + TriggerUpdate rising edge → assert provider holds 128 (tryGetNewBpmRate returns true once → 128, then false). After the frame_cook pull, g_transport.bpm==128.
- No trigger (TriggerUpdate stays false / no edge) → provider returns false, transport.bpm UNCHANGED (proves the triggered-pull semantics, not per-frame overwrite).
- Clamp: BpmRate=300 → clamped to 240; BpmRate=10 → 54.
- Edge semantics: holding TriggerUpdate=true across frames fires ONCE (edge, not level) — assert a second frame with TriggerUpdate still true does NOT re-fire (per SetBpm.cs edge behavior — CONFIRM the .cs uses an edge vs level and match it).
- -bug RED (level instead of edge / wrong clamp / writes without trigger).

## Forks
- Port SetBpm.cs + BpmProvider.cs verbatim. If sw's transport has no bpm field yet or a different range, name a fork. If the bool-edge helper differs from TiXL, name it.

## Refuter focus
- Triggered-pull semantics faithful (BpmProvider.cs:24 `if(!SetBpmTriggered)` → transport.bpm only changes on the edge, NOT every frame). The gated-off / no-trigger no-op is the make-or-break (a per-frame overwrite would be wrong).
- Edge vs level on TriggerUpdate (match SetBpm.cs).
- Clamp 54..240 verbatim.
- No point_graph touch; frame_cook ≤402 via peel not bump.
- The landed BpmDetection engine stays UNCONSUMED (faithful to TiXL) — this op does NOT and should NOT feed it.

## Risk: R2. Small VJ op + singleton + 1 frame_cook pull, fixed .cs source, app-level. Machine-verifiable triggered-pull golden.

## Critical files
- external/tixl/Operators/Lib/numbers/anim/vj/SetBpm.cs + Core/IO/BpmProvider.cs + Editor/.../PlaybackUtils.cs:74-78 (port targets)
- app/src/runtime/transport.{h,cpp} (g_transport.bpm sink)
- app/src/app/frame_cook.cpp (per-frame pull site, AT 402 cap)
- grep for an existing bool-edge / TriggerUpdate op precedent in sw before inventing edge-detect.
