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

**R 工單模板必含（TransformFromClipSpace 教訓，2026-07-10）**：kernel 吃 **host 端預處理資料**（矩陣上傳、pre-transpose、量化…）時，cbuffer 的真實語義可能定義在 **TiXL C# host 層**而非 HLSL 檔面——且 host 效果可能被 HLSL packing 慣例（如預設 column-major，DX11ShaderCompiler.cs ShaderFlags.None）**抵銷或反轉**。R 的隔離規則讓它看不到 C#，所以 R 遇此類輸入**必須標 AMBIGUITY 留給 S 稽核 C# 端**，嚴禁自行假設「檔面轉錄即淨語義」。實證：TFCS 的 host pre-transpose 被 column-major packing 抵銷（HLSL 可見 M≡邏輯 N），D 誤診 ref 錯→fixer 反修→S 挖三層才翻正——kernel rotation 共軛 bug 曾活在 production。旁證鏈可用：同 cbuffer 的其他消費者 shader（如 WrapPointPosition.hlsl `_m30_m31_m32` 用法）+ 姊妹 op 慣例 + kernel 內部同矩陣兩用法互證。

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

## 10. Transpiler 量產工作流（kernel-port 量產版，取代 137 顆未 port kernel 的手刻步驟）

**背景**：137 顆未 port 的 shader body（見頂部帳）過去要靠人手把 TiXL HLSL 逐行翻成 MSL——這是每顆 kernel-port 最貴的步驟，也是唯一沒有機械化的一環（R 寫 CPU ref / D 寫 fuzz driver 都已有 §1.1 的固定形式）。試金石 branch `worktree-agent-ae12359c71deefe72`（commits `0277899`/`07c0c3f`/`2523a07`，2026-07-10）證明**這一步可以完全機械化**：轉換工具鏈把 HLSL body 逐行搬成 MSL，人手只補一份 ~4-12 行的 ABI 轉接層。本節把那個實驗結果制度化成量產 SOP。

### 10.1 工具鏈＋版本＋一行配方

```
glslang（16.3.0，brew 現成）：HLSL → SPIR-V
  glslang -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 \
          -e <EntryName> -S comp -V --target-env vulkan1.0 \
          -I external/tixl/Operators/Lib/Assets/shaders \
          external/tixl/.../<Op>.hlsl -o <op>.spv

spirv-cross（1.4.350.1，brew 現成）：SPIR-V → MSL
  spirv-cross --msl --msl-version 20000 <op>.spv --output <op>_raw.metal
```
兩個工具都是 `brew install glslang spirv-cross` 現成二進位，無需編譯、無需 vendor；DXC（DirectX Shader Compiler）路線曾評估但撞版本/平台死路，全倉一律走 glslang+spirv-cross。`--hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24` 四個 shift flag 是 SSOT——固定不變，換 op 只換輸入檔名；`-e main`（TiXL 大多數 compute shader 的 entry 名）**不是恆定**，撞到非 `main` entry（如 `sort-1-CleanBucketCounter.hlsl` 的 `ClearBucketCounter`）要照實改 `-e` 值（見 §10.5 限制②）。兩步驟零 CMake 整合——都在轉換階段的暫存檔跑完，落地的只有最終 `.metal`（走 `app/shaders/*.metal` 既有 glob，零編輯）。

### 10.2 每顆流程（轉換→adapter→selftest 閘→分流）

```
① glslang+spirv-cross 轉換出 <op>_raw.metal（見 §10.1 一行配方）
② 讀 raw 輸出的 buffer 綁定順序（每顆不同，順序=HLSL 宣告序，SRV/UAV/CB 各自從 0 起算，
   spirv-cross 用 t#/u#/b# 對應 [[buffer(N)]] 的映射規則不是全域固定值，逐顆讀輸出核對）
③ 寫 adapter（手動的唯一部分，實測 1-12 行）：
   - entry 改名：main0 -> <kernel-name>
   - [[buffer(N)]] 字面數字換成具名 binding enum（純識別字重寫，不動運算式）
   - 若 HLSL 呼叫過 .GetDimensions()：spirv-cross 會多注入一個 spvBufferSizeConstants
     [[buffer(25)]] 常數陣列（§10.5 限制①）——adapter 把它換成從 host ABI 算出的一行運算式
   - 若原 cbuffer 跨兩個 register（b0/b1）：adapter 重組成 host 送來的單一 flat ABI struct
     視圖（AddNoise 先例：`Params _736 = {AP.Amount, AP.Frequency, ...}`）
④ 產物落地 `app/shaders/<op>.metal`（body 逐行= raw 輸出 verbatim，只有③改動可見）
⑤ 寫 mathv_ref_<op>.h（R 角色不變，仍是獨立讀 HLSL 轉錄，見 §10.4 為何雙路徑不衝突）
⑥ 寫 selftests_mathv_<op>.cpp fuzz driver（D 角色不變，§1.3 direct-dispatch）
⑦ `--selftest-mathv-<op>` 跑到底 → 綠直接收；紅進 §10.3 分流
```
adapter 行數三個實測資料點：AddNoise ~12 行（雙 cbuffer 重組 + GetDimensions 替換）、BRDFLookup 1 行（純 texture kernel，無 CB/SRV/sampler，只需 entry 改名）、本批 SimBlendTo/AppendPoints ~4-6 行（單 cbuffer 已扁平、無 GetDimensions，只需 entry 改名+binding enum 替換）——adapter 成本跟「cbuffer 是否跨暫存器」「是否呼叫 GetDimensions」正相關，不跟 kernel 數學複雜度相關（AddNoise 帶 simplex noise 但 adapter 仍只 12 行；SnapPointsToGrid 數學簡單但 adapter 也要處理同款雙 cbuffer）。**首波量產第四個資料點**：PointSimulation（單 cbuffer、但撞 GetDimensions）adapter ~12 行，跟 AddNoise 同量級——證實「行數跟 GetDimensions/雙cbuffer 相關、不跟數學複雜度相關」這條規則本身也不看 op 是 exact/branchy/transcendental 哪一類（PointSimulation 帶 qSlerp 但 adapter 仍只 12 行，跟不帶任何 transcendental 的 AddNoise 一樣）。

**首波量產追加發現（分流判準第三格的邊界情況，2026-07-10）**：SimBlendTo（exact 類）撞 mathv_input.h 通用 ±1e6 特殊值格時，(posB-posA) 極小差值被放大 ~1e6 倍導致單 ulp 捨入差異炸開成明顯 absErr——這是 §10.3 表格第三格「ill-conditioned-lookup」的同類現象，但**exact 類在 `mathv_compare.h` 沒有 §2 2b 的豁免通道**（`branchDist`/`envelopeOk` 機制只接在 Transcendental/Branchy 的 `case` 分支裡，Exact 的 `case` 直接判死）。PointSimulation（transcendental 類，qSlerp 的 `sin(halfAngle*t)`）撞同一個 ±1e6 格時可以直接掛 §2 2b 既有豁免通道收斂；SimBlendTo 只能改用**同檔案 measured rtol 加寬**（`rtol` 1e-5→1e-4，附實測 derivation 註解，golden_lint `--audit` 的 MATHV-EPS 規則會抓沒附註解的加寬）。**給下一棒的教訓**：exact 類 op 若對某參數做「小差值 × 大權重」的乘法（lerp/mix 的常見形狀），一撞通用 ±1e6 格幾乎必炸，量產前就該預期到，不必當紅燈驚慌——量測後加寬 rtol 收斂即可，不是 kernel 或 ref 有錯。

### 10.3 分流判準（紅的兩種對治，不可混淆）

試金石三數據點釘死判準：
- **AddNoise（5/5 綠）**：sw 現有 kernel 對 raw HLSL **忠實到連陷阱都保留**（RLD=0 的 NaN trap 沒被「修掉」）→ 轉換產物與 sw 現有數學語義本來就相同 → 直接綠。
- **SnapPointsToGrid（紅，`2523a07`）**：`compared=201984 miss=4608`（2.008% > 1% 閘），**miss 100% 落在 sw 三個具名 hardening fork 上**（`safeGridSize` select 擋 div-by-zero、`idx>=Count` 邊界守衛、`ApplyGainAndBias` vec4→scalar 修正）——identity sentinel 768/0 全過，證明轉換本身結構正確，發散只出現在 sw 刻意加的守衛邊界。
- **BRDFLookup（1/1 綠，`5b0ae7d`）**：無 CB/SRV/sampler 的純 texture kernel，逐行核對 HLSL（radicalInverse_VdC 位元反轉/sampleGGX alpha=roughness²/Schlick-GGX IBL k）全數吻合。

判準表：
| 紅的形狀 | 歸因 | 動作 |
|---|---|---|
| miss **全部**落在 sw 已具名的 fork 邊界（`safeGridSize`/`idx>=Count`/已知 vec4→scalar 修正等） | **轉換正確，raw HLSL 本身有 sw 刻意不繼承的瑕疵**（div-by-zero/邊界溢位/上游 bug） | 補 1-3 行守衛到轉換產物（照抄 sw 既有 fork 的做法），**不動 ref、不動轉換配方** |
| miss **散佈**在非 fork 邊界的一般輸入（如隨機層大面積 RED、非邊界值也錯） | 轉換配方本身有問題（binding 錯位/GetDimensions 算錯/矩陣 major 序弄反等）或 ref 轉錄有誤 | 升級：回 §7 失敗路由表（X 手推樣本裁 ref 錯 vs MSL 錯 vs eps 太緊），**不可自行加豁免蓋過去** |
| miss 全部落在 §2 2b 的 ill-conditioned-lookup 範圍 | transcendental/branchy 既有豁免通道 | 走既有 §2 2b 五判準，跟轉換無關 |

**核心規則**：轉換出紅 ≠ 轉換工具有問題。137 顆未 port 顆多數會落在第一格（sw 過去手刻 port 時已經在同樣的位置踩過坑、加過守衛；轉換產物只是「還沒繼承那些守衛」的乾淨版本）——**這是量產的常態，不是異常**。只有第二格才是真正要停下來查的訊號。

### 10.4 adapter 的 P5 地位（雙獨立路徑，不削弱 R≠MSL-author 的稽核紀律）

§4.1 的隔離規則要求「R（ref 作者）≠ MSL 作者」，目的是防止同一個人的理解錯誤同時滲進 ref 和 kernel 兩邊、讓比對兩邊自我印證假綠。transpiler 量產不改這條規則的精神，因為**adapter 不是數學創作**：

- transpiler 產出的 kernel body 是**機械轉換**（glslang/spirv-cross 的確定性編譯輸出），沒有人在裡面做數學判斷——R≠MSL-author 防的是「人的理解錯誤」，機械轉換沒有「人的理解」可錯。
- adapter 本身（§10.2③ 那 1-12 行）只碰 ABI 重排（binding 綁定、cbuffer 扁平化、GetDimensions 替換），不碰任何運算式——lint 可機械稽核「body 逐行 verbatim」這件事（diff raw 輸出 vs 落地檔，非 adapter 註記的行必須逐字相同）。
- R 仍然是**獨立讀 HLSL 轉錄 ref**，全程不看轉換產物（§4.1 隔離不變）——ref 與 kernel 是兩條完全獨立的路徑：一條人手轉錄（R）、一條機械轉換（glslang+spirv-cross），兩者的共同祖先只有 TiXL 原始 HLSL 文字。

**交叉驗證加強而非削弱**：AddNoise 的 rotation tooth 是實證——ref 用 `qFromMatrix3Precise`（R 手推的四元數重建路徑）算出的旋轉，跟轉換產物用 SPIRV-Cross 自動產生的 `transpose(float3x3)` 路徑（完全不同的中間表示法）算出的旋轉，兩條**結構上不相干**的路徑收斂到同一個數值答案（5/5 綠，`07c0c3f`）。若兩條路徑有任一邊藏著理解錯誤，兩條不相干的計算路徑同時錯成同一個答案的機率極低——這比「R 和 D 各自手刻但互相看得到對方在幹嘛」更強的獨立性證據，不是更弱。

### 10.5 限制（撞到就是撞到，不要假裝沒有）

- **① GetDimensions 魔法緩衝**：HLSL 呼叫 `Buffer.GetDimensions()` 時 spirv-cross 會多插入一個 `constant uint* spvBufferSizeConstants [[buffer(25)]]` 常數陣列參數（本批 PointSimulation 實測），kernel body 讀 `spvBufferSizeConstants[N]`（N=該 buffer 的 binding 序）取得 byteSize。adapter 必須決定：要嘛真的多綁一個 buffer(25) 塞入正確的 size 陣列，要嘛（AddNoise/PointSimulation 兩份先例做法）把那行替換成從 host ABI 算出的一行運算式（`uint SourcePoints_1BufferSize = AP.Count * 64u;`）——後者省一個 buffer slot，是本倉慣例。**這行手改必須人核**：GetDimensions 語義是「buffer 的位元組大小」不是「元素數」，替換運算式要對 stride（通常 SwPoint 64B）負責，算錯會讓 bound-guard 悄悄失效。
- **② entry 改名逐顆讀，非固定 `main`**：多數 TiXL compute shader entry 叫 `main`，但不是全部（sort-1-CleanBucketCounter.hlsl 的 entry 叫 `ClearBucketCounter`）——轉換前先 `grep numthreads -A2` 確認真正的 entry 名，`-e` flag 跟著換，不能假設每顆都是 `main`。
- **③ binding 順序不是固定公式，逐顆讀轉換輸出核對**：spirv-cross 把 SRV/UAV/CB 各自從宣告序編號、合流進 `[[buffer(N)]]`，但**合流順序=HLSL 原始宣告順序**（不是「CB 永遠 0 開頭」這種全域規則）——AddNoise 是 buffer(0)=SRV/1=UAV/2=CB，SimBlendTo 是 buffer(0)=UAV/1=SRV/2=CB，AppendPoints 是 buffer(0)=CB/1=UAV/2=SRV/3=SRV2。adapter 寫死 binding enum 前必須看那一顆的實際轉換輸出，不能照抄別顆的順序。
- **④ 雙 cbuffer 需要 adapter 手動重組視圖**：HLSL 用兩個 `register(b0)/register(b1)` cbuffer 時 spirv-cross 各自產生一個 struct（如 AddNoise 的 `Params`/`Params_1`），adapter 要從 host 送來的單一 flat ABI struct 重建這兩個 view（§10.2③ 第三點）——這是 AddNoise 12 行 adapter 裡最貴的部分；本批三顆單 cbuffer op 都不撞這條，adapter 因此壓到個位數行。
- **⑤ 轉換只搬「這顆 kernel 讀什麼、寫什麼」，佈線鏈不歸這步管**：轉換產物只證明 kernel body 的數學語義（配 mathv 的 R6），dispatch bound / FloatsToBuffer 排布 / MultiInput 順序 / 誰把 buffer 接給誰，仍歸退場②parity 閘（§8）——轉換綠不代表這顆已經可以接 stage、上圖。
- **⑥ `StructuredBuffer<int3>`/`RWStructuredBuffer<int3>` → spirv-cross 裸 `int3` 陣列元素，stride 16B vs HLSL 緊排 12B**：raw 輸出把 `StructuredBuffer<int3>` 譯成 `struct { int3 _data[1]; }`——Metal 對「vec3 陣列元素」的 ABI 是每元素捨入到 16 bytes（extended-vector alignment），但 HLSL StructuredBuffer<int3>（對應 TiXL host 端真正的 `Int3`、`sw_mesh.h` 的 `SwTriIndex`）是緊排 12 bytes/元素。裸 `int3` 留著不改＝index 0 巧合對齊、index ≥1 全部讀寫在錯的 byte offset——**不 crash，靜默腐蝕**，只有逐元素比對才抓得到（identity sentinel 常常在 index 0 就過，掩護整條路徑）。修法：兩邊 buffer struct 的 `int3 _data[1]` 全換成 `packed_int3 _data[1]`；讀取端要先 `int3(...)` 轉型再 swizzle（`packed_int3` 不支援 `.zyx` 這類直接 swizzle）；寫入端可靠 `int3 -> packed_int3` 隱式轉換（已驗證可行）。實證＝波次2 `app/shaders/reversefacevertexindexorder.metal`（首例，header 內 EMPIRICALLY PROVEN 段）+ `app/shaders/combineindexbuffers.metal`（同款修法覆用）。
- **⑦ 具名 struct 連續多個 `float3` 欄位，spirv-cross 只自動 pack 最後一個**：多欄位 struct（如 PbrVertex：Position/Normal/Tangent/Bitangent/ColorRGB）若逐欄位都是 `float3`，raw 輸出常只把**最後一個**欄位標成 `packed_float3`，前面幾個留成裸 `float3`——裸 `float3` 在具名 struct 內一樣吃 16-byte vec3 對齊捨入，導致整個 struct 的視覺 stride 被撐大（PbrVertex 真實 stride 80B [`runtime/sw_mesh.h` SwVertex 佐證] 被撐成 112B，spirv-cross 甚至會自動包一層 `spvPaddedArrayElement<PbrVertex,112>` 模板來「圓謊」掩蓋這個撐大的 stride，而不是報錯）。修法：**該 struct 內每一個 `float3` 欄位全換成 `packed_float3`**（不是只修最後一個），stride 收斂回真實值後，`spvPaddedArrayElement` 包裝層與它的 `.data`/指標轉型逐分量寫入 hack 可以整層拿掉。實證＝波次2 `app/shaders/combinevertexbuffers.metal`（header 內 ★TRANSPILER GAP 段，PbrVertex 四欄位全 pack 後 stride 80→112 的撐大消失）。同一類 bug 的一般化描述：**任何 spirv-cross 判斷「這是 vector 陣列元素」的位置都可能漏 pack**——不管是裸陣列（⑥）還是具名 struct 裡的欄位（⑦），出手前都要假設「除了最後一個以外都沒 pack」。
- **方法註記（⑥⑦共用）：懷疑 struct-packing/stride 對不上時，先寫 standalone GPU stride test 證明再改正式檔**——不要在完整 mathv fuzz TU 裡邊猜邊改（GPU dispatch 失敗、CPU ref 錯、struct packing 錯三種紅燈長得很像，混在一起會誤診）。做法：獨立小 Metal kernel，dispatch 寫入一個該 struct 型別的小 buffer（3-4 個元素即可），host 端 readback 逐 byte dump 實際 offset，跟預期緊排 stride 比對——bare 型別在哪個 index 開始偏移、pack 後偏移消失，一次隔離證明。上述兩例（⑥ reversefacevertexindexorder、⑦ combinevertexbuffers）都是先在 mathv worktree scratch 空間跑通這個 stride test 才動正式 `.metal` 檔，不是憑印象加 `packed_`。同方法論延伸到矩陣佈局疑慮（非 struct packing 但同款「先證再改」精神）：波次3 TransformMeshUVs 首次撞矩陣型 op（HLSL `mul(v,M)` 轉譯成 MSL 原生 `v*float4x4`，不像手刻 production kernel 用 flat-array `M·v`）——先代數推導 row-major 上傳應與既有 production 慣例（`computeshaderstage_transformmesh.metal` 的 M·v，translation 在 column 3）等價，再寫一顆 standalone `matrixConventionTooth`（不對稱 swap+平移矩陣，dispatch 真 kernel 直接量輸出座標）實測驗證，一次過——推導與實測一致，此模式在候選5 SelectVertices 複用（同一 `v*float4x4` 轉譯形狀，免重推）。
- **⑧ `switch` 在未轉型的 `float` 運算式上（無 `(int)`/`(uint)` 顯式轉型）→ glslang 報 `case duplicated value` 編譯失敗，非機械可修**：mesh-CollapseVertices.hlsl 的 `switch (VolumeShape)`（VolumeShape 是裸 `float` cbuffer 欄位，case 標籤 `VolumeSphere=0.5` 等也是 `static const float`）撞 glslang HLSL frontend 真實編譯錯誤（`ERROR: case : duplicated value` + 級聯 parse 錯誤），**轉換直接失敗，連 raw MSL 都不產生**——這不是 §10.3 判準表兩格任一種紅（不是「轉換正確、HLSL 有瑕疵」也不是「轉換配方錯」），是工具鏈本身對這個 HLSL 寫法的支援缺口。對照 mesh-Deform.hlsl 的 `switch((int)TwistAxis)`／`switch((int)TaperAxis)`——**同一份 TiXL 原始碼庫裡，作者顯式轉型的 switch 轉換完全正常**，只有這一顆漏轉型撞坑。判斷是否可修：目前沒找到繞過的 glslang flag（SSOT 四個 shift flag 之外不允許額外發明配方，§10.1）；**分流動作 = 跳過換顆，不是卡住重試**——波次3 撞到後直接換 SimPointMeshCollisions.hlsl（候選6），無需為單顆撞坑犧牲量產節奏。若之後真的要收這顆，唯一乾淨路徑是回 TiXL upstream 送一個「cast to int before switch」的原始碼補丁（不在 mathv 對映範圍內，屬另一條線）。
- **⑨ 候選篩選：`grep app/shaders/*.metal` 抓不到的三種「已覆蓋」形狀，選顆前必須逐一排除**（波次4 實證，三次換牌）：(a) **合併多 kernel 檔**——一個 `.metal` 檔可能手刻了同一 `.hlsl` 家族的多顆 entry（如 `pointtrail.metal` 一檔裝 `pointtrail_clear`/`_collect`/`_copy` 三個 kernel），provenance 註解若用 `PointTrail-{Clear,Collect,Copy}.hlsl` 花括號展開格式引註來源，簡單 regex（如 `shaders/[A-Za-z0-9_./-]+\.hlsl`）掃不到——選顆前對候選 `.hlsl` basename 額外跑一次全文 `grep`（不只 provenance 逐行 regex），且要打開懷疑最像的 `.metal` 檔案人眼確認。(b) **cook 層 kernel 別名收斂**：`buffer_ops_executecombinebuffers.cpp` 的 `combineKernelFor()` 這類函式會把兩個文字不同但語意等價的 `.hlsl`（如 `CombineBuffers.hlsl` 與 `CombineBuffersAsInt.hlsl`）都路由到同一顆 `combinebuffers` kernel——`.hlsl` 檔名本身「聽起來」沒 port，但已被 production 刻意收斂，硬 port 第二顆是重複工。(c) **CPU-side 已有等價實作**：`mesh_ops_blendmeshvertices.cpp` 這類 `mesh_ops_*.cpp`/`point_ops_*.cpp` CPU cook 函式，可能已經把某顆 `.hlsl` 的數學搬到 CPU 路徑實作（非 GPU compute kernel），`app/shaders/` 掃描看起來「無對應」但這顆的數學其實已經進production——選顆前對候選 `.hlsl` 的獨特識別字（cbuffer 欄位名、函式簽名）額外 `grep -rl` 全 `app/src/runtime/`（不只 `app/shaders/`），排除掉語意已被 CPU 側吃掉的候選。
- **⑩ 象徵性雜湊/mod 鏈（如 `hash11` 的 `p*=p+33.33; p*=p+p` 平方鏈）在 fast-math 下對 GPU/CPU 的微小捨入差異有數千倍放大係數，用於「離散桶選擇」（如 `axisAngles[(int)(hash*6)%6]`）時會產生遠超 `mathv_compare.h` `exemptMax`（1%）上限的桶錯位率**（波次4 grid-walk-points 實測：4096 樣本 sweep 出 ~7% WRAPPED 分支的 table-row 錯位，且錯位幅度非「剛好卡在邊界」的細微 1-2 ULP——手推 Python 交叉驗證證實 CPU ref 公式本身完全正確，錯位純粹是 hash11 平方鏈把 fast-math 允許的求和重結合誤差放大成整數桶跳動）。**不要嘗試用 boundary-distance pre-filter 硬吃**（實測 threshold 拉到 0.15/0.1667≈90% 桶寬仍攔不住，因為放大係數比預期大一個數量級以上）；正確作法＝**TU 自己做結構性驗證取代逐點相等**：對「不觸發雜湊選擇」的部分（如本例的 Position 更新、未 wrap 分支的 Rotation 不變）照常用 `Comparator` 逐點比對；對「觸發雜湊選擇」的輸出改驗**集合成員資格**（GPU 輸出是否等於查表公式在**某個**合法索引下的結果，不要求與 CPU 選中的**同一個**索引一致）——這樣仍能抓到查表公式本身的真錯誤（PI 常數錯、table 值錯、qFromAngleAxis 符號錯 → 輸出會不屬於任何合法成員），但不會被雜湊桶選擇的 GPU/CPU 天生不可重現性拖累。實證＝`app/src/selftests_mathv_gridwalkpoints.cpp` 的 `matchesAnyTableRow()`。

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
