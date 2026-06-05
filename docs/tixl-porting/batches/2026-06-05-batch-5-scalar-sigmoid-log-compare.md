# Batch 5 Scalar Sigmoid/Log/Compare Acceptance Matrix

Scope: three unbuilt Grade A scalar value nodes from `docs/tixl-porting/namespaces/numbers.md`: `Sigmoid`, `Log`, and `Compare`.

Gate: every row needs TiXL source/default evidence, semantic edge cases, corrected Vuo C source, installation into the Vuo user module folder, and a Vuo-visible proof that consumes the exact `my_<TiXLName>` node output.

| TiXL node | grade | Vuo title | Vuo source | source evidence | semantic/source tests | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.numbers.float.adjust.Sigmoid` | A | `my_Sigmoid` | `vuo-nodes/my.numbers.float.adjust.sigmoid.c` | C# `external/tixl/Operators/Lib/numbers/float/adjust/Sigmoid.cs`; `.t3` `external/tixl/Operators/Lib/numbers/float/adjust/Sigmoid.t3`; docs `external/tixl/.help/docs/operators/lib/numbers/float/adjust/Sigmoid.md` | `tests/tixl_batch5_scalar_semantics.test.js`; `tests/tixl_batch5_scalar_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-5-scalar-proof.vuo` -> `my_Batch5ScalarProof.sigmoidValue`; Vuo-saved image `artifacts/vuo_cli/batch-5-scalar-vuo-save.png` | done |
| `Lib.numbers.float.basic.Log` | A | `my_Log` | `vuo-nodes/my.numbers.float.basic.log.c` | C# `external/tixl/Operators/Lib/numbers/float/basic/Log.cs`; `.t3` `external/tixl/Operators/Lib/numbers/float/basic/Log.t3`; docs `external/tixl/.help/docs/operators/lib/numbers/float/basic/Log.md` | `tests/tixl_batch5_scalar_semantics.test.js`; `tests/tixl_batch5_scalar_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-5-scalar-proof.vuo` -> `my_Batch5ScalarProof.logValue`; Vuo-saved image `artifacts/vuo_cli/batch-5-scalar-vuo-save.png` | done |
| `Lib.numbers.float.logic.Compare` | A | `my_Compare` | `vuo-nodes/my.numbers.float.logic.compare.c` | C# `external/tixl/Operators/Lib/numbers/float/logic/Compare.cs`; `.t3` `external/tixl/Operators/Lib/numbers/float/logic/Compare.t3`; docs `external/tixl/.help/docs/operators/lib/numbers/float/logic/Compare.md` | `tests/tixl_batch5_scalar_semantics.test.js`; `tests/tixl_batch5_scalar_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-5-scalar-proof.vuo` -> `my_Batch5ScalarProof.compareValue`; Vuo-saved image `artifacts/vuo_cli/batch-5-scalar-vuo-save.png` | done |

## Batch Notes

- `my_Sigmoid` follows TiXL's source sign exactly: `1/(1+e^(Stretch*Value))`; positive values lower the result.
- `my_Log` intentionally keeps TiXL/.NET `Math.Log(value, base)` behavior. `.t3` defaults `Value=1`, `Base=1` produce `NaN`; `Value=1`, `Base=0` returns `0`; the visual proof uses `Value=8`, `Base=2` for a stable visible row.
- `my_Compare` clamps mode to `0..3`, matching TiXL's `Mode.GetValue(context).Clamp(...)`; default mode is `1` (`IsEqual`), not `0`.
- Vuo CLI proof `batch-5-scalar-save-proof` compiled and linked with return code `0`, loaded all four custom nodes, opened an onscreen runner window, and produced Vuo-rendered PNG evidence at `artifacts/vuo_cli/batch-5-scalar-vuo-save.png`.
- Current macOS window capture failed with `could not create image from window`, including a Batch 4 control rerun, so the screenshot failure is classified as a capture-layer issue. The Vuo-native saved image is the non-black visual artifact: `960x540`, average luma `0.344151`, bright ratio `0.601576`, `mostlyBlack=false`.
