# 值脊椎（Value Spine）— 設計 spec

> 日期：2026-06-08。權威：TiXL（`external/tixl`，已查證）。
> 對症：param 現在是死 float，只能 Inspector 手調，不能被連線驅動。值脊椎讓「值會流動」。
> 完成定義（凌駕一切）：柏為親眼看到 `Time→Sine→Remap→粒子 Speed` 接起來、粒子真的脈動。selftest 綠是必要不充分。

## 目標

讓一個 param 能被另一個節點的輸出**連線驅動**：沒連線時用 Inspector 的常數，連了線就用上游求值結果。最小可玩鏈 = `Time → Sine → Remap → ParticleSystem.Speed`，粒子脈動。

## 非目標（YAGNI，砍掉但網不塌）

- Vec2/Vec3/Color 值型別、自動轉型 → 先只有 Float。
- Curve（動畫曲線）節點 → 第二棒（**但記著：柏為是要畫動畫曲線的人，遲早要**）。
- 求值快取、拓樸排序 → 每幀重算幾個 float 極便宜，先不優化。
- `IsDefault` 旗標、MultiInput、非 Float 的 reference 型別 → 不做（理由見 schema）。

## 權威：TiXL 的 input 結構（已查證，非記憶）

TiXL **沒有** params/ports 兩套東西——每個 input 是一個帶預設值的 slot：
- `Core/Operator/Slots/InputSlot.cs:29`：`Value = Input.IsDefault ? TypedDefaultValue.Value : TypedInputValue.Value`
  —— 沒連線/沒手動改用預設，否則用連線/手調值。一個 slot 表達「常數 + 可被連線覆蓋」雙重身分。
- `Core/Operator/Symbol.Child.cs:177` `Input`：有 `Value` / `DefaultValue` / `IsDefault`。
- `Core/Operator/Symbol.cs:157` 連線用 `(TargetParentOrChildId, TargetSlotId)` 定址到「某節點的某 input slot」= (node, input port)。

→ **param = 帶預設值的 input port。一套，不是兩套。**

## 接縫（已查證，值脊椎的輸出端接這裡）

`app/src/main.cpp:322-325` 每幀：
```cpp
g_particles->setTurbulenceAmount(g_graph.param("TurbulenceForce", "Amount", 15.0f));
g_particles->setSpeed(g_graph.param("ParticleSystem", "Speed", 1.0f));
// ...
```
`Graph::param(type, paramId, fallback)`（graph.cpp:74）= 找 `firstOfType(type)->params[paramId]`。

**值脊椎只改這個讀取點**：把「讀死 float」換成「有連線→evalFloat 上游；沒連線→死 float」。
**GPU buffer 流（Points/Force buffer 在 GPU 間傳）完全不動。** 兩套求值在 setter 這點乾淨交接，不糾纏。

## 核心 schema 決策（第 5 柱，命門）

把分開的 `ParamSpec`(slider) 與 `PortSpec`(GPU buffer) **合一**：param 變成 `dataType="Float"` 的 input port，帶 default/min/max。

```cpp
// graph.h — PortSpec 擴充（Float input port 帶預設值；buffer port 不用這些欄位）
struct PortSpec {
  std::string id, name, dataType;   // dataType: "Points" | "ParticleForce" | "Float"(新)
  bool isInput;
  float def = 0, minV = 0, maxV = 1; // 僅 Float input 用：Inspector 常數的預設/範圍
};
// ParamSpec 退役（其資料併入 Float PortSpec）。Node::params map 留用，改 key = input port id：
// 它就是「沒連線時的 fallback 常數」，Inspector 編的就是它。
```

五個子問題全部塌縮成「重用已有」：

| 子問題 | 決策 |
|---|---|
| pinId 涵蓋 param | **免費**：param 是 input port，排進 ports 清單自動有 `pinId(node,idx)=node*100+idx+1`。無需另開定址空間。 |
| 「被驅動了嗎」 | **重用 Phase 2 `Graph::connectionToInput(pin)`**：非 null→上游求值；null→存的常數。不需 `IsDefault`（從連線表 derive）。 |
| 常數存哪 | **重用 `Node::params` map**（key=input port id），= 沒連線時的 fallback。 |
| 存檔 | **幾乎不動**：常數存 params map、連線存 fromPin/toPin（toJson/fromJson 既有）。 |
| 增刪/驅動切換走命令 | **重用 Phase 1**：拉值線=`AddConnectionCommand`、拔=`DeleteConnectionsCommand`；改常數=新 `SetInputValueCommand`（小）。 |

## 五根承重柱

1. **Float port 型別** — `PortSpec.dataType` 加 `"Float"`（純值，非 GPU buffer）。型別系統第一根柱。
   - 砍：Vec/Color/自動轉型。先打通一條。

2. **節點求值函數（cook）** — `NodeSpec` 現在只宣告 ports/params，沒有「怎麼算輸出」。每個值節點要有
   `float evaluate(const float* inputs, int n, const EvaluationContext& ctx)`（純值，無 GPU）。資料驅動：
   一張表登記 type→evaluate（沿用 menu/spec 資料驅動慣例）。

3. **拉動式求值器 `evalFloat(graph, outPin, ctx)`** — 給一個輸出 pin，沿連線往上游遞迴求值；
   到了某 input：`connectionToInput(inputPin)` 有→遞迴求其來源 output；無→該 input 的存的常數。
   選 pull（要值才往上拉），天然支援多扇出。
   - 砍：拓樸排序、快取。
   - **防環**：遞迴帶 visited set / 深度上限，偵測到環回 fallback 常數（值節點不該成環，但要擋）。

4. **`EvaluationContext`** — 已存在於 runtime（`Particle.h` 的 `{frameIndex,time,deltaTime}`，16B，step1 鎖死）。
   值節點求值吃這個 ctx；Time 節點從 `ctx.time` 讀。**重用，不新發明**（master 進度載明此 struct 已鎖）。

5. **param = 帶預設的 input port（schema 合一）** — 見上「核心 schema 決策」。最承重、最危險，已釘死。

## 五起手節點（最小可玩集）

| 節點 | 簽章 | 用途 |
|---|---|---|
| Time | ctx → float | 會動的源頭（讀 ctx.time） |
| Const | (param 常數) → float | 手調常數源 |
| Multiply | (Float, Float) → float | 最小數學 |
| Sine | Float → float | -1..1 波，調變器（給音樂家） |
| Remap | Float, (min,max 常數) → float | -1..1 → min..max 轉接頭（Sine 接不上 Speed 的範圍，靠它轉） |

- 砍到第二棒：Add/Sub/Divide（資料驅動，加一行 NodeSpec 就有）、Curve、Oscillate 相位/波形。

## 縫合 + 試壓

6. **UI** — 值節點進 Add menu（`specTypes` 已資料驅動，免費）；Float 連線要能拉（editor_ui 現在只認
   Points/Force，要放行 Float×Float）；**Inspector 的 SliderFloat：被連線驅動時變灰／標來源**（看不見的驅動=沒驅動，承重）。
7. **存檔** — 值節點=node、值線=connection，沿用 toJson/fromJson；常數在 params map。roundtrip 用既有 `--selftest-graph` 守。
8. **隔離 selftest（鐵律 5）** — `--selftest-valuecook`：建 `Const(3)→Multiply←Const(4)` 斷言=12；
   `Time→Sine` 餵 `ctx.time` 斷言輸出對。純值斷言，不靠畫面。RED→GREEN。
9. **眼手驗（完成定義）** — 接 `Time→Sine→Remap→Speed`，用 eye/hand 截前後幀，看粒子脈動。
   **這次驗得比 reconnect 好**：脈動是畫面變化，eye 截圖看得出（reconnect 拖線卡在沒 pin 座標）。

## 重用 Phase 1/2 vs 新碼（柏為要的對照）

**重用（不重寫）**
- `connectionToInput`（Phase 2）→ 判 input 被驅動。
- `AddConnectionCommand` / `DeleteConnectionsCommand`（Phase 1）→ 值線增刪可 undo。
- `MacroCommand` / `CommandStack`（Phase 1）→ 命令層。
- `EvaluationContext`（step 1，已鎖）→ time 入口。
- `Node::params` map / `toJson`/`fromJson`（step 3）→ 常數存取與存檔。
- Add menu / `specTypes` 資料驅動（B0）→ 值節點自動進選單。
- `--selftest-graph` / 眼手 harness（含新加的鍵盤）→ 回歸與驗收。

**新碼**
- `PortSpec` 加 `"Float"` + def/min/max 欄位；`ParamSpec` 退役併入。
- `NodeSpec` 加 `evaluate` 函數 + type→evaluate 資料表。
- `evalFloat(graph, pin, ctx)` 拉動求值器（含防環）。
- 五個值節點的 evaluate 實作 + NodeSpec 登記。
- `SetInputValueCommand`（改常數可 undo）。
- editor_ui：Float 連線放行、Inspector 被驅動變灰、值節點繪製。
- main.cpp:322-325 改用 evalFloat（有連線走求值，否則常數）。
- `--selftest-valuecook`。

## 實作順序（紙上→疊碼）

> 順序鎖：schema（第 5 柱）已釘死本 spec。下面按承重柱往上疊，每階段一個 selftest + 能跑。

1. Float port 型別 + PortSpec/ParamSpec 合一（schema 落地）+ 存檔相容 → `--selftest-graph` 仍綠。
2. `evaluate` 表 + `evalFloat` 求值器（含防環）+ 五節點 evaluate → `--selftest-valuecook` RED→GREEN。
3. main.cpp setter 改走 evalFloat（接縫）→ 連 Const 驅動 Speed，值真的進 runtime。
4. editor_ui：Float 連線放行 + 值節點入 menu + Inspector 變灰 + `SetInputValueCommand`。
5. 眼手驗 `Time→Sine→Remap→Speed` 脈動 → 柏為親手測。

## 自檢（架構憲法）
- 求值器/節點 evaluate/Float 型別/evalFloat = `runtime`（純計算）✓；`SetInputValueCommand` = `app`✓；
  editor_ui = `ui`✓。依賴 `ui→app→runtime` 不變。
- 值節點 evaluate 資料驅動（一表一登記，加節點=加一行）符合鐵律 7。
- 每柱一個 selftest（鐵律 5）。
