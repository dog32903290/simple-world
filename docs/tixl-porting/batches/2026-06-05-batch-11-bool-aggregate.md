# Batch 11 Bool Aggregate Acceptance Matrix

Scope: two remaining Grade A boolean aggregate nodes from `Lib.numbers.bool.combine`: `All` and `Any`.

Gate: every row needs TiXL source/default evidence, semantic edge cases, corrected Vuo C source, installation into the Vuo user module folder, and a Vuo-visible proof that consumes the exact `my_<TiXLName>` node output.

Vuo body-layer note: TiXL `MultiInputSlot<bool>` is represented as a `VuoList_VuoBoolean` port. This preserves the aggregate law while keeping the Vuo proof simple and inspectable.

| TiXL node | grade | Vuo title | Vuo source | source evidence | semantic/source tests | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.numbers.bool.combine.All` | A | `my_All` | `vuo-nodes/my.numbers.bool.combine.all.c` | C# `external/tixl/Operators/Lib/numbers/bool/combine/All.cs`; `.t3` `external/tixl/Operators/Lib/numbers/bool/combine/All.t3`; docs `external/tixl/.help/docs/operators/lib/numbers/bool/combine/All.md` | `tests/tixl_batch11_bool_aggregate_semantics.test.js`; `tests/tixl_batch11_bool_aggregate_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-11-bool-aggregate-proof.vuo` -> `my_Batch11BoolAggregateProof.allValue`; Vuo-saved image `artifacts/vuo_cli/batch-11-bool-aggregate-vuo-save.png` | done |
| `Lib.numbers.bool.combine.Any` | A | `my_Any` | `vuo-nodes/my.numbers.bool.combine.any.c` | C# `external/tixl/Operators/Lib/numbers/bool/combine/Any.cs`; `.t3` `external/tixl/Operators/Lib/numbers/bool/combine/Any.t3`; docs `external/tixl/.help/docs/operators/lib/numbers/bool/combine/Any.md` | `tests/tixl_batch11_bool_aggregate_semantics.test.js`; `tests/tixl_batch11_bool_aggregate_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-11-bool-aggregate-proof.vuo` -> `my_Batch11BoolAggregateProof.anyValue`; Vuo-saved image `artifacts/vuo_cli/batch-11-bool-aggregate-vuo-save.png` | done |

## Batch Notes

- `my_All` preserves TiXL's non-vacuous empty-input behavior: empty input returns `false`, not `true`.
- `my_Any` preserves TiXL's empty-input behavior: empty input returns `false`.
- Both nodes use TiXL value color `ColorForValues #868C8D`, matching their `bool` primary output.
- Vuo CLI proof `batch-11-bool-aggregate-proof` compiled and linked with return code `0`, loaded all three custom nodes, opened an onscreen runner window, and produced Vuo-rendered PNG evidence at `artifacts/vuo_cli/batch-11-bool-aggregate-vuo-save.png`.
- Current macOS window capture failed with `could not create image from window`, matching previous batch reruns; this remains classified as a capture-layer issue. The Vuo-native saved image is non-black: `960x540`, average luma `0.29383`, bright ratio `0.443702`, `mostlyBlack=false`.
