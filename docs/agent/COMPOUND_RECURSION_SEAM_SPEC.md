# 匯入器遞迴 seam — 施工規格（keystone，2026-07-08）

> 復合戰役的總上游閘。補好這根 = 151 顆手鍛 flat atom 能被各自 TiXL .t3 重放成巢狀複合取代 + TiXL 教學/example .t3 能匯入。
> **狀態：藍圖已定（4 scout + 1 Plan 對過碼），測材篩選中，build 未開。**

## 已確認事實（file:line，直接信不重查）
- 巢狀複合機器**已 live**：巢狀模型 `compound_graph.h`、resident 遞迴 inline `resident_eval_flatten.cpp:27-33,65-69`（compound child 遞迴、depth 64、onPath 守衛，**已 live 不用改**）、複合→NodeSpec 註冊 `graph_bridge.cpp:5,32`、dive-in `editor_ui.cpp:252`、boot catalog `catalog_boot.cpp:29-80`+`main.cpp:329`、8 顆真 .t3 在 `assets/catalog_t3`（`app/CMakeLists.txt:1001`）。
- **唯一缺口 = 匯入器單層**：`t3_import.cpp:211-216`——子節點 unmapped 即 `skip`；`:217-222` 灌 atom 是唯一出路；不遞迴匯入「子節點本身是複合」。
- collapse 岔路 `t3_import.cpp:176-185`→`t3_import_collapse.cpp`（image-fx 專用，root-guid keyed，表在 `t3_import_maps_collapse.cpp:33-84`）。
- importer 是 pure text-in（`t3_import.h:43`）——**runtime 葉子不能碰 filesystem**。
- 循環守衛 `compound_graph.h:279 addChildWouldCycle`（transitive）。

## 關鍵約束
1. **只遞迴「純子圖複合」，排除 code-op**：`_Execute*`/framework op（如 `_ExecuteCombineBuffers` `t3_import_maps.cpp:93`）在 .t3 看似複合，但 C# `.cs` 有手寫 Update/Slot 邏輯 = code-op，被手鍛成 sw atom，**遞迴會匯成錯空殼**。判準：nested child 的 .cs 無手寫 Update/Slot = 純子圖 ✓。
2. **runtime 保持葉子**：guid→.t3 路徑解析走注入 callback（照 `setAssetTextureDecoder` 範式 `main.cpp:365`）。app（`catalog_boot.cpp`）建 `guid→json` 索引（用 `symbolIdOfT3` peek `t3_import.cpp:115`）綁 resolver；`importT3Symbol` 加 overload 帶 `T3Resolver`，null 時退化成今日單層（零 churn）。
3. **compound_graph.h 零 schema 改動**——巢狀 Symbol 是 native model，不與序列化/存檔 lane 撞。最大安全點。

## 演算法（改 t3_import.cpp:211-216 skip 分支）
```
childType = swTypeForSymbolGuid(symbolId)
if childType 非空:            灌 atom（今日路徑不動）
else if resolve && !lib.count(guid) && resolve(guid, childJson):
    if addChildWouldCycle(lib, sym.id, guid): warn+skip
    importT3Symbol(childJson, lib, &nestedId, warnings, resolve, depth+1)  // 遞迴，硬 depth 上限=64
    childType = guid          // 父 SymbolChild 引用 guid
else:                         warn("unmapped, skipped")  // fail-soft 維持今日
```
- 去重：`lib.symbols.count(guid)` 已有就跳遞迴直接引用。
- **§1.3 巢狀 override/跨層 slot 解析（隱藏必修，同批做）**：`swSlotNameForGuid` 對複合 guid 回空→override/connection 靜默掉光。修法：slot 解析加 fallback——若 swType 是 lib 裡複合（`atomic==false`），slotGuid→lc 直接對 nested Symbol 的 `inputDefs[].id`（`t3_import.cpp:237` override 端 + `:291-296` connection 端）。**不修=巢狀進來但父參數/接線全空，parity 假綠。**

## Golden（硬閘、第一 deliverable）
- **★測材已鎖定（2026-07-08 篩選 scout）**：
  - Root：`external/tixl/Operators/Lib/point/draw/DrawPointsDOF.t3`（67 children，**恰 1 顆** TransformPoints 實例，其 `Points`(565ff364) 由 **ROOT-INPUT 薄殼直餵**→pin 無歧義）。
  - Nested：`external/tixl/Operators/Lib/point/transform/TransformPoints.t3`（經 .cs 確認**純子圖**：`TransformPoints.cs` 無 `protected override void Update`，`ITransformable`/`TransformCallback` 只是 viewport gizmo 拖曳鉤不參與 cook；8 顆葉原子全在 `t3_import_maps.cpp:51-97`）。
  - 兩層：DrawPointsDOF(複合)→TransformPoints(複合)→8 顆映射原子。匯入器**必須遞迴建 TransformPoints 子複合**=受測 seam。
  - **oracle 複用現成** `t3import_transformpoints_golden.cpp:20-37`（point-buffer 閉式 host-matrix：fixture `t3xf_input_points` 餵已知 N 點、在匯入後 `TransformMatrix` child 蓋常數 SRT override、cook 讀回 buffer 比對主機矩陣）。**新 golden 只改一件**：TP.t3 從 root 改成嵌在 DrawPointsDOF.t3 內的 nested child，fixture/override/oracle 全不變。
  - **★中段 pin（非全圖零 skip）**：嚴格「零 skip」結構性不存在（root 側 draw 葉子不映射）。退路=pin 打**巢狀 TransformPoints 的輸出 buffer**（中段），draw 支線 skip 落在 pin 依賴錐外不影響 parity。命中記憶 `replay-golden-pins-must-sample-diverging-middle`。
  - **§1.3 邊界必須被咬**：probe/override 要跨 DrawPointsDOF→TransformPoints 邊界（TP 的 Points 由 root-input 餵=connection 跨層解析；override 下在 TP 自己的 transform 參數=override 跨層解析），否則 §1.3 slot fallback 沒被測到。
  - 備選：`VisualizePoints.t3`（同薄殼但 6 個 TP 實例，pin 需指定 instance，次選）；若要巢狀零 code-op 疑慮可換 `CombineBuffers`（11 行全裸）但無薄殼 root 乾淨餵、oracle 較弱。
- **測材嵌入**：父+nested 兩顆 .t3 都 embed 成 `*_t3_embed.inc`（照 `transformpoints_t3_embed.inc`），golden 內建 in-memory `T3Resolver`（map<guid,json>），走 production `importT3Symbol(...,resolver)`，headless 可跑。
- **結構斷言（遞迴靈魂）**：import 後 `lib.find(nestedGuid)->atomic==false && children 非空`（證建出巢狀子複合非 skip 非壓扁）+ 父有 `SymbolChild.symbolId==nestedGuid` + warnings 無 "unmapped skipped"。
- **Parity 斷言**：`buildEvalGraph→cookResident` readback 對閉式。**oracle 手推自兩層各自 TiXL .cs/.hlsl/.t3 常數（引行號），絕不拿 sw 自身輸出當錨（P5）。probe 坐非恆等中段（P2），且落在「父對 nested child 下 override」的參數上（才咬得到 §1.3）。**
- **injectBug（真 cook seam）**：新增 `t3RecurseDisable()` 旗標→關遞迴→nested 退 skip→下游發散。did-not-trip 則 `return 0` 進 NO-BITE 名單（反型 P1）。讀 `docs/agent/GOLDEN_STANDARD.md` 三特徵+五反型。

## 檔案影響面 + owner-lock
| 檔 | 動作 | lock |
|---|---|---|
| `runtime/t3_import.cpp` | 遞迴分支+slot fallback+resolver 參數（現 384 行逼近 400 閘，**遞迴邏輯很可能拆 `t3_import_recurse.cpp` 保線**） | **主戰場 LOCK** |
| `runtime/t3_import.h` | `T3Resolver` 型別+overload+`t3RecurseDisable()` | LOCK |
| `app/app/catalog_boot.cpp` | guid→json 索引+綁 resolver（app zone 做 fs） | LOCK |
| `runtime/t3import_nestedcompound_golden.cpp`+2 `*_t3_embed.inc` | 新 golden+測材 | 新 |
| `selftests_point.cpp:160-172` | 註冊一行 | 附加 |
| `app/CMakeLists.txt` | 加 golden TU（+可能 recurse TU） | 附加 |

**不碰**：`compound_graph.h`（零 schema）、`t3_import_collapse.cpp`/`t3_import_maps_collapse.cpp`（collapse 12 退場是獨立後續 lane）、`resident_eval_flatten.cpp`（遞迴 inline 已 live）。

## build 節奏（一條 lane、harness-first、sequential——全咬 t3_import.cpp 不可平行）
1. 前置：鎖測材（scout 進行中）。
2. Golden RED：embed 兩 .t3+in-memory resolver+結構斷言+parity 斷言+`t3RecurseDisable` 牙。此刻 measured RED。`golden_lint.sh`+`--bite`。
3. 接 resolver 葉子接縫（app zone `catalog_boot.cpp`+`t3_import.h`）。
4. 接遞迴+slot fallback（`t3_import.cpp`，必要時拆 `t3_import_recurse.cpp`）→golden GREEN+牙咬。
5. `run_all_selftests.sh --bite` 全綠 + 8 顆 collapse golden + TransformPoints/CombineBuffers 不回歸（證遞迴沒污染單層/collapse）。

## ★後續 lane：.t3ui 排版（柏為 2026-07-08 觀察「鑽入複合子節點全擠一起」）
**緊接 keystone 落地後做，owner-lock 同 t3_import.cpp + catalog_boot.cpp，不可與 keystone 並行。** 一次修好→之後 151 顆退場替換每顆帶 .t3ui 就自動有 TiXL 排版。

**根因（scout 07-08 確認）**：座標**不在 .t3**，在 sibling **`.t3ui`**（TiXL 序列化到另一檔，key `"Position"`）。sw `assets/catalog_t3/` **只帶 .t3、無 .t3ui**（`find app -name "*.t3ui"` 空）→ 8 顆複合 child 全落預設 (0,0)。
- SymbolChild 有座標欄 `compound_graph.h:103 float x,y=0`；dive-in 直接餵 `editor_ui_layout.cpp:33 SetNodePosition(child.x,child.y)`，**無 x==0 fallback→座標有值就照擺，UI 零改動**。
- 匯入器 `t3_import.cpp:228-263` 建 child **完全沒碰 x/y**（核心缺口）。
- TiXL 格式：`TransformPoints.t3ui` `SymbolChildUis[]` 每項 `{"ChildId": guid, "Position": {"X":.., "Y":..}}`；定義 `external/tixl/Editor/UiModel/SymbolUiJson.cs:93/114-115`(寫)、:417(讀)。
- **座標系不用轉換**（兩邊 Y 向下、同單位、直接 copy），`NavigateToContent` 吸收絕對偏移。

**兩步修**：
1. **帶 .t3ui 進來**：補進 `assets/catalog_t3/` 每顆 .t3 旁的 .t3ui，或 catalog_boot 從 `external/tixl` 原地讀 sibling。
2. **importer 解析**：`importT3Symbol` 加 .t3ui JSON 參數/overload，解析 `SymbolChildUis[]`：`ChildId`(guid)→經現有 `childGuidToId` map→int childId→寫 `child.x=Position.X, child.y=Position.Y`。順手讀 `InputUis/OutputUis[].Position` 填 `SlotDef.x/y`（`compound_graph.h:46`）讓邊界 pin 不疊原點。
**harness**：import 一顆帶 .t3ui 的複合→斷言 children 的 x/y **非全 0 且互異**（等於 .t3ui 的 Position 值）；injectBug=不讀 .t3ui→全 0→斷言 diverge。

## 風險假設
1. 【高】生產 guid→.t3 索引根：現 `assets/catalog_t3` 僅 8 顆。真要取代 151 手鍛 atom，索引根要擴（掃 Lib 全量或 curated 子集）。**golden 用 in-memory resolver 繞開，生產化是獨立決策，本 seam 先成立不預設擴 925。**
2. 【高】純子圖 vs code-op 辨識：生產化需一個「這 guid 是不是 code-op」判準（現只能靠「已手鍛」或「在 collapse 表」反推）。最大語義未知。
3. 【中】§1.3 巢狀 override/跨層 slot——同批硬需求。
4. 【中】nested outputDef 型別推斷（`t3_import.cpp:365` 預設 Texture2D，值/point 域會錯標 pin 型別，cook 值不受影響→先容忍）。
