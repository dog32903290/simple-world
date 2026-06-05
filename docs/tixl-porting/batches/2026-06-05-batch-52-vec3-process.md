# Batch 52 Lib.numbers.vec3.process Acceptance Matrix

## Scope

Batch 52 ports `Lib.numbers.vec3.process` into Vuo node sources with TiXL-visible `my_` names, TiXL categories, value-color metadata, semantic primary outputs, and a proof-only numeric tap. Stateful animation, gizmo, list, and matrix nodes are bounded body-layer adapters; they preserve creator-facing contract evidence but do not claim full TiXL host-state parity.

## Matrix

| TiXL node | Port grade | Vuo title | Vuo source | TiXL evidence | Vuo output/proof | Proof composition | Status |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `Lib.numbers.vec3.process.EaseVec3` | bounded vector easing adapter | `my_EaseVec3` | `vuo-nodes/my.numbers.vec3.process.easeVec3.c` | C# `external/tixl/Operators/Lib/numbers/vec3/process/EaseVec3.cs`; `.t3` `external/tixl/Operators/Lib/numbers/vec3/process/EaseVec3.t3` | VuoPoint3d `Result`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-52-vec3-process-proof.vuo` | done |
| `Lib.numbers.vec3.process.EaseVec3Keys` | bounded vector key easing adapter | `my_EaseVec3Keys` | `vuo-nodes/my.numbers.vec3.process.easeVec3Keys.c` | C# `external/tixl/Operators/Lib/numbers/vec3/process/EaseVec3Keys.cs`; `.t3` `external/tixl/Operators/Lib/numbers/vec3/process/EaseVec3Keys.t3` | VuoPoint3d `Result`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-52-vec3-process-proof.vuo` | done |
| `Lib.numbers.vec3.process.SpringVec3` | bounded vector spring adapter | `my_SpringVec3` | `vuo-nodes/my.numbers.vec3.process.springVec3.c` | C# `external/tixl/Operators/Lib/numbers/vec3/process/SpringVec3.cs`; `.t3` `external/tixl/Operators/Lib/numbers/vec3/process/SpringVec3.t3` | VuoPoint3d `Result`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-52-vec3-process-proof.vuo` | done |

## Trial Pressure

- `tests/tixl_batch52_vec3_process_semantics.test.js`
- `tests/tixl_batch52_vec3_process_vuo_nodes.test.js`
- `tests/vuo_batch_52_vec3_process_composition.test.js`
- Vuo CLI proof: `tools/vuo_harness.py cli-proof vuo-compositions/generated/myworld-batch-52-vec3-process-proof.vuo`
