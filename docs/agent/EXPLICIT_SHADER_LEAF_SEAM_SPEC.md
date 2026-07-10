# Explicit bare-shader render leaf 縫 — 施工藍圖（63 顆 render 複合「真正出畫面」的最後一塊）

> 盤點來源：merge `ecd3564`/`0156f48`（Execute-siblings render-state 累積縫 LIVE）＋ EXECUTE_SIBLINGS_STATE_SEAM_SPEC.md §4.1 ⚠ 段 + §5 階段 4 劃界。前縫把 FrozenRenderState frozen tuple 送到了 Draw item；**本縫把 Explicit item 的實際 render（VS/PS 綁定 + draw）接上**——render-state 縫的自然下一戰役。file:line 為撰寫時錨點，動土前復查。

## 0. Keystone 真相（全案依此）——「狀態縫已把子彈上膛，本縫只差扣下 render 扳機」

前縫（render-state 累積）跑通後，GridPlane.t3 等 63 顆 render 複合 **native import**（遞迴進 children graph，非 collapse 成手刻 atom），Execute 的 flat siblings 中 Rasterizer/OutputMerger/InputAssembler 全 mapped，frozen tuple 已忠實累積到末尾的 Draw item（`hasRenderState==true`、`frozen==` 逐格正確）。Draw 是 `DrawKind::Explicit`（point_ops_draw_explicit.cpp:45-61 已把 `explicitVertexCount`/`explicitBaseVertex`/topology 全 cook 好、閉式驗過）。

**唯一斷點**：executor 的 `case DrawKind::Explicit`（point_ops_rendertarget.cpp:483-484）是 `break;`——named-deferred no-op。point_ops_draw_explicit.cpp:20-28 的 ★SCOPE 早已誠實預言此刻：

> 「it does NOT bind an arbitrary application vertex shader — sw has no generic "bind any VS + draw N verts" pipeline … the executor draw-path for Explicit is a named deferred leaf (needs a generic-VS binding that no census graph exercises).」

每顆複合 import 後剩的 unmapped **全是** shader 家族：VertexShaderStage(`a9600440`)/PixelShaderStage(`75306997`)/VertexShader(`646f5988`)/PixelShader(`f7c625da`)（+ 帶貼圖的 PS 另有 SrvFromTexture2d/SamplerState）——這 4 顆 op **在 sw 全未註冊**（t3_import_maps.cpp TABLE③ 無其 guid，registerCmdOp 無其名，確認為純 unmapped drop）。它們的入 Execute 線在 t3_import.cpp:327-335 被原地 drop → Execute 剩 `[IA, Rasterizer, OM, Draw]`，Draw 仍最後、仍 Explicit、仍能出鏈但**無 shader 可 render**。

**缺口不在 GPU、不在 PSO 快取、不在 state stamp，而在兩處**：①collector 沒累積 shader-binding（VS/PS 身分 + 資源接線）；②executor 的 Explicit case 沒 render leaf。**極性鐵律**：不新造平行系統——重用既有 `pickPSO`/`makeDrawPSO`/`cachedSourcePSO`/`concatRenderSibling`，只補「shader-binding 累積」與「Explicit render leaf」兩塊小邏輯。

## 1. TiXL 端語義考古 — shader 怎麼從 sibling 掛上 device context

### 1.1 四顆 shader op 的隱式狀態機語義（與 render-state ops 同型）

sw 這棵 corpus 缺獨立 VertexShaderStage.cs/PixelShaderStage.cs，但**組合形** SetPixelAndVertexShaderStage.cs（`b956f707`，distortandshade/layer2d.cpp 已引為 back-trace 權威）逐行等價，抄錄其 Update（:44-60）：

```
vsStage = deviceContext.VertexShader;  psStage = deviceContext.PixelShader;
vs = VertexShader.GetValue(ctx);       ps = PixelShader.GetValue(ctx);   // 先跑 shader-compile op
if (vs) { vsStage.Set(vs);
          vsStage.SetSamplers(0, samplerStates);
          vsStage.SetConstantBuffers(0, constantBuffers);      // b0.. 陣列序
          vsStage.SetShaderResources(0, shaderResourceViews);  // t0.. 陣列序
          vsStage.SetShaderResources(N, additionalSrvs); }
if (ps) { psStage.Set(ps); … 同上，同一組 CB/SRV/Sampler 也綁到 PS 級 }
```

Restore（:92-105）把前一組 VS/PS/CB/SRV/Sampler pop 回。**與 Rasterizer/OM/IA 完全同構**：flat sibling、Update 在 execute pass 設 device state、Restore-on-pop = render-pass 邊界。差別只在它設的是「shader + 資源」而非「blend/cull/depth」。獨立的 VertexShaderStage（`a9600440`）＝只設 VS 級；PixelShaderStage（`75306997`）＝只設 PS 級——同一機制拆兩顆。

**VertexShader/PixelShader op**（`646f5988`/`f7c625da`）＝**shader-compile 資源 producer**：讀 `Source`（HLSL 路徑字串，如 `"Lib:shaders/dx11/draw-GridPlane.hlsl"`）+ `EntryPoint`（`"vsMain"`/`"psMain"`）→ GetValue 時編譯出一個 shader 物件餵給 Stage。它是**縫的關鍵**：整個 render 的身分（哪支 shader、哪個 entry）全在這兩顆 op 的 param。

**綁定慣例**（DX register → 陣列序）：`SetConstantBuffers(0, [...])` → b0,b1,…按 MultiInput 陣列序；`SetShaderResources(0, [...])` → t0,t1,…按陣列序；`SetSamplers(0, [...])` → s0,…。importer 讀 Connections 陣列序即得 register 分派（t3_import.cpp:305,316-351 陣列序保留）。

### 1.2 抽 GridPlane.t3（純 VS/PS 無貼圖）畫接線

Execute=`03ef5880`、Command slot `5d73ebe6`。shader 子接線（GridPlane.t3 Connections :398-432 逐條）：

```
VertexShader(f88d926a: Source="Lib:shaders/dx11/draw-GridPlane.hlsl", Entry="vsMain")
    ──ed31838b→b1c236e5──▶ VertexShaderStage(c5e54bdf)
PixelShader (941c9370: Source= 同上,                          Entry="psMain")
    ──9c6e72f8→1b9be6eb──▶ PixelShaderStage(f8c4bac9)

cbuffer 餵料（同時餵 VS+PS 兩級 stage 的 ConstantBuffers MultiInput）：
    TransformsConstBuffer(6a42140e) ──7a76d147→{bba8f6eb(VS), be02a84b(PS)}──▶ b0=Transforms
    FloatsToBuffer      (20c6ab84) ──f5531ffb→{bba8f6eb(VS), be02a84b(PS)}──▶ b1=Params
        FloatsToBuffer.Params ← Vector4Components(b3a0b21b: GridPlane.Color→4 floats)
                              + GridPlane.Size(39a74407) + GridPlane.Scale(7096708e)
無 SRV、無 sampler wire（draw-GridPlane.hlsl:34 宣告 texSampler:s0 但 psMain 未用）。
```

HLSL 端印證（draw-GridPlane.hlsl:13-32）：`cbuffer Transforms:register(b0)`（10 矩陣，VS 只讀 ObjectToClipSpace+WorldToObject）、`cbuffer Params:register(b1){float4 Color; float Size; float Scale;}`（6 floats，序＝FloatsToBuffer 打包序）。VS＝`vsMain(uint vertexId:SV_VertexID)`（:47，Quad[6] 常數，**無 vertex buffer/InputLayout**）；PS＝`psMain`（:78，程序化格線）。

**這顆是純骨架**：VS/PS 各一支、b0/b1 兩 cbuffer、0 SRV、0 sampler、SV_VertexID 合成 6 verts。是第一顆封印的最小接線。

### 1.3 抽 BlendWithMask.t3（PS 帶 3 SRV-tex + sampler）畫接線

⚠ **誠實標記**：BlendWithMask.t3 在 sw 已被 **collapse 成手刻 atom**（t3_import_maps.cpp:154 `{"7da55d23…","BlendWithMask"}` → blendwithmask.metal 影像濾鏡 texture-op），**不走** Explicit render 路。此處抽它**純為示範 SRV/sampler 綁定的 canonical wiring**（`_ImageFxShaderSetup2` 家族形狀，本縫的通用 SRV 綁定須支援此形）。真正走 Explicit 路的帶貼圖 render 複合須從 census 另挑（見 §5 切片 2）。

PixelShaderStage(`1bc9e608`) 綁定（BlendWithMask.t3 Connections :285-320）：

```
PixelShader(1d8662a0: Source="Lib:shaders/img/fx/BlendWithMask.hlsl", Entry="psMain") ─▶ PixelShaderStage
ShaderResources(MultiInput, slot 50052906) 陣列序：
    SrvFromTexture2d(9df55df1 ← ImageA) ─▶ t0
    SrvFromTexture2d(e8f720be ← ImageB) ─▶ t1
    SrvFromTexture2d(d2c3a258 ← Mask)   ─▶ t2
SamplerStates(slot c4e91bc6): SamplerState(14605cc9: AddrU=Mirror,V=Mirror,W=Wrap) ─▶ s0
ConstantBuffers(slot be02a84b): FloatsToBuffer(28a1db99) + ResolutionConstBuffer(4417f6a1) ─▶ b?,b?
VertexShader(202963b2: Source="Default2-vs.hlsl", vsMain) 走全螢幕三角形（SV_VertexID）
```

**要點**：SRV 陣列序**唯一決定** t-register；sampler s0 是**顯式資源**（帶 AddressMode，非固定）；PS 讀 t0/t1/t2 + s0。這是通用 Explicit render leaf 對「帶貼圖 PS」必須忠實搬的分區。

## 2. sw 端現況考古

- **Explicit item 形狀**（render_command.h）：`DrawKind::Explicit=9`（:63-66）；`explicitVertexCount`/`explicitBaseVertex`（:274-275）；primitive 來自 IA-stamped `frozen.topology`（:132-136，唯一 topology-driven kind）；`hasRenderState`/`frozen`（:268-269）前縫已填。**目前無 shader-identity 欄位**——本縫要補。
- **executor named-deferred 精確行**：point_ops_rendertarget.cpp:**483-484** `case DrawKind::Explicit: break;`。期望形狀：一個「查 VS/PS → 建/取 PSO → 綁 cbuffer/SRV/sampler → setViewport → drawPrimitives(topology, base, count)」的 leaf，形態比照 GridPlane case（:469-479 `pickPSO` + `encodeGridPlaneDraw`）。
- **runtime 編譯 render shader 前例＝有，且已證**：`cachedSourcePSO(dev, mslSource, srcHash, vsName, fsName, fmt)`（tex_op_cache.h:65-66）——把 runtime 組出的 MSL **源字串**（含 VS+FS 兩函數）編成 render PSO，以 srcHash 為 key（零 per-frame 重編）。field_render.cpp:64-66/:142-143/:251-252 三處已在用（renderField2d/3d/ToImage），main.cpp:373-376 以 `setFieldSourceCompiler` 把 `platform::compileLibraryFromSource` 註冊進去（runtime↛platform 葉子接縫）。**「VS+FS 對走同一 source-PSO cache」機制完全現成**——轉譯引擎（Engine B）可直接複用；名對映引擎（Engine A）則走 precompiled 的 `makeDrawPSO`/`pickPSO`（rendertarget.cpp:74/:181）。
- **collector 累積機制＝已 LIVE**：`concatRenderSibling`（render_command_flow.h:37-45）＋ `frozenDeltaForRenderStateOp`（:31-36），兩腿共呼（command_cook.cpp:345 flat；resident twin 同呼）。這是 shader-binding 累積要**平行擴充**的骨架。
- **GridPlane oracle 現成**：`gridPlaneFrozenState()`（point_ops_gridplane.cpp:81-94）閉式 frozen；手刻 render 路 `runGridPlaneSelfTest`（:179-270）已示範 faithful（alpha 衰減 (0.2,0.2,0.9)·alpha）vs unstamped（opaque (51,51,229)）閉式分離；`gridplane_vs`/`gridplane_fs`（gridplane.metal:51/:75）**已在 metallib**。
- **無 HLSL→MSL 轉譯器**：field graph 的「transpiler」是 mathv **算式**→MSL（field_graph.h：template hook + 每節點 MSL snippet，snippet 本就手寫 MSL），非 HLSL。render shader 全**手刻 .metal**（app/shaders/ 183 支，CMakeLists.txt:44-94 `xcrun metal -c … → metallib`，precompiled）。glslang/spirv-cross **不在依賴樹**（僅 imgui vulkan backend 有無關的 spirv 引用）。→ 轉譯引擎是**新建置依賴**，此為 §3.2 下注的關鍵事實。

## 3. 縫設計（核心決策）— 雙引擎，兩路線壓測後下注

### 3.1 三個候選

- **(A) shader-identity 名對映 → precompiled**：Explicit item 攜 `(hlsl-source, entry)` 字串；executor 查閉式表 `(source,entry)→metallib fn name`（GridPlane：`draw-GridPlane.hlsl:vsMain→"gridplane_vs"`, `:psMain→"gridplane_fs"`）→ 走 `pickPSO`/`makeDrawPSO`。**每支不同 shader 需一支手刻 .metal**（GridPlane 已有）。
- **(B) runtime 轉譯 → cachedSourcePSO**：`(hlsl-source,entry)` → 讀 HLSL 檔 → glslang `-S vert -e vsMain` / `-S frag -e psMain` → SPIR-V → spirv-cross → MSL 源字串 → `cachedSourcePSO`（srcHash key）。**免手刻每支**，但新工具鏈 + 新 parity 面。
- **(C) importer 靜態轉 DrawKind**：import 時偵測 known shader（draw-GridPlane.hlsl）→ 改吐 `DrawKind::GridPlane` 手刻 item。→ 這只是繞過本縫，對未手刻的 62 顆無解，且與 native-import 誠實度相悖。**淘汰**。

### 3.2 判準比較（A vs B）

| 判準 | (A) 名對映 precompiled | (B) runtime 轉譯 |
|---|---|---|
| **第一顆封印可達性** | ✅ 即刻（gridplane_vs/fs 已存在，零 shader 工） | ❌ 需先建 glslang/spirv-cross 工具鏈才能編第一支 |
| **pixel-parity vs 手刻 DrawKind::GridPlane** | ✅ 同一支 metallib fn → 應 byte-parity | △ 轉譯 MSL≠手刻 MSL，需自證 parity 才可信 |
| **擴到 62 顆的成本** | ❌ 每支 shader 手刻 .metal | ✅ 零手刻（吃 HLSL 直出） |
| **新建置依賴** | ✅ 無（吃現有 metallib 管線） | ❌ glslang + spirv-cross（供應鏈 + build 成本） |
| **parity 風險面** | ✅ 手刻本就 golden 閉式驗 | ❌ fwidth/row-major/register 空間/雙 stage 單源撞名 |
| **忠實度（native import 誠實）** | ✅ 圖結構原樣，只補 render | ✅ 同 |
| **重用既有機制** | ✅ pickPSO/makeDrawPSO | ✅ cachedSourcePSO（field lane 已證） |

### 3.3 下注：**雙引擎，A 承重／B 後置擴散，GridPlane 為兩者共用 parity 錨**

不是二選一，是**分工 + 先後**：

1. **Engine A 為第一路（承重）**：先用名對映把 Explicit render leaf 的**全套 plumbing**（item→shader 身分→PSO→cbuffer/SRV/sampler 綁定→viewport→drawPrimitives(topology,base,count)）端到端接通並封印 GridPlane pixel-parity。**完全不碰轉譯**——把「render leaf 通路」這件事與「HLSL 轉譯」這件事**解耦驗證**（unknown-proof-engineering：一次只讓一個未知變數浮動）。
2. **Engine B 為擴散引擎（後置切片，自帶閘）**：轉譯只在 A 已封印、且轉譯器對 **GridPlane 自證「轉譯 MSL vs 手刻 gridplane.metal 同輸出」**（AddNoise transpiled-vs-手寫同解先例）後才解鎖，然後才餵其餘 62。GridPlane 同時有 HLSL 源 + 手刻 .metal + DrawKind golden → 是**唯一能同時錨住 A 與 B**的節點。

**為何雙引擎不是「平行系統」違規**：兩引擎都只在 Explicit case 內產出一個 `MTL::RenderPipelineState*` 餵同一 draw leaf；差別只在 PSO 來源（precompiled fn vs runtime-compiled source）——**這正是 sw 既有的兩條 PSO 路**（`makeDrawPSO` vs `cachedSourcePSO`）。executor 依 item 的 shader-binding 是否「已有名對映 hit」決定走哪條，與 `pickPSO` 依 `hasRenderState` 分流同構。

### 3.4 route (A) 機制（三決策）＋ collector 延伸

**決策①：collector 累積 shader-binding delta，平行 concatRenderSibling。**
新增純函數 `shaderBindingDeltaForOp(opType, params) → {ShaderBindingDelta, mask}`（比照 `frozenDeltaForRenderStateOp` render_command_flow.h:31）：VertexShaderStage 吸 VS 身分、PixelShaderStage 吸 PS 身分 + SRV/sampler wire。collector 掃 sibling 時，state-setter 已由 `concatRenderSibling` 吃；**新增 `concatShaderSibling`（或把 concatRenderSibling 泛化成 concatDeviceStateSibling）**累積 shader-binding accum，遇產 items 的 sibling（Draw）把 accum 蓋上。兩腿共呼**單一** helper（S2c 血訓，render_command_flow.h:28 已明文此紀律）。

**決策②：Explicit item 攜 shader-binding（新欄位或側表）。**
最小侵入：render_command.h 的 RenderDrawItem 追加（僅 Explicit 讀）——vsSource/vsEntry/psSource/psEntry + 有序 srvTex（borrowed 單幀壽命）+ samplers（AddressMode）+ paramFloats（FloatsToBuffer raw）。
⚠ render_command.h 是 ~249 includers 最高爭用檔。**替代方案**（建議）：shader-binding 走**側表**（PointGraph 單幀 map：itemId→ShaderBinding），Explicit item 只留一個 `uint32_t shaderBindingId`——避開 ABI 爆炸，與前縫「delta 用純 helper 不改 ABI」同姿。

**決策③：executor Explicit leaf（rendertarget.cpp:483 實作）。**
```
case DrawKind::Explicit: {
  if (!it.hasShaderBinding || it.explicitVertexCount==0) break;   // 守 dormant/空 draw
  // ① 取 PSO：Engine A 名對映 hit → pickPSO(it, &psoExplicit, mapFn(vsSrc,vsEntry), mapFn(psSrc,psEntry), …)
  //          miss 且轉譯已解鎖 → cachedSourcePSO(dev, transpiledMSL, srcHash, vsFn, psFn, pf)
  //          兩者皆走 it.frozen（前縫 blend/depth/cull 已在）
  // ② cbuffer：b0=Transforms（executor 建，複用 itemCamera(it) 的 objectToClipSpace + 10-矩陣佈局）
  //            b1=Params（it.paramFloats）；VS+PS 兩級同綁（SetPixelAndVertexShaderStage 語義）
  // ③ SRV/sampler：t0.. = it.srvTex（陣列序）；s0.. = it.samplers（fragment 級）
  // ④ applyFrozenRasterEncoderState(enc,it) + applyItemViewport(enc,it,W,H)（前縫已有）
  // ⑤ drawPrimitives(metalPrimitiveType(it.frozen.topology), it.explicitBaseVertex, it.explicitVertexCount)
}
```

**cbuffer/SRV 分區忠實度**：
- **① VS/PS 各轉譯 vs 拼一 .metal**：Engine A＝各自名對映到獨立 metallib fn（gridplane_vs/gridplane_fs 本就一檔兩函數，天然單 library）。Engine B＝glslang 是 stage+entry scoped（`-S vert -e vsMain` 與 `-S frag -e psMain` 分兩次），產兩份 SPIR-V → 兩份 spirv-cross MSL。**須拼成一個 source 餵 cachedSourcePSO**——但兩份 MSL 各自 emit struct/cbuffer 宣告 → **撞名風險（真實 wrinkle）**。緩解：spirv-cross `--rename-entry-point`/命名空間，或改 cachedSourcePSO 收兩源建雙 library 再合 PSO（小改）。建議：**單源拼接 + 去重共用 struct**，與 field_render 單源雙函數模型對齊。
- **② vertex attribute/stage_in DX↔Metal**：**census render 複合的 VS 全 SV_VertexID 合成**（draw-GridPlane.hlsl:47 Quad[] 常數陣列 index by vertexId；render_command.h:135 明載 IA 的 InputLayout/VertexBuffers/IndexBuffer 本就 DROPPED）。→ **無 stage_in vertex-attribute 翻譯問題**（spirv-cross SV_VertexID→[[vertex_id]] 直對）。**FORK（named）**：若 census 某顆有真 InputLayout+VertexBuffer，劃外。
- **③ cbuffer 在 render pass 綁定慣例**：b0=Transforms（TiXL 10 矩陣，executor 從 `itemCamera(it)`×object 建——與 Layer2d/Mesh 現況同源，rendertarget.cpp:155-168）；b1=Params（FloatsToBuffer raw floats）。VS+PS 兩級同綁。⚠ **Engine A 與 B 的 cbuffer ABI 不同**：A 複用 sw 手刻 fn 的既有 ABI（gridplane_vs 吃小 struct objectToClipSpace，非全 10 矩陣）；B 須重現 TiXL 全 Transforms cbuffer。**第一顆封印用 A → 用 sw 既有 ABI（encodeGridPlaneDraw 現成綁定）**；轉譯引擎落地時才建全 Transforms cbuffer。關鍵設計點，別把兩 ABI 混。
- **④ SRV-tex+sampler 進 fragment 分區**：HLSL `Texture2D:register(t0..)`→Metal `[[texture(0..)]]`；`sampler:register(s0..)`→`[[sampler(0..)]]`；按 MultiInput 陣列序。executor `enc->setFragmentTexture(srv[i], i)` + `setFragmentSamplerState(samp[j], j)`（field_render.cpp:296-307 前例）。Metal buffer/texture/sampler **三獨立綁定空間** → 不撞。

**向後相容**：shader-binding 累積只在 flat shader-stage sibling（未註冊 op、mask≠0）時觸發；render-state 縫既有 golden 無 shader sibling → accum 空 → 零回歸。Explicit case 前守 `hasShaderBinding` → 未帶 binding 的 Explicit item 仍 no-op（byte-id）。

## 4. 驗證閘（harness-first，measured RED→GREEN，分層）

### 4.1 第一顆封印：GridPlane.t3（兩條獨立路徑同輸出＝最強交叉驗證）

GridPlane 是唯一同時有 **path 1 手刻 DrawKind::GridPlane**（point_ops_gridplane.cpp，pixel oracle 閉式）與 **path 2 native-import Explicit leaf** 的節點。用 **Engine A 名對映**（Explicit 綁 `gridplane_vs`/`gridplane_fs`＝path 1 同一 metallib fn），兩路應收斂到**同像素**——這是 AddNoise「轉譯 vs 手寫同解」的 render 版對照。

### 4.2 閘（分層，每階段 measured RED→GREEN）

1. **cook 閘（PRIMARY）**：GridPlane.t3 餵 production importer → 以 Execute 子節點為 targetNodeId cook。斷言：唯一 Explicit item `hasShaderBinding==true` 且 `vs==draw-GridPlane.hlsl:vsMain`、`ps==:psMain`、`srvTex.size()==0`、`samplers.size()==0`、`paramFloats==[Color.rgba, Size, Scale]`、且 `hasRenderState && frozen==gridPlaneFrozenState()`（前縫回歸守門）。**injectBug**＝關掉 shader-binding 累積 → RED。
2. **兩腿閘（★最高風險 latch）**：同 GridPlane 複合 drive flat + resident → 斷言兩腿 shader-binding **byte-identical**（S2c 血訓）。
3. **pixel 閘（本縫的 keystone 證明，Engine A）**：GridPlane.t3 匯入版 → Explicit leaf render（綁 gridplane_vs/fs + 同 objectToClipSpace + 同 params）→ readback；對照手刻 `runGridPlaneSelfTest` faithful probe（gridplane.cpp:238-243，R,G∈[2,45] B∈[15,160]）。同 shader → 應落同帶。**injectBug**＝丟 PS wire → 黑/亂 → RED。
4. **SRV 閘（切片 3，DEPENDENT）**：一顆帶 SRV-fed PS 的 native render 複合（census 另挑，**非 BlendWithMask**——它已 collapse）→ 斷言 t0/t1/t2 綁定序正確 + sampler s0 生效。
5. **轉譯 parity 閘（Engine B，DEPENDENT、擴散前置）**：GridPlane draw-GridPlane.hlsl → glslang+spirv-cross → cachedSourcePSO render → 對照手刻 gridplane.metal / DrawKind::GridPlane golden 逐像素。**此閘綠前，轉譯不得餵其餘 62**。

golden 形狀：cook 閘 + 兩腿閘＝閉式 shader-binding 比對；pixel 閘＝兩獨立路徑收斂（最強）。

## 5. 工程切片（可獨立驗證，標風險/依賴）

| 階段 | 範圍 | 新機制 delta | 封印/閘 | 風險 | 依賴 |
|---|---|---|---|---|---|
| **1. Explicit render leaf 通路（承重）** | executor rendertarget.cpp:483 實作 Explicit case（PSO via Engine A 名對映 + b0/b1 cbuffer + viewport + drawPrimitives）；shader-binding 側表 | Explicit render leaf；名對映表 | pixel 閘（§4.2.3，GridPlane 兩路 parity） | 【中】cbuffer ABI 對齊（用 sw 既有 gridplane ABI） | render-state 縫（已 LIVE） |
| **2. collector shader-binding 累積** | 兩腿 cook leg 加 `concatShaderSibling`；merge/stamp 進 render_command_flow.h 兩腿共呼 | shader-binding 累積 | cook 閘 + 兩腿閘 | 【中】owner-locked 兩腿；靜默 unstamped 失敗類 | 階段 1 |
| **3. SRV/sampler/cbuffer 通用性** | 帶貼圖 render 複合（census 挑未 collapse 的）：t0..SRV + s0..sampler + 全 Transforms cbuffer | SRV/sampler fragment 綁定分區 | SRV 閘 | 【中】census 挑對 native-import 帶 SRV 複合 | 階段 2 |
| **4.（另切片，自帶閘）轉譯擴散引擎** | glslang+spirv-cross → cachedSourcePSO；名對映 miss fallback；餵其餘 62 | HLSL→MSL render 轉譯管線 | 轉譯 parity 閘（GridPlane 轉譯 vs 手刻） | 【高】新建置依賴 + 新 parity 面 | 階段 1；parity 閘綠前不擴散 |

階段 1-3 = 本縫核心（Engine A 承重）；階段 4 = 擴散引擎，解耦、自帶 parity 閘。

## 6. 風險假設

1. 【最高】**flat/resident shader-binding 累積分岔**（S2c 血訓）。緩解＝單一 render_command_flow.h helper 兩腿共呼＋兩腿閘 byte-identical。
2. 【高】**轉譯引擎新依賴 + parity 面**（Engine B）：glslang/spirv-cross 不在樹；fwidth（draw-GridPlane.hlsl:80 psMain 用）、row-major cbuffer、SV_ 語義、register 空間、雙 stage 單源撞名。緩解＝第一顆封印完全走 Engine A；Engine B 獨立切片 parity 閘關住。
3. 【中】**cbuffer 雙 ABI 混用**：executor 依 PSO 來源選對應 cbuffer builder；第一顆封印只用 A ABI。
4. 【中】**render_command.h ABI 爆炸**（~249 includers）：走 PointGraph 單幀側表 + `shaderBindingId`。
5. 【中】**census 挑錯帶 SRV 複合**：BlendWithMask 已 collapse（t3_import_maps.cpp:154）。階段 3 從未 collapse 的 native-import 複合挑（先跑 census 確認）。
6. 【中/明列劃外】**D 桶 6 顆動態 shader-graph 材質**（CustomVertexShader/CustomFaceShader/CustomPixelShader/CustomPointShader + ShaderGraphNode.cs）：shader 本體執行期由 user node-graph 生成，名對映無 key、轉譯無靜態源。**另立戰役**。
7. 【低】**Explicit topology 非 TriangleList**：census 全 TriangleList(4)；非預期 topology 走 dormant guard。
8. 【低】**空 Explicit draw**：守 `hasShaderBinding && explicitVertexCount>0`（draw_explicit.cpp:56 前例）。

## 7. Critical Files
- `app/src/runtime/point_ops_rendertarget.cpp`（Explicit render leaf :483-484；pickPSO :181/makeDrawPSO :74/itemCamera :155 複用）
- `app/src/runtime/render_command_flow.h` + `app/src/runtime/point_ops_renderstate.cpp`（shader-binding 累積 helper，兩腿共呼）
- `app/src/runtime/point_graph_command_cook.cpp`（flat collector :335-346 加 concatShaderSibling）＋ `app/src/runtime/point_graph_resident_command_cook.cpp`（resident twin，**必與 flat 同改**）
- `app/src/runtime/render_command.h`（Explicit shader-binding 側表 id；RenderDrawItem :142-287、Explicit :270-275）
- `app/src/runtime/tex_op_cache.h`（cachedSourcePSO :65-66，Engine B PSO 快取）＋ `app/src/runtime/field_render.cpp`（:64/:296-307 runtime VS+FS + fragment SRV 前例）
- 錨/oracle：`app/src/runtime/point_ops_gridplane.cpp`（gridPlaneFrozenState :81/手刻 render golden :179）＋ `app/shaders/gridplane.metal`（gridplane_vs:51/gridplane_fs:75，Engine A 名對映目標）＋ `external/tixl/Operators/Lib/render/gizmo/GridPlane.t3`（接線權威）
