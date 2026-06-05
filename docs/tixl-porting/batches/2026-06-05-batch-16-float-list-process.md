# Batch 16 Float List Process Acceptance Matrix

Scope: three `Lib.numbers.floats.process` nodes: `AnalyzeFloatList`, `SumRange`, and `CompareFloatLists`.

Gate: every row needs TiXL source/default evidence, semantic edge cases, Vuo C source, installation into the Vuo user module folder, and a Vuo-visible proof that consumes the exact `my_<TiXLName>` node output.

Vuo body-layer note: TiXL `List<float>` maps to `VuoList_VuoReal`. `SumRange` exposes TiXL's empty/null early-return cache behavior through an explicit `previousSelected` input. `CompareFloatLists` is exact for same-length and empty-list cases; non-empty length mismatch is a bounded adapter because the TiXL C# source can index out of range in that branch.

| TiXL node | grade | Vuo title | Vuo source | source evidence | semantic/source tests | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.numbers.floats.process.AnalyzeFloatList` | B adapter | `my_AnalyzeFloatList` | `vuo-nodes/my.numbers.floats.process.analyzeFloatList.c` | C# `external/tixl/Operators/Lib/numbers/floats/process/AnalyzeFloatList.cs`; `.t3` `external/tixl/Operators/Lib/numbers/floats/process/AnalyzeFloatList.t3` | `tests/tixl_batch16_float_list_process_semantics.test.js`; `tests/tixl_batch16_float_list_process_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-16-float-list-process-proof.vuo` -> `my_Batch16FloatListProcessProof.minValue/maxValue/averageMean/allValid`; Vuo-saved image `artifacts/vuo_cli/batch-16-float-list-process-vuo-save.png` | done |
| `Lib.numbers.floats.process.SumRange` | B adapter | `my_SumRange` | `vuo-nodes/my.numbers.floats.process.sumRange.c` | C# `external/tixl/Operators/Lib/numbers/floats/process/SumRange.cs`; `.t3` `external/tixl/Operators/Lib/numbers/floats/process/SumRange.t3` | `tests/tixl_batch16_float_list_process_semantics.test.js`; `tests/tixl_batch16_float_list_process_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-16-float-list-process-proof.vuo` -> `my_Batch16FloatListProcessProof.sumRangeSelected`; Vuo-saved image `artifacts/vuo_cli/batch-16-float-list-process-vuo-save.png` | done |
| `Lib.numbers.floats.process.CompareFloatLists` | B bounded adapter | `my_CompareFloatLists` | `vuo-nodes/my.numbers.floats.process.compareFloatLists.c` | C# `external/tixl/Operators/Lib/numbers/floats/process/CompareFloatLists.cs`; `.t3` `external/tixl/Operators/Lib/numbers/floats/process/CompareFloatLists.t3` | `tests/tixl_batch16_float_list_process_semantics.test.js`; `tests/tixl_batch16_float_list_process_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-16-float-list-process-proof.vuo` -> `my_Batch16FloatListProcessProof.compareDifference`; Vuo-saved image `artifacts/vuo_cli/batch-16-float-list-process-vuo-save.png` | done |

## Batch Notes

- `my_AnalyzeFloatList` preserves TiXL's invalid-value rule: only finite values update min/max/sum, but `AverageMean` divides by the full list count. Empty input returns NaN/NaN/NaN and `AllValid=false`. A non-empty all-invalid list returns `+Infinity`, `-Infinity`, `0`, and `false`.
- `my_SumRange` uses TiXL's half-open `[LowerLimit, UpperLimit)` bounds, clamps lower to `0`, clamps upper to list count, and outputs `0` when lower is greater than or equal to upper. `.t3` creator-facing `UpperLimit=999999` wins over the constructor's `0`.
- `my_SumRange` keeps TiXL's empty/null "do not assign Selected" behavior through `previousSelected`; this makes the cache behavior visible instead of hiding it in Vuo node state.
- `my_CompareFloatLists` returns `1` if either list is null/empty and counts only differences where `abs(a-b) > Threshold`; equality at the threshold is not different.
- `my_CompareFloatLists` does not claim exact TiXL crash parity for non-empty length mismatches. The TiXL source checks `list.Count < index` instead of `index >= list.Count`, so the shorter list can be indexed out of range. The Vuo body treats missing elements as different and records that bounded divergence.
- Vuo CLI proof `batch-16-float-list-process-proof` compiled and linked with return code `0`, loaded all four custom nodes, opened an onscreen runner window, and produced Vuo-rendered PNG evidence at `artifacts/vuo_cli/batch-16-float-list-process-vuo-save.png`.
- Current macOS window capture failed with `could not create image from window`, matching previous batch reruns; this remains classified as a capture-layer issue. The Vuo-native saved image is non-black: `960x540`, average luma `0.19908`, bright ratio `0.546265`, `mostlyBlack=false`.
