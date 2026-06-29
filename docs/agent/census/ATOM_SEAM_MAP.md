# ATOM_SEAM_MAP — 原子量產普查（新方法 Phase-A 普查 SSOT）

> 2026-06-29 柏為拍板全面換兩軌（原子手刻 + 複合 .t3 重放，spike 已證 rail 成立）後的量產地圖。
> read-only scout 親數 code/.t3，非引文件（柏為血證「信文件不讀 code」根線）。**這是「哪批立刻開、
> 哪條縫先蓋、什麼卡住」的量產選批 SSOT。** shader 桶結論為初判，待 shader-codegen 深讀 scout 收尾。

## 數字（親數核對）
- 總算子 925（`find Lib -name '*.t3' | wc -l`）= **原子 569**（`"Children": []`）+ **複合 356**（`"Children": [`+換行）。
- 交叉驗零異常：Children 空∧Connections 空 = 569；569+356 = 925 全覆蓋。
- 核對 ROI 估 925/563/349：差 ≤1%，**估值成立**。

## ★翻盤：「~200 shader 原子卡死」是恐慌誤判
- shader-codegen 葉子實 **48 個**，其中 **47 是 field/SDF（sw 已解決**，`field_graph_builder.cpp` codegen seam 運作中），真未解 render-shader 葉子 **≈1**（`SetPixelAndVertexShaderStage`）。
- 「200 shader 節點」絕大多數是**複合（~174）**——走 .t3 重放軌，**不是要手刻的原子**。真正卡 shader-codegen 的原子是個位數。
- ⚠ 待深讀確認的一個 subtlety：shader 複合照 .t3 重放給的是「圖結構」，要真跑出 shader 仍可能需要 codegen（圖→可執行 shader）。複合是否真繞過 codegen＝shader-codegen 深讀 scout 收尾。

## 原子按 seam 分桶（569，總和對帳）

| 桶 | 顆數 | 需要的 seam | sw 現狀（讀 code 證） | 狀態 |
|---|---|---|---|---|
| 純值/數學/邏輯 | **331** | value-op 自登記 sink | ✅ 已齊（`value_op_registry.h:1-58`，85 個活範例） | **立刻開** |
| list 轉換/聚合 | **24** | list-op registry | ✅ 已齊（22 個 floatlist/host_scalar/stringlist） | **立刻開** |
| field/SDF | **47** | field-graph codegen | ✅ 已齊（43 個 field_ops_*sdf shipped） | **立刻開** |
| buffer-marshalling | **49** | raw-Buffer currency + 自登記 | ⚠️ 差一段（spike 步 3 缺口） | **補完即開** |
| dx11-render-state | **41** | dx11-wrapper | ❌ 哲學不合（sw opinionated command stream，`render_command.h:35-110`） | 卡設計 |
| texture | **24** | 逐顆手刻 .metal | ◐ 有路徑無 codegen | 慢工 |
| io（midi/osc/udp/video） | **33** | 各自平台 I/O | ❌ 雜散無共用 seam | 卡散 |
| audio | **17** | audio-ingest | ◐ 部分已有 | 部分 |
| gpu-compute/no_cs | **3** | 特例 | — | 邊角 |

## 決策地圖

### 現在就能無縫量產（seam 已齊）= **402 顆**（331 值 + 24 list + 47 field/SDF）
三條 seam 都有 shipped 消費者。各走自己 registry，無共享撞點 → **可並行開大。**

### 差一小段（補完即開）= **49 顆**（buffer-marshalling）= ★第一優先
缺口具體 2 件（= spike 步 3，步 1/2 已備 80%）：
- (a) production cook 加一條 **raw `Buffer` currency**（`point_graph_cook_ctx.h` 現有 14 種 currency
  `Float/FloatList/ColorList/Command/Curve/Field/Gradient/Mesh/ParticleForce/PointList/Points/String/StringList/Texture2D`，**無 Buffer** → 加欄 + `dataType=="Buffer"` wire 分支）。
- (b) buffer 原子 cook 自登記（仿 value_op_registry，把 spike `floats_to_buffer.cpp` 從 standalone 接進 driver）。
**為何第一蓋**：解鎖 49 原子 + FloatsToBuffer 是 **208 個複合（58%）的 keystone**（`floats_to_buffer.h:6`）→ 同時解鎖純原子量產 **和** .t3 重放軌的複合 cook。ROI 最高單點。

### 卡大難題（設計未定，先別碰）
- dx11-render-state 41：撞 command-stream 哲學，需重設計抽象。
- 真 shader codegen：葉子個位數（待深讀），複合 174 走 .t3 重放軌。
- io 33 / texture 24 / audio 17：散，各自平台 seam，逐顆人工，非阻塞但慢。

### 量產施工圖（順序）
1. **並行開 402 顆**（seam 已齊，三 registry 各走，立刻）。
2. **蓋 buffer-currency seam（spike 步 3）→ 開 49 顆 + 解鎖複合 keystone**。
3. 複合 .t3 重放軌（待 buffer keystone + 步 3 多原子串接驗）。
4. 延後桶（dx11/io/texture/audio/真 shader）按設計就緒逐批。

## stale 落差（文件 vs code）
- 「~200 shader 原子卡死」言過其實（真 ≈1，200 是複合）。
- 「真原子只做 ~102」在「未壓平純原子」語意下大致對；sw 實際 op 檔數遠超（85 value+191 point+43 field…），但多數 point_ops 是壓平的複合（要停的東西）。
- production 巢狀/resident 非 flat（再次實證，cook ctx 全走 point_graph_resident*）。

## Critical Files
- `app/src/runtime/point_graph_cook_ctx.h` — currency ABI，buffer-currency seam 主戰場
- `app/src/runtime/value_op_registry.h` — 已齊自登記 seam 範本（list/buffer 照抄）
- `app/src/runtime/pointlist_ops_listtobuffer.cpp` — 現有 host→GPU buffer workaround，raw-buffer 起點
- `app/src/runtime/render_command.h` — opinionated render-state，dx11 桶卡點根源
- `app/src/runtime/field_graph_builder.cpp` — field/SDF codegen（已解 47 顆）
