# ATOM_SEAM_MAP — 原子量產普查（新方法 Phase-A 普查 SSOT）

> 2026-06-29 柏為拍板全面換兩軌（原子手刻 + 複合 .t3 重放，spike 已證 rail 成立）後的量產地圖。
> read-only scout 親數 code/.t3，非引文件（柏為血證「信文件不讀 code」根線）。**這是「哪批立刻開、
> 哪條縫先蓋、什麼卡住」的量產選批 SSOT。** shader 桶結論為初判，待 shader-codegen 深讀 scout 收尾。

## 數字（親數核對）
- 總算子 925（`find Lib -name '*.t3' | wc -l`）= **原子 569**（`"Children": []`）+ **複合 356**（`"Children": [`+換行）。
- 交叉驗零異常：Children 空∧Connections 空 = 569；569+356 = 925 全覆蓋。
- 核對 ROI 估 925/563/349：差 ≤1%，**估值成立**。

## ★★翻盤定論：「shader 哲學衝突」sw 早已解決（shader-codegen 深讀，讀 code 證）
GenerateShaderGraphCode **不是通用 shader 編譯器**，是 **SDF/field 距離函數的字串組裝器 + 模板注入**：動態的只有距離函數那 1-3 行 HLSL 片段，塞進**手寫固定 .hlsl 模板**；參數走 cbuffer（改參不重編），只 structural/code 變才重生。
- **真動態 shader（需 codegen，實作 `IGraphNodeOp`）= 46 個，全在 `Lib/field/`（SDF）**。固定 .hlsl（load-and-run，不 codegen）= **422 個**（image/fx/points/particles/mesh 全固定）。比例 ~1:9。
- **sw 早已把這套（方向 C 混合）造好 ~90%**：`field_render.cpp`（`assembleFieldMSL`→`cachedSourcePSO` srcHash 快取，「exactly TiXL's code/param split」）+ `field_graph.cpp`（FieldNode/CodeAssembleCtx 鏡像 ShaderGraphNode）+ `metal_compile.mm`（runtime `newLibrary(source)` 已在生產跑）+ 11 個 MSL 模板。**離 runtime 編譯＝零。**
- **46 動態 op：41 已移植且 parity golden 綠**（對抗壓測證 golden 是真 parity 非 smoke：70 個錨 TiXL `.cs`/`.hlsl` 公式、82 個 injectBug 咬合、0 個錨 sw readback），剩 **6 個未移植**（壓測修正：scout 少算 1）：ExecuteHeightmapSdf/ExecuteRepeatFieldAtPoints/SubDivPattern3d/SdfToVector/_ExecuteSdfToColor(+_Old)。
- **★Image2dSdf 已完成**（scout 誤列待辦）——有真 GPU texture-binding parity golden（`field_render.cpp:86-92` Seam A texture-bind 已驗），**texture-resource seam 不是黑洞**。
- **★真正待壓的唯一 resource 接縫＝`RepeatFieldAtPoints`（point-buffer 餵進 field）**——這條 resource 種類 sw 尚無任何驗過消費者，是 6 顆裡唯一真硬骨頭（不是「重節點」一句帶過）。第一步 spike 改採這顆（非 Image2dSdf，後者已完成）。
- **方向 A「全翻譯機」是誤判**（會逼 422 固定 shader 也走 codegen＝浪費）；sw 無通用 HLSL→MSL 翻譯器，是**逐 op 手刻 MSL 片段**（HLSL↔MSL 差極小：saturate→clamp/lerp→mix/register(tN)→[[buffer(N)]]/inout→thread&）。**真正落地的是方向 C 且已落地。**
- **結論：哲學衝突在 codebase 層已解決。** 剩 (a) 固定 422 批體力手翻（中，已翻 ~150，無架構風險，可並行）(b) 收尾 5 個重 field op 的 resource seam（小-中，唯一真未知）。第一步 spike＝`ExecuteImage2dSdf`（驗 codegen+SRV/纹理 resource binding，過了 46 動態批全收）。

## 原子按 seam 分桶（569，總和對帳）

| 桶 | 顆數 | 需要的 seam | sw 現狀（讀 code 證） | 狀態 |
|---|---|---|---|---|
| 純值/數學/邏輯 | **331** | value-op 自登記 sink | ✅ 已齊（`value_op_registry.h:1-58`，85 個活範例） | **立刻開** |
| list 轉換/聚合 | **24** | list-op registry | ✅ 已齊（22 個 floatlist/host_scalar/stringlist） | **立刻開** |
| field/SDF | **47** | field-graph codegen | ✅ 已齊（43 個 field_ops_*sdf shipped） | **立刻開** |
| buffer-marshalling | **49** | raw-Buffer currency + 自登記 | ✅ BUILT(keystone+6 ops+resident,production live,vec4 橋 closed;WO-C skip) | **已完成** |
| dx11-render-state | **41** | dx11-wrapper | ⚠️ TURNKEY: 哲學衝突已解,blueprint+census DONE(SEAM2_RENDERSTATE_BUILD_PLAN+DX11_METAL_CONVERSION_TABLE),build 未開始(FrozenRenderState accumulator) | 待 build |
| texture | **24** | 逐顆手刻 .metal | ◐ 有路徑無 codegen | 慢工 |
| io（midi/osc/udp/video） | **33** | 各自平台 I/O | ❌ 雜散無共用 seam | 卡散 |
| audio | **17** | audio-ingest | ◐ 部分已有 | 部分 |
| gpu-compute/no_cs | **3** | 特例 | — | 邊角 |

## 決策地圖

### ⚠ 現在就能無縫量產（seam 已齊）= 遠少於 402（census 第三次高估，2026-06-29 讀 code 修正）
**「331 純值立刻開」嚴重高估**——大批純標量數學/邏輯（Add/Sub/Multiply/Div/Sine/Cos/Atan2/Log/Sigmoid/
Round/Abs/Floor/Ceil/Sqrt/Pow/Modulo/Lerp/Clamp/Remap/SmoothStep/And/Or/Xor/Not/FlipBool/ToggleBoolean/
Trigger/Damp*/Spring*/Ease* …）**早已 ported，在中央 `node_registry_math_{arithmetic,logic,vector,stateful}.cpp`
表裡、不是 value_op leaf**（grep 證：這些 type 在中央表、無 `value_op_<name>.cpp`）。
- **★done-check 鐵律**：判一顆 value/math 原子做了沒，**必須同時 grep `node_registry_math_*.cpp` 的型名**，
  不能只看 value_op leaf 檔在不在 → 否則重織 ~30+ 已完成 scalar 數學。
- **value/math/logic 桶讀 code 後近乎見底**——乾淨純值無狀態真沒做的剩個位數（少數 vec、需 ctx 的 GridPosition）。
  **這桶不是大彈藥庫。**
- **真乾淨可立刻織**：list 聚合家族 ~6-8 顆真缺（FloatListLength/IntListLength/MergeIntLists/SumRange/
  CompareFloatLists/PickIntFromList，各走 floatlist/intlist registry，互不撞）。
- ~~真正下一個大批 = String 家族 ~25 顆，但卡 String-value seam~~ → **第四次高估，String seam 早已建好且 live**：String 是 sw 第 6 條 cook flow（`string_op_registry.h` host std::string currency + `resident_string_cook.cpp` resident production cook + `cook_host_values.cpp:54` 每幀跑），**26 個 string op leaf 已建**（FloatToString/IntToString/Vec3ToString/ChangeCase/SubString/IndexOf/SearchAndReplace/WrapString/CombineStrings/StringLength… 全在 `CMakeLists.txt:476-504`）。新增一顆 = 純 leaf（一檔 + 一行 CMake，零 cook-core）。**string 真缺剩個位數**（TryParse 等）。
- field/SDF 47：41 已做，剩 6（見 shader 桶）。

## ★★今天的承重發現：census/scout 數字四次高估，sw 遠比文件完整
2026-06-29 連續四次「待辦/卡縫」一讀 code 都是早建好：① image 真缺 1→34（fold 假象）② value 331→近乎見底（中央表已做）③ shader「200 卡死」→假問題（codegen 等價機制已建 41 顆）④ String「25 卡 seam」→ seam 早 live + 26 顆已做。**模式：sw 的硬基建大多早已建，census 的「todo/blocked」標籤系統性 stale**（done-check 只看單一處→漏中央表/registry；seam 標籤停在開工前狀態）。
- **縫範圍勘查定論（2026-06-29，讀 code 修正——又抓 6/7 次「卡縫其實早建好」）**：
  - ~~(b) Matrix port type~~ → **已建好且 live，刪除**：TransformVec3/MulMatrix 走 `fork-mat4-as-16-floats`（16 Float port row-major，`value_op_transformvec3.cpp`/`value_op_mulmatrix.cpp`，GLOB 自動註冊），numbers/ 只 2 op 碰 Matrix 都已做。`string_ops_vec3tostring.cpp:20-26` 的「BLOCKED」是 stale 注釋（待刪）。**無 Matrix 縫。**
  - **(c) DateTime ★可自走（路線 B float-epoch）**：DateTime 走 double-epoch Float port（仿 Matrix-as-16-floats），避開新 currency → 5 顆全 leaf 級（string_ops + value_op，DateTimeToString/StringToDateTime 用 std::tm/strftime host helper）。路線 A（新 DateTime currency）碰 cook-core 不划算。
  - **(d) anim ★~70% 已建，7 顆可自走 + 2 顆等柏為**：transport core（`transport.h/cpp` 雙時鐘+automation）、SetBpm/GetBpm、AnimValue 族全已建。可自走：組 A SetTime/SetPlaybackTime/SetPlaybackSpeed/SetSpeedFactors（clone SetBpm 寫 `g_transport`，stateful_value GLOB）+ 組 B TriggerAnim/SequenceAnim/AdsrEnvelope（純 stateful value）。**等柏為**：FindKeyframes/SetKeyframes（碰 editor curve-store 內省）。
  - **(e) Dict/Iterator = 最低 ROI 陷阱，等柏為且優先序最後**：iteration 機制早建（`point_ops_loop.cpp` re-cook keystone）；**Dict 全庫無 host 生產者**（只 io 節點產 Dict）→ 建 Dict currency 只名義解鎖 4 顆 Select* 無料吃；Iterator 半卡 StructuredList currency（cook-core）。
  - **(a) raw Buffer currency（不變）**：spike 步 3，49+2 原子+複合 keystone，碰 cook-core，等柏為。

- **★自走優先序（ROI=解鎖÷難度，零 cook-core）**：① anim 組 A+B（7 顆，clone SetBpm/stateful）② DateTime 路線 B（5 顆，float-epoch）③ stateful 乾淨尾巴（WasTrigger 等）。**等柏為（碰 owner-locked spine）**：buffer keystone / keyframe editor-introspection / Dict+StructuredList currency。

## ★★定論 code-verified backlog（numbers+string，2026-06-29，取代 census 桶數）
五處 done-check（value_op leaf / 中央表 / registry / CMake / 節點碼）+ `grep "\"Name\""` 二次確認 0 命中。
- **numbers+string 原子 ~245 / 已做 ~191（~78%）/ 真 TODO ~54**。
- **真 TODO 拆解**：**乾淨可立刻織 ~12-14** + **卡 seam ~40**（Texture 5 / Buffer 2 / DateTime 5 / Dict-Iterator 13 / anim-ctx 12 / FFT 1 / keyframe-ease 2）。
- **★乾淨彈藥庫（currency 齊、互不撞，唯一可大量自走織的）**：
  - 純值 leaf：`TryParse`(string→float+bool)、`TryParseInt`、`GridPosition`(index→Vec2，讀 ctx 無 resource)。
  - host-list（floatlist/intlist registry 已建）：`MergeIntLists`、`MergeFloatLists`、`PickFloatList`。
  - stateful（走已建 stateful_value_ops，需 stateful golden 紀律 [[sw-stateful-node-parity-gap]]）：`WasTrigger`/`CacheBoolean`/`DelayBoolean`/`KeepInts`/`EaseKeys`/`ValueToRate`/`RandomChoiceIndex`/`ComposeVec3FromList`。
- **★真 backlog 體量在 seam-gated 桶（anim+data+texture+buffer+datetime），不在純值**——value/math/string 主體 ~95% 已做。**選批鐵律：anim(36)/data(13) 整族卡 ctx-currency，不是純值，別當 value 桶開。**
- census 假陽性剔除：DefineGradient/DefineIqGradient/PickGradient/BlendGradients（gradient_ops 已做）、Int2ToVector2（fork 改名 value_op_int2tovec2）。
- field/mesh/point（量級）：field 41/47 done 剩 6（RepeatFieldAtPoints 卡 point-buffer）；point 大批是壓平複合（要停，非原子 backlog）；mesh 小。
- **行動鐵律**：任何「待辦/卡縫」claim 進工單前，先 code-verified done-check（grep 中央表+registry+CMake+cook flow 多處），別信 census 數字、別信前一個 scout 的「blocked」。
- **真 backlog 取得法**：對照 TiXL `Operators/Lib/<family>/*` vs sw 已建清單（CMake + registrar），差集才是真缺。逐家族做一次 definitive 對帳，取代不可信的 census 桶數。

→ **量產不是「並行開 402」**。原子層的 value/math 主體**已建好**（在中央表）；剩的是 list 小尾巴 +
seam-gated 批（String ~25 / buffer ~49）+ 固定 shader 手翻批（~270，見下）+ dx11/io/audio。架構硬的已解決，
剩 seam-building + 體力 volume。

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
