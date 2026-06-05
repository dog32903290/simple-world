# Batch 8 Mixed Math Acceptance Matrix

Scope: four unbuilt Grade A math nodes from `docs/tixl-porting/namespaces/numbers.md`: `Atan2`, `AddInts`, `MultiplyInts`, and `SumInts`.

Gate: every row needs TiXL source/default evidence, semantic edge cases, corrected Vuo C source, installation into the Vuo user module folder, and a Vuo-visible proof that consumes the exact `my_<TiXLName>` node output.

| TiXL node | grade | Vuo title | Vuo source | source evidence | semantic/source tests | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.numbers.float.trigonometry.Atan2` | A | `my_Atan2` | `vuo-nodes/my.numbers.float.trigonometry.atan2.c` | C# `external/tixl/Operators/Lib/numbers/float/trigonometry/Atan2.cs`; `.t3` `external/tixl/Operators/Lib/numbers/float/trigonometry/Atan2.t3`; docs `external/tixl/.help/docs/operators/lib/numbers/float/trigonometry/Atan2.md` | `tests/tixl_batch8_mixed_math_semantics.test.js`; `tests/tixl_batch8_mixed_math_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-8-mixed-math-proof.vuo` -> `my_Batch8MixedMathProof.atan2Value`; Vuo-saved image `artifacts/vuo_cli/batch-8-mixed-math-vuo-save.png` | done |
| `Lib.numbers.int.basic.AddInts` | A | `my_AddInts` | `vuo-nodes/my.numbers.int.basic.addInts.c` | C# `external/tixl/Operators/Lib/numbers/int/basic/AddInts.cs`; `.t3` `external/tixl/Operators/Lib/numbers/int/basic/AddInts.t3`; docs `external/tixl/.help/docs/operators/lib/numbers/int/basic/AddInts.md` | `tests/tixl_batch8_mixed_math_semantics.test.js`; `tests/tixl_batch8_mixed_math_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-8-mixed-math-proof.vuo` -> `my_Batch8MixedMathProof.addIntsValue`; Vuo-saved image `artifacts/vuo_cli/batch-8-mixed-math-vuo-save.png` | done |
| `Lib.numbers.int.basic.MultiplyInts` | A | `my_MultiplyInts` | `vuo-nodes/my.numbers.int.basic.multiplyInts.c` | C# `external/tixl/Operators/Lib/numbers/int/basic/MultiplyInts.cs`; `.t3` `external/tixl/Operators/Lib/numbers/int/basic/MultiplyInts.t3`; docs `external/tixl/.help/docs/operators/lib/numbers/int/basic/MultiplyInts.md` | `tests/tixl_batch8_mixed_math_semantics.test.js`; `tests/tixl_batch8_mixed_math_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-8-mixed-math-proof.vuo` -> `my_Batch8MixedMathProof.multiplyIntsValue`; Vuo-saved image `artifacts/vuo_cli/batch-8-mixed-math-vuo-save.png` | done |
| `Lib.numbers.int.basic.SumInts` | A | `my_SumInts` | `vuo-nodes/my.numbers.int.basic.sumInts.c` | C# `external/tixl/Operators/Lib/numbers/int/basic/SumInts.cs`; `.t3` `external/tixl/Operators/Lib/numbers/int/basic/SumInts.t3`; docs `external/tixl/.help/docs/operators/lib/numbers/int/basic/SumInts.md` | `tests/tixl_batch8_mixed_math_semantics.test.js`; `tests/tixl_batch8_mixed_math_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-8-mixed-math-proof.vuo` -> `my_Batch8MixedMathProof.sumIntsValue`; Vuo-saved image `artifacts/vuo_cli/batch-8-mixed-math-vuo-save.png` | done |

## Batch Notes

- `my_Atan2` preserves TiXL's component order: `MathF.Atan2(Vector.X, Vector.Y)`.
- `my_AddInts` is separate from `my_IntAdd` because TiXL exposes both creator-facing node names.
- `my_MultiplyInts` returns `0` for an empty input list, not `1`.
- `my_SumInts` returns the multi-input default value for an empty input list; the Vuo adapter exposes `defaultValue` to make that fallback visible.
- Vuo CLI proof `batch-8-mixed-math-save-proof` compiled and linked with return code `0`, loaded all five custom nodes, opened an onscreen runner window, and produced Vuo-rendered PNG evidence at `artifacts/vuo_cli/batch-8-mixed-math-vuo-save.png`.
- Current macOS window capture failed with `could not create image from window`, matching previous batch reruns; this remains classified as a capture-layer issue. The Vuo-native saved image is non-black: `960x540`, average luma `0.195748`, bright ratio `0.37725`, `mostlyBlack=false`.
