# GOLDEN_STANDARD — 寫 / 改 golden 前必讀

出身:2026-07-03 全量 oracle 審計(123 顆,docs/agent/GOLDEN_ORACLE_AUDIT.md)。
0 顆全爛,但 55 顆帶同樣五種病。此檔是防再犯的規格;`tools/golden_lint.sh`
機械掃可 grep 的部分,其餘靠這份 checklist 在 review 時逐條過。

**校準範本**:`app/src/gradient_golden.cpp`(期望值)、`app/src/field_paramapply_golden.cpp:360-371`(咬合 polarity)、`app/src/fractalsdf` 與 `vectorfield_force_parity`(TiXL 轉錄 + 真 cook-through)。

## 三特徵(每顆 golden 必須同時成立)

1. **期望值 independent-of-impl**:手推自 TiXL `.cs`/`.hlsl`/`.t3`(引行號),
   或閉式公式/幾何不變量。**絕不**拿 sw 自己的輸出、sw 自己的 helper、
   或同一顆 kernel 的另一次 dispatch 當期望(→ P5 自洽假綠)。
2. **取樣坐發散中段**:參數離開 identity(Amount≠0、Scale≠1、Rotation≠0)、
   probe 離開端點/零值/飽和平台/奇異點。判準:**把 op 本體公式換成錯的,
   這顆 golden 會不會紅?** 不會=沒在測本體。
3. **咬合是真注入 + 正確 polarity**:injectBug 腐蝕**真 cook 路徑**的資料/行為
   (不是翻轉期望值),同一批 assert diverge;
   注入沒咬到(did not trip)時 **`return 0`**,讓 `--bite` 的 NO-BITE 名單接住
   ——dead tooth 回 exit 1 = 閘永遠看不見它死了。

## 五反型(審計實抓,別再犯)

| 型 | 病徵 | 實例 |
|---|---|---|
| P1 閘空轉 | did-not-trip 分支 `return 1`,--bite(只看 exit code)驗不出牙斷 | planesdf 等 ~25 顆(已修) |
| P2 恆等點 | 本體只在 Amount=0/Scale=1/飽和端測,公式錯永綠 | bendfield kAmount=0、twistfield、mesh 家族 Radius=1 |
| P3 want-flip | -bug 不動 impl 只換期望值:證明 assert 能分辨兩個值,沒證明它綁在真 cook 上 | axisstep :253、particle_sim :182 |
| P4 無值 oracle | 行為帶(>40/<30)、substring 斷言、只驗「有動+決定性」 | noisedisplacesdf simplex、toroidalvortex velocity |
| P5 自洽 oracle | 期望值來自 sw 自身(同 kernel 兩次 dispatch 互比=A==A 恆真;mirror 抄 sw helper 非 TiXL .cs) | snaptoangles 恆真假錨、octahedronsdf、locator |

## 新 golden checklist(逐條打勾)

- [ ] 期望值旁註明 TiXL 來源行號(`.cs:NN` / `.hlsl:NN` / `.t3` 欄位)
- [ ] 至少一個 probe 在非恆等參數的中段;心算過「本體公式錯這裡會紅」
- [ ] injectBug 走真 cook seam(configure*/shared 旗標),不是 want-flip;
      確實無 seam 可用時,檔頭寫明技術理由(比照 fieldvolumeforce_field :14-25)
- [ ] did-not-trip → `return 0`;綠 leg `return ok ? 0 : 1`
- [ ] `tools/golden_lint.sh` 綠;`tools/run_all_selftests.sh --bite` 該顆有咬
- [ ] 有狀態/emergent 的 op(粒子、跨 frame):沒有 closed-form 就明標 smoke 級,
      不冒充 parity golden(見 memory `sw-stateful-node-parity-gap`)

## 新 golden 出廠檢查(refuter 波必跑;左移閘,把 2026-07-03 的事後審計搬到源頭)

**這是 /sw-node-batch refuter 波對每顆新 golden 的固定 checklist。** 五反型不是等以後翻舊帳,
是出廠當下逐條考——舊帳從此不再累積。機器代勞的先跑,剩下純語義的人工判。

**機器層(先跑,秒級):**
- P1 → `tools/golden_lint.sh`(硬閘,擋 build):did-not-trip 極性。0 違規才往下。
- P3 → `tools/golden_lint.sh --audit`(軟篩,report-only):列出「期望值 = injectBug ?」的 want-flip
  嫌疑當攻擊清單。**每一條 refuter 都要親判**:是真 flip(期望值隨 bug 翻=病),還是合法的
  路徑分支斷言(severed 時斷不同的真實物理量=OK)。機器只篩,判定是人的。

**人工層(refuter 對每顆逐條問,grep 抓不到):**
- **P2 恆等點**:把這顆 op 的本體公式在腦中換成錯的(角度縮放/swizzle/符號/漏乘 radius)——
  golden 會紅嗎?若所有 probe 都坐在 Amount=0/Scale=1/飽和端/奇異點 → 本體從沒被測 → 打回。
- **P4 無 oracle**:期望值是「行為帶(>40/<30)」或「substring 斷言」或只驗「有動+決定性」嗎?
  → 沒有 TiXL 數值錨 → 要求補閉式牙或誠實降級標 smoke。
- **P5 自洽 oracle**:期望值的出處是 TiXL .cs/.hlsl(引行號)嗎?還是抄了 sw 自己的 helper / MSL /
  同一 kernel 的另一次 dispatch(A==A)?→ 自洽假綠 → 回 TiXL 重錨。

**refuter 出廠判定 = 機器兩層綠 + 人工三條各有明確答案。** 任一條「需人看」未解 → 該 golden 不入主線。

> **實證(2026-07-03,harness 的存在證明)**:turbulence_parity 的 TOOTH 2 是 want-flip,穿過了當日
> 123 顆全量審計(審計火力集中在它的 TOOTH 1a,把 TOOTH 2 當真閘)——一個逃逸紅。隔日建此 harness 時,
> `golden_lint --audit` 的 P3 篩子**在工具還沒建完的試壓階段就把它撈出來**。若此閘當時已在生產線上,
> 它根本逃不掉。這就是「事後審計 → 源頭閘」左移的全部理由。

## 修理進度指標

修 FLAGGED 顆時照 GOLDEN_ORACLE_AUDIT.md 檔尾順位;每修一顆,把該顆從
AUDIT 的 FLAGGED 表挪到 PASS 清單並註記 commit。
