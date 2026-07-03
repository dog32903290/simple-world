# GOLDEN ORACLE AUDIT — golden 期望值可信度審計

> **★2026-07-03 結案(同日修完)**:下表 55 顆 FLAGGED 已全數修理或裁決,全量 `--bite` PASS=572/FAILED 0/NO-BITE 0。
> 修理分四波(P1 機械 44 顆 → 判斷修 11 顆 → P2/P3/P4 掃 30 顆 → 行數閘拆檔),過程抓到**三隻真獵物**(全是「自洽所以永綠」的物種):
> 1. **bend/twist 旋轉方向反轉**(HLSL row-major 字面值 × MSL column-major:`mul(m,v)` 該譯 `v*m` 非 `m*v`)——golden 手推 TiXL 期望首跑 RED、實際值與轉置預測**逐位吻合**實錘;leaf 已修(field_ops_bendfield/twistfield.cpp),同款 frozen fork 他處需警惕。
> 2. **Locator 臂長 2× fork**(sw 自創 `k=2*Size`,TiXL 是 Size→UniformScale ⇒ 臂半長 0.5·Size)——源碼實錘,leaf 已照 TiXL 重錨。
> 3. **snaptoangles NodeSpec 恆真假錨**——重建為真 cook-through 三腿結構,production NodeSpec 漂移現在真的會紅。
> 其餘 sw 實作對 TiXL 逐行核過 **verbatim**(mesh 全家、SDF 家族、simplex noise 位元組相同)。
> 防再犯已落閘:`tools/golden_lint.sh`(run_all 每次自動掃)+ --bite vacuous-exit 第二層 + `docs/agent/GOLDEN_STANDARD.md` + CLAUDE.md 鐵律 8 + sw-batch/sw-node-batch 品質閘。
> 裁決「接受不修」三項:particlefield_probe(檔頭自認探針)、field_raymarch 兩顆(結構/接線定位,已修 P1 極性)、fieldvolumeforce_field swap(承重 liveness,檔頭有理由)。
> 下表保留為病理記錄(修理前的原始判定),供寫新 golden 時對照五反型。

**日期**:2026-07-03 **方法**:6 個並行 agent 逐行審 `app/src/*_golden.cpp` 全部 123 顆,
以 `gradient_golden.cpp` 為校準基準(①期望值 independent-of-impl 手推自 TiXL 公式
②取樣坐發散中段 ③injectBug 腐蝕真 cook、no inversion、`return ok?0:1`)。
最嚴重三顆宣稱(snaptoangles / bendfield / randomjumpforce)已由 orchestrator 親自對碼確認。

**總判**:✘✘✘(三軸全爛)**0 顆**——底子是好的,沒有一顆整顆是拿 sw 輸出當期望的假 golden。
但病集中成 **5 個系統性的型**,每型都有清楚修法。PASS 68 / FLAGGED 55(多數為「需人看」級)。

---

## 待修第一批(最危險,已親驗)

| # | 檔 | 病 | 證據 |
|---|---|---|---|
| 1 | `snaptoangles_force_parity_golden.cpp` | **恆真假錨**:velDefault45 與 velProdDefault 是同參數同 dispatch 的兩次呼叫,`prodDefault==AngleCount45` 是 A==A;production NodeSpec 從未被讀,default 漂移穿綠燈 | :300-309(同一 `nodeSpecDefaultParams(N,kTiXLAngleCount)` 兩次)。修法照抄 `vectorfield_force_parity_golden.cpp` 的真 cook-through TOOTH 2 |
| 2 | `field_ops_bendfield_golden.cpp` | **本體恆等點**:`kAmount=0.0f`,bend 旋轉公式從未生效,錯的角度/swizzle/符號永綠 | :64 |
| 3 | `field_ops_twistfield_golden.cpp` | 同型:twist 只在 Amount=0 跑,真被驗的只有 StepFactor post 線 | :20-26, :65 |
| 4 | `field_ops_rotatedplanesdf_golden.cpp` | 同型:只測 default(Normal=(0,1,0)→d=p.y),硬編碼 `p.y` 的實作照樣綠 | :13-16, :122 |
| 5 | `field_ops_noisedisplacesdf_golden.cpp` | **本體無 oracle**:simplex 噪聲只驗「有動+決定性」,錯的常數/梯度表終身綠 | :8-27, :186-224 |
| 6 | `randomjumpforce_field_golden.cpp` | ratio 斷言對常數因子數學上盲(宣稱 gate 的 Amount/100×fieldAmount 漏了照樣綠)+ -bug 反轉式 + 檔內 fieldAmount 敘述自相矛盾 | :159-174 |
| 7 | `fielddistance_force_parity_golden.cpp` | 咬合正式 ✘:純 inversion(-bug 只換期望值 :246,:268,:271),量測鏈壞成恆 0 時 bite 閘看不出 | :246 |

## 系統性的型(修一次除一窩)

### P1 — --bite 閘空轉(~25 顆 field/連接 golden)
-bug 路徑無條件 `return 1`:注入失效(did not trip)也 exit 非零,而
`tools/run_all_selftests.sh:44` 只看 exit code(stdout 進 /dev/null)。
今天齒還咬得到,但注入哪天靜默失效,--bite 照樣綠——`gate-or-it-rots` 活體。
**修法(機械,可派 fable)**:did-not-trip 分支改 `return 0`,讓 harness 的 NO-BITE 名單接住。
**自家正確範本**:`field_paramapply_golden.cpp:369`、`fieldtree_builder_golden.cpp:153`、`gradient_golden.cpp:149`。
中招例:field_ops_planesdf :206-214、field_ops_absolutesdf :172-180、connect_cooks :169-177、
field_ops_boxsdf :213-221、field_ops_boxframesdf :223-231、field_ops_blendsdfwithsdf :309-317、
field_ops_repeatfieldlimit :224-232(其餘同模式 field ops 均需掃)。

### P2 — 本體只在恆等/飽和點取樣(最大宗)
op 的本體公式在 golden 裡從未生效——參數乘零、identity 配置、飽和端點:
- field:bendfield(Amount=0)、twistfield(Amount=0)、rotatedplanesdf(default 恆等)、
  spatialdisplacesdf(精確值只在 Amount=0)、rotatefield(x/y 軸只跑 0°)、
  setsdfmaterial(gate 只測 firing 側)、pyramidsdf(兩 probe 全坐 base plateau 同值)、
  raster3dfield(只取 smoothstep 兩飽和端)
- mesh generator 家族:mesh_golden/cube/cylinder/sphere/torus/input/modify/modify2 —
  Radius/Scale=1、Rotation/Twist/Spin=0(公式主幹有咬,參數接線沒咬;
  mesh_modify2 的 SelectVertices 只採 s=0/1 端點,falloff 中段無採=正式 ✘)
- 其他:samplecpupoints(TangentScale=0,Bezier 塌成 lerp,tangent 公式從未生效)、
  pointcolorwithfield(Strength=1 飽和+field 恆白)、image2dsdf(y-翻轉無 probe)
**修法**:每顆加 1-2 個非恆等參數的中段 probe,期望值手推 TiXL 公式(同 `replay-golden-pins-must-sample-diverging-middle`)。

### P3 — 期望翻轉(want-flip)代替真注入(~14 顆)
-bug 不動 impl、只把 want 換成錯值:證明「assert 能分辨兩個值」,沒證明「assert 綁在真 cook 路徑上」。
axisstep(:253)、directional(:242,且 :245-247 註釋 stale)、bpm_transport(:161)、
setbpm、setplayback、particle_sim_integrate(:182)、randomjumpforce(:173)、
pointlist LEG2-4、drawboxgizmo/drawlinegrid/drawspheregizmo LEG2(反轉式)、locator LEG2(:137)、
fieldvolume_force_parity(:290-296)。
註:部分 swap 是承重的(fieldvolumeforce_field :170-172 = liveness 證明,檔頭有技術理由)——逐顆判,不齊頭改。

### P4 — 本體無值 oracle(行為帶/性質斷言)
afterglow(:140-142)、advancedfeedback(:179-181):行為帶(>40/<30),無 TiXL 數值錨,
decay 倍率未隔離(afterglow2 的 ratio=0.75 隔離法是自家正確範本)。
field_raymarch/_output:純性質斷言(對稱/範圍),glow 值無公式 parity。
toroidalvortexfield:velocity 本體只做 MSL substring 斷言,cross 符號錯照綠(decay 通道有值驗)。
particlefield_probe:檔頭自認非 deliverable golden。

### P5 — 自洽 oracle(巢狀節點同物種,最要警惕)
- snaptoangles(恆真,見待修 #1)
- `field_ops_octahedronsdf_golden.cpp`:host mirror 抄的是 sw 自己的 helper 非 TiXL .cs(:72 自承);
  header 手算錨值只在註解沒進 assert(:18-21)
- `locator_golden.cpp`:臂長 scale 語義引 sw 自己的 cook 非 TiXL 源(:33-34,:77-79)
- `turbulence_parity_golden.cpp` TOOTH 1a:expectedTiXL 用同一顆 sw kernel 自校準(:204-207),
  只驗線性律非 noise parity(TOOTH 1b 是真閘,整檔仍站得住)
**修法**:回 TiXL .cs 重推期望值,把註解裡的手算錨值升級成 assert。

---

## 各批 FLAGGED 明細

(三軸:期望值來源 / 取樣點 / 咬合;✘=確定破,問=需人看)

| 檔 | 軸 | 摘要 |
|---|---|---|
| snaptoangles_force_parity | ✘/✔/問 | P5 恆真假錨(已親驗) |
| fielddistance_force_parity | ✔/✔/✘ | P3 純 inversion |
| field_ops_bendfield | ✔/✘/問 | P2 Amount=0 + P1 |
| field_ops_twistfield | ✔/✘/✔ | P2 Amount=0 |
| field_ops_rotatedplanesdf | ✔/✘/問 | P2 default 恆等 + 注入在 template 層 |
| field_ops_spatialdisplacesdf | 問/✘/✔ | P2 精確值只在 identity |
| mesh_modify2 | ✔/✘/✔ | P2 smoothstep 端點 |
| field_ops_pyramidsdf | ✔/✘/✔ | P2 base plateau 同值雙 probe |
| field_ops_noisedisplacesdf | 問/問/✔ | P4 simplex 無 oracle |
| randomjumpforce_field | 問/問/問 | P3+P4 常數盲(已親驗) |
| field_ops_octahedronsdf | 問/✔/✔ | P5 mirror 自 sw helper |
| locator | 問/✔/問 | P5 + LEG2 反轉 |
| turbulence_parity | 問/✔/✔ | P5 TOOTH1a 自校準(1b 真) |
| field_ops_raster3dfield | ✔/問/✔ | P2 飽和端 |
| field_ops_image2dsdf | ✔/問/✔ | P2 y-翻轉未咬 |
| field_ops_rotatefield | ✔/問/✔ | P2 x/y 只跑 0° |
| field_ops_setsdfmaterial | ✔/問/✔ | P2 gate 單側 |
| field_ops_toroidalvortexfield | ✔/問/✔ | P4 velocity substring |
| field_raymarch(+_output) | 問/✔/✔ | P4 性質斷言 |
| afterglow / advancedfeedback | 問/✔/✔ | P4 行為帶 |
| particlefield_probe | 問/✔/✔ | P4 自認探針 |
| samplecpupoints | ✔/問/✔ | P2 tangent 未生效 |
| pointcolorwithfield | ✔/問/✔ | P2 飽和+恆白 |
| mesh/cube/cylinder/sphere/torus/icosahedron/input/modify | ✔/問/✔ | P2 identity 參數家族 |
| axisstep / directional / bpm_transport / setbpm / setplayback / particle_sim / pointlist(LEG2-4) | ✔/✔/問 | P3 want-flip |
| drawboxgizmo / drawlinegrid / drawspheregizmo | ✔/✔/問 | P3 LEG2 反轉 |
| fieldvolume_force_parity | ✔/問/✘ | P3 反轉(sibling 有承重聲明) |
| fieldvolumeforce_field | ✔/✔/問 | swap 但承重(liveness),留人判 |
| connect_cooks / field_ops_absolutesdf / blendsdfwithsdf / boxsdf / boxframesdf / staircombinesdf / torussdf / repeatfieldlimit 等 | ✔/✔/問 | P1 閘空轉(部分兼 template 層注入) |
| setbpm 120-case | — | bug 期望與正解重合,靠 90-case 咬(:158-160 自承) |

## PASS(三軸全 ✔,68 顆)

批1:afterglow2、buildrandomstring、colorlist_fanout、colorlist、conegizmo、drawpoints_parity
批2:cappedtorussdf、capsulelinesdf、chainlinksdf、combinefieldcolor、combinesdf、customsdf、cylindersdf、fractalsdf(全審最佳範本之一)、invertsdf、planesdf、prismsdf、pushpullsdf、reflectfield、repeataxis、repeatfield3、repeatfieldatpoints
批3:repeatfieldlimit、repeatpolar、rotateaxis、transformfield、translate、translateuv、field_paramapply(bite polarity 正確範本)、field_render、fieldtree_builder(bite polarity 正確範本)
批4:floatlist 全家(8)、grid_points、hasstringchanged、hexgrid_points、joinlists、keepcolors、keeppreviousframe、line_points、list_routing(+wave1)、merge_lists
批5:mesh_blendpick、mesh_cube_uv、mesh_icosahedron_uv、movepointstosdf、pointstocpu、radial_points_parity、raymarchpoints
批6:sdfreflectionlinepoints、selectpointswithsdf、string_builder、blendstrings、getattributefromjson、wrapstring、string_rail、stringctxvar、swaptextures、tryparse、valuestotexture、vectorfield_force_parity(snaptoangles 的正確對照組)

---

**修理順位建議**:
1. P1 閘空轉——機械修,一趟掃 ~25 顆,可派 fable(範本:field_paramapply :369)
2. 待修第一批 #1-#7——判斷修,opus 逐顆(snaptoangles 照 vectorfield 抄)
3. P2 恆等點家族——半機械(加中段 probe + 手推 TiXL 期望值),可 fable 打草稿、opus 驗
4. P5 自洽 oracle 三顆——回 TiXL .cs 重錨,opus
5. P3 want-flip——逐顆判哪些該換真注入(有些 swap 承重),opus 排
