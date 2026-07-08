# snapshot 真渲染縮圖牆 — 施工規格（柏為 2026-07-08 拍板「跟 TiXL 一樣的縮圖牆」）

> 批 2 lane，與 `.t3ui` 排版/namespace 不撞檔（動 app/variation + ui/variation_panel + main.cpp shell seam）。估工=中偏輕，不碰核心深水（隔離 cook-to-texture 機器已 live）。

## 現狀（scout 07-08 確認）
- 面板已在 `ui/variation_panel.cpp`（3×4 固定 12 格，功能綠、有牙）；每格現在只有**文字佔位**，柏為要換成**該 variation 的即時 render 縮圖**（照 TiXL）。
- **隔離 cook-to-texture 機器已 live 兩份**：`app/export_session.h`（`begin(settings,frozen lib,...)`→export-local PointGraph，`stepOneFrame()` cook→readback，完全隔離 live transport/warm pools）+ 裸 `PointGraph::cookResident(...)`+`target()/residentTexFor()`（selftests 大量用，如 `selftests_buffer_resident.cpp:91`）。
- **sw 不需要 atlas**：TiXL 4K atlas/LRU（`ThumbnailManager.cs:436` AtlasSize=4096/MaxSlots=500）是為 500+ 資產的本質複雜；sw snapshot pool 只 ~9-12 格，直接留 N 張 scratch texture。
- 節點縮圖管線（`node_faces.cpp:147 drawTexturePreviewFace`→`residentTexFor` borrow）**無法複用**：它只 borrow 單一 live target 的 cook，沒有「套 override→cook→抓」任何一段。

## override 套用/還原
- `buildBlendTowardsVariationCommand`/`buildNWayMixCommand`（`app/variation_apply.h:83/107`）套 override 但**改 LIVE 文件**（會污染 live，抓縮圖不能用）。
- 乾淨隔離=ExportSession pattern：copy `g_lib`→把 `DocVariation.parameterSets[childId][slotId]`（`variation_apply.h:60-72`）套到 copy 的 overrides→scratch PointGraph cook copy。無 undo churn、無 live 污染。`DocVariation` 型別現成、pool 已存這些值。

## TiXL 對照
`VariationThumbnail.cs:62` 貼 atlas UV 子矩形（純顯示）；真 render 在 `ThumbnailManager.SaveThumbnail:281-384`（178×133 temp RT letterbox-fit source→CopySubresourceRegion 進 atlas），source=套 variation 後離線 render 的 composition 輸出。**渲一次進快取、之後每 frame 只貼**。ThumbnailSize 160×(160/aspect)。

## 切法
**複用**：scratch `PointGraph`+`cookResident`+`target()`/readback（或 ExportSession 去掉 VideoWriter）。DocVariation apply-onto-lib-copy（新小 helper，鏡射 `buildBlendTowardsVariationCommand` per-slot 邏輯但寫 copy overrides 非 command）。
**新建**：
- `app/variation_thumb.{h,cpp}`：`cookVariationThumb(lib, slotIndex, res)->MTL::Texture*` + per-slot 快取(revision 失效) + **round-robin 一 frame 一格**。
- shell seam（mirror `residentNodeTexture`，放 `main.cpp:103` 旁）：`variationThumbTexture(int slotIndex)`。
- `ui/variation_panel.cpp drawSlotCell:30`：title 文字佔位換 `dl->AddImage(該 texture)`（cell 已有 p0/p1；aspect sw 16:9 `kCellAspect:23`，TiXL 4:3，若對齊改 133/178）。

## ★唯一承重力學（試壓點）
cook 整個 composition N 次不是免費（每次全圖 GPU cook）。**別每 frame cook 全 9-12 格**——照 TiXL async 佇列：一 frame round-robin cook 一格 + 快取到 dirty(snapshot 改或 graph revision bump)才重 cook。這是唯一會塌的點。
次要語義坑：有狀態節點(particle sims)離線一 frame cook 只給 frame-0 態非 live 收斂樣（TiXL 同病、可接受、標明即可）。

## harness-first（第一 deliverable，`--selftest-variation-thumb`，headless）
兩個 snapshot 對同一 Texture2D-output composition 給**不同**參數→cook 兩張 thumb→斷言 (1) 兩張 readback 像素**相異**(variation 真改了畫面) (2) cook thumb **前後 live 文件 `effectiveInput` 不變**(隔離)。**injectBug**=套到 live `g_lib` 而非 copy→live 污染牙咬。承重線=**隔離 + 真背離**。

## 柏為簽收（eye-hand）
功能 golden 過後仍要柏為/orchestrator 眼判「像不像 TiXL 縮圖牆」——開 Variation 面板截圖對 TiXL。標 `[待柏為簽收]`，不擋 loop。

## metal-cpp 紀律
碰 MTL::Texture 生命週期/AutoreleasePool/scratch cook，讀 skill `metal-cpp-discipline`。scratch texture 別每 frame 新建洩漏，快取住。
