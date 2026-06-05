# Batch 48 Lib.numbers.float.random Acceptance Matrix

## Scope

Batch 48 ports `Lib.numbers.float.random` into Vuo node sources with TiXL-visible `my_` names, TiXL categories, value-color metadata, semantic primary outputs, and a proof-only numeric tap. App/timeline side-effect nodes are bounded body-layer adapters; they preserve creator-facing contract evidence but do not claim TiXL host-state parity.

## Matrix

| TiXL node | Port grade | Vuo title | Vuo source | TiXL evidence | Vuo output/proof | Proof composition | Status |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `Lib.numbers.float.random.FloatHash` | deterministic hash adapter | `my_FloatHash` | `vuo-nodes/my.numbers.float.random.floatHash.c` | C# `external/tixl/Operators/Lib/numbers/float/random/FloatHash.cs`; `.t3` `external/tixl/Operators/Lib/numbers/float/random/FloatHash.t3` | VuoInteger `Result`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-48-float-random-proof.vuo` | done |
| `Lib.numbers.float.random.PerlinNoise` | bounded value-noise adapter | `my_PerlinNoise` | `vuo-nodes/my.numbers.float.random.perlinNoise.c` | C# `external/tixl/Operators/Lib/numbers/float/random/PerlinNoise.cs`; `.t3` `external/tixl/Operators/Lib/numbers/float/random/PerlinNoise.t3` | VuoReal `Result`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-48-float-random-proof.vuo` | done |
| `Lib.numbers.float.random.Random` | deterministic random adapter | `my_Random` | `vuo-nodes/my.numbers.float.random.random.c` | C# `external/tixl/Operators/Lib/numbers/float/random/Random.cs`; `.t3` `external/tixl/Operators/Lib/numbers/float/random/Random.t3` | VuoReal `Result`; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-48-float-random-proof.vuo` | done |

## Trial Pressure

- `tests/tixl_batch48_float_random_semantics.test.js`
- `tests/tixl_batch48_float_random_vuo_nodes.test.js`
- `tests/vuo_batch_48_float_random_composition.test.js`
- Vuo CLI proof: `tools/vuo_harness.py cli-proof vuo-compositions/generated/myworld-batch-48-float-random-proof.vuo`
