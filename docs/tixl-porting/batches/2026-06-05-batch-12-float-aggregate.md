# Batch 12 Float Aggregate Acceptance Matrix

Scope: three remaining Grade A float aggregate/process nodes: `Sum`, `BlendValues`, and `RemapValues`.

Gate: every row needs TiXL source/default evidence, semantic edge cases, corrected Vuo C source, installation into the Vuo user module folder, and a Vuo-visible proof that consumes the exact `my_<TiXLName>` node output.

Vuo body-layer note: TiXL float multi-inputs are represented as `VuoList_VuoReal` ports. TiXL `Vector2` remap pairs are represented as `VuoList_VuoPoint2d` where `x` is lookup value and `y` is output value.

| TiXL node | grade | Vuo title | Vuo source | source evidence | semantic/source tests | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.numbers.float.basic.Sum` | A | `my_Sum` | `vuo-nodes/my.numbers.float.basic.sum.c` | C# `external/tixl/Operators/Lib/numbers/float/basic/Sum.cs`; `.t3` `external/tixl/Operators/Lib/numbers/float/basic/Sum.t3`; docs `external/tixl/.help/docs/operators/lib/numbers/float/basic/Sum.md` | `tests/tixl_batch12_float_aggregate_semantics.test.js`; `tests/tixl_batch12_float_aggregate_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-12-float-aggregate-proof.vuo` -> `my_Batch12FloatAggregateProof.sumValue`; Vuo-saved image `artifacts/vuo_cli/batch-12-float-aggregate-vuo-save.png` | done |
| `Lib.numbers.float.process.BlendValues` | A | `my_BlendValues` | `vuo-nodes/my.numbers.float.process.blendValues.c` | C# `external/tixl/Operators/Lib/numbers/float/process/BlendValues.cs`; `.t3` `external/tixl/Operators/Lib/numbers/float/process/BlendValues.t3`; docs `external/tixl/.help/docs/operators/lib/numbers/float/process/BlendValues.md` | `tests/tixl_batch12_float_aggregate_semantics.test.js`; `tests/tixl_batch12_float_aggregate_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-12-float-aggregate-proof.vuo` -> `my_Batch12FloatAggregateProof.blendValue`; Vuo-saved image `artifacts/vuo_cli/batch-12-float-aggregate-vuo-save.png` | done |
| `Lib.numbers.float.process.RemapValues` | A | `my_RemapValues` | `vuo-nodes/my.numbers.float.process.remapValues.c` | C# `external/tixl/Operators/Lib/numbers/float/process/RemapValues.cs`; `.t3` `external/tixl/Operators/Lib/numbers/float/process/RemapValues.t3`; docs `external/tixl/.help/docs/operators/lib/numbers/float/process/RemapValues.md` | `tests/tixl_batch12_float_aggregate_semantics.test.js`; `tests/tixl_batch12_float_aggregate_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-12-float-aggregate-proof.vuo` -> `my_Batch12FloatAggregateProof.remapValue`; Vuo-saved image `artifacts/vuo_cli/batch-12-float-aggregate-vuo-save.png` | done |

## Batch Notes

- `my_Sum` preserves TiXL's empty-input fallback by exposing `defaultValue`; empty list returns that default instead of a hard-coded `0`.
- `my_BlendValues` preserves TiXL `MathUtils.Fmod` wrapping and C-style integer cast behavior from C# `(int)f`. This matters for negative fractional `F`: `F=-0.25` uses index `0` for both blend endpoints.
- `my_RemapValues` chooses the first pair with the smallest absolute x-distance to `InputValue`; no pairs returns `0`.
- Vuo CLI proof `batch-12-float-aggregate-proof` compiled and linked with return code `0`, loaded all four custom nodes, opened an onscreen runner window, and produced Vuo-rendered PNG evidence at `artifacts/vuo_cli/batch-12-float-aggregate-vuo-save.png`.
- Current macOS window capture failed with `could not create image from window`, matching previous batch reruns; this remains classified as a capture-layer issue. The Vuo-native saved image is non-black: `960x540`, average luma `0.219322`, bright ratio `0.369842`, `mostlyBlack=false`.
