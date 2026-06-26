# MAINTENANCE_HARDENING — 維護加固施工圖（三項預防，下次施工）

> 柏為 2026-06-26:「這三點都動，但另一個 session 正在動工。變成工單／文件，要他下次施工。」
> **本檔 = 預防性維護工單，非 parity 開採。** 三項來自一次「本質複雜孤島」盤點（見下「背景」）。排程權威仍是 [MASTER_PLAN](MASTER_PLAN.md)；結構債權威仍是 [DEBT_LEDGER](DEBT_LEDGER.md)（工作 2 是它的子項）。
> **錨點紀律**（同 [POST_PARITY_MV_TOOLING_PLAN](POST_PARITY_MV_TOOLING_PLAN.md) 的★源碼地圖）：用檔路徑 + grep 符號 + selftest 名稱（穩定），不信死行號。行號是 2026-06-26 快照（HEAD `29556d1` 附近）。

---

## 背景：為什麼有這三項

一次孤島盤點的結論：**simple_world 有 7 座本質複雜的孤島（領域難、非亂），目前全部被 `--selftest-*` + golden + `-bug` 咬合鎖住——無裸島。** 工程紀律到位。

但「無裸島」藏兩個脆弱點，這三項就是針對它們：
1. **覆蓋靠紀律、不靠機制**：沒有任何東西在掃「哪座島／哪個 op 缺閘」。今天 7 座全鎖，是 7 次「人工記得立閘」的結果。第 8 座島冒出來時沒有紅燈說它裸著 → **工作 1**。
2. **島 7（PointGraph 雙驅）結構在惡化**：閘鎖住「行為對」，鎖不住「越來越難安全動」（雙驅快 900 行、撞行數上限）→ **工作 2**。
3. **未來新島（決定性 export）別裸著誕生** → **工作 3**。

---

## ★開工閘（每項時機不同，別一起開）

| 工作 | 風險 | 時機閘 |
|---|---|---|
| **1 閘缺口 census** | 低（純新增 tool，不碰 runtime/cook-core） | **absent-safe，可獨立施工**。唯一注意：若要接進 `sw_status.sh` 需確認沒有別的 session 正在改它 |
| **2 島 7 拆 TU** | ⚠️**高（動承重 cook-core）** | 硬規四條，全滿足才開：①`point_graph.*` 不再 dirty（另一 session 收工）②柏為在場 ③單一 driver、不並行雙開（見 [DEBT_LEDGER](DEBT_LEDGER.md) + 教訓：同 checkout 雙寫會損壞）④拆前先存基準：守門 selftest 全綠 + `--bite` PASS |
| **3 決定性島帶閘** | 中（跟 MV C 塊路 2 綁） | 非獨立工單，跟 [POST_PARITY_MV_TOOLING_PLAN](POST_PARITY_MV_TOOLING_PLAN.md) 的 **C 塊路 2** 一起做，作為其誕生條件 |

---

## 工作 1：閘缺口 census（gate-gap census）

**動機**：把「閘覆蓋」從靠紀律升級成靠機制。不是自動寫測試，是**自動抓誰裸著**。

**scope**：新增一個盤點工具（仿現有 census），掃兩種缺口並報出：
- **op 缺口**：每個註冊的 op 有沒有對應的 golden / refuter / selftest。
- **島缺口**：維護一張「本質複雜島 → 守它的 selftest」對照表（初始 7 列見附錄），檢查每座島的守門 selftest 是否仍存在且仍在 `run_all_selftests.sh` 的掃描表裡。

**現有基建錨點（直接讀這些，仿它們的形）**：
- `tools/op_census.sh` — TiXL→sw 節點 clone 進度盤點（仿它的「現算」風格）
- `tools/ui_census.sh` — UI 對齊缺口盤點（另一半防漏網）
- `tools/run_all_selftests.sh` — 全 `--selftest-*` 掃描 + `--bite` 咬合（grep 它怎麼列舉 selftest 全表）
- `tools/check_arch.sh` + `tools/linecount-grandfather.txt` — 既有的自動閘（依賴方向 + 行數 ratchet），看它怎麼當 build target
- `tools/sw_status.sh` — 結帳閘；新工具理想上接進 `--check`，讓「有島裸著」也能擋結帳

**設計要點**：
- op→測試的對應關係：grep op 註冊表（`point_ops.h` / 各家族 registrar）對 golden/selftest 檔名慣例。
- 島清單先用附錄那張**手寫表**當 SSOT（7 座），新島落地時加一列——這張表本身就是「立閘紀律」的載體。
- 輸出：一份「裸清單」（op X 無 golden、島 Y 的守門 selftest 不在表裡）。

**驗收（這工具自己也要咬合）**：
- 跑工具 → 列出當前裸 op／島（現況應接近 0，因為 7 座島都鎖了）。
- **`-bug` 咬合**：故意把某座島的守門 selftest 從 `run_all` 表移除 → 工具必須報該島裸 → 證明它真的會抓。
- 接進 `sw_status.sh --check`：島裸著時結帳紅燈。

---

## 工作 2：島 7 — PointGraph 雙驅拆 TU

**動機**：結構債。selftest 鎖住「行為對」，但兩個 cook driver 快 900 行、無共享提煉、撞行數 ratchet 上限、註解擠不進。**趁它還拆得動時拆**，別等漲到沒人／沒 agent 能安全動。

**scope**：[DEBT_LEDGER](DEBT_LEDGER.md) 已記的「拆 Command 分支」。把 flat 與 resident 兩驅的鏡像邏輯提煉共享、Command 分支（Loop / SetVar / Camera scope / Field gather）拆成獨立 TU。

**錨點**：
- `app/src/runtime/point_graph.cpp` — flat cook driver（grep `flatCook`；~868 行，已頂 ratchet）
- `app/src/runtime/point_graph_resident.cpp` — resident cook driver（grep `cookResident`；~717 行，已頂 ratchet）
- `app/src/runtime/point_graph_internal.h` — `CmdCookCtx`，兩驅共享 seam（提煉的落點）
- 已分出的小片可參照：`point_ops_loop.cpp` / `point_ops_setvar.cpp` / `point_ops_camera_scope.h`

**守門 selftest（拆前存基準、拆後必全綠 + `--bite`，這是「行為不變」的鐵證）**：
- `--selftest-loop`（flat + resident 兩腿）
- `--selftest-camera-scope`
- `--selftest-setvar-scope`
- `--selftest-layercompose`（resident texture gather）

**完成判準**：兩驅行數雙雙降到 ratchet 之下、共享邏輯單一來源、上述 selftest 全綠 + `--bite` PASS、`check-arch` 綠。

**⚠️ 高危提醒**：這是承重 cook-core，且正是另一 session 在動的檔。**開工閘四條（見上表）一條都不能跳。** 不滿足就不碰。

---

## 工作 3：決定性 export 島 — 誕生即帶閘

**動機**：你未來做決定性離線 render 會生出一座新島（「離線固定 dt 決定性」）。**別讓它裸著誕生。**

**好消息：這座島天生可測。** 決定性的定義就是「同輸入跑兩次，輸出 byte 完全一樣」——閘現成。

**閘設計（path-2 的誕生條件，不是事後補）**：
- `--selftest-deterministic-render`：固定 dt 從 t=0 跑 N 幀**兩次**，比對輸出 buffer → 必須 byte-match。
- `-bug` 咬合：故意注入一個 wall-clock 依賴（讓 sim 讀真實時鐘而非注入的固定 dt）→ 兩次結果不一致 → 紅燈。

**歸屬**：本項是 [POST_PARITY_MV_TOOLING_PLAN](POST_PARITY_MV_TOOLING_PLAN.md) **C 塊路 2** 的驗收條件，已在該檔 C 塊註記。做 path-2 時這個 selftest 跟 dt 解耦同批落地，不另開工單。

---

## 附錄：7 座本質複雜島 → 守門 selftest（工作 1 的初始 SSOT 表）

新島落地時在此加一列。工作 1 的 census 以此表為輸入。

| # | 島 | 核心檔（grep 符號） | 守門 selftest |
|---|---|---|---|
| 1 | Packed-float3 / struct 對齊 | `tixl_point.h`（`SwPoint` 64B static_assert）/ `sw_mesh.h` | `pointlist_golden.cpp`（packed_float3 stride 證明）+ field golden |
| 2 | 粒子循環緩衝 + 生命週期 | `particle_system.{h,cpp}` / `particle_params.h`（grep `cycle` / `particlePoolCount`） | `particle_decay_selftest.cpp`（5 分快轉穩定帶 + `-bug` legacyPolicy） |
| 3 | Transport 雙時鐘 | `transport.{h,cpp}`（grep `fxTime` / `position` / `advance`） | `transport_selftest.cpp` |
| 4 | EvaluationContext 時域轉換 | `eval_context.h` / `frame_cook.h`（grep `simDeltaFromWall`） | `--selftest-arclock`（`frame_cook_selftest.cpp`） |
| 5 | Stateful-value + Context-var 2-pass | `stateful_value_ops.h` / `frame_cook.h`（grep `cookStatefulValueNodes`） | `frame_cook_contextvar_selftest.cpp`（golden A–E）+ `frame_cook_animvalue_selftest.cpp` |
| 6 | Field-into-force 樹蒐集 | `point_graph_cook_ctx.h`（grep `inputFieldTree`）/ `field_graph_builder.{h,cpp}` | `field_graph_builder_selftest` / `resident_eval_graph_selftest` |
| 7 | PointGraph 雙驅 + Command 多輸入 | `point_graph.cpp` / `point_graph_resident.cpp` / `point_graph_internal.h`（grep `CmdCookCtx`） | `--selftest-loop` / `--selftest-camera-scope` / `--selftest-setvar-scope` / `--selftest-layercompose` |
