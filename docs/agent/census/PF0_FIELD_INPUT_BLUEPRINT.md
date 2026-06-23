# PF0_FIELD_INPUT_BLUEPRINT.md — PF-0 field-input-projection keystone 施工藍圖

> Read-only Plan pass, 2026-06-23. file:line 對 authoring 時 HEAD（probe commit `f5fe112` 主樹常駐 tooth），動碼前 re-confirm。
> 起點 = `PARTICLE_FIELD_BLUEPRINT.md §0.5`（probe=B + PF-0 五步草案）。本檔把那五步細化成可施工的階段、owner-lock、與每階驗收 op。
> 核心張力（先說結論）：**resident `cc.graph=nullptr` 不是障礙。** probe golden 自己證明了 resident 的 `ResidentEvalGraph (rg)` 已攜帶 field 連線（`{5,"Result",4,"VectorField"}`，`particlefield_probe_golden.cpp:204`）。`cc.graph=nullptr`（`point_graph_resident.cpp:330`）只擋「OP 讀 flat Graph」，不擋「resident DRIVER 走 rg 重建 field tree」——precedent 就是 `cookResidentGradient`（`:395-430`）已經這樣遞迴重建 host-value tree。**真正的最大未知不是 resident 無 graph，而是「FieldNode 怎麼從 resolved param map 取值」——目前每個 field op 只有手寫 positional `configure*()`，沒有 generic param-apply 路徑。** 見 §1.5 + §9。

---

## 0. 一句話判決

PF-0 = **把 wired field 節點（ToroidalVortexField.Result）遞迴組成 `FieldNode` tree，並在 force cook 拿得到的 channel 上交付**——flat 與 resident 各自的 gather 都要做，且 field op 必須能從「resolved param map」而非「test factory」取得自己的數值。force kernel 注入（PF-a）不在 PF-0 scope；PF-0 只負責「讓 wired field 到得了 force cook」。

probe=B 證的縫：force NodeSpec 無 Field port（`node_registry_particle.cpp:47-54`）、兩 cook driver 無 `"Field"` gather、無 graph→FieldNode builder（`renderField2d`/`makeFieldNode` 唯一 caller 是 field render golden）。

---

## 1. graph→FieldNode builder 設計（核心未知 #1）

### 1.1 現狀：tree 的 source 哪來
`assembleFieldMSL(root, templateMsl)`（`field_graph.h:151` / `field_render.cpp:27`）走的是一個**已經組好的 `shared_ptr<FieldNode>` tree**——它只負責 codegen（遞迴 `collectFieldCode`，`field_graph.h:183`），不負責「組 tree」。tree 怎麼來？看 golden：
- `field_ops_blendsdfwithsdf_golden.cpp:134-142` — `makeFieldNode("BlendSDFWithSDF","golden0")` 建 root，`configureBlendSdfWithSdf(*blend, range, offset, bug)` 灌參數，`blend->inputs.push_back(make_shared<GoldenSphere>(...))` 手接 children。
- **source = test factory + 手寫 push_back + 手寫 configure**。production 無此路徑。

所以 PF-0 builder 要做的，是 golden 那段「makeFieldNode + configure + 接 children」的 **graph-driven 版本**：source 從 graph node + connections 走，不是 hand-coded。

### 1.2 builder 形狀（per-type makeFieldNode + recurse children）
新函式（建議落 `field_node_registry.cpp` 或新 `field_graph_builder.cpp`，純 runtime、無 platform）：

```
// 概念簽名（flat 版；resident 版見 §3）
std::shared_ptr<FieldNode> buildFieldTree(
    /*graph 走訪所需*/ <node-lookup>, <connection-lookup>,
    <this-field-node-id>,
    const std::map<std::string,float>* params);   // THIS node 的 resolved params
```

遞迴骨架（鏡像 `cookResidentGradient` + `assembleFieldMSL` 的 tree shape）：
1. `const NodeSpec* s = findSpec(nodeType)`；若無或 output 非 `"Field"` → nullptr。
2. `auto node = makeFieldNode(nodeType, shortId)`（`field_node_registry.h:45` 已存在的 type dispatch——**這就是 node→FieldNode 的 type dispatch 點**）。`shortId` = node 的 guid/id 前綴（builder 算，餵進 prefix=BuildNodeId，保證 param 不撞，`field_graph.h:99`）。
3. **灌 THIS node 的參數**（§1.5 的核心未知）：把 resolved param map 投進 node。
4. **遞迴 children**：對 `s->ports` 中每個 `isInput && dataType=="Field"` 的 port，找其 connection，遞迴 `buildFieldTree(upstreamId, upstreamParams)`，`node->inputs.push_back(child)`。（multiInput → 展開 extraConns，同 `cookResidentGradient:411-416`。）
5. return node。

children 遞迴的 **port-type dispatch = `dataType=="Field"`**（鏡像 flat gather 對 `"Gradient"`/`"Mesh"`/`"Texture2D"` 的 dataType 分派，`point_graph.cpp:241,258,278,300`）。ToroidalVortexField 是葉（無 Field input port，`field_ops_toroidalvortexfield.cpp:217` 只有 Center/Radius/.../Result），故遞迴在它停。combiner（BlendSDFWithSDF 之類）有 Field input → 遞迴下去。

### 1.3 與 TiXL 對照
TiXL `ShaderGraphNode.CollectEmbeddedShaderCode`（`ShaderGraphNode.cs:183`）的 `InputNodes`（`:134-165`）就是「從 connected node ops 收集 child ShaderGraphNode」——TiXL 在 Update 時把 `_connectedNodeOps` 同步進 `InputNodes`，codegen 再走 `InputNodes`。**sw 的 builder = 把 TiXL「connected node ops → InputNodes」這一步，從 graph connection 顯式做出來**（sw 無 ShaderGraphNode 的 live slot 機制，故 builder 在 cook 時一次性組 tree）。**這是忠實 TiXL，非 fork**——只是 sw 把 TiXL slot-update 時機的隱式收集，挪到 cook-time 顯式遞迴。

### 1.4 lifetime
builder 每次 cook 重建 tree（cheap，純 host 物件 + shared_ptr）。assembled MSL 由 srcHash 快取（PF-a 的事），所以「每幀重建 tree」不觸發重編。tree 本身單幀存活、交付給 force cook 後即可棄（PF-a 在 cook 內就 assemble 完）。**不需 cross-cook 保存 tree**（見 §8）。

### 1.5 ★核心未知 #1（NEED-CODE-PROBE 候選）：FieldNode 怎麼從 param map 取值
**目前不存在 generic「apply resolved params to FieldNode」。** 每個 field op 只有手寫 positional `configure*(FieldNode&, float, float, int, ...)`（`field_ops_toroidalvortexfield.cpp:233` 等 20+ 個），參數順序、型別、名字全 hand-coded，且 leaf type 是 TU-private（`dynamic_cast` 在 owning TU 內）。golden 靠這個灌值；production builder **沒有對應入口**。

三條候選（預設選 C，最忠實資料驅動）：
- **A. 每個 field op 加 `configureFromParams(FieldNode&, const map<string,float>&)`**：每 leaf 自己從 map 取自己的 named param（`m.at("Radius")` 等）。最直接，但 N 個 op 各加一個 entry point（散）。
- **B. 在 `FieldNode` base 加 virtual `applyParams(const map<string,float>&)`**：每 leaf override，從 map 取值。比 A 集中（走 base seam），但要動 frozen base `field_graph.h:93`（風險：base 是宣告凍結的）。
- **C. 把 param-apply 接進 `makeFieldNode` 的 factory 簽名**：`makeFieldNode(type, shortId, const map<string,float>* params)`，factory 內部呼叫該 op 自己的 param-apply。最資料驅動（builder 只呼 `makeFieldNode`，不知道 op 細節），但要改 `FieldNodeFactory` typedef（`field_node_registry.h:38`）+ 每個 leaf 的 factory lambda。

**判決：選 C，但分階——PF-0a 先讓 builder 跑通 ToroidalVortexField 單 leaf（手接一個 configure-from-map），證遞迴 + 投影骨架；PF-0c 再 generalize 成 factory 簽名擴充覆蓋所有 field op。** 理由：ToroidalVortexField 是 probe 唯一要的 leaf，先窄後寬避免一次動 20 個 leaf。**這條標 NEED-CODE-PROBE**：需確認 `makeFieldNode` factory lambda 改簽名不破壞既有 field render golden（它們呼 `makeFieldNode(type, shortId)` 兩參數——C 須保留 default `params=nullptr` overload）。

---

## 2. flat gather 分支（`point_graph.cpp`）

### 2.1 落點
flat 的 buffer-input gather 在 `point_graph.cpp:232-245`（force 走這裡，`isBufferInput` 收 `ParticleForce` port，force node 進 `ins[1]`/`insParams[1]`）。Field 不是 buffer，是 node-tree——仿 **Gradient gather（`:284-309`）**：獨立 loop，按 `dataType` 分派，落到新 `PointCookCtx` channel。

### 2.2 關鍵 wiring 差異（血淚錨點）
Gradient gather 是「**THIS node** 的 Gradient input port → 遞迴 upstream」。但 field **不是接在 ParticleSystem 上，是接在 force node 上**（probe `:152`：`ToroidalVortexField.Result → VectorFieldForce(id 4)`，不是 → ParticleSystem(id 2)）。

所以 flat field gather 的形狀是：cookParticleSim 的 cook 在 gather 它的 `ParticleForce` buffer input 時，對每個 wired force node，**再 chase 該 force node 的 `"Field"` input port**，遞迴 builder，把結果 tree 與該 force slot 對齊收進 channel。具體：

```
// 在 :232-245 的 buffer-input loop 內，或緊接其後一個 field loop：
for (force input slot i where port.dataType=="ParticleForce" && wired):
    int forceNodeId = pinNode(c->fromPin);
    // chase the force node's "Field" input:
    const NodeSpec* fs = findSpec(forceNode->type);
    for (PortSpec& fp : fs->ports) if (fp.isInput && fp.dataType=="Field"):
        const Connection* fc = g.connectionToInput(pinId(forceNodeId, fpIdx));
        if (fc):
            fieldTree = buildFieldTree(g, pinNode(fc->fromPin), nodeParams(pinNode(fc->fromPin)));
    // stash fieldTree aligned to force slot i
```

**`buildFieldTree` 在 flat 有完整 graph access**（`cookNode` 的 closure 捕 `g`，`point_graph.cpp:186`；`nodeParams(id)` 已存在 `:147`/`:311` resolve 完整 value spine）。所以 flat builder 直接走 `g.node()` / `g.connectionToInput()` / `nodeParams()`——零新基礎設施。

### 2.3 byte-identical 保證
新 field loop 對「無 Field input port 的 force / 無 wired field」→ tree=nullptr → channel 空 → 既有 baked path 不變（force kernel 仍吃 baked (1,1,1)，因為 PF-0 只交付 tree，PF-a 才消費）。**PF-0 落地後，probe 的 flat 腿仍 isotropic**（tree 到了但沒人吃）——這是對的，§6 解釋驗收怎麼分階。

---

## 3. resident gather 鏡像（`point_graph_resident.cpp`）

### 3.1 `cc.graph=nullptr` 不是障礙——澄清
`point_graph_resident.cpp:330` 的 `cc.graph=nullptr` 註解「resident path: ops read params, never a graph」——這只說 **OP body** 不拿 graph。但 **resident DRIVER**（cookNode lambda、cookResidentGradient lambda）本身持有 `rg`（ResidentEvalGraph）+ `rc`，整個 driver 就是在走 rg 重建各種 tree。`cookResidentGradient`（`:395-430`）就是活證據：它從 `rg.node(path)` + `n->input(port.id)->srcNodePath` + `nodeParams(path)` **遞迴重建 host-value tree**，跟 flat 對等。

**所以 resident field tree 不需「物化成可攜資料過 flatten」——driver 在 cook-time 走 rg 即時重建，同 Gradient。** probe golden `cookResidentLeg` 已把 `VectorField` Field input + `{5,"Result",4,"VectorField"}` wire 放進 SymbolLibrary（`:187,204`），`buildEvalGraph` 把它 flatten 進 rg 的 `ResidentInput::Driver::Connection`。resident builder 只要照 `cookResidentGradient` 的形狀走。

### 3.2 落點 + 形狀
鏡像 §2.2，但用 resident 的 input 解析（`n->input(port.id)`、`ri->srcNodePath`，`point_graph_resident.cpp:290-295`）：

```
// resident cookNode 內，gather force buffer input 後：
for (force slot i, ResidentInput* fri = n->input(forceSlotId), driver==Connection):
    const ResidentNode* forceNode = rg.node(fri->srcNodePath);
    const NodeSpec* fs = findSpec(forceNode->opType);
    for (PortSpec& fp : fs->ports) if (fp.isInput && fp.dataType=="Field"):
        const ResidentInput* fieldRi = forceNode->input(fp.id);
        if (fieldRi && fieldRi->driver==Connection):
            fieldTree = buildResidentFieldTree(rg, fieldRi->srcNodePath, depth+1);
```

`buildResidentFieldTree` = §1 builder 的 resident 變體（peer to `cookResidentGradient`，宣告在 `:126` 那群 lambda 旁），用 `rg.node(path)` + `n->input()` + `nodeParams(path)` 遞迴。**param 取值同 flat 走 §1.5 的 configure-from-map**（`nodeParams(path)` 在 resident `:299` 已 resolve 完整）。

### 3.3 唯一真風險（標 FLAG，非 blocker）
flat 與 resident 的 builder 是**兩份遞迴**（如同 Gradient 有 flat `cookGradientNode` + resident `cookResidentGradient` 兩份）。漏鏡 → resident prod 拿不到 tree → PF-a 落地後 prod-only 退化。**守法：probe golden 已是雙腿（flat + resident 各跑），PF-0+PF-a 後翻轉它時，resident 腿獨立斷言 anisotropy≠0。** 這正是 probe `:240,263-265` 已備好的 resident 行。

---

## 4. PointCookCtx field channel

仿 `inputGradients`（`point_graph.h:85`）/ `meshVtx`（`:92`）加一個 borrowed-single-frame channel。建議：

```cpp
// FIELD input (PF-0 field-into-force seam): a force op wired to a field op (VectorFieldForce <-
// ToroidalVortexField.Result) gets the assembled FieldNode tree here, per force slot. SAME borrowed-
// single-frame lifetime as inputGradients (driver-owned shared_ptr, never retained past cook). null
// for every force with no wired Field -> byte-identical (PF-a's kernel falls back to baked (1,1,1)).
std::shared_ptr<FieldNode> inputFieldTree = nullptr;   // single force-slot v1 (one VectorFieldForce)
// or, if multi-force pools need per-slot fields:
// std::shared_ptr<FieldNode> inputFieldTrees[kMaxForceInputs] = {};
```

**設計選擇 v1：單一 `inputFieldTree`**（probe 只有一個 VectorFieldForce，一個 field）。多 force × 多 field 的 per-slot 對齊標為 named fork [fork-VFF-singlefield]，延後。cook（PF-a）取：`if (c.inputFieldTree) assembleFieldMSL(c.inputFieldTree, forceTemplate) ...`。

**型別注意**：`PointCookCtx` 目前不 include `field_graph.h`（FieldNode）。加 `struct FieldNode;` forward decl + `<memory>`，避免重 include（FieldNode 完整定義只在 builder TU + PF-a cook TU 需要）。`shared_ptr<FieldNode>` 前向宣告即可放 ctx 成員。

---

## 5. NodeSpec field port（`node_registry_particle.cpp`）

VectorFieldForce spec（`:47-54`）加一個 input port，dataType `"Field"`，鏡像 TiXL `VectorField` slot（`VectorFieldForce.cs:9-10`，`InputSlot<ShaderGraphNode>`）：

```cpp
{"VectorFieldForce",
 "VectorFieldForce",
 {{"force", "force", "ParticleForce", false},
  {"VectorField", "VectorField", "Field", true},   // NEW: TiXL ShaderGraphNode input, un-omits the comment :44-45
  {"Amount", "Amount", "Float", true, 1.0f, 0.0f, 10.0f},
  {"Randomize", "Randomize", "Float", true, 0.0f, 0.0f, 1.0f},
  {"_ForceKind", ...}},
 nullptr},
```

**資料驅動，零 cook 改**（spec 是純資料）。port id `"VectorField"` 對齊 probe 的 resident symbol（`:187` `{"VectorField","VectorField","Field",0.0f}`）與 wire（`:204`）。**改完 probe 的 `forceHasFieldInput()`（`:111-117`）即回 YES**——這是 PF-0 第一個可見翻轉（見 §6）。

**注意**：`"Field"` dataType 須在 wire 相容性表 / port colour / `isBufferInput()` 被正確分類為「非 buffer、非 float」——確認 `isBufferInput()`（`point_graph.cpp` 附近）不會誤收 Field port 進 `ins[]`（鏡像它已 skip Texture2D/Mesh/Gradient，`:258,278,297`）。**標小 FLAG：grep `isBufferInput` 定義確認 Field 不被當 buffer。**

---

## 6. 分階 + 每階驗證 op

PF-0 可再拆三小階（builder 先、gather 後、generalize 最後）：

| 階 | 內容 | 檔 | 驗收 = probe golden 怎麼動 |
|---|---|---|---|
| **PF-0a** | (1) NodeSpec 加 Field port（§5）。(2) builder 骨架 + ToroidalVortexField 單 leaf configure-from-map（§1.5 窄路）。(3) flat gather（§2）+ PointCookCtx channel（§4）。 | `node_registry_particle.cpp`, 新 `field_graph_builder.cpp`, `point_graph.cpp`, `point_graph.h` | probe `forceHasFieldInput()` → YES。但 PF-a 未做，kernel 仍 baked → **flat 腿仍 isotropic**。**此階不翻 probe 主斷言**；它的可見效果是 static 行從 NO→YES。新增一個 builder 單元 selftest（見下）。 |
| **PF-0b** | resident gather 鏡像（§3）。 | `point_graph_resident.cpp` | resident 腿也能拿到 tree（仍未消費）。probe resident 行的 mean 不變（baked）。 |
| **PF-0c** | param-apply generalize 成 factory 簽名（§1.5 選 C），覆蓋所有 field op。 | `field_node_registry.h/.cpp` + 各 leaf factory | 既有所有 field render golden 仍綠（C 保留兩參數 overload）。 |
| **PF-a**（非 PF-0） | kernel 注入 + source-compute PSO（原 §3 PF-a）。 | `point_ops.cpp`, `tex_op_cache`, template, `particle_params.h` | **此時才翻轉 probe**：改 no-bug 斷言 anisotropy≠0、移除 `!fieldInputExists` 條件。`-bug` 改成「斷 field → baked isotropic」RED。 |

**每階配的 op / golden**：
- **PF-0a/b 的 acceptance 不是 probe 主斷言**（probe 量的是 kernel 效果，PF-a 才有）。PF-0 階段需要一個**新的 builder-level golden**：`--selftest-fieldtree-builder`（或擴充 probe 加一個 `--selftest-particlefield-probe` 的 builder 子斷言），直接斷言「flat cook 後，VectorFieldForce 的 PointCookCtx.inputFieldTree != nullptr 且 root type==ToroidalVortexField 且 param Radius==wired值」。**這條 golden 在 PF-0 完成時 GREEN、PF-0 之前 RED**——它是 PF-0 的真驗收 tooth，獨立於 PF-a。建議落 `app/src/`（shell tier，鏡像 `particlefield_probe_golden.cpp`）。
- **probe golden `--selftest-particlefield-probe` 的翻轉時機 = PF-a 完成**。PF-0 完成只讓 `forceHasFieldInput()`→YES，故 PF-0 落地後須**同步更新 probe 的 no-bug 斷言**：`gapHolds` 現在要求 `!fieldInputExists`（`:285`），PF-0 加了 Field port 後這條會 FAIL——**PF-0a 必須把 probe no-bug 斷言從「no Field input」改成「有 Field input 但 kernel 仍 baked isotropic」**（過渡狀態斷言），PF-a 再改成終態 anisotropy≠0。**這是 probe golden 的兩段式翻轉，標清楚避免 PF-0a 一落地就紅 probe。**

---

## 7. 與 S4 拆 point_graph 的撞檔協調

PF-0a 動 `point_graph.cpp`（862 行，超 cap），PF-0b 動 `point_graph_resident.cpp`——S4 也拆這兩檔。撞點具體：
- PF-0 在 flat `point_graph.cpp:232-309` 的 gather 區插新 field loop；S4 可能把整個 gather 抽成 method。
- PF-0 在 resident `:284-338` 插鏡像；S4 同樣動這段。

**owner-lock 順序建議**：
1. **PF-0 的 gather 改動體量小**（一個新 loop + 一個新 builder TU + 一個 ctx 欄位），**建議 PF-0 先 land、S4 後拆**。理由：PF-0 的 field loop 是「再加一個 dataType 分支」，與既有 Gradient/Mesh/Texture loop 同構；S4 拆檔時把這些 loop 一起搬，PF-0 先進去 = S4 多搬一個同構 loop，零額外衝突。反之 S4 先拆 → PF-0 要追新檔位置，且 builder 落點要對齊 S4 的新模組邊界（未定）。
2. 若 S4 已在飛行：PF-0 與 S4 **同檔 owner 序列化**，PF-0 在 S4 的新邊界內插（PF-0 的 field loop 跟著 gather method 走）。
3. **新 builder TU（`field_graph_builder.cpp`）獨立檔，與 S4 零撞**——builder 邏輯放這裡，`point_graph.cpp` 只留「呼叫 builder + 收進 ctx」幾行，最小化 S4 撞面。**這是降撞的關鍵設計：把 PF-0 的肉放新檔，老檔只留 thin call-site。**

標 FLAG：PF-0 開工前確認 S4 是否已動 `point_graph.cpp`/`point_graph_resident.cpp`（git/owner-lock board）。

---

## 8. 本質 vs 意外複雜

| 點 | 本質 / 意外 | 說明 |
|---|---|---|
| graph→FieldNode builder 不存在 | **本質** | field tree 在 TiXL 是 ShaderGraphNode 的 InputNodes 隱式收集；sw 無 live slot，必須 cook-time 顯式遞迴。包成乾淨接縫（新 TU `buildFieldTree` + resident twin）。 |
| FieldNode 無 generic param-apply（只有 positional configure*） | **意外** | 純歷史產物——golden-only 的 configure* 簽名沒留 production 入口。§1.5 選 C 補一個資料驅動 factory 簽名即除。**這是 PF-0 最大的意外複雜，但有界。** |
| resident 「無 graph」field tree 物化 | **偽問題（澄清掉）** | `cc.graph=nullptr` 只擋 OP，driver 持 rg。`cookResidentGradient` 已證 resident 能即時重建 host-value tree。**不需把 field tree 物化成可攜資料過 flatten**——driver cook-time 走 rg 重建即可。原 §0.5「最大設計問題」在此降級為「照 Gradient precedent 抄一份 resident builder」。 |
| cross-cook field lifetime | **非問題（包乾淨）** | tree 單幀重建、交付即棄（§1.4）；不跨 cook 保存。srcHash 快取在 PF-a 處理 MSL 重編，與 tree 物件生命週期正交。 |
| flat/resident 兩份 builder | **本質**（sw 雙-pass cook 世界觀） | 同 Gradient/Mesh 都兩份。雙腿 golden 守（§3.3）。 |
| field 接在 force node 非 ParticleSystem | **本質** | TiXL force 自帶 field slot；sw 鏡像（force node 的 Field input）。gather 須 chase force→field 二跳（§2.2/§3.2），非一跳。 |

---

## 9. 風險 / 未知（NEED-CODE-PROBE 標記）

| # | 未知 | 嚴重度 | 處置 |
|---|---|---|---|
| 1 | **FieldNode param-apply from map**（§1.5）：`makeFieldNode` factory 改簽名是否破壞既有 ~20 field render golden？leaf type TU-private，generic apply 要不要動 frozen base？ | **高（PF-0 最大未知）** | **NEED-CODE-PROBE**：PF-0a 先只對 ToroidalVortexField 加一個 `configureToroidalVortexFieldFromParams(FieldNode&, const map&)`（窄），跑通 builder；PF-0c 再決 generalize 形狀。先別動 base。 |
| 2 | resident builder 能否從 rg 完整取 field 子樹的 params | **低（已証）** | `cookResidentGradient` + probe 的 SymbolLibrary 已証 rg 攜 field wire + nodeParams 可 resolve。照抄。**不需額外 probe。** |
| 3 | `isBufferInput()` 是否誤收 `"Field"` port 進 `ins[]` | **低** | grep 確認（§5）；鏡像 Texture2D/Mesh/Gradient 已被 skip 的證據。 |
| 4 | `"Field"` dataType 在 wire 相容 / port 顏色 / Add-menu 是否已註冊 | **低-中** | ToroidalVortexField 已有 `"Field"` output（`field_ops_toroidalvortexfield.cpp:216`），故 dataType 字串已在系統流通；確認 VectorFieldForce 的 Field **input** 能與之連線（wire type-match 表）。標小 FLAG。 |
| 5 | PointCookCtx include FieldNode 是否引 runtime↛platform 違規 | **低** | FieldNode 是純 runtime（`field_graph.h` 零 Metal）；forward-decl + shared_ptr 在 ctx 安全。 |
| 6 | probe golden 兩段式翻轉（§6）漏改 → PF-0a 一落地紅 probe | **中（流程風險）** | PF-0a 的 PR **必須**同步把 probe no-bug 斷言從 `!fieldInputExists` 改成過渡斷言。列為 PF-0a 驗收 checklist 第一項。 |

**結論**：原 §0.5 點名「resident 無 graph 是最大未知、可能需再一個 probe」——**本藍圖判定 resident 無 graph 是偽問題**（`cookResidentGradient` precedent 已解），**真正需要 code-probe 的是 #1（FieldNode param-apply 路徑）**。建議 PF-0a 第一步先寫 `--selftest-fieldtree-builder`（builder 把 ToroidalVortexField 從 graph 組出來、param Radius 對得上）的 RED 證據，再開工 builder——鏡像 probe golden 先紅後綠的紀律。

---

### Critical Files for Implementation
- `app/src/runtime/point_graph.cpp`（flat gather `:232-309`，插 field loop + 呼 builder；thin call-site，肉放新 TU）
- `app/src/runtime/point_graph_resident.cpp`（resident gather `:284-338`，鏡像 `cookResidentGradient:395-430` 的遞迴 builder）
- `app/src/runtime/field_node_registry.h`（`makeFieldNode` type dispatch `:45` / `FieldNodeFactory` `:38`——param-apply 簽名擴充的核心未知 #1）
- `app/src/runtime/node_registry_particle.cpp`（VectorFieldForce spec `:47-54`，加 `"Field"` input port）
- `app/src/runtime/point_graph.h`（`PointCookCtx` `:54`，加 `inputFieldTree` channel 仿 `inputGradients:85`）
- 新檔（建議）`app/src/runtime/field_graph_builder.cpp`（graph→FieldNode 遞迴 builder，降 S4 撞面）
- ref-only：`app/src/runtime/field_graph.h`（`assembleFieldMSL:151` / `collectFieldCode:183` / FieldNode base `:93`）、`app/src/particlefield_probe_golden.cpp`（acceptance FLAG，兩段式翻轉）、`external/tixl/Operators/Lib/particle/force/VectorFieldForce.cs`（`InputSlot<ShaderGraphNode> VectorField` ground truth）、`external/tixl/Core/DataTypes/ShaderGraphNode.cs:134-201`（InputNodes 收集 = builder 對照）

---

## 關鍵發現摘要（給 orchestrator）

三個與原 §0.5 草案不同的判決，會改變施工排序：

1. **resident「無 graph」是偽問題**。probe golden 自己的 `cookResidentLeg` 已把 field wire 放進 `SymbolLibrary` 並 flatten 進 `rg`；`cookResidentGradient`（`point_graph_resident.cpp:395-430`）已證 resident driver 能從 `rg` + `nodeParams(path)` 即時遞迴重建 host-value tree。`cc.graph=nullptr` 只擋 OP body，不擋 driver。**不需把 field tree 物化過 flatten——照 Gradient precedent 抄一份 resident builder 即可。**

2. **真正的最大未知是 FieldNode 的 param-apply 路徑**（§1.5 / §9#1）。每個 field op 只有手寫 positional `configure*(FieldNode&, float, float, int...)`，且 leaf type TU-private，沒有 production 用的「從 resolved param map 灌值」入口。builder 能 `makeFieldNode(type)` 建出殼，但灌不進 graph 的數值。這是要先 code-probe 的點，建議窄路先過（ToroidalVortexField 單 leaf），再 generalize。

3. **probe golden 是兩段式翻轉，不是一次翻**。PF-0 加 Field port 後 `forceHasFieldInput()`→YES，會讓現有 no-bug 斷言（要求 `!fieldInputExists`，`:285`）立刻 FAIL。PF-0a 的 PR 必須同步把斷言改成「有 port 但 kernel 仍 baked」的過渡態，PF-a 才改終態 anisotropy≠0。漏改 = PF-0a 一落地就紅 probe。

施工排序建議：**PF-0 先 land、S4 後拆 point_graph**，且 builder 肉放新 TU（`field_graph_builder.cpp`）讓老檔只留 thin call-site，把 S4 撞面降到最小。
