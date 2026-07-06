# FABLE_VERIFY_WORKORDER — 原子節點驗收先後順序(2026-07-06)

> 給 Fable 的驗收工單。**動手前先讀 `docs/agent/GOLDEN_STANDARD.md`(三特徵＋五反型)。**
> 這份工單只排「從哪開始、為什麼這個順序」;每顆節點怎麼驗，判準在 GOLDEN_STANDARD。

---

## 0. 唯一鐵律(從模型外面釘上的牆)

**每個期望值都必須指向一個 sw 以外的真相**：TiXL 的 `.cs`/`.hlsl` 行號，或閉式公式/幾何不變量。
**禁止**從 sw 自己的實作、helper、或同一顆 kernel 的另一次 dispatch 回推期望值（=P5 自洽假綠，你會綠、但驗了個寂寞）。

- 期望值旁註明來源：`external/tixl/....cs:NN` / `.hlsl:NN` / `.t3` 欄位，或「幾何不變量：<公式>」。
- injectBug 腐蝕**真 cook 路徑**（configure*/shared 旗標），不是翻期望值（=P3 want-flip）。
- did-not-trip → `return 0`；`tools/golden_lint.sh` 綠；`--bite` 該顆有咬。

**回報格式**（每顆一行，不要長敘述）：
`<檔名> | 判決:乾淨/帶病(P1..P5) | 錨:<TiXL 行號或公式> | 若帶病:<行號+怎麼改>`

---

## 1. 兩軌，別混

- **A 軌 · 原子葉子數學** — 驗「這顆公式對不對」。期望值 = TiXL 源碼 / 閉式。**本工單主體。**
- **B 軌 · 組合縫** — 驗「importer 把原子合併成複合時，elide/對位/drop/cook 對不對」。
  **原子全對 ≠ 這軌對**（血證：`t3import_bubblezoom` 的 `[fork-scalar-drop-benign]`——一條 wire 被丟、
  靠「port 預設 byte-equal 邊界值」撐；預設若不等，複合靜默錯而原子完美）。**Fable 不准把「原子做完」當這軌做完。**

---

## 2. A 軌驗收順序（原子）

### Tier 0 — 校準（先證明管線有牙，才准放它咬）
先跑 3 顆，其中 **1 顆由 orchestrator 種一個已知 bug**。看管線抓不抓得到種進去的錯。
抓不到 = 管線無牙，停，先修管線。
- `gradient_golden.cpp` — 已錨、GOLDEN_STANDARD 的校準範本，當**已知良品對照**。
- `field_ops_boxsdf_golden.cpp` — 幾何閉式，快，當**乾淨對照**。
- **〈orchestrator 種毒的那顆〉** — 證明紅得起來。

### Tier 1 — 承重葉子（blast radius 最大，錯了整片複合繼承）
value/list producer 看起來「簡單」，但每顆複合都踩在上面。**最高優先，別因為它短就跳。**
- `runtime/value_op_animvec3_golden.cpp`
- `runtime/value_op_perlinnoise3_golden.cpp`
- `pointlist_golden.cpp` / `composevec3fromlist_golden.cpp`
- `floatlist_golden.cpp` / `floatlist_animfloatlist_golden.cpp` / `floatlist_smoothvalues_golden.cpp`
- `colorlist_golden.cpp` / `colorlist_fanout_golden.cpp`

### Tier 2 — 有狀態 / 跨 frame（done≠verified 的危險區；原子孤立測結構性看不見）
「單顆對、連起來錯」住這裡。既有覆蓋多半是 aggregate/smoke。
- `particle_sim_integrate_parity_golden.cpp` — 自承 parity，**核對它是不是真的**（取樣有沒有離開恆等）。
- `audio_playback_golden.cpp` — 無錨，補 TiXL 錨或標 smoke。
- `afterglow_golden.cpp` / `afterglow2_golden.cpp` / `advancedfeedback_golden.cpp` — feedback 累積器；**有錨**，是**重審**不是重推：確認取樣在發散中段、injectBug 咬真 ring。
- **跳過** `particlefield_probe_golden.cpp` — 檔頭自承「NOT a deliverable，故意 RED」，別誤判成 bug。

### Tier 3 — 簡單值/字串/貼圖 op（無錨但低風險，批次快掃）
`buildrandomstring` / `string_builder` / `string_ops_blendstrings` / `valuestotexture` / `valuetorate` /
`swaptextures` / `loadsvgastexture2d` / `drawlinegrid` / `drawspheregizmo` / `connect_cooks` /
`selectpointswithsdf` / `readpixel` / `texttopoints`(CoreText 破例路線，注意它是 TiXL-absent，錨是閉式不是 TiXL)。

### Tier 4 — 幾何 SDF（多半是合法閉式幾何不變量 = 假陽性）
`absolutesdf/blendsdfwithsdf/boxframesdf/cappedtorussdf/capsulelinesdf/chainlinksdf/customsdf/cylindersdf/
invertsdf/planesdf/prismsdf/pushpullsdf/torussdf/staircombinesdf/repeataxis/repeatfield3/repeatfieldatpoints/
repeatpolar/transformfield/translate/translateuv/reflectfield/customsdf`(~24 顆)。
**不要逐顆花 Fable。** 抽 2-3 顆確認「幾何不變量」這個錨成立即可；成立就整批放過，回報「抽樣 N 顆，錨=幾何，其餘同型放過」。
**（明講：這裡是抽樣不是全覆蓋——不要讓綠燈假裝掃完了。）**

---

## 3. B 軌驗收順序（組合縫 — 原子做完後才開，但別以為免費）

這軌驗 importer 的 elide/對位/drop/cook。我的 grep 把它們誤標成「無錨」，peek 後確認**它們是閉式錨**
（`BubbleZoom.hlsl` gradient oracle），但它們驗的是**合併層**，不是葉子數學。

- `runtime/t3import_*_golden.cpp`（12 顆：blend/boxgradient/bubblezoom/combinebuffers/hse/lineargradient/
  ngongradient/radialgradient/remapcolor/transformmesh/transformpoints）
  — 重點看每顆的 **`[fork-*-drop-benign]`**：被丟的 wire 靠「預設 byte-equal」撐，逐顆確認那個相等**真的成立**。
  — `bubblezoom` 有血（記憶 `replay-golden-pins-must-sample-diverging-middle`：曾坐端點漏掉中段）；取樣要掃中段。
  — `radialgradient` / `remapcolor` 檔頭自承 smoke，優先。
- `runtime/point_ops_renderstate_golden.cpp` — flat vs resident **byte-identical** 雙腿；確認雙腿真的都跑到。
- `runtime/resident_mixed_multiinput_golden.cpp` — **★記憶標「碼修閘沒補、回歸無防護」**，最高疑點，先驗。
- `runtime/point_ops_camera_resident/camera_scope/group/draw_explicit/inputassembler` — context/scope stack。

---

## 4. 查 Fable 的查（閉環，別漏）

Fable 自驗仍是自己批改考卷。便宜層（orchestrator/haiku）做兩件機械事：
1. **引用審計** — 去 `external/tixl` 那個行號，確認那行真寫著 Fable 宣稱的公式（抓「引了 sw 不是 TiXL」「行號沒那句」）。
2. **咬合確認** — `golden_lint.sh` + `--bite` 綠。

承重節點（particle/render/multiinput）再加一層：**第二個 Fable、乾淨 context、盲推同一組 TiXL 行號**，
兩次期望值必須吻合；不吻合 → 上柏為的桌。（貴，只留給下游一堆東西壓在上面的。）
