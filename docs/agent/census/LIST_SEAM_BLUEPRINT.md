# LIST_SEAM_BLUEPRINT — ★ 結論：list-seam 已建，這是 FAN-OUT 不是 SEAM（2026-06-25 Plan 勘查）

> Plan agent a19cc06 read-only 勘查（2026-06-25 夜批）。**list value-type seam 早已全建**（FloatList/ColorList/StringList/PointList host-currency rails + host-scalar bridge + resident mirrors + goldens，散在前面 commit arc：`45ba597`/`7646140`/`0bb25e2`/`12aec28`/`d468c16` 等）。OP_BACKLOG 把 list-ops 誤歸 bucket C BLOCKED，**實為 READY-LEAF 機械 fan-out**。
> **Blast=LOW（不碰 cook-core spine）。柏為不需要。Sonnet 機械 fan-out lane，可並行（每 leaf 獨立檔，零 point_graph.cpp 編輯）。**

## 1. 已建的 seam（別重建）
list value = **host-side currency**（`std::vector<T>` 掛 `PointGraph::Impl`，非 GPU buffer，非騎 `NodeSpec::evaluate`）。四條 host-list cook-rail + 一個 bridge：

| Currency | dataType | Registry | Cook-walker (point_graph.cpp) |
|---|---|---|---|
| List\<float\> | `"FloatList"` | `floatlist_op_registry.{h,cpp}` | `cookFloatListNode` (:633) |
| List\<Vector4\>(color) | `"ColorList"` | `colorlist_op_registry.h` | `cookColorListNode` (:654) |
| List\<string\> | `"StringList"` | `stringlist_op_registry.cpp` | `cookStringListNode` (:709) |
| List\<Point\>(CPU) | `"PointList"` | `pointlist_op_registry` | `cookPointListNode` |

**Bridge（FloatList→Float，承重）**：list-consume 輸出 scalar 的 op（如 FloatListLength）不能騎 evaluate→由 **host-scalar branch**（`point_graph_hostscalar_cook.cpp`，dispatch @ `point_graph.cpp:741` via `findHostScalarOp`）cook：gather FloatList input→算 scalar→寫 `Node::outCache[outIdx]`；`evalFloat`(graph.cpp) 已從 hard-coded AudioReaction escape 泛化成 registry query（`isHostScalarOp`），resident_eval mirror 同步。
**IntList**：無 Int port→ints 溶成 Float（Cut32），IntsToList/PickIntFromList/IntListLength=free fold（但每顆 backward-trace .cs 確認無 overflow/bitwise）。
**新 op 只 self-register NodeSpec + cook fn 進現有 registry，現有 walker 自動消費，零 point_graph.cpp 編輯。**

## 2. Blast=LOW + 零柏為
不碰 cook-core spine（walker/dispatch/resident mirror 都在，table-driven `findFloatListOp`/`findHostScalarOp`）。additive（新 type 撞不到既有圖；既有 value op 都 `evaluate!=nullptr` 永不進 host-scalar branch=byte-identical）。每 leaf 獨立檔無共享編輯點。

## 3. Fan-out 波（keystone 已laid，純 fan-out）
**已 landed（skip）**：FloatsToList/FloatListLength/PickFloatFromList/ColorsToList/ColorList/CombineColorLists/KeepColors/ReadPointColors/SplitString/ZipStringList/JoinStringList/PickStringFromList/FloatListToString + PointList CPU 全家族。
**Wave 1 — FloatList→Float bridge consumers**（clone `host_scalar_ops_floatlistlength.cpp`，`hostScalarInjectBug()` RED）：SumRange / AnalyzeFloatList(Min/Max/Mean/AllValid multi-output 仿 PickStringPart) / IntListLength / PickIntFromList / CompareFloatLists / RandomChoiceIndex。
**Wave 2 — FloatList→FloatList producers**（clone `floatlist_ops_floatstolist.cpp`，`floatListInjectBug()` drop-last RED）：AmplifyValues / RemapFloatList / KeepFloatValues / MergeFloatLists / CombineFloatLists / IntsToList / MergeIntLists / SetFloatListValue / SetIntListValue。
**Wave 3 — 需小 NEW sub-currency（flag，低優先，唯一可能要柏為）**：ComposeVec3FromList(需 Vec3 output rail，先查存不存在) / GetListItemAttribute,GetPointDataFromList(PointList element attr) / SmoothValues,DampFloatList,DampPeakDecay,DeltaSinceLastFrame(stateful，clone KeepColors `34de721` state pattern)。
**解鎖 ~20-24 numbers list-op，主力 wave1-2 R1。**

## 4. Golden（chain-through-evalFloat，bridge-not-transport）
clone `floatlist_golden.cpp`/`list_routing_golden.cpp`。鐵律：①producer/consumer chain golden=`FloatsToList([...])`→op→**經下游 value op 用 `evalFloat` 讀回**（非只 `debugCookedFloatList` readback；transport-only readback 即使 bridge 壞也過=原 census bug）②closed-form vs TiXL `.cs` Update() 手算③RED tooth `floatListInjectBug()`/`hostScalarInjectBug()` 改真 cook output→golden 經 evalFloat 讀 RED，`--bite` FAILED:[] NO-BITE:[]④boundary：empty/single/OOB(consumer Mod count)/short-cell→0⑤flat+resident parity。

## 5. 風險/fork
- **別重建 seam**（最大風險，task premise stale）→確認已建，clone 兩 template。
- 無界 -j fork-bomb（夜批踩，`b0446e9` 修）→worktree build pin `-j 4` 別 override。
- 孤兒 test loop→`--bite` 跑一次 plain。
- list buffer lifetime=`std::vector` on Impl keyed `flatKey(id)`，自 size、無 GPU lifetime（只 ValuesToTexture own-R32Float 有 GPU ownership 已解）。
- empty-list edge=TiXL `list==null→0`，sw unwired→empty inputLists→Count 0 identical（複製每 .cs empty branch）。
- IntList int 語意=free fold only if 無 overflow/bitwise（每顆 backward-trace）。
- ColorList 無 CJK（Vec4）；CJK 只 StringList（已 handled）。
- Wave-3 Vec3/stateful=唯一可能要柏為 ping（new sub-currency）；stateful 有 KeepColors precedent。

## 6. 順序 vs 其他剩餘 seam
| 工作 | 類 | 成本 | 解鎖 | 裁 |
|---|---|---|---|---|
| **list-ops fan-out（本）** | 已建 seam leaf fan-out | trivial R1 | ~20-24 | **先做**，並行 Sonnet lane，零 seam 風險零柏為 |
| context-var(~15) | NEW seam | R1 小 | 15 flow | 最便宜未建 seam，要真 seam 就這 |
| datetime(~6) | leaf/小 | R1 | 6 string | 便宜 leaf，並行安全 |
| dx11-wrapper→camera3d(~50)/Layer2d(~37) | NEW承重 seam | R2-3 Opus | ~110 | 大承重，需柏為在場(cook-core spine) |
| shader-graph(~64) | NEW承重 seam | R1-2 Opus | field 島 | 大獨立島自含 |
| mesh-pipeline(~49) | NEW seam | R1 | mesh 島 | CPU-geometry 自含 |
**list fan-out=今晚最安全並行燃料；大承重 seam（dx11/camera3d）留柏為在場。**

## Templates（implementer clone 這些）
- `app/src/runtime/floatlist_ops_floatstolist.cpp` — producer leaf template（wave-2）
- `app/src/runtime/host_scalar_ops_floatlistlength.cpp` — bridge consumer template（wave-1）
- `app/src/runtime/floatlist_op_registry.{cpp,h}` — self-register sink + `floatListParam`（不編輯，看 contract）
- `app/src/floatlist_golden.cpp` + `list_routing_golden.cpp` — golden harness template
- TiXL ground truth：`external/tixl/Operators/Lib/numbers/floats/list/*.cs` + `.../basic/*.cs`
