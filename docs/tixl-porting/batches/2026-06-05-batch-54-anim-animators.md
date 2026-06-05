# Batch 54 Lib.numbers.anim.animators Acceptance Matrix

## Scope

Batch 54 ports `Lib.numbers.anim.animators` into Vuo node sources with TiXL-visible `my_` names, TiXL categories, value-color metadata, semantic primary outputs, and a proof-only numeric tap. Stateful animation, gizmo, list, and matrix nodes are bounded body-layer adapters; they preserve creator-facing contract evidence but do not claim full TiXL host-state parity.

## Matrix

| TiXL node | Port grade | Vuo title | Vuo source | TiXL evidence | Vuo output/proof | Proof composition | Status |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `Lib.numbers.anim.animators.AnimBoolean` | bounded LFO bool adapter | `my_AnimBoolean` | `vuo-nodes/my.numbers.anim.animators.animBoolean.c` | C# `external/tixl/Operators/Lib/numbers/anim/animators/AnimBoolean.cs`; `.t3` `external/tixl/Operators/Lib/numbers/anim/animators/AnimBoolean.t3` | VuoBoolean `TriggerOutput`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-54-anim-animators-proof.vuo` | done |
| `Lib.numbers.anim.animators.AnimFloatList` | bounded list summary adapter | `my_AnimFloatList` | `vuo-nodes/my.numbers.anim.animators.animFloatList.c` | C# `external/tixl/Operators/Lib/numbers/anim/animators/AnimFloatList.cs`; `.t3` `external/tixl/Operators/Lib/numbers/anim/animators/AnimFloatList.t3` | VuoReal `Result`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-54-anim-animators-proof.vuo` | done |
| `Lib.numbers.anim.animators.AnimInt` | bounded integer LFO adapter | `my_AnimInt` | `vuo-nodes/my.numbers.anim.animators.animInt.c` | C# `external/tixl/Operators/Lib/numbers/anim/animators/AnimInt.cs`; `.t3` `external/tixl/Operators/Lib/numbers/anim/animators/AnimInt.t3` | VuoInteger `Result`, VuoBoolean `WasHit`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-54-anim-animators-proof.vuo` | done |
| `Lib.numbers.anim.animators.AnimValue` | bounded LFO float adapter | `my_AnimValue` | `vuo-nodes/my.numbers.anim.animators.animValue.c` | C# `external/tixl/Operators/Lib/numbers/anim/animators/AnimValue.cs`; `.t3` `external/tixl/Operators/Lib/numbers/anim/animators/AnimValue.t3` | VuoReal `Result`, VuoBoolean `WasHit`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-54-anim-animators-proof.vuo` | done |
| `Lib.numbers.anim.animators.AnimVec2` | bounded vector LFO adapter | `my_AnimVec2` | `vuo-nodes/my.numbers.anim.animators.animVec2.c` | C# `external/tixl/Operators/Lib/numbers/anim/animators/AnimVec2.cs`; `.t3` `external/tixl/Operators/Lib/numbers/anim/animators/AnimVec2.t3` | VuoPoint2d `Result`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-54-anim-animators-proof.vuo` | done |
| `Lib.numbers.anim.animators.AnimVec3` | bounded vector LFO adapter | `my_AnimVec3` | `vuo-nodes/my.numbers.anim.animators.animVec3.c` | C# `external/tixl/Operators/Lib/numbers/anim/animators/AnimVec3.cs`; `.t3` `external/tixl/Operators/Lib/numbers/anim/animators/AnimVec3.t3` | VuoPoint3d `Result`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-54-anim-animators-proof.vuo` | done |
| `Lib.numbers.anim.animators.OscillateVec2` | exact-ish sine vector adapter | `my_OscillateVec2` | `vuo-nodes/my.numbers.anim.animators.oscillateVec2.c` | C# `external/tixl/Operators/Lib/numbers/anim/animators/OscillateVec2.cs`; `.t3` `external/tixl/Operators/Lib/numbers/anim/animators/OscillateVec2.t3` | VuoPoint2d `Result`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-54-anim-animators-proof.vuo` | done |
| `Lib.numbers.anim.animators.OscillateVec3` | exact-ish sine vector adapter | `my_OscillateVec3` | `vuo-nodes/my.numbers.anim.animators.oscillateVec3.c` | C# `external/tixl/Operators/Lib/numbers/anim/animators/OscillateVec3.cs`; `.t3` `external/tixl/Operators/Lib/numbers/anim/animators/OscillateVec3.t3` | VuoPoint3d `Result`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-54-anim-animators-proof.vuo` | done |
| `Lib.numbers.anim.animators.SequenceAnim` | bounded sequence adapter | `my_SequenceAnim` | `vuo-nodes/my.numbers.anim.animators.sequenceAnim.c` | C# `external/tixl/Operators/Lib/numbers/anim/animators/SequenceAnim.cs`; `.t3` `external/tixl/Operators/Lib/numbers/anim/animators/SequenceAnim.t3` | VuoReal `Result`, VuoBoolean `WasStep`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-54-anim-animators-proof.vuo` | done |
| `Lib.numbers.anim.animators.TriggerAnim` | bounded trigger envelope adapter | `my_TriggerAnim` | `vuo-nodes/my.numbers.anim.animators.triggerAnim.c` | C# `external/tixl/Operators/Lib/numbers/anim/animators/TriggerAnim.cs`; `.t3` `external/tixl/Operators/Lib/numbers/anim/animators/TriggerAnim.t3` | VuoBoolean `HasCompleted`, VuoReal `Result`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-54-anim-animators-proof.vuo` | done |

## Trial Pressure

- `tests/tixl_batch54_anim_animators_semantics.test.js`
- `tests/tixl_batch54_anim_animators_vuo_nodes.test.js`
- `tests/vuo_batch_54_anim_animators_composition.test.js`
- Vuo CLI proof: `tools/vuo_harness.py cli-proof vuo-compositions/generated/myworld-batch-54-anim-animators-proof.vuo`
