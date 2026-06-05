# Batch 46 Lib.numbers.anim.utils Acceptance Matrix

## Scope

Batch 46 ports `Lib.numbers.anim.utils` into Vuo node sources with TiXL-visible `my_` names, TiXL categories, value-color metadata, semantic primary outputs, and a proof-only numeric tap. App/timeline side-effect nodes are bounded body-layer adapters; they preserve creator-facing contract evidence but do not claim TiXL host-state parity.

## Matrix

| TiXL node | Port grade | Vuo title | Vuo source | TiXL evidence | Vuo output/proof | Proof composition | Status |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `Lib.numbers.anim.utils.FindKeyframes` | side-effect bounded keyframe adapter | `my_FindKeyframes` | `vuo-nodes/my.numbers.anim.utils.findKeyframes.c` | C# `external/tixl/Operators/Lib/numbers/anim/utils/FindKeyframes.cs`; `.t3` `external/tixl/Operators/Lib/numbers/anim/utils/FindKeyframes.t3` | VuoInteger `KeyframeCount`, VuoReal `Time`, VuoReal `Value`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-46-anim-utils-proof.vuo` | done |
| `Lib.numbers.anim.utils.SetKeyframes` | side-effect bounded keyframe adapter | `my_SetKeyframes` | `vuo-nodes/my.numbers.anim.utils.setKeyframes.c` | C# `external/tixl/Operators/Lib/numbers/anim/utils/SetKeyframes.cs`; `.t3` `external/tixl/Operators/Lib/numbers/anim/utils/SetKeyframes.t3` | VuoReal `CurrentValue`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-46-anim-utils-proof.vuo` | done |

## Trial Pressure

- `tests/tixl_batch46_anim_utils_semantics.test.js`
- `tests/tixl_batch46_anim_utils_vuo_nodes.test.js`
- `tests/vuo_batch_46_anim_utils_composition.test.js`
- Vuo CLI proof: `tools/vuo_harness.py cli-proof vuo-compositions/generated/myworld-batch-46-anim-utils-proof.vuo`
