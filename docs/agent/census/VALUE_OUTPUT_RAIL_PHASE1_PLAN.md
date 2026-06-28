> ⚠️ **HISTORICAL — 已實作，此文僅留作設計脈絡（2026-06-28 stamp）**
> Phase 1（cookValueOutputNodes / RequestedResolution）與 Phase 3（cookMatrixOutputNodes）皆已 SHIPPED：
> `resident_value_output_cook.cpp` + `resident_matrix_output_cook.cpp`，接線於 `app/src/app/cook_host_values.cpp`。
> 下方規劃內容為當時的設計藍圖，非待辦；不要照此重蓋已建縫。
>
# Value-Output-Rail Phase 1 Build Plan (cook-emit Int2/Int3/Vec3 scalar outputs)

> HEAD 97a8451. The "seam" joins two ALREADY-SHIPPING mechanisms — multi-Float-output ops + extOut[8] cook-emit — for cook/context ops. Phase 1 = SMALL, cook-core-FREE. Unlocks CalcDispatchCount/RequestedResolution/GetTextureSize/GetPosition-WorldSpace.

## The two existing mechanisms being joined (read first)
1. **Multi-Float-output** (`value_eval_ops.cpp:228` evalAddVec3 / Vector3Components): one op declares N Float output ports; the eval rail pulls each by `outIdx` with convention `component k = outIdx - n` (n = Float input count). Vec3 output = ports `Result.x/.y/.z` at indices n,n+1,n+2.
2. **Cook-emitted value** (`extOut[8]`, `resident_eval_graph.h:105-107`): a `evaluate=nullptr` op cooked once/frame by a host pass (`resident_host_scalar_cook.cpp` cookHostScalarNodes) writing `ResidentNode::extOut[outputPortIndex]`; `evalResidentFloat` returns `extOut[outIdx]`. DetectBpm precedent = "point_graph 零觸".
- Downstream read already wired: a ResidentInput Connection carries srcSlotId → `evalResidentFloat` resolves to outIdx by port-id match (`resident_eval_graph.cpp:177-180`). A downstream op reading a Vec3 wires 3 Float input ports (Result.x/.y/.z). No new wire type/resolver.

## Phase 1 design — N scalar Float output ports + extOut emit (cook-core-FREE)
A cook op emits a Vec3/Int2/Int3 by: declaring N Float output ports (Result.x/.y/.z), `evaluate=nullptr`, and a per-frame cook pass writing `extOut[0..N-1]` — mechanically identical to DetectBpm's single-Float extOut[0], just wider. Int2/Int3/Vec3/Vec4 all fit extOut[8].

**NAMED FORK `fork-vec-output-as-n-scalar-ports`**: TiXL wires one Slot<Vector3>; sw wires 3 Slot<float>. Faithful in VALUE (same numbers), forked in wire-CARDINALITY. This EXTENDS the already-shipped input-side scalar-pack fork (appendVec3Param / Widget::Vec, graph.h:19-25 "a vector is N scalars wearing one widget"). A .t3 round-trip of a single Vector3 wire won't byte-match topologically; equivalence is semantic. Same bargain sw already made on inputs.

## Phase-1 ops (build these 3; add GetPosition-WorldSpace if clean)
1. **RequestedResolution (Int2)** — LOWEST RISK FIRST: reads `p_->requestedResolution` (an Int2 ALREADY pushed/popped during cook, point_graph.cpp:121,469,554 — no new context to thread). 2 Float output ports (Width/Height) ← extOut[0..1]. TiXL: render census :162.
2. **GetTextureSize (Int2)** — reads a Texture2D description w/h → 2 Float ports. Needs a Texture2D→cook-emit read (the input texture's size; check how texture inputs reach the cook pass). TiXL: render census :221.
3. **CalcDispatchCount (Int3)** — pure arithmetic: ceil(N/groupSize) per component → 3 Float ports. Zero context. TiXL: render census :223.
4. (optional) **GetPosition WorldSpace mode (Vec3)** — ObjectToWorld translation, no camera. Only the WorldSpace mode (CameraSpace/ClipSpace = Phase 2). 3 Float ports.

## Files (additive, no point_graph cook-core recursion/collector touch)
- A new cook-emit pass (model on `resident_host_scalar_cook.cpp` cookHostScalarNodes) OR extend it — a function in the frame_cook slot writing extOut for these ops (the cookColorListNodes/cookStringNodes precedent: added without touching point_graph.cpp recursion).
- NodeSpec for each op (N Float output ports) in the appropriate registrar.
- For RequestedResolution: surface `p_->requestedResolution` to the emit pass (additive read, like the camera bridge surfaced the camera — but requestedResolution ALREADY exists in cook state, so just read it).
- selftests + CMake.
- Keep files ≤400, no grandfather bump.

## Golden (3-leg, machine-verifiable, no 柏為) — model on detect_bpm.cpp:227-283
For each op: drive the node through its cook pass, read extOut[0..N-1] back, AND cross-check evalResidentFloat(Result.x/.y/.z) returns the same extOut — then assert against a HAND-DERIVED expected independent of the impl.
- CalcDispatchCount: N=(100,50,1) group=(8,8,1) → expected (13,7,1). Assert extOut[0..2]==(13,7,1) AND evalResidentFloat agrees. -bug = floor instead of ceil (or N/group truncation) → component flips → RED.
- RequestedResolution: push a known resolution (e.g. 1920×1080) into cook state → assert Width=1920, Height=1080. -bug = returns default/0 → RED.
- 3 legs: flat cook, resident cook, evalResidentFloat/extOut agreement.

## Phasing (record for next builds)
- Phase 2: + `cameraToClipSpace[16]` on CmdCookCtx (additive, camera-bridge template 590b840) → GetScreenPos, GetPosition camera modes. Builder must refuse fake projection (Cut47).
- Phase 3: Matrix/Vector4[] via the existing `extColorOut` channel (resident_eval_graph.h:114, already Slot<List<Vector4>>; Mat4 = 4×float4 fits) → PointToMatrix, TransformMatrix.
- Deferred: camera-ref (CpuPointToCamera Slot<Object>) — a reference not a value, belongs to the camera-scope/Slot<Object> seam. Do NOT fake onto extOut.

## Risk: R1-R2. Both mechanisms proven (multi-output + extOut). No cook-core touch (Phase 1). The fork is an accepted extension of the shipped input-side fork.

## Critical files
- resident_eval_graph.h (extOut[8] / evalResidentFloat contract), resident_host_scalar_cook.cpp (the cook-emit pass to model on), value_eval_ops.cpp:228 (component k=outIdx-n multi-output convention), graph.h (PortSpec/NodeSpec output ports), point_graph.cpp:121 (requestedResolution cook state), detect_bpm.cpp:227-283 (the 3-leg golden pattern).
