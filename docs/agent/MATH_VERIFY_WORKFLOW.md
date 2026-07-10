# MATH_VERIFY_WORKFLOW（mathv）— 數學驗證五關施工藍圖

> 柏為 2026-07-09 17:14 令：無 Windows TiXL 對照下，確保全部節點底層數學完全正確，之後所有節點生產（143 shader 複合退場 + 137 未做 shader + 129 未做 CPU）拿它過閘。範圍鐵律＝**302 顆 shader 強驗、600 顆純 CPU 走普通 golden、增量過閘非 blanket 重驗**（清點帳見 memory [[math-verify-workflow-2026-07-09]]）。Plan agent 對碼產出 2026-07-09。

## 0. 現有基建盤點（設計全站在這些之上，不發明平行系統）

| 既有物 | 位置 | 對 mathv 的意義 |
|---|---|---|
| CPU 純量 oracle 先例（已存在兩份） | `app/src/tixl_noise_oracle.h`（檔頭明寫 "TRANSCRIBED from TiXL … NOT copied from sw's shaders"）、`app/src/runtime/t3import_displacemeshnoise_oracle.h` | 關1 非新發明＝把已驗形式**制度化+量產**。float-not-double 紀律（simplex cell 邊界）已寫死檔頭，直接繼承 |
| GPU direct-kernel dispatch + oracle 逐點比對 | `turbulence_parity_golden.cpp:75-112`（dispatchTurb）+ `:204-256`（逐 particle vs `tixl_noise::curlNoise`，1e-3 gate + ≥90% fraction，容差有實測校準註記） | 關2 fuzz 的 GPU 餵法＝這個 shape，不走 buildEvalGraph |
| RAII Metal 骨架 | `parity_golden_harness.h`（header-only 零 CMake） | mathv harness 沿用此模式 |
| selftest 自註冊 | `app/src/runtime/selftest_registry.h` + CMake `file(GLOB "src/selftests*.cpp")`（`app/CMakeLists.txt:218-220`） | 命名對→**零 CMake 改動**掛新牙 |
| --bite 體系 | `tools/run_all_selftests.sh` + `tools/golden_lint.sh`（P1 硬閘/P3 軟篩） | mathv 牙自動收編 |
| 參數 ABI 接縫 | `<op>_params.h`（host/MSL 共用 struct + binding enum） | fuzz driver 與 CPU ref 的**唯一**合法共享物（名字/offset，非數學） |
| 參數範圍來源 | `.t3` DefaultValue；`.t3ui` Min/Max/Scale；`--dump-nodespec` | fuzz 輸入域 |
| Metal 編譯 | `xcrun metal` 無 `-fno-fast-math`（CMakeLists:83-88）→ **fast-math ON** | epsilon 與陷阱清單的物理前提 |
| 行數閘 | `check_arch.sh` Pass 2 ≤400 行 | 拆檔規格 §1.4 |

## 1. 基建落點與形式

### 1.1 檔案佈局（全 shell tier＝`app/src/` 根，比照 golden 慣例；不進 verify/）
```
app/src/mathv_harness.h          共用骨架 header-only（RAII 沿 ParityHarness + fuzz 驅動迴圈）
app/src/mathv_input.h            輸入生成（決定性 PRNG + 特殊值格 + 域描述 struct）
app/src/mathv_compare.h          比對器（abs+rel 混合 / fraction gate / NaN-Inf-denormal 語義 / 分場報表）
app/src/mathv_ref_<op>.h         關1 CPU 純量參考（每 op 一檔，agent-R 寫）
app/src/selftests_mathv_<op>.cpp 關2 每 op fuzz TU（agent-D 寫）：域+dispatch adapter+REGISTER_SELFTESTS
```
- `selftests_mathv_*.cpp` 命名騎上 `file(GLOB "src/selftests*.cpp")` → 加一顆 op＝加兩檔 + reconfigure，CMake 零編輯。
- header-only harness/ref 不進 build 清單。
- 不建 `app/src/mathv/` 子目錄：`check_arch.sh` Pass 1 只認五個具名 zone 目錄，新目錄落依賴稽核盲區；root shell tier 已有 ~150 顆 golden 先例。

### 1.2 selftest 形式：每顆一牙（`--selftest-mathv-<op>` / `-bug`），不做總牙
(a) NO-BITE 名單以牙為粒度——總牙揉掉死牙可見性 (b) 增量過閘只跑該顆 (c) registry order key 天然支援批次追加。

### 1.3 GPU 餵法：direct-kernel dispatch，不走 buildEvalGraph
- point/particle 類：照 dispatchTurb——host 填 MTLBuffer → setBytes params → dispatch → readback。每 TU 自帶 ~30 行 adapter lambda（binding 引 `<op>_params.h`）。
- image 類：host 生成輸入 texture → dispatch → readback（`readpixel_golden.cpp` 先例）。**8-bit format 比對域＝CPU ref float → 量化 ±1 LSB**。
- 退場複合 kernel（`computeshaderstage_<op>.metal`）：直接 dispatch，const buffer 由 TU 手工組裝（模擬 FloatsToBuffer/IntsToBuffer 排布，cb 佈局引 TiXL .hlsl cbuffer 宣告行號）。**cook 鏈路正確性不歸 mathv 管，歸退場②閘**（§8）。

### 1.4 行數閘拆檔
每 TU 100-250 行天然 <400；大依賴閉包（noise/quat）獨立成共享 oracle header 一份多 op 引用（`tixl_noise_oracle.h` 首例；未來 `mathv_ref_shared_quat.h`），每份 <400。

### 1.5 injectBug（P1-safe 咬合）
`-bug` 腿在**送 GPU 的 params 上**注入微擾（第一個 float +1e-2），CPU ref 收原值 → 必然分岔 → 比對器必翻紅才算牙活。腐蝕真 dispatch 路徑非 want-flip；did-not-trip `return 0`。擾動參數必須選輸出**連續依賴**的（勿選僅經離散分支影響輸出的參數，如 wrap 的 Center——會咬合 margin 趨零）。

**已知限制（X flag，跨 op 慣例級，2026-07-10，harness backlog，非阻斷）：`-bug` 腿目前只咬 PRIMARY 牙。**
每個 Tier-H 直接 dispatch op（`selftests_mathv_<op>.cpp` 手刻多顆 tooth 的形狀，如 BlendPoints/
SnapPointsToGrid/ReorientLinePoints）的 `runMathv<Op>SelfTest(bool injectBug)` 入口都是
`if (injectBug) return mathvVerdictToExit(checkPrimaryTooth(disp, true, ...), true, opName);`——
**只重跑 PRIMARY/main tooth**，其餘牙（identity sentinel、quirk probes、branch-coverage 等）在
injectBug=true 下完全不執行。若那些牙自己的比對邏輯壞掉（例如 identity tooth 的容差、quirk probe 的
gate 判斷），`--bite` 咬不到——因為它們從未在擾動路徑下跑過一次。（唯一例外：走
`mathv_harness.h::runMathvFuzz` 通用 3 層 harness 的 op，因為 `gpuParams()` 擾動包在 `runCompare`/
`runSmoke`/identity 迴圈的每一次 GPU 呼叫外層，所以 identity sentinel 也連帶被咬——通用 harness 沒有
這個洞，只有手刻多牙 TU 有。）**建議修法**：新 op 的 D 工單應對每根牙（不只 PRIMARY）都做一次
injectBug 敏感性檢查——或至少每根手刻 tooth 自己判斷是否值得被 -bug 咬（有些牙如 quirk probe 本身就是
釘死已知分岔的 pin，不見得需要二次咬合）。既有 op 不強制回頭補（backlog 級，不阻斷量產）。

## 2. epsilon 策略（每顆 `MathvCase::EpsSpec`，調整必附實測推導註記——turbulence :227-233 是範本）

1. **exact 類**（加減乘/mod/select/swizzle/仿射）：`|a−b| ≤ atol + rtol·max(|a|,|b|)`，atol=1e-6、rtol=1e-5，100% 樣本過。
2. **transcendental 類**（sin/cos/exp/pow/noise/normalize/rsqrt）：rtol=1e-3；**雙層 fraction gate**：≥99% 過緊容差 + 100% 過寬容差(10×)。
   - **2b. transcendental-wrapping-branchy 次類**（noise 類 op 的 lookup 座標 ill-conditioned；AddNoise 為首例，mathv 工單2 fixer S-verdict 2026-07-10）：wide-gate 仍 miss 時，五條判準缺一不可才豁免——① TU 的 `branchDist` 回呼（與 Branchy 同一通道，`mathv_compare.h` `Comparator::add`）對該樣本回傳 `>=0`（TU 判定該 transcendental 的 lookup 座標 ill-conditioned：座標量級大到自身 ulp ≈ 該函式的 cell/週期尺度，如 simplex noise cell 寬度 O(1)，fast-math 重結合因而可合法落到不同 cell），`<0`=不適用 ② 兩側已分類 Finite（NaN/Inf 已在更早處理，豁免不吃 class mismatch）③ TU 的 `envelopeOk(gpu,ref)` 回呼（從該樣本的 P/in 閉包出的輸出量級包絡，Comparator 本身看不到 P）為真 ④ 全域豁免率 ≤ `exemptMax`（同 Branchy 用的 1% 欄位），超帽即 RED（`verdict()` 內查核）。判準與落地見 `mathv_compare.h` 檔頭 + `app/src/selftests_mathv_addnoise.cpp`；`envelopeOk` 缺省 nullptr＝整條路徑死碼，既有 TU 行為不變。**此豁免通道同時適用 Transcendental 與 Branchy 兩類**（orchestrator 裁決 2026-07-10，SnapPointsToGrid pilot #3 殘尾：巨量級參數把 CPU/GPU ULP 級排程差放大過緊 gate 而遠離任何分支邊界——五判準原樣，Branchy 的豁免計數與 knife-edge 計數共享同一 `exemptMax` 1% 帽；落地見 `app/src/selftests_mathv_snappointstogrid.cpp`）。
3. **branchy 類**（floor/step/simplex cell/閾值）：mismatch 印「距最近分支邊界距離」，僅 `dist < δ_branch`（~1e-4）豁免且豁免率 ≤1%。CPU ref 用 **float 不用 double**。

特殊值語義（`mathv_compare.h` 全域一致）：NaN 兩邊皆 NaN=match（class 不比 payload；fast-math 下 NaN 輸入 probe 降級為「不 crash+可分類」，除非 HLSL 顯式處理如 Particle.BirthTime sentinel）；±Inf 符號一致；±0 相等；denormal 兩側 FTZ 歸 zero-class 再比（輸入仍生成 denormal 考下游分岔）。

## 3. 輸入生成（`mathv_input.h`）

- **決定性種子**：`seed = splitmix64(fnv1a(opName))`，每次列印；`SW_MATHV_SEED` 環境變數可覆寫（refuter 換種子攻擊）。
- **三層取樣**（防端點空心 [[replay-golden-pins-must-sample-diverging-middle]]）：
  1. **特殊值格**：每標量輪流放 {0, ±1e-4, ±0.5, ±1, ±邊界, ±1e6, denormal, NaN, ±Inf}，其餘參數固定**非恆等中段值**。
  2. **域內均勻隨機**：範圍取 `.t3ui` Min/Max（**authoring 時 agent-D 讀 external/tixl 烘成常數寫進 TU 並引檔**——binary 不 runtime 依賴 external）；無 Min/Max 用 DefaultValue 鄰域 ×[−4,+4]。預設 4096 輸入元素 × 8 組參數向量。
  3. **恆等哨兵**：每 op ≥1 組已知恆等參數（Amount=0/Scale=1）——期望輸出==輸入，考「參數真的接進 kernel」。
- **中段活性自檢**（機械防空心）：隨機層 GPU 輸出對輸入逐元素方差 >0 且非恆等樣本 ≥90%，否則紅。
- 參數空間＝TU 內 `ParamDomain` 表 `{name, lo, hi, kind(linear/log/enum/int), 來源引註}`；enum 逐值全掃。

## 4. Agent 編制與 token 節奏

### 4.1 一顆過閘隊形（鐵律：關1 作者 ≠ MSL 作者/關2 作者）

| 角色 | 做什麼 | 模型 | 隔離紀律 |
|---|---|---|---|
| **R（ref 作者）** | 讀 TiXL .hlsl/.cs **only** → 寫 `mathv_ref_<op>.h`，逐函式引 `:NN` | Sonnet（noise/quat/矩陣重用 Opus） | **禁開 `app/shaders/`**；產物零 metal include/零 sw math helper（lint 機械查） |
| **D（driver 作者）** | 寫 `selftests_mathv_<op>.cpp`（域表+adapter+eps），跑到綠 | Sonnet | 可讀 .metal（要 binding），**不可改 ref**；eps 放寬必附實測推導 |
| **S（語義稽核）** | HLSL vs MSL 逐 op 並排 + 關5 陷阱逐項答 + 稽核 ref 轉錄忠實度 | **最強模型** | verdict（命中/不適用+證據行號）記 TU 檔頭 + ledger |
| **X（refuter）** | 構造分岔輸入（換 seed/攻分支邊界/手推 2-3 樣本攻 ref 忠實度）+ GOLDEN_STANDARD 出廠 checklist | Opus | 可追加 adversarial probe；verdict 未清不入主線 |

R 與 D **不同 session/worktree**（防同錯互證）；S、X 各自獨立。歷史 .metal 作者無法回溯排除——以「R 產物零 metal 痕跡 + S 獨立轉錄稽核」補償。

### 4.2 token 帳
簡單 op ≈180k/顆；重 op（noise/矩陣/quat）≈300-400k/顆；302 顆 ≈60-100M，**攤進退場與新建工單不獨立成戰役**：143 退場顆併退場工單（先 mathv 後四閘）；137 未 port 顆併 port 工單（port 本要讀 HLSL，R 成本均攤，但 R≠port 作者）；7 真原子 + 已 port 136 未退場殘量＝低優先 backfill（每波 5-8 顆按數學風險排序 noise/quat/矩陣>純仿射）；15 非NodeSpec 等 spec 化。

### 4.3 量產隊形修訂（2026-07-10 柏為令 token/速度檢討；兩 pilot 實測 690k/1.18M 遠超 4.2 估算）
1. **鏈式 worktree 復用**：R→D→fixer 序列**共用一個 worktree**（R≠D 隔離＝不同 session 作者、非檔案系統；P5 判準③照樣可稽）。X 保留獨立 worktree（要 rot 演練）。**S 唯讀化只適用 Tier-H**（那裡 X 另有 worktree 實測補位）；**Tier-L 的 XS 合併位必須保留 worktree+build**——pilot #3 實證：三個承重結論（AGB 反證探針/Metal saturate(NaN)=0 硬體實測/liveness 跨 seed 量化）全靠親手 build+注 bug 拿到，唯讀只能 pattern-猜＝pilot #2 誤診坑。每顆 5 個 setup+build → 2 個（Tier-L：R-D-fixer 鏈 1 + XS 1）。
2. **--bite 範圍化**：agent 只跑 `--selftest-mathv-<op>`(+`-bug`) + `--selftest-mathv-core`；全量 sweep 只在 orchestrator 合流時跑。（附帶根治「kick 背景 sweep 卡死」：agent 無長 sweep 可跑。工單模板頂仍放硬警語：**所有命令一律前景跑到底**。）
3. **按難度分流**：**Tier-L**（exact/簡單 kernel：HLSL <60 行且無超越函式/無多分支）＝R(Sonnet)→D(Sonnet) 共用 worktree + **XS 合併位**（一個 Opus 兼語義稽核+對抗）＝3 agents。**Tier-H**（noise/quat/矩陣/多分支/transcendental-wrapping-branchy）＝全 R/D/S(Fable)/X＝5 agents。
4. **D 儀器化紀律（硬性入工單）**：miss 歸因**必須 batch-tag 儀器化**（取樣層×參數×值 分佈表），不准從 pattern 猜（pilot#2 denormal 誤診＝S+X 各花一輪推翻的教訓）。
5. **批次合流**：一條 op 鏈全完成後 orchestrator 一次合流+一次全量驗，不逐棒。
6. **報告瘦身**：回報限結構化表格+關鍵 measured 行。
修訂後估：Tier-L ~250-350k/顆、Tier-H ~500-600k/顆 → 302 顆 ≈80-130M（vs 未修訂 ~270M 軌跡）。**pilot #3 SnapPointsToGrid 用 Tier-L 隊形當試金石**，實測省幅後定稿入工單4。

## 5. 與 GOLDEN_STANDARD 銜接

**CPU ref 非新 P5**（出身=TiXL HLSL 直譯=外部 oracle，與 `tixl_noise_oracle.h` 同宗），但判準寫死。**P5-safe oracle 判準**（寫進 GOLDEN_STANDARD 新節），合格 iff：① 檔頭 `TRANSCRIBED from external/tixl …` + 逐函式 `:NN` 引註 ② 全檔零 metal include/零 shaders 引用/零 sw math helper ③ R≠D/MSL 作者（工單可稽）④ refuter 手推樣本 spot-check 過。

**golden_lint.sh 新規則**：硬閘＝`mathv_ref_*.h`（含 `*_oracle.h`）grep 到 shaders include/metal 字樣→fail；檔頭缺 TRANSCRIBED+external/tixl→fail；P1 掃描 glob 擴到 `selftests_mathv_*.cpp`。軟篩＝eps 超類別預設而無 `measured` 註記→SUSPECT。

**mathv 綠不豁免常規 golden**——mathv 驗 kernel 函數，常規 golden 驗 cook 佈線與 NodeSpec 預設（turbulence TOOTH 1a vs 1b 分工先例）。

## 6. pilot：WrapPointPosition → AddNoise → SnapPointsToGrid（三顆各代表一個 eps 類別）

1. **WrapPointPosition**（exact）：kernel 極小；自帶翻譯陷阱範本 floored-mod vs truncated-fmod（`wrappoints.metal:14-21` NAMED FORK＝關3 verdict 形式的原型）；負座標=發散中段。
2. **AddNoise**（transcendental）：考 fraction gate+分支豁免；`tixl_noise_oracle.h` 現成共享；現行 golden 是 P4 級 smoke（`point_ops_addnoise.cpp:99-106`）＝mathv 落地即實質強化樣板。
3. **SnapPointsToGrid**（branchy）：rounding/select 密集，考分支邊界豁免。

pilot 出廠＝三顆五關全綠 + --bite 咬 + lint 新規則綠 + **故意壞 ref、故意壞 kernel 各演練一次**（證 §7 兩條路由走通）。

## 7. 失敗路由（mismatch 證據包＝輸入向量/兩側輸出/abs-rel 誤差/距分支邊界距離/seed）

```
fuzz 紅
 ├─ dist<δ_branch 且豁免率<1% → 分支邊界 fp 抖動 → 自動豁免
 ├─ X 手推樣本（獨立於 ref 與 kernel）
 │   ├─ 手推 ≠ ref        → 【ref 錯】回 R 修（附 S 稽核註記）→ fuzz 重跑
 │   ├─ 手推 == ref ≠ GPU → 【MSL 錯】開 production shader 修單（S 的並排 verdict＝修單線索）
 │   ├─ 手推 == GPU、誤差在 fast-math 物理範圍 → 【eps 太緊】D 重校+實測推導註記
 │   └─ HLSL 本體歧義（UB/死碼/DX intrinsic） → 【TiXL 歧義】升柏為拍板→AMBIGUITY 表→比對器 pin 該語義
 └─ 每紅一行進 mathv ledger（op/關/分類/路由/結案）
```
關3 紅＝產 MSL 或 ref 修單；關4 紅＝打回作者；關5 命中未處理＝擋出廠。

## 8. 與退場四閘（RETIREMENT_BATTLE_SPEC §5）：疊加不取代

- **②parity 閘保留但縮焦**＝驗**佈線鏈**（import 映射/boundary 注入/FloatsToBuffer 組裝/MultiInput 順序/stride 64-80）——mathv direct-dispatch 摸不到這些。
- **mathv 驗 kernel 函數本身**（數千組+邊角 vs ②少數 probe 點）。mathv 是②的 **oracle 供應商+前置閘**——②期望值可直接呼叫已過關 `mathv_ref_<op>.h`，不必每顆重手推。
- **退場一顆完整順序**：mathv 五關綠 → ①接管 → ②parity(佈線焦點) → ③引用 → ④排版。mathv＝**R6：kernel 數學已驗**，掛進 ready-set（RETIREMENT_BATTLE_SPEC §2 加一行）。
- 137 新 port 顆：mathv＝port 出廠閘（kernel 落地→mathv 過→才算 ported 餵 R4）。
- 129 未做 CPU/600 純 CPU：**不進 mathv**，普通 golden 足夠（CPU op 的 evaluate 本身就是純量函式，無第二實作可打）。

## 9. 關5 已知陷阱清單（初版，S agent 逐項核；來源＝本倉已實證 fork）

mul(m,v) 順序/row-vs-col-major（TransformPoints host 矩陣 v·M）｜Euler 順序 yaw/pitch/roll（cfypr 已證）｜floored vs truncated mod（wrappoints NAMED FORK）｜saturate/clamp NaN fast-math 行為｜整數除法截斷與 uint wrap｜HLSL 隱式截斷 vs MSL 顯式｜float3 packing/SW_PACKED3 對齊｜denormal FTZ｜fma 收縮改變捨入｜step/sign 在 0｜normalize(0)/rsqrt 精度｜quaternion 乘法約定（qMulD）｜lerp 參數順序｜frac/pow(neg)/atan2(0,0)｜asuint/asfloat 位技巧｜texture wrap/sRGB｜dispatch 邊界 `idx>=Count` 守衛｜stride 64(SwPoint)/80(SwVertex)｜NaN sentinel 依賴（Particle.BirthTime）｜Metal fast-math 可能把 `isnan()` 摺疊成恆 false——每顆帶 isnan 的 kernel 必須實測 NaN probe 活性（本倉 WrapPointPosition 實測 LIVE）｜transcendental 類 noise-lookup 座標 ill-conditioned（座標量級 ulp≈函式 cell/週期尺度，fast-math 重結合合法落不同 cell；判準見 §2 2b，AddNoise 首例：Frequency=±1e6 特殊值格觸發，denormal 格實測 0 貢獻，S 2026-07-10 拔格實驗 + X bisect 獨立確認）。

## Critical Files
- `app/src/turbulence_parity_golden.cpp`（direct-dispatch + CPU-oracle 逐點比對 + 實測校準容差母版 :75-112/:204-256）
- `app/src/tixl_noise_oracle.h`（合格 CPU ref 的 provenance/float 紀律範本）
- `app/src/runtime/selftest_registry.h`（牙自註冊接縫）
- `tools/golden_lint.sh`（新兩條規則落點）
- `app/src/parity_golden_harness.h`（header-only harness 形式先例）

## 工單切法
1. **工單0（基建）**：mathv 三 header + golden_lint 兩新規則 + GOLDEN_STANDARD 補 P5-safe oracle 判準節 + 比對器自測牙 `--selftest-mathv-core`。無 op 綁定可獨立驗。
2. **工單1（pilot WrapPointPosition）**：R/D/S/X 四角色分 session 五關走完 + 故意壞 ref/故意壞 kernel 兩演練＝驗 workflow 本身。
3. **工單2（pilot AddNoise）**：驗 transcendental fraction gate + 共享 oracle 復用。
4. **工單3（pilot SnapPointsToGrid）**：驗 branchy 豁免。
5. **工單4（制度化）**：RETIREMENT_BATTLE_SPEC §2 加 R6 + /sw-node-batch refuter 波掛 X 角色 checklist。此後 mathv 隨退場/port 工單增量走，不另開戰役。
