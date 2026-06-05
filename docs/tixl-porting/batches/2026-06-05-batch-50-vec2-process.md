# Batch 50 Lib.numbers.vec2.process Acceptance Matrix

## Scope

Batch 50 ports `Lib.numbers.vec2.process` into Vuo node sources with TiXL-visible `my_` names, TiXL categories, value-color metadata, semantic primary outputs, and a proof-only numeric tap. Stateful animation, gizmo, list, and matrix nodes are bounded body-layer adapters; they preserve creator-facing contract evidence but do not claim full TiXL host-state parity.

## Matrix

| TiXL node | Port grade | Vuo title | Vuo source | TiXL evidence | Vuo output/proof | Proof composition | Status |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `Lib.numbers.vec2.process.EaseVec2` | bounded vector easing adapter | `my_EaseVec2` | `vuo-nodes/my.numbers.vec2.process.easeVec2.c` | C# `external/tixl/Operators/Lib/numbers/vec2/process/EaseVec2.cs`; `.t3` `external/tixl/Operators/Lib/numbers/vec2/process/EaseVec2.t3` | VuoPoint2d `Result`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-50-vec2-process-proof.vuo` | done |
| `Lib.numbers.vec2.process.EaseVec2Keys` | bounded vector key easing adapter | `my_EaseVec2Keys` | `vuo-nodes/my.numbers.vec2.process.easeVec2Keys.c` | C# `external/tixl/Operators/Lib/numbers/vec2/process/EaseVec2Keys.cs`; `.t3` `external/tixl/Operators/Lib/numbers/vec2/process/EaseVec2Keys.t3` | VuoPoint2d `Result`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-50-vec2-process-proof.vuo` | done |
| `Lib.numbers.vec2.process.SpringVec2` | bounded vector spring adapter | `my_SpringVec2` | `vuo-nodes/my.numbers.vec2.process.springVec2.c` | C# `external/tixl/Operators/Lib/numbers/vec2/process/SpringVec2.cs`; `.t3` `external/tixl/Operators/Lib/numbers/vec2/process/SpringVec2.t3` | VuoPoint2d `Result`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-50-vec2-process-proof.vuo` | done |

## Trial Pressure

- `tests/tixl_batch50_vec2_process_semantics.test.js`
- `tests/tixl_batch50_vec2_process_vuo_nodes.test.js`
- `tests/vuo_batch_50_vec2_process_composition.test.js`
- Vuo CLI proof: `tools/vuo_harness.py cli-proof vuo-compositions/generated/myworld-batch-50-vec2-process-proof.vuo`
