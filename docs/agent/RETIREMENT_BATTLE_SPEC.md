# 廢棄扁平化節點退場戰役 — 執行藍圖（柏為 2026-07-08 令，Plan agent 對碼產出）

> 復合戰役主戰場：151 顆壓平複合裡**可退 139 顆**（扣 collapse-8）換成巢狀 .t3 複合。機器齊（遞迴 keystone + .t3ui 排版 + 8 catalog 範本）。

## 0. keystone 真相（全案依此）——name vs guid key domain
`findSpec()`(`node_registry.cpp:100-172`) 解析序寫死：`registry()` 烘焙表 → 15 個 atom sink → **最後** `dynamicSpecs().find(type)`(`:170`)。而 `dynamicSpecs` 由 `refreshCompoundSpecs`(`graph_bridge.cpp:32-37`) key=**Symbol.id=guid**，`specFromSymbol`(`:5-7`) 令 `spec.type=s.id`=guid。
- flat atom「X」→ type=="X"(人名)；匯入 X.t3 的複合 → type==guid、name=="X"。
- **兩者 key domain 不同 → 今天並存但永不相遇**：所有引用(`graph.cpp:62 makeNode("X")`/`document_io.cpp:319`/`compound_save.cpp:22` uuid 表/.swproj)用**人名**→ 永遠只命中 atom；複合躺 guid key 沒人碰。`node_registry.cpp:88-91`「複合不能 shadow atom」真因是 key domain 不同、非「atom 贏」。
- **純刪 flat atom「X」→ `findSpec("X")` 回 nullptr**（sink miss + dynamicSpecs guid-key 也 miss 人名）→ makeNode 建不出 port、libFromGraph 丟節點、預設圖/demo 啞掉。**這是戰役 keystone gap。**

## 1. replace-in-place 機制（承重單點，全案僅改一次）
在 `graph_bridge.cpp:refreshCompoundSpecs` 對每個非原子 symbol **多插 name key**（或建 name→spec 別名索引）；在 `node_registry.cpp:170` findSpec 尾端 guid-miss 後**加 name-fallback**（找 `dynamicSpecs` 內 `title==type` 的複合）。
- **極性鐵律**：name-fallback **排在所有 atom sink 之後**。flat atom 在→sink 命中 atom（複合摸不到，零行為改變）；atom 退場→sink 全 miss→name-fallback 命中同名複合→**引用自動接管**。退場+接管同一 build 原子完成、**腳手架字串零改**。

### 一顆 X 退場替換流程
1. **補材**：`X.t3`+`X.t3ui` 進 `assets/catalog_t3/`（`catalog_boot.cpp:30 loadCatalogFromFolder` 掃入，:109 遞迴 resolver + layout resolver 已 live）。
2. **驗接管前置**：複合 boot-load 成功 + import warnings 無 "unmapped skipped"。
3. **退場 flat atom**：刪葉 `<family>_<stem>.cpp`（CMake `file(GLOB)` `CMakeLists.txt:180` 自動 drop）+ `<stem>_params.h` + registrar 行 + forward decl + selftest 行 +（若獨立 golden）刪 golden+其 selftest 行。**8 collapse 顆的 `*_t3_embed.inc` 不刪**。
4. **驗接管後置**：`findSpec("X")` 回複合 spec（evaluate==nullptr 走 resident inline）；graph.cpp/demo/compound_save 對「X」引用全解析到複合。
- **先刪後補必炸**：步驟 3 先於 1/2 → findSpec nullptr → 啞。補材+name-fallback 先在位才安全。

## 2. ready-set 判定（機械可 script，隨映射補齊單調成長）
三條全過才 ready，全由「拿 X.t3 餵 production importer」求值：
- **R1** .t3 存在（`external/tixl/…/X.t3`）。
- **R2** 全葉映射∨遞迴成功：餵 `importT3Symbol`(`t3_import.cpp:120`)+catalog resolver → warnings **零 "unmapped SymbolId … skipped"**(`:219`) 且結果 `atomic==false`+children 非空。
- **R3** 無 code-op 卡點：子節點 guid 不落 framework code-op 集(`_multiImageFxSetup*` `t3_import_maps_collapse.cpp:21-27`、`ComputeShaderStage`/`StructuredBufferWithViews`/`TransformMatrix` `t3_import.cpp:220`)。R2 會自動擋。
- **R4（2026-07-09 加，mesh+point 兩 lane 試壓實證）compute-stage kernel 已 ported**：R1-R3 只驗**結構**、不驗 cook。多數 flattened 是 GPU-compute 複合走 generic `ComputeShaderStage`，退場後要真 cook；kernel 未 port（`kernelNameFor()` `buffer_ops_computeshaderstage.cpp:57-67` 無列 + 無 `app/shaders/computeshaderstage_<op>.metal`）→ PSO miss → `if(!pso) return` → UAV 未寫 → ②parity RED。probe 須加 R4：任一 compute 子節點 kernel 未 ported → NOT-READY。**R4 必要非充分。**
- **R5 production takeover cook 正確（超越 kernel 存在）**：TransformMesh 過 R4 卻卡 R5——golden 只靠 test-only PbrVertex stride 64→80 override(`SwVertex`80B/PbrVertex64B)+vec3-wire-lands-on-head fork 才 cook 對，production 無此 fork→退了溢位。修在 `t3_import_maps.cpp`＝「烤進 catalog asset vs 泛化進 importer」**架構分岔=柏為決策**。所有 mesh-compute 退場卡此。
- **★★框架修正**：退場**非機械家族收割，它騎在 GPU-compute kernel porting 上**（見 memory `retire-gated-on-kernel-porting`）。真工＝逐顆 port kernel（＝GPU-compute 複合重放軌），port 完退場才 trivial。**別再盲派家族機械 sweep**（mesh+point 兩 lane 已證零產出）。可機械退殘量＝非 compute 純子圖 or kernel 已 ported，用 probe-with-R4 掃真 ready-set。2 pilot(CombineBuffers 非compute / TransformPoints kernel早port+point無stride)能退非通例。
- **實作**：binary 加 `--probe-import <t3>` dump warnings headless 模式（或掃 catalog boot stderr `[catalog] X: …unmapped…`）。逐顆掃 `node_health --tsv | awk '$3=="flattened"'`。
- **估量**：初期 ready ~15-40（卡 `swTypeForSymbolGuid` 今 87 guid 覆蓋率）；節奏=補 `t3_import_maps.cpp` 映射→ready 長→sweep 收割，**迭代非一次退 139**。

## 3. collapse-8 = 不可退、正確終態（從廢棄名單移出）
`swTexOpForCollapseRootGuid`(`t3_import_maps_collapse.cpp:33-84`) **恰 8 顆**：HSE/Blend/BubbleZoom/NGonGradient/RadialGradient/BoxGradient/RemapColor/LinearGradient。collapse 複合(guid,atomic=false)的**唯一實體子節點就是那顆 flat atom**(`t3_import_collapse.cpp:257`)。反轉需 framework 內部 op(`_multiImageFxSetupStatic` cc34a183)的真原子=code-op、sw 沒有也不該有。
→ **8 顆是「guid 薄殼複合包 flat atom」的正確終態、flat atom 是實作本體不可刪。sweep 的 ready 判定必須硬排除這 8 root（`t3_import_maps_collapse.cpp:33`），否則刪 flat atom 同時 gut 掉 collapse 複合。** 可退真數 = 139。
（註：`*_t3_embed.inc` 13 檔 = 8 collapse + 4 純子圖[CombineBuffers/DisplaceMeshNoise/TransformMesh/TransformPoints] + 1 root 測材[DrawPointsDOF]。別跟 collapse-8 混。）

## 4. 分批 + owner-lock
- **批 A safe/130 資料驅動**：`retire_table.tsv`(op/family/leaf/registrar/has_separate_golden) + sweep script 逐行刪 4 樣（**非手工 130 次**）；每顆進 sweep 前過 §2 ready 判定。
- **批 B deps/21 先修腳手架**：引用全人名→name-fallback 自動導向、**字串零改**。唯一真手術=`compound_save.cpp:22-34` type→uuid 表（手寫 uuid，退場後「X」仍需穩定 uuid 供存檔→保留該行或改指複合 guid）。deps 全在腳手架非場景（repo .swproj/.scn 對 flattened 零綁定，退場場景層零風險）。
- **批 C collapse/8 不退**（§3）。

| 檔 | lock |
|---|---|
| `node_registry.cpp`(findSpec name-fallback)+`graph_bridge.cpp`(name 別名) | **序列瓶頸·機制主戰場 LOCK（先落地）** |
| `t3_import_maps.cpp`(補葉映射→ready 覆蓋率) | 序列瓶頸（全家族共用） |
| `assets/catalog_t3/`+`catalog_boot.cpp` | 序列（每批補材動） |
| 各家族 leaf `*_ops_*.cpp`+`*_params.h`+registrar split+selftest 行 | **worktree 並行**（image/point/mesh/particle/field 各一 lane，registrar 已 split） |

## 5. harness（每退一顆四閘全綠，期望值一律 TiXL 常數非 sw 自輸出 P5）
1. **①接管閘**：`findSpec("X")` spec `evaluate==nullptr` + `lib.find(guidX)->atomic==false`+children 非空。injectBug=保留 flat atom registrar→sink 搶先命中 atom→diverge（證 name-fallback 真在最後、真被 atom 遮）。
2. **②parity 閘**：`buildEvalGraph→cookResident` readback 對閉式，期望手推自 X 的 TiXL .cs/.hlsl 常數（引行號），probe 坐非恆等中段+跨層 override（咬 §1.3）。復用現成 oracle（TransformPoints host-matrix `t3import_transformpoints_golden.cpp:20-37`）。
3. **③引用閘**：`makeNode("X")` 或載入引用 X 的 demo lib，斷言解析到複合+cook 正確。injectBug=`t3RecurseDisable()`→複合建不出→nullptr→diverge。
4. **④排版閘**：import 帶 .t3ui 的 X→子節點 x/y 非全 0 且互異（=.t3ui Position）。injectBug=不餵 .t3ui→全 0→diverge。
- 過廠=四閘 measured RED→GREEN + `golden_lint.sh` 綠 + `--bite` 該顆咬 + did-not-trip `return 0`（避 P1）。

## 6. 風險假設
1.【高】name 唯一性：catalog 內同名複合須定勝負；擴量前加唯一性 lint。假設 catalog name 唯一。
2.【高】同名共存期 shadow 序：退場 build 內 registrar 行漏刪(forward decl/selftest 仍 registerXOp)→atom 搶先→假綠。①閘 injectBug 咬這條；sweep 要 nm 掃「registrar 符號沒 link 進去」。
3.【高】deps 腳手架換節點後 selftest 紅：`compound_save.cpp:22-34` uuid 表 + `compound_save_selftest.cpp:95` 依賴 atom uuid→保留 uuid 表行。
4.【高】collapse-8 誤入 sweep→刪 flat atom gut 掉複合。ready 判定硬排除 8 root。
5.【中】缺 .t3ui 的顆：`catalog_boot.cpp:72-74` 已容忍（留 0,0 不失敗）→仍能退，④閘降 N/A 非紅。
6.【中】ready 天花板：多數卡未映射葉→戰役是迭代收割非一次 139。
7.【中】nested outputDef 預設 Texture2D→pin 型別錯標、cook 值不受影響→容忍。

## 7. pilot（先落地 §1 機制骨幹，再退 3 顆證機制）
1. **TransformPoints**（point，頭號）：純子圖已證、oracle+embed+.t3ui 齊、跨層能咬、deps:2 純腳手架。證「有引用的 flat atom 能被複合原子接管」。
2. **CombineBuffers**（point，11 行全裸純子圖，embed 現成，deps:1）：證機制非 TransformPoints 專屬。
3. **一顆 safe/deps:0 point 純子圖**（§2 script 掃出 ready∩safe 首顆）：證零腳手架修改的資料驅動 sweep 路徑。
pilot 全綠→開各家族 worktree 平行收割 139（扣 collapse-8）。collapse-8 不進 pilot/sweep。

## Critical Files
- `app/src/runtime/node_registry.cpp`（findSpec 加 name→compound fallback，機制主戰場）
- `app/src/runtime/graph_bridge.cpp`（refreshCompoundSpecs 補 name 別名索引）
- `app/src/app/catalog_boot.cpp`（補材入口）
- `app/src/runtime/t3_import_maps.cpp`（swTypeForSymbolGuid 葉映射=ready 覆蓋率瓶頸）
- `app/src/runtime/t3_import_maps_collapse.cpp`（collapse-8 表=sweep 硬排除集）
