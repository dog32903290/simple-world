# Batch 47 Lib.numbers.bool.process Acceptance Matrix

## Scope

Batch 47 ports `Lib.numbers.bool.process` into Vuo node sources with TiXL-visible `my_` names, TiXL categories, value-color metadata, semantic primary outputs, and a proof-only numeric tap. App/timeline side-effect nodes are bounded body-layer adapters; they preserve creator-facing contract evidence but do not claim TiXL host-state parity.

## Matrix

| TiXL node | Port grade | Vuo title | Vuo source | TiXL evidence | Vuo output/proof | Proof composition | Status |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `Lib.numbers.bool.process.CacheBoolean` | bounded value adapter | `my_CacheBoolean` | `vuo-nodes/my.numbers.bool.process.cacheBoolean.c` | C# `external/tixl/Operators/Lib/numbers/bool/process/CacheBoolean.cs`; `.t3` `external/tixl/Operators/Lib/numbers/bool/process/CacheBoolean.t3` | VuoBoolean `Result`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-47-bool-process-proof.vuo` | done |
| `Lib.numbers.bool.process.DelayBoolean` | bounded stateless delay adapter | `my_DelayBoolean` | `vuo-nodes/my.numbers.bool.process.delayBoolean.c` | C# `external/tixl/Operators/Lib/numbers/bool/process/DelayBoolean.cs`; `.t3` `external/tixl/Operators/Lib/numbers/bool/process/DelayBoolean.t3` | VuoBoolean `DelayedTrigger`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-47-bool-process-proof.vuo` | done |
| `Lib.numbers.bool.process.DelayTriggerChange` | bounded stateless delay adapter | `my_DelayTriggerChange` | `vuo-nodes/my.numbers.bool.process.delayTriggerChange.c` | C# `external/tixl/Operators/Lib/numbers/bool/process/DelayTriggerChange.cs`; `.t3` `external/tixl/Operators/Lib/numbers/bool/process/DelayTriggerChange.t3` | VuoBoolean `DelayedTrigger`, VuoReal `RemainingTime`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-47-bool-process-proof.vuo` | done |
| `Lib.numbers.bool.process.KeepBoolean` | bounded freeze adapter | `my_KeepBoolean` | `vuo-nodes/my.numbers.bool.process.keepBoolean.c` | C# `external/tixl/Operators/Lib/numbers/bool/process/KeepBoolean.cs`; `.t3` `external/tixl/Operators/Lib/numbers/bool/process/KeepBoolean.t3` | VuoBoolean `Result`, VuoReal `TimeSinceFreeze`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-47-bool-process-proof.vuo` | done |

## Trial Pressure

- `tests/tixl_batch47_bool_process_semantics.test.js`
- `tests/tixl_batch47_bool_process_vuo_nodes.test.js`
- `tests/vuo_batch_47_bool_process_composition.test.js`
- Vuo CLI proof: `tools/vuo_harness.py cli-proof vuo-compositions/generated/myworld-batch-47-bool-process-proof.vuo`
