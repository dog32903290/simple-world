# Execute-siblings render-state 鏈縫 — 施工藍圖（63 顆 render 複合的承重線）

> 盤點來源：merge `361d70a`（render-state 膠水 guid 接線）＋ t3_import_renderstate.cpp:27-38
> 的 ★STRUCTURAL FINDING。TiXL 把 Rasterizer/OutputMergerStage/InputAssemblerStage 接成
> Execute 的 MultiInput<Command> **flat siblings**，不是嵌套 Command→Command 鏈。sw 的 Seam2
> 摺疊機制假設嵌套（cookRasterizer/cookOutputMerger 看 `!c.inputCommand` 就 no-op），import
> 結構乾淨但 cook 時 render-state stamp 永遠到不了 sibling Draw item。本縫讓 63+ 顆 render
> 複合的 render-state 真正落到它們的 Draw。

## 0. keystone 真相（全案依此）——「機制全在、只差把 stamp 接到對的節點」

Seam2 已 LANDED：FrozenRenderState 結構、dx11_metal_state_map.h 閉式表、stampRenderState /
frozenPSOKey 共用 helper、Rasterizer/OutputMerger/InputAssembler/Draw 四顆 op、executor 的
`applyFrozenRasterEncoderState`/`applyFrozenBlend`/`pickPSO` frozen-blend cache（SEAM2_RENDERSTATE_BUILD_PLAN.md）。
**GPU 側、PSO 側、stamp 側全部跑通**——唯一斷點是 **stamp 的目標從哪來**。

Seam2 build 選了「Camera/Group per-item STAMP」路線：cookRasterizer 是 Command→Command op，
兩條 cook leg 都用 `cc.inputCommand` gather 同一棵子樹，再 `stampRenderState()` 蓋到每個
未 stamp 的 item（innermost-wins = push/pop）。這在**嵌套鏈**下正確。但真實 .t3 從不嵌套：

- **Rasterizer.cs:6-13,45-53**：`Output = new(new Command())`，`Output.Value.RestoreAction = Restore`，
  **沒有 Command 輸入 slot**、**沒有 PrepareAction**。它的 `Update`（GetValue）把 state 寫進
  `deviceContext.Rasterizer.State`（:26-31）——DX11 隱式狀態機的副作用。
- **OutputMergerStage.cs:8-15,17-101**、**InputAssemblerStage.cs:6-13,16-37**：同型——Update 設
  blend/depth/rendertargets / PrimitiveTopology，Restore 復原，皆無 Command 輸入。

所以四顆 op 的 Command **輸出**是 Execute 的 MultiInput 的 **平輩 sibling**，Draw 也是平輩
sibling。cookRasterizer 進來時 `c.inputCommand==null`（沒有子樹）→ `return RenderCommand{}`
（point_ops_renderstate.cpp:176,250；inputassembler.cpp:64）→ stamp 從未發生；Draw 的 item
單獨經 Execute concat 存活但 **unstamped** → render-state 遺失。

**缺口不在 GPU、不在 PSO、不在 stamp helper，而在 collector**：Execute 的 MultiInput 目前只做
**concat**（point_graph_command_cook.cpp:334-342），沒做 **state accumulation**。而
point_ops_execute.cpp:20-34 的 header 早就預言了這一刻：

> 「Cross-sibling PrepareAction state (Execute.cs risk #1) is N/A: no sw render op uses a Prepare
> side-effect visible to a later sibling … **if one ever does, the three-pass split must be
> reinstated — flagged in the report.**」

render-state ops **正是**「前一個 sibling 的副作用被後一個 sibling 看見」那個 case。但關鍵細節：
TiXL 裡它**不是** PrepareAction，而是 Update（execute pass），且 Draw 在 wire 序**最後**——所以
**單趟 wire-order 累積-蓋章**就能忠實重現（見 §1），不需真的把 Execute 拆成三趟。

**極性鐵律**：不新造平行系統。重用既有 FrozenRenderState + stampRenderState + executor frozen
cache；只在 collector 補「累積」。render 島劃在縫外——本縫只把 frozen tuple **送到** Draw item；
Explicit item 的**實際 render**（bare-shader 綁定）是**另一條縫**（§4.4）。

## 1. TiXL 端語義考古 — state 怎麼從 sibling 流到 Draw

**Execute.cs:14-39 三趟**（point_ops_execute.cpp:8-18 已抄錄）：
```
commands = Command.CollectedInputs        // MultiInput，wire-declaration 序
for i: commands[i].Value?.PrepareAction   // 1) prepare-all（render-state ops 無 Prepare → 空轉）
for i: commands[i].GetValue(ctx)          // 2) execute-all ← state Update 與 Draw 都在這趟
for i: commands[i].Value?.RestoreAction   // 3) restore-all（pop state）
```

**state 怎麼流**：execute pass 按 wire 序呼叫每個 sibling 的 GetValue → Update。走到 Rasterizer
sibling → 寫 `deviceContext.Rasterizer.State`；走到 OM sibling → 寫 blend/depth；走到 Draw
sibling（**最後**）→ `deviceContext.Draw` 吃到累積在 device context 上的 state。這是 **DX11 隱式
狀態機**：前面的 sibling 設 state、後面的 Draw 吃到。restore-all 趟把 state 復原，讓下一個
Execute/frame 乾淨（= sw retained-mode 的 render-pass 邊界，state 不跨 Execute 洩漏）。

**順序敏感性**：state-setter 只影響 wire 序**在它之後**的 Draw。若 wire 序是
`[Rasterizer(Back), Draw1, Rasterizer(None), Draw2]`，DX11 會用 Back 畫 Draw1、None 畫 Draw2。

### 抽 2 顆真 .t3 的 wire 序

**① GridPlane.t3（render/gizmo/GridPlane.t3:30-433）**——Execute=`03ef5880`，Command slot
`5d73ebe6`。6 條 sibling 入線，Connections 陣列序（= MultiInput 序，t3_import.cpp:305,316-351
陣列序保留）：

```
[wire0] InputAssemblerStage(f968037a) ─┐
[wire1] VertexShaderStage(c5e54bdf)    ├─→ Execute.Command(5d73ebe6) ─→ Execute ─→ Transform ─→ GridPlane.Output
[wire2] Rasterizer(2b4af537)           │      (:284,290,296,302,308,314)
[wire3] PixelShaderStage(f8c4bac9)     │
[wire4] OutputMergerStage(a36689fc)    │
[wire5] Draw(0586daed, VertexCount=6)  ─┘   ← Draw 在最後

  子 state（嵌套進 state op，非進 Execute）：
    RasterizerState(e9ca366b: CullMode=None, FrontCounterClockwise=true) ─→ Rasterizer.RasterizerState
    BlendState(7d616c65) ─RTBD(6d9f2c25: BlendEnabled, SourceAlpha/InvSourceAlpha)─→ OutputMerger.BlendState
    DepthStencilState(4b8041c3: ZWrite=false, ZTest=true) ─→ OutputMerger.DepthStencilState
```

**② DrawMeshUnlit.t3（mesh/draw/DrawMeshUnlit.t3）**——同型 mesh twin：Execute=`47e9240c`，
siblings = InputAssemblerStage / VertexShaderStage / PixelShaderStage / Rasterizer(c813444e) /
OutputMergerStage(dfcfe8e7) / Draw(e6a43d54, 亦為 Explicit)，加 Mesh 輸入 + _MeshBufferComponents。
RasterizerState(6e672779)/DepthStencilState(61714c96) 餵 state op。**結論：63 顆全是這個「Execute +
N flat siblings + 末尾 Draw(Explicit)」骨架**——mesh 與 gizmo 唯一差別是多一條 Mesh 輸入。

## 2. sw 端現況考古

- **collector（承重點）**：`PointGraph::Impl::cookFlatCommand` 的 Command MultiInput 分支
  （point_graph_command_cook.cpp:185-347）。一般 MultiInput 走 :334-342 的 concat：逐 wire
  `cookCommand(sibling)` → `inCmd.items.insert(...)`，只 concat，**不累積 state**。resident twin =
  point_graph_resident_command_cook.cpp:346-355（`ri->extraConns` 走同樣的 concat）。兩腿都在
  :368 填 `cc.inputCommand = &inCmd`，再 :383 呼叫 op fn。
- **cookExecute**（point_ops_execute.cpp:66-71）：`if (enabled && c.inputCommand) rc.items =
  c.inputCommand->items;`——薄 passthrough，只吃 collector 已 concat 好的鏈。
- **state ops 的 no-op**：cookRasterizer(point_ops_renderstate.cpp:175-185)、cookOutputMerger
  (:249-279)、cookInputAssembler(inputassembler.cpp:63-69) 皆 `if (!c.inputCommand) return
  RenderCommand{};`——flat-sibling 下必空回。它們**已經**會從 param 建出正確的 FrozenRenderState
  （:177-184 / :251-277 / :65-67），只是 stamp 目標（inputCommand）不存在。
- **stamp 機制**：`stampRenderState`(renderstate.cpp:110-119) 蓋 `st` 到每個 `hasRenderState==false`
  的 item（innermost-wins）。**FrozenRenderState 欄位天生分組不相交**（render_command.h:108-137）：
  Rasterizer 擁 fillMode/cullMode/frontCCW/depthBias*；OutputMerger 擁 rt.*/alphaToCoverage/
  depthCompare/depthWrite；InputAssembler 擁 topology。
- **executor 現況**：`pickPSO`(rendertarget.cpp:181-193) 對 `it.hasRenderState` item 用
  `frozenPSOKey` cache 出 frozen-blend PSO；`applyFrozenRasterEncoderState`(renderstate.cpp:47-56)
  設 encoder cull/winding/depthBias。**只要 item 帶對 frozen，executor 已能吃**——不需改。
- **importer fold（已 land）**：foldRenderStateOntoStages(t3_import_renderstate.cpp:232-282) 已把
  RasterizerState/DepthStencilState/(RTBD→BlendState→)OutputMerger 的值摺成 Rasterizer/OM 的
  overrides。所以 import 後這些 op 的 param **已正確**；缺的純粹是 cook 時的累積。
- **oracle 現成**：point_ops_gridplane.cpp:81-94 的 `gridPlaneFrozenState()` 是手刻、code-checked
  的 GridPlane 期望 frozen tuple（cull None + frontCCW + SrcAlpha/InvSrcAlpha blend + depth
  Less/no-write）——**與 GridPlane.t3 的 state 子節點逐格對應**。這是本縫最強的閉式錨。

## 3. 縫設計（核心決策）— 兩路線比較後下注

### 3.1 兩條候選（t3_import_renderstate.cpp:36-38 已命名）

- **(a) collector state-accumulation**：教 Execute 的 MultiInput collector 按 wire 序掃 sibling，遇
  state-setter 累積進 running FrozenRenderState，遇產出 Draw item 的 sibling 把累積 state 蓋上去。
  貼 DX11 隱式狀態機。
- **(b) importer 嵌套重合成**：import 時把 flat siblings 重寫成嵌套 Command 鏈
  （`Draw→OM.command→Rasterizer.command→IA.command→Execute`），餵給既有 cookRasterizer 的
  inputCommand stamp。cook core 不動。

### 3.2 判準比較

| 判準 | (a) collector 累積 | (b) importer 重合成 |
|---|---|---|
| **順序敏感性**（Draw 在 state-setter 前/後） | ✅ 天生：累積到哪蓋到哪，前置 Draw 得舊 state | ❌ 嵌套包整棵子樹，無法表達 per-draw |
| **多 Draw 共享 state**（一個 Execute 多 Draw） | ✅ accum 跨 draw 存活，每個 Draw 各自 stamp | △ 需把所有 draw gather 成一棵內層 Command，可行但脆 |
| **state 重設規則**（後 setter 覆蓋同 stage 欄位） | ✅ masked-merge，last-wins per field | △ 靠嵌套深度，跨 stage 混設難表達 |
| **貼 TiXL 語義** | ✅ 直接建模 device-context 狀態機 | ❌ 靜態改圖，語義是「翻譯」非「重現」 |
| **動的檔** | 5 檔（2 owner-locked：兩條 cook leg） | 2-3 檔（純 importer） |
| **import 誠實度** | ✅ 圖結構原樣（sibling 保持 sibling） | ❌ 合成不存在的嵌套，圖與 .t3 拓樸分岔 |
| **重用既有機制** | ✅ FrozenRenderState + stamp + executor 全重用 | ✅ 重用 cookRasterizer 嵌套 stamp |

### 3.3 下注 (a)——理由

**承重線 × 忠實度優先**。63 顆複合是承重批；route (b) 的靜態重寫會**靜默丟失順序敏感性與
多-Draw 共享**——這是跨 63 顆的潛在 correctness bug，且與 census「if(!pso) return 靜默 RED」
同類不可見。route (a) 把語義在**正確的位置**（collector = DX11 device-context 的對應物）建模**一次**、
正確。多動的兩個 owner-locked 檔（兩條 cook leg）**本來任何 MultiInput 改動都要動**，且已有
render_command_flow.h 的共用-helper 紀律（switchSelectIndex:40 / loopRunIterations:59 兩腿共呼）
把「兩腿分岔」風險收斂。且 route (a) **零改 render_command.h、零改 executor**（見 3.4）——實際新
邏輯很小。

### 3.4 route (a) 機制（三個關鍵決策）

**決策①：delta 用共用純 helper，不改 RenderCommand ABI。**
新增純函數 `frozenDeltaForRenderStateOp(opType, const map<string,float>& params) → {FrozenRenderState
delta, uint32_t fieldMask}`（把 cookRasterizer:177-184 / cookOutputMerger:251-277 /
cookInputAssembler:65-67 的 param→frozen 邏輯抽出）。collector 有 `nodeParams(sibId)`（兩腿皆有），
直接算 delta——**不需** cookCommand 回傳 state、**不需**給 RenderCommand 加欄位（render_command.h
是 ~249 includers 的最高爭用檔，避開它是淨賺）。cookRasterizer/OM/IA 的既有嵌套路徑**也改呼同一
helper** 再 stampRenderState → 單一真相源，enum-index 不會兩處分岔。

**決策②：masked-merge（欄位分組不相交）。**
collector 維護 `FrozenRenderState accum` + `uint32_t accumMask`。逐 wire：
- sibling 是 render-state op → `accum` 只吸收 delta 的 mask 欄位（Rasterizer 蓋 raster 欄位、OM 蓋
  blend+depth、IA 蓋 topology）。mask 必要——否則 Rasterizer 的預設「blend off」會蓋掉前面 OM 的
  「blend on」。分組不相交 → 累積 per-field last-wins = DX11 語義。
- sibling 產出 items（Draw/其他）→ 對每個 `hasRenderState==false` 的 item 蓋上 accum（只蓋
  accumMask 已設的欄位，其餘留 struct 預設）→ 標 hasRenderState=true → concat。

**決策③：單趟 wire-order，不重建三趟。**
因 Draw 在 wire 序最後、state Update 與 Draw 都在 execute pass，單趟「累積再蓋」即忠實（§1）。
restore-all = sw 的 Execute 輸出鏈邊界（accum 不跨 Execute 洩漏，naturally scoped）。

**共用邊界**：merge/stamp 數學（`mergeFrozenDelta` / `stampAccumOntoItems`）進
render_command_flow.h 當兩腿共呼 helper（比照 switchSelectIndex:40）；各腿仍擁自己的 per-wire
迴圈（flat = g.connections;  resident = extraConns），只呼共用數學——與現況 concat 完全同構。

**向後相容**：既有嵌套 stamp 路徑（cookRasterizer 的 `c.inputCommand` 分支）**保留**——真實 corpus
從不嵌套，只有既有 goldens（renderstate_golden / 兩腿 selftest）走它。route (a) 的累積只在
flat-sibling（state op 無 inputCommand）時觸發 → 既有 golden 全綠、零回歸。

**unmapped sibling 的健壯性**：GridPlane 的 VertexShaderStage(wire1)/PixelShaderStage(wire3) 目前
unmapped → 其入 Execute 的線在 t3_import.cpp:327-328 被**原地 drop 不重排** → Execute 剩
`[IA, Rasterizer, OM, Draw]` 相對序不變、Draw 仍最後。route (a) 對 shader-op drop 天生健壯。

## 4. 驗證閘（harness-first，measured RED→GREEN）

### 4.1 第一顆封印：`GridPlane`（是最佳錨，但要分清層次）

**是**——但理由是 **cook-level oracle**，不是 pixel。
- ✅ 期望 frozen tuple 是 code-checked oracle：`gridPlaneFrozenState()`(gridplane.cpp:81-94)，與
  .t3 state 子節點逐格對應——閉式、無歧義。
- ✅ import 乾淨：只剩 **5 unmapped**（VertexShaderStage / PixelShaderStage / VertexShader /
  PixelShader / Transform），**皆非 render-state op**——是 shader-binding 縫（4 顆）＋ command-
  transform 縫（Transform 1 顆），與本縫正交。render-state op（Rasterizer/OM/IA）全 mapped、fold
  已摺好 param。
- ⚠️ **但 GridPlane.t3 的 Draw 是 Explicit（bare shader）**，其**實際 render** 需 Explicit
  executor leaf + draw-GridPlane.hlsl 綁定（point_ops_draw_explicit.cpp:20-28 /
  rendertarget.cpp:483 = named deferred）。**手刻 GridPlane 走 DrawKind::GridPlane 專屬 shader，與
  匯入版的 Explicit 是不同 render 路徑**——所以「對照手刻輸出的 pixel parity」**不能**由本縫單獨
  達成。別把兩層混為一談。

### 4.2 閘（分層，每階段 measured RED→GREEN）

1. **接管閘（PRIMARY，cook-level）**：GridPlane.t3 餵 production importer → 以 Execute 子節點為
   targetNodeId cook（比照 --selftest-execute 的 `pg.cook(g,ctx,nullptr,targetNodeId=executeId)`；
   因 Transform unmapped，compound output 端無值，故直接錨 Execute 輸出鏈）。斷言：輸出鏈唯一
   Explicit item `hasRenderState==true` 且 `frozen == gridPlaneFrozenState()`（逐欄位）。
   **injectBug**＝關掉 collector 累積 → item unstamped → 斷言 RED。
2. **順序閘（route (a) 獨有的忠實度證人）**：合成 Execute wire 序
   `[Rasterizer(cull=Back), Draw1, OutputMerger(blendEnable), Draw2]` → 斷言 Draw1.frozen 為
   Back+blend-off、Draw2.frozen 為 Back+blend-on。**route (b) 過不了這關**（嵌套無法表達）→ 這關就是
   下注 (a) 的驗證。injectBug＝把累積改成「Execute 結束才蓋」→ Draw1 誤得 blend-on → RED。
3. **兩腿閘（★最高風險 latch）**：同一 GridPlane 複合 drive flat + resident（比照既有 both-leg
   selftest + renderStateCaptureForTest:75-78）→ 斷言兩腿 frozen tuple byte-identical。
4. **多-Draw 共享閘**：一個 Execute 內 state-setter + 2 Draw → 兩 Draw 同 frozen（accum 跨 draw 存活）。
5. **pixel 閘（DEPENDENT，非本縫）**：待 Explicit executor leaf + shader 綁定落地後，GridPlane.t3
   匯入版 readback 對 blend-on vs blend-off 的可見差異（手刻 GridPlane golden gridplane.cpp:179-270
   已示範 faithful=alpha 衰減 (0.2,0.2,0.9)·alpha vs unstamped=opaque (51,51,229) 的閉式分離）。
   **明列為依賴 §4.4 的後續閘，不阻擋 1-4。**

golden 形狀：本縫的真正證明是 **cook-level frozen-tuple 比對**（閉式 oracle），非 pixel。
pixel 是 downstream 縫的證明。

## 5. 工程切片（可獨立驗證，標風險/依賴）

| 階段 | 範圍 | 新機制 delta | 封印/閘 | 風險 | 依賴 |
|---|---|---|---|---|---|
| **1. delta helper 抽取** | `frozenDeltaForRenderStateOp(opType,params)→{delta,mask}`；重構 cookRasterizer/OM/IA 呼它（含既有嵌套路徑） | 純 refactor，param→frozen 邏輯單一源 | 既有 renderstate/inputassembler goldens 保持綠 | 【低】純重構 | 無 |
| **2. collector 累積（核心）** | 兩條 cook leg 的 concat 分支（command_cook:334-342 + resident:346-355）加「render-state op → 累積；else → 蓋 accum + concat」；merge/stamp 數學進 render_command_flow.h 兩腿共呼 | 累積-蓋章（stamp 已在） | 接管閘 + 兩腿閘（GridPlane frozen oracle） | 【中】owner-locked 兩腿；靜默 unstamped 失敗類 | 階段 1 |
| **3. 順序 + 多-Draw 忠實度** | 確認累積按 wire 序、前置 Draw 不吃後置 state、多 Draw 共享 | 邊界情形 | 順序閘 + 多-Draw 閘 | 【低-中】route (a) 的忠實度證人 | 階段 2 |
| **4.（另縫，記錄不做）Explicit render leaf** | bare-shader Explicit executor + draw-GridPlane.hlsl 轉譯綁定（5 unmapped 中的 4 shader op） | 通用 VS/PS 綁定管線 | pixel 閘（§4.2.5） | 【高】無現成通用綁定；transpile | 階段 2；**本縫外** |

階段 1-3 = 本縫；階段 4 = 自然下一戰役（shader-binding 縫），與本縫解耦。

## 6. 風險假設

1. 【最高】**flat/resident 累積分岔**（S2c 血訓，render_command.h:120/SEAM2 §7）：resident-only 漏累積
   = 靜默 wrong-render，flat selftest 抓不到。**緩解**＝merge/stamp 是**單一** render_command_flow.h
   helper 兩腿共呼（比照 loopRunIterations:59-61），＋兩腿閘 drive 同複合斷言 byte-identical。
2. 【中】**與既有嵌套 stamp 雙路徑相撞 / 雙重 stamp**：真實 corpus 從不嵌套（只 golden 走嵌套）。
   累積只在 state op 無 inputCommand 時觸發，嵌套路徑不變。**緩解**＝階段 1 保既有 golden 綠 +
   斷言累積路徑不 double-stamp（innermost-wins 已在 stampRenderState:114）。
3. 【中】**mask 遺漏 → 跨 stage 覆蓋**：若某欄位漏標 mask，Rasterizer 的預設會蓋掉 OM 的設定。
   **緩解**＝mask 逐 stage 靜態常數 + 順序閘（跨 stage 混設）咬。
4. 【中】**Explicit render 未落 → pixel 無法驗**：別對本縫過度承諾。**緩解**＝本縫閘全走 cook-level
   frozen-tuple oracle（§4.2.1-4），pixel 明列 DEPENDENT（§4.2.5 / 階段 4）。
5. 【低】**unmapped sibling drop 改變 wire 序**：t3_import.cpp:327-328 原地 drop 不重排、
   :305,316-351 陣列序保留 → Draw 仍最後。**緩解**＝接管閘 probe 斷言 Draw 為累積後蓋章對象。
6. 【低】**Transform(284d2183) unmapped 使 compound output 端無值**：接管閘錨 Execute 子節點而非
   compound output（--selftest-execute:163 precedent）。Transform-command 縫另立。

## 7. Critical Files
- `app/src/runtime/point_graph_command_cook.cpp`（flat collector concat 分支 :334-342 → 累積-蓋章）
- `app/src/runtime/point_graph_resident_command_cook.cpp`（resident twin :346-355，**必與 flat 同改**）
- `app/src/runtime/point_ops_renderstate.cpp`（抽 frozenDeltaForRenderStateOp；cookRasterizer:175-185/
  cookOutputMerger:249-279 重構；stampRenderState:110-119 重用）
- `app/src/runtime/render_command_flow.h`（宣告 merge/stamp 兩腿共用 helper，比照 switchSelectIndex:40）
- `app/src/runtime/point_ops_inputassembler.cpp`（cookInputAssembler:63-69 delta 抽取）
