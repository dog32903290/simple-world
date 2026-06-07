# IMPORTANT：架構憲法

動任何程式碼前，先讀並遵守 `ARCHITECTURE.md`。核心鐵律：

1. 程式碼分五區：`runtime`(純計算) / `app`(產品行為) / `ui`(imgui 畫圖) / `platform`(原生接口) / `verify`(眼耳手驗證)。
2. 依賴單向：`ui → app → runtime`，`app → platform`；底層葉子不往上依賴。
3. verify 是葉子：業務/UI 碼對驗證系統只留一行 hook，實作肉全在 `verify/`。絕不把驗證邏輯寫進業務檔。
4. 一個檔一個職責；單檔 > ~400 行 = 警訊，要拆。
5. 每個子系統有可單獨跑的隔離測試（`--selftest-*`）。
6. 動程式碼前過自檢三題：①屬哪一區 ②依賴方向 ③要 hook 驗證嗎（一行 hook，肉放 verify/）。

---

# Repo Instructions: simple_world

## Goal Continuation

In `/goal` or any explicitly long-running task, the gates below are line-switching rules, not stopping points.

- After a manual read, source audit, acceptance record, proof, or verification passes, update the relevant ledger/status and continue to the next unfinished load-bearing line.
- Do not stop after one node, one patch, one proof, or one test batch unless the original goal is fully verifiable and already tested.
- Ask 柏為 only for irreversible choices, missing credentials/permissions, genuinely ambiguous human judgment, or a concrete blocker already tried through reasonable fallback paths.
- If a gate says to stop expansion, keep working on the blocking line itself; do not turn that into "wait for confirmation."

## Engineering Skill Loading Logic

### 預設（非工程對話）
不載入任何工程 skill。創作討論、方向探索、研究 → 零工程 skill。
`alignment-ledger` 是唯一可在任何對話自動亮的，且只在「回答快超過 3 個重點 / 柏為說太長、分段」時。

### Bundle A — 當前階段（TiXL 合約 + Vuo + UI 皮）
做工程時預設只帶這三個：
- `node-contract-architect` — 節點合約 / port / param 對不對
- `tixl-vuo-node-port`（repo-local）— `my_<ExactTiXLName>` 命名、分類色、Vuo class body
- `ui-skin-pressure-gate` — 皮有沒有超前合約；runtime 還是 Vuo 時這是主 gate

觸發語 → 帶 Bundle A：
「做這個節點」「加 my_X」「這個 port / param 怎麼定」「先看到畫面」「先把皮做出來」「TiXL 的某節點怎麼搬」「Vuo node 黑畫面 / Not Installed」「inspector / panel / timeline 加一個」

### Bundle B — 聲音線（Bespoke）
- `bespoke-sound-in-area`（repo-local）— 先讀 manual 再答

觸發語：「Bespoke」「sound_in_area」「looper / recorder / sampleplayer / 4-bar」
（B 和 A 可並存，不要無事先載。）

### 元層 skill — 按需單獨上，不進預設 bundle

| Skill | 觸發條件 |
|---|---|
| `press-pass` | 「你壓一下」「這個方向可行嗎」「卡住了」→ 快壓初判 |
| `unknown-proof-engineering` | press-pass 初判不穩、需要建 input→component→observable 閉環、「好累 / 每步都未知 / 完成度多少」 |
| `single-plan-progress-gate` | 「換 session」「追進度」「現在多少」「多個 plan 互相猜」 |
| `alignment-ledger` | 快講超過 3 個重點 / 柏為說太長 |

交棒順序：`press-pass`（快壓）→ 不穩 → `unknown-proof-engineering`（建閉環）→ 多 lane → `single-plan-progress-gate`（跨 session 路由）。

### 被動 skill — 只在貼材料進來時用，永不預設
- `triage-review-advice`：貼了一份 code review 或「這段會不會太 AI」

### 更深階段才需要（現在不載）
- `tooll3-interaction-compatibility` → 等決定自己寫 native runtime / C++ command stack 才上
- `agent-tool-harness` → 等讓 agent 自動操作 Bespoke/Vuo（非柏為手動）才上

---

## Bespoke Sound-In-Area Skill

This repo contains a local skill:

```text
skills/bespoke-sound-in-area/SKILL.md
```

Use it whenever the task mentions:

- Bespoke / BespokeSynth usage
- writing or modifying Bespoke nodes
- Bespoke script module code
- patching audio, notes, pulses, modulators, looper, recorder, sampleplayer, buffershuffler, notesinger
- `sound_in_area` buffer memory, relation control, continuity, arrangement modes, or previous 4-bar loop prototypes

Before answering or editing those topics, read:

```text
bespoke_ai_manual_v1.md
bespoke_ai_manual_v1.json
```

Rules:

- Do not invent Bespoke patch cable ports, control names, script methods, save-state fields, or native module requirements.
- Separate audio, notes, pulses, modulation/control, and module-specific special cables.
- Prefer Bespoke native audio modules before custom DSP.
- Put custom work first in analysis, relation control, memory metadata, continuity, and arrangement modes.
- If a needed node or method is missing from the v1 manual, inspect upstream docs/source before claiming behavior.

## TiXL to Vuo Node Port Skill

This repo contains a local skill:

```text
skills/tixl-vuo-node-port/SKILL.md
```

Use it whenever the task mentions:

- porting or naming TiXL nodes into My World / Vuo
- shader, SDF, raymarch, mesh, image, render, or renderTick Vuo prototypes
- comparing TiXL node function against our runtime fixtures
- debugging Vuo custom nodes that show `Not Installed`, black windows, or stale values

Rules:

- Creator-facing node names follow TiXL exactly with only `my_` in front: `my_<ExactTiXLNodeName>`, such as `SphereSDF` -> `my_SphereSDF`.
- Categories follow TiXL `Operators/Lib` families.
- Colors follow TiXL type color, not namespace color; e.g. `ShaderGraphNode` uses `ColorForShaderGraph`.
- Prove shader behavior in our runtime tools before treating the Vuo node as valid.
- Separate event flow from data flow: `renderTick` triggers cooking; data ports stay semantic values.
