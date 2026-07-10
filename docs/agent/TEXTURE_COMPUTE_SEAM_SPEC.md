# texture-bound compute stage 縫 — 施工藍圖（第二 keystone）

> 盤點 census：docs/agent/census/ENGINE_GAP_BUFFER_SHAPES.md。全域 190 顆 TiXL compute
> shader，66 顆（34.7%）帶 Texture2D/RWTexture2D 綁定，全數被擋在 generic
> ComputeShaderStage 之外（該 op 只有 enc->setBuffer，無 setTexture）。頭部形狀第 4-9
> 名（50 顆=26.3%）全是 texture-bound。本縫讓這 66 顆能走通用縫，不重寫 render 管線。

## 0. keystone 真相（全案依此）——「機制已在、只差泛化＋摺疊」

census 意外① 讀作「引擎缺 setTexture 路徑」，對；但對碼後真相更輕：**texture-into-compute
的每一條 GPU 機制在 sw 都已被 bespoke tex-compute atom 證實跑通**——

- texture-SRV 讀進 compute：`point_ops_growstrains.cpp:108-120`（compute encoder + `enc->setTexture(growthTex,0)`）、`point_ops_attributesfromimagechannels.cpp:134-142`（setTexture + `setSamplerState` + dispatch）、`point_ops_fastblur.cpp:101-109`。
- RWTexture2D UAV **寫出** 進 compute：`point_ops_fastblur.cpp:103-104,167`——`dst` = `cachedScratchTex(..., shaderWrite=true)` 綁在 `texture(1)`，2D `dispatchThreadgroups`。
- shaderWrite 貼圖分配：`point_graph_internal.h:286,291-306`（`ensureTex` 對 RWTexture2D 加 `MTL::TextureUsageShaderWrite`）、`cachedScratchTex`。
- cook 期 sampler：`attributesfromimagechannels.cpp:140`（`dev->newSamplerState`）。

**缺口不在 GPU，而在兩件事**：(a) generic `ComputeShaderStage` 縫沒有把上述 bespoke
綁定「按 wire 貨幣自動化」；(b) importer 沒有摺疊 texture 那批 framework 膠水 op，texture-
compute 的 .t3 子圖今天全落 `unmapped SymbolId … skipped`（`t3_import.cpp:219`）→ census
R2 判定 NOT-READY。**本 keystone＝把「像 fastblur 那樣逐顆 bespoke port」的成本，換成
「走通用縫」**（如 buffer-only stage 今已對 105 顆純 buffer kernel 做到的）。

- **極性鐵律**：不新造平行系統。texture-output stage 落 **tex 軌**（tex 軌本就 60 檔用 compute encoder + 會分配 shaderWrite 貼圖 + 有 readback golden），texture-SRV-讀-buffer-寫 stage **擴充現有 buffer 軌 ComputeShaderStage**。CB/SRV-buf 綁定＋dispatch 抽成共用 helper，兩軌共用。
- **render 島明確劃在縫外**：VertexShader / OutputMergerStage / DrawMesh 那批不動。

## 1. TiXL 端語義考古 — texture 怎麼流進 compute stage

DX11 下 compute stage 的 `t#`/`u#` register 空間 **buffer-view 與 texture-view 共用**：
一個 SRV 可包 StructuredBuffer 或 Texture2D；UAV 同理。ComputeShaderStage.ShaderResources /
Uavs 是 MultiInput<ShaderResourceView/UnorderedAccessView>，buffer-SRV 與 texture-SRV 按
wire 序共同填 t0,t1,…。三顆膠水 op 把貼圖轉成 view：

- **SrvFromTexture2d**（`SrvFromTexture2d.cs:21,53,76`）：`Texture2D` → `ShaderResourceView`。純視圖包裝（`:53 new ShaderResourceView(device, texture)`）。**Metal 語義：貼圖本身就是 SRV**，view 物件不存在 → 摺成「貼圖直連 ShaderResources」。
- **UavFromTexture2d**（`UavFromTexture2d.cs:25,28,43`）：`Texture2D` → `UnorderedAccessView`（`:25` 檢查 BindFlags.UnorderedAccess）。同理摺成「貼圖直連 Uavs（寫出）」。
- **GetTextureSize**（`GetTextureSize.cs:53,79-81`）：貼圖 → `Int2 Size`（取 `texture.Description.Width/Height`，或 `context.RequestedResolution`，或 override）。這是 **texture-only kernel 的 dispatch/分配尺寸來源**（無 SRV buffer 可 GetDimensions 時）。

### .t3 圖上 texture 流進 compute 的完整接線模式（3 顆代表實例）

**① image 島 texture-out，最簡（`3d/rendering/ComputeBrdfLookupTexture-cs.hlsl`，複合 `_ComputeBRDFLookup.t3`）**——`RWTexture2D<float4> LUT : register(u0)`、`[numthreads(32,32,1)]`、`LUT.GetDimensions()` 取尺寸、無 SRV/CB/sampler，輸出純為 thread 座標的決定性函數。接線（`_ComputeBRDFLookup.t3`）：
```
Size(Int2,512×512, :6-9) ─┬─→ Texture2d(f52db9a4,:65) [R16G16B16A16_UNorm, BindFlags SRV+UAV] ─┬─→ UavFromTexture2d(:36) ─→ Stage.Uavs
                          │                                                                     └─→ ExecuteTextureUpdate.Texture(:42)
                          └─→ CalcInt2DispatchCount(cc11774e,:30) ─→ Stage.Dispatch
ComputeShader(a256d70f,:48, Source=…BrdfLookup) ─→ Stage.ComputeShader（sw 已摺為 KernelName）
Stage.Output(Command) ─→ ExecuteTextureUpdate(6c2f8241,:42) ─→ 複合 Output(貼圖)
```

**② image 島 texture-in+texture-out（`img/post-fx/depth-to-linear.hlsl`）**——`Texture2D<float> InputTexture : register(t0)`、`RWTexture2D<float> OutputTexture : register(u0)`、`cbuffer ParamConstants : b0`；`InputTexture.GetDimensions()` 取尺寸。SRV-tex 與 UAV-tex 共用 t/u 空間，加一顆 b0 CB。接線＝①再加一條 SrvFromTexture2d(source tex) → Stage.ShaderResources。

**③ point/mesh 島 buffer+texture 混（`points/generate/PointsFromMeshData.hlsl`，census 形狀 4 首選）**——`StructuredBuffer<Face> FaceBuffer : t0`、`Texture2D<float4> inputTexture : register(t1)`、`RWStructuredBuffer<LegacyPoint> points : u0`、`SamplerState linearSampler : register(s0)`。**鐵證：buffer-SRV(t0) 與 texture-SRV(t1) 共用 DX11 t 空間**；**輸出是 buffer（u0），非貼圖** → 這批留 buffer 軌。census 形狀名已把 SRV-buf / SRV-tex 分開計 → 分類天生就在。

## 2. sw 端現況考古

- **texture 貨幣**：無 SwTexture 包裝，直接用 metal-cpp `MTL::Texture*`。tex 軌 currency = `MTL::Texture*`（`point_graph.cpp` cookTexNode / cookResidentTexNode）；pin 型別 `"Texture2D"` 已在型別系統（`node_registry_draw_render.cpp:184` RenderTarget.out）。
- **render 島貼圖綁定路徑（已 port 的 image 節點）**：`fastblur.cpp:101-109`（compute encoder：`src`@texture(0) 讀、`dst`@texture(1) 寫、2D dispatch）、`attributesfromimagechannels.cpp:134-142`（+sampler）。**皆為 bespoke atom（各有 `<op>_params.h` + 手寫綁定 + 專屬 MSL kernel），未走 generic 縫**。
- **importer 對 texture 類子節點現況**：`t3_import.cpp:202-210` 只摺 `ComputeShader`（guid a256d70f，Source → KernelName）。`SrvFromTexture2d/UavFromTexture2d/Texture2d/CalcInt2DispatchCount/ExecuteTextureUpdate` 皆不在 `swTypeForSymbolGuid`（`t3_import_maps.cpp:49-`）→ 落 `:219 unmapped skipped`。ComputeShaderStage 的 slot 表（`t3_import_maps.cpp:146-154`）已路由 ConstantBuffers/ShaderResources/Uavs/Output，且 **明列 SamplerStates 被丟棄**（:152-153）。
- **generic stage 現況**：`buffer_ops_computeshaderstage.cpp`——`inputBufferPorts` 只認 ConstantBuffers/ShaderResources/Uavs 三個 **Buffer** port（:97-99）；綁定迴圈只有 `enc->setBuffer`（:119-124）；1D dispatch `calcDispatchCount(numStructs,64)`（:113,133-135）；單一 `Output`(Buffer)（:152）。`computeshaderstage_params.h:23-36` flat buffer 分區 CB[0,4)/SRV[4,12)/UAV[12,16)/SRVCOUNT[16]——**Metal 貼圖走獨立 `[[texture(n)]]` 空間，與此 buffer-index 不相撞**（天生留白）。
- **dormant 鉤子**：`buffer_op_registry.h:83` 已有 `const MTL::Texture* inputTexture`（單顆，現無 driver 填）——證明曾預留，但單顆不夠（需多 texture + SRV/UAV 分類）。

## 3. 縫設計（核心決策）

### 3.1 兩軌分流（fork `computeshaderstage-splits-by-uav-currency`）
importer 摺疊時看 Stage.Uavs 的來源 op：
- 來自 **UavFromTexture2d** → texture-out → 發 **tex 軌新原子 `ComputeShaderStageTex`**（cook 於 cookTexNode，複用 tex 軌貼圖分配/SRV 綁定/sampler/readback）。
- 來自 **StructuredBufferWithViews/GetBufferComponents**（buffer-UAV）→ 沿用現 buffer 軌 `ComputeShaderStage`，僅加 texture-SRV **輸入** 綁定（輸出仍 buffer）。

理由：sw 全站貨幣分軌是硬不變式；強一顆原子跨兩軌才是過度設計。CB/SRV-buf 綁定＋
dispatch 抽成 `bindComputeStageBuffers(enc, cbs, srvs, uavs)` 共用 helper，避免重複。

### 3.2 NodeSpec：不新增 dataType，加分類 texture ports
`"Texture2D"` dataType 已存在。ShaderResources/Uavs 現為 Buffer port，無法承貼圖貨幣。
加分類 texture MultiInput port（與現有 point tex-op 的 Texture2D 輸入同型）：
- buffer 軌 stage：加 `ShaderResourceTextures`(Texture2D, multi)——texture-SRV 落此，與 buffer-SRV 分開；MSL kernel 各自編 buffer-index 與 texture-index（DX11 共用 t 空間僅來源語言 artifact，Metal 兩空間獨立，**只需保各貨幣內序**）。
- tex 軌 `ComputeShaderStageTex`：`ShaderResourceTextures`(in) + `Output`(Texture2D, out)；輸出貼圖由 stage 分配（見 3.4）。
- sampler：SamplerStates wire 照 TiXL 現況丟棄（`t3_import_maps.cpp:152`）→ stage 於 cook 期建 **預設 MTLSamplerState（linear + clamp/wrap）** 綁固定 sampler index，比照 `attributesfromimagechannels.cpp:140`。per-op sampler 參數化＝後階細化（fork `computestage-default-sampler`）。**不選 constexpr sampler**——與既有 tex-op 精神一致、可日後接 wire。

### 3.3 DX11 view 摺疊（照 ComputeShader fold 前例）
importer 新摺（elide 子節點、來源直連 stage port，貨幣標 texture）：
- `SrvFromTexture2d`(c2078514, 輸出 DC71F39F) → 摺成貼圖直連 `ShaderResourceTextures`。
- `UavFromTexture2d`(84e02044, 輸出 83D2DCFD) → 摺成貼圖直連 `Uavs`（tex 軌：標記 stage 為 texture-out + 綁輸出貼圖）。
- `CalcInt2DispatchCount`(cc11774e) → elide（stage 由輸出/輸入貼圖 WxH 自算 2D dispatch＝GetDimensions 語義；比照 `CalcDispatchCount` 現摺法 `t3_import_maps.cpp:161-166`）。
- `ExecuteTextureUpdate`(6c2f8241) → tex 軌 forwarder，twin of `ExecuteBufferUpdate`（`buffer_ops_executebufferupdate.cpp`）。sw 的 `computeshaderstage-dispatch-in-cook` fork 令 stage 於 cook 期 dispatch 並直接輸出貼圖 → ExecuteTextureUpdate 退化為 passthrough。
- `Texture2d`(f52db9a4) 輸出貼圖分配：**摺進 stage**（stage 依 Size + Format 分配 shaderWrite 輸出貼圖，比照 fastblur `cachedScratchTex`/`ensureTex`）。format 映射表：`R16G16B16A16_UNorm→MTL::PixelFormatRGBA16Unorm` 等，小表逐格補。fork `computestage-allocates-uav-texture`。

### 3.4 dispatch 尺寸與 numthreads
buffer 軌現寫死 tg=64、1D。texture kernel numthreads 各異（BRDF 32×32、depth 16×16、
PointsFromMeshData 160×1）。`kernelNameFor()`（`buffer_ops_computeshaderstage.cpp:57`）擴成
**kernel→metadata 表**（+numthreads dims + dispatch 尺寸來源：SRV-tex GetDimensions / 輸出貼圖
WxH / SRV-buf elementCount）。tex 軌走 2D `dispatchThreadgroups(ceil(W/tgx),ceil(H/tgy))`
（fastblur:109 前例）。fork `computestage-per-kernel-threadgroup`。

### 3.5 多 UAV 出口（census 意外②）——明確劃出去
JumpFloodFill(1SRV-tex+2UAV-tex)/SimpleLiquid(…3/4UAV-tex)/`2SRV-buf+2UAV-buf` 需第二+
Output port。**本縫先做單 UAV 出口**（涵蓋形狀 4/5/6/7/8 大宗）；多 UAV 出口 = 階段 4
獨立切片（tex 軌加 `Output2..`、cook 轉發第 N 顆 UAV），與 census 意外②同框，不阻擋前三階段。
census 意外③（spatial-hash-map uav=5 > CS_MAX_UAV=4）＝更後，留到多 UAV 補完。

## 4. 驗證閘（harness-first）

### 4.1 第一顆封印節點：`_ComputeBRDFLookup`（1UAV-tex）
選它因**驗證無歧義**：`ComputeBrdfLookupTexture-cs.hlsl` 無 SRV/CB/sampler，輸出純為
(x/W, y/H) 的決定性 Cook-Torrance split-sum 積分（1024-sample Hammersley）→ CPU ref 可逐格
直轉。它恰好**強制建齊 texture-out 核心機制**（分配 UAV 貼圖 + setTexture 寫綁 + 2D dispatch
自 WxH + tex 軌 Output），卻不牽涉 SRV-tex 讀/sampler。候選名單（shape 6，1UAV-tex，9 顆）：
- **`_ComputeBRDFLookup`（首選，零輸入純生成器）**
- 備選＝shape 6 中掃一顆更純的 UV/gradient 寫出器（若 BRDF 的 1024-loop MSL 移植 eps 難調）
- 次一顆＝`_ComputeDepthToLinear`（shape 5, 1SRV-tex+1UAV-tex）＝自然階段 2（加一條 SRV-tex 讀 + b0 CB）

### 4.2 mathv：texture 輸出怎麼驗（已有前例）
`MATH_VERIFY_WORKFLOW.md §1.3` 已立 image 類 mathv 路徑：**host 生輸入 texture → direct-kernel
dispatch → readback（`app/src/readpixel_golden.cpp` 先例）**，比對域＝CPU ref float → 量化
±1 LSB。texture readback→CPU 逐像素比對前例充分：`pbr_shading_golden.cpp:176-182`
（`tex->getBytes(pxv.data(), W*4, MTL::Region::Make2D(0,0,W,H),0)` 後對 host PBR oracle），
及 `adjustcolors/blur/channelmixer` 全用同 `getBytes` 模式。BRDF format R16G16B16A16_UNorm
→ eps＝16-bit ±1 LSB（transcendental eps 類，`MATH_VERIFY_WORKFLOW.md §2`）。

五關（沿 RETIREMENT_BATTLE_SPEC R6 / mathv SSOT）：CPU ref 逐格 / fuzz（Size 掃 32²..1024²）/
語義稽核（HLSL vs MSL 逐 op，含 Hammersley bit-reverse）/ refuter（換種子/邊角像素）/
陷阱清單（fast-math：pow/sqrt、u32 bit ops、GetDimensions 對齊）。

### 4.3 縫閘（每階段 measured RED→GREEN）
1. **接管閘**：`_ComputeBRDFLookup` .t3 餵 production importer → 零 `unmapped skipped`、複合 atomic==false + children 非空、Stage 摺為 `ComputeShaderStageTex`。injectBug＝關掉 UavFromTexture2d 摺 → texture UAV 未綁 → 黑貼圖 → readback 分岔（咬 census「if(!pso) return 靜默 RED」同類）。
2. **parity/mathv 閘**：4.2，期望值＝手推自 .hlsl 常數（引 kernel 行號）。
3. **引用閘**：載入引用 `_ComputeBRDFLookup` 的 demo/複合，斷言解析＋cook 正確。
4. **排版閘**：帶 .t3ui 匯入 → 子節點座標非全 0（若無 .t3ui 降 N/A 非紅）。

## 5. 工程切片（可獨立驗證，標風險/依賴）

| 階段 | 範圍 | 新機制 delta | 封印顆 | 風險 | 依賴 |
|---|---|---|---|---|---|
| **1. UAV-tex 寫出** | tex 軌 `ComputeShaderStageTex`：分配 shaderWrite 輸出貼圖 + setTexture 寫綁 + 2D dispatch 自 WxH + tex 軌 Output；importer 摺 Texture2d/UavFromTexture2d/CalcInt2DispatchCount/ExecuteTextureUpdate | 皆已在 fastblur 證實，僅泛化＋摺疊 | `_ComputeBRDFLookup`(1UAV-tex) | 【中】rail 分流 + importer 5 摺；靜默黑貼圖失敗類 | 無（GPU 機制已在） |
| **2. SRV-tex 讀（tex-out）** | `ShaderResourceTextures` 輸入綁定 + b0 CB；SrvFromTexture2d 摺；dispatch 自輸入貼圖 GetDimensions | 已在 growstrains/attributes 證實 | `_ComputeDepthToLinear`(1SRV-tex+1UAV-tex) | 【低】 | 階段 1 |
| **3. SRV-tex 讀（buf-out）+ sampler** | buffer 軌 stage 加 `ShaderResourceTextures` 輸入 + 預設 sampler；共用 `bindComputeStageBuffers` helper | texture-SRV 進 buffer 軌（跨貨幣 gather 於 `point_graph_buffer_cook.cpp`） | `PointsFromMeshData`(形狀4) / `GetImageBrightness`(形狀7) | 【中】buffer 軌 cook driver 需跨 tex 軌 cook 上游貼圖 | 階段 2 |
| **4. 多 UAV 出口** | tex 軌 `Output2..` + cook 轉發第 N UAV（census 意外②） | 第二 Output 轉發 | `JumpFloodFill`(1SRV-tex+2UAV-tex) | 【中】ExecuteTextureUpdate 多輸出 | 階段 1-2 |

（census 意外③ spatial-hash-map uav=5＞CS_MAX_UAV=4＝階段 4 之後，需先擴位元分區。）

## 6. 風險假設

1.【高】**rail 分流誤判**：ComputeShaderStage 現僅註冊為 buffer op；texture-out 需 tex 軌新原子。若 importer 對 UAV 貨幣分流判錯 → texture-compute 複合靜默出空/黑貼圖（UAV 未寫），與 census `if(!pso) return` 同類不可見 RED。緩解＝階段 1 golden 對 real readback measured RED→GREEN，接管閘 injectBug 咬 UavFromTexture2d 摺。
2.【中】**importer 5 摺相互依賴**：Texture2d/Srv/UavFromTexture2d/CalcInt2Dispatch/ExecuteTextureUpdate 任一漏摺 → `unmapped skipped` → NOT-READY。逐摺加 census-style probe。
3.【中】**共用 t 空間 → 分離 index 的序**：DX11 t0(buf)+t1(tex) 拆成 Metal buffer-index+texture-index，需保各貨幣內序；MSL kernel 手編兩空間。稽核關（S）逐 register 對照。
4.【低】**sampler 預設值**：SamplerStates 丟棄 → 預設 linear-clamp；若某 kernel 靠 wrap/mirror（如 BlendWithMask 用 Mirror）出錯。per-op sampler＝階段 3 細化。
5.【中】**format 映射覆蓋**：Texture2d Format 枚舉多；逐格補表，未覆蓋 format loud-fail 非靜默。

## 7. Critical Files
- `app/src/runtime/buffer_ops_computeshaderstage.cpp`（generic stage：kernelNameFor→metadata 表、共用 bind helper、texture-SRV 輸入）
- `app/src/runtime/t3_import_maps.cpp`（+SrvFromTexture2d/UavFromTexture2d/Texture2d/CalcInt2DispatchCount/ExecuteTextureUpdate 摺 + ComputeShaderStage rail 分流）
- `app/src/runtime/t3_import.cpp`（:202-210 摺 pass 擴為 texture-view 摺 + UAV 貨幣分流）
- `app/src/runtime/point_graph.cpp` + `point_graph_buffer_cook.cpp`（tex 軌 `ComputeShaderStageTex` cook + buffer 軌跨 tex 貼圖 gather）
- `app/src/runtime/computeshaderstage_params.h`（per-kernel threadgroup + texture-index 分區，與 buffer-index 不撞）
