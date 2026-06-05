# Batch 36 Fractal And Munching Generate Acceptance Matrix

Scope: finish `Lib.image.generate.fractal` and `Lib.image.generate.MunchingSquares2`.

| TiXL node | acceptance | Vuo visible title | Vuo source | source evidence | tests | proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.image.generate.fractal.MandelbrotFractal` | C bounded shader adapter | `my_MandelbrotFractal` | `vuo-nodes/my.image.generate.fractal.mandelbrotFractal.c` | C# `external/tixl/Operators/Lib/image/generate/fractal/MandelbrotFractal.cs`; `.t3` `external/tixl/Operators/Lib/image/generate/fractal/MandelbrotFractal.t3`; shader `MandelbrotFractal.hlsl` | Batch 36 tests | `vuo-compositions/generated/myworld-batch-36-fractal-generate-proof.vuo` | done |
| `Lib.image.generate.MunchingSquares2` | C bounded shader adapter | `my_MunchingSquares2` | `vuo-nodes/my.image.generate.munchingSquares2.c` | C# `external/tixl/Operators/Lib/image/generate/MunchingSquares2.cs`; `.t3` `external/tixl/Operators/Lib/image/generate/MunchingSquares2.t3`; shader `MunchingSquares.hlsl` | Batch 36 tests | same proof | done |

## Proof Notes

- Primary output type for both manufactured nodes is `Texture2D`, so node/cable color remains TiXL `ColorForTextures #9F008A`.
- Vuo CLI proof target: `batch-36-fractal-generate-proof`.
- Vuo compile/link/run proof passed: `compile.returncode=0`, `link.returncode=0`, `run.ok=true`; loaded user nodes included `my.image.generate.fractal.mandelbrotFractal`, `my.image.generate.munchingSquares2`, and `my.image.batch.batch36FractalGenerateProof`.
- Runner screenshot proof passed at `artifacts/vuo_cli/batch-36-fractal-generate-proof.run.png`; harness visual stats: average luma `0.234035`, bright ratio `0.751913`, `mostlyBlack=false`.
- Vuo-native save node wrote `artifacts/vuo_cli/batch-36-fractal-generate-vuo-save.png`; saved PNG stats: `640x320`, mean luma `72.85`, bright ratio `0.604209`, nontransparent ratio `1.0`, `mostly_black=false`.
- Bounded adapter limits: TiXL Gradient is represented as `colorA/colorB` for `MandelbrotFractal`; source-image blending for `MunchingSquares2`, exact integer bitwise shader behavior, mips, and DXGI formats are not fully represented in this Vuo body layer.
- Non-fatal Vuo stderr still includes the local no-Pro-license notice, a composition-state warning after termination, and the existing GL texture warning seen in nearby image batches.
