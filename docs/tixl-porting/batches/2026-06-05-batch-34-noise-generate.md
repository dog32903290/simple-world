# Batch 34 Image Generate Noise Acceptance Matrix

Scope: finish `Lib.image.generate.noise` after Batch 33 completed the remaining basic image generators.

| TiXL node | acceptance | Vuo visible title | Vuo source | source evidence | tests | proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.image.generate.noise.FractalNoise` | C bounded shader adapter | `my_FractalNoise` | `vuo-nodes/my.image.generate.noise.fractalNoise.c` | C# `external/tixl/Operators/Lib/image/generate/noise/FractalNoise.cs`; `.t3` `external/tixl/Operators/Lib/image/generate/noise/FractalNoise.t3`; shader `external/tixl/Operators/Lib/Assets/shaders/img/generate/FractalNoise.hlsl` | `tests/tixl_batch34_noise_generate_semantics.test.js`; `tests/tixl_batch34_noise_generate_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-34-noise-generate-proof.vuo` -> first band shows fbm/fractal noise | done |
| `Lib.image.generate.noise.Grain` | C bounded shader adapter | `my_Grain` | `vuo-nodes/my.image.generate.noise.grain.c` | C# `external/tixl/Operators/Lib/image/generate/noise/Grain.cs`; `.t3` `external/tixl/Operators/Lib/image/generate/noise/Grain.t3`; shader setup `Grain.hlsl` from TiXL graph | same tests | same proof -> second band shows high-frequency grain | done |
| `Lib.image.generate.noise.ShardNoise` | C bounded shader adapter | `my_ShardNoise` | `vuo-nodes/my.image.generate.noise.shardNoise.c` | C# `external/tixl/Operators/Lib/image/generate/noise/ShardNoise.cs`; `.t3` `external/tixl/Operators/Lib/image/generate/noise/ShardNoise.t3`; shader `external/tixl/Operators/Lib/Assets/shaders/img/generate/ShardNoise.hlsl` | same tests | same proof -> third band shows shard/cubism noise | done |
| `Lib.image.generate.noise.TileableNoise` | C bounded shader adapter | `my_TileableNoise` | `vuo-nodes/my.image.generate.noise.tileableNoise.c` | C# `external/tixl/Operators/Lib/image/generate/noise/TileableNoise.cs`; `.t3` `external/tixl/Operators/Lib/image/generate/noise/TileableNoise.t3`; shader `external/tixl/Operators/Lib/Assets/shaders/img/generate/PerlinNoise2d.hlsl` | same tests | same proof -> fourth band shows tileable fbm | done |
| `Lib.image.generate.noise.WorleyNoise` | C bounded shader adapter | `my_WorleyNoise` | `vuo-nodes/my.image.generate.noise.worleyNoise.c` | C# `external/tixl/Operators/Lib/image/generate/noise/WorleyNoise.cs`; `.t3` `external/tixl/Operators/Lib/image/generate/noise/WorleyNoise.t3`; shader `external/tixl/Operators/Lib/Assets/shaders/img/generate/WorleyNoise.hlsl` | same tests | same proof -> fifth band shows cellular F2-F1 noise | done |

## Proof Notes

- Primary output type for all five manufactured nodes is `Texture2D`, so node/cable color remains TiXL `ColorForTextures #9F008A`.
- Creator-facing titles preserve TiXL capitalization exactly: `my_FractalNoise`, `my_Grain`, `my_ShardNoise`, `my_TileableNoise`, `my_WorleyNoise`.
- Vuo CLI proof target: `batch-34-noise-generate-proof`.
- Vuo compile/link/run proof passed: `compile.returncode=0`, `link.returncode=0`, `run.ok=true`; loaded user nodes included all five noise nodes and `my.image.batch.batch34NoiseGenerateProof`.
- Runner screenshot proof passed at `artifacts/vuo_cli/batch-34-noise-generate-proof.run.png`; harness visual stats: average luma `0.368384`, bright ratio `0.841983`, `mostlyBlack=false`.
- Vuo-native save node wrote `artifacts/vuo_cli/batch-34-noise-generate-vuo-save.png`; saved PNG stats: `800x160`, mean luma `91.123`, bright ratio `0.902375`, nontransparent ratio `1.0`, `mostly_black=false`.
- Bounded adapter limits: source texture blending for `Grain`/`WorleyNoise`, DXGI output format, mip generation, and TiXL OpenSimplex/normal-map variants are not fully represented in this Vuo body layer.
- Non-fatal Vuo stderr still includes the local no-Pro-license notice, a composition-state warning after termination, and the existing GL texture warning seen in nearby image batches.
