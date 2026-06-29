# PARAM_COMPLETION_MAP — 跨島 param 補全缺口地圖

> 2026-06-29 跨島偵察產出（read-only scout，binary 12:50 build）。把 param-completion fan-out
> 從「盲掃」變成「地圖」。**這是下批 fan-out 的選批 SSOT。** 缺口數字會隨補完移動——
> 真相源永遠是 `tools/nodespec_integrity.sh`（現只懂 generator 島，擴島見 §閘擴充）。

## ★核心：EXTRA 有兩種成因，分不清下批會白工

其他島主流是 `sw > TiXL`（EXTRA），但 EXTRA **不等於**「sw 多做了要砍」。兩種成因：

- **成因 A（image 島）— 真 baked 的 resolution trio**：每顆 image op 的 sw spec 塞了
  `Resolution`(enum)+`CustomW`+`CustomH` 三顆 output-format 旋鈕。TiXL `.cs` 不把這三顆寫成
  `[Input(`（在 `_ImageOutputFormat`/`.t3` 層）。所以 image 島幾乎每顆 `sw = tixl + 3`。
  **這不是缺口，是 RenderTarget 慣例。** 閘要排除這三顆（見閘擴充工單 C）。
- **成因 B（field/mesh 島）— `--dump-nodespec` 的 Vec fold 壞掉（閘掃不準）**：
  field/mesh 的 Vec3/Vec2 input，head 標 `VEC-HEAD/arity3` 但 `.y/.z` 沒被折進去，各算 1。
  實證：`BoxSDF` sw=8 但真 logical=4（Center/Size 三軸沒折）；`CubeMesh` vec-component grep==0。
  **field/mesh 的 EXTRA 大多是 fold 假象，不是真 param 差。** 不修 fold 就掃 → 狂噴假紅。
  **工單 D 優先於把 field/mesh 納入閘。**

## 缺口地圖（每島一行，全掃非抽樣）

| 島 | 顆數 | MATCH | 真缺(MISSING) | EXTRA | EXTRA 主因 | 傾向 |
|---|---|---|---|---|---|---|
| string | 25 | 23 | 2 | 0 | — | 最乾淨，幾乎全齊 |
| flow | 14 | 7 | **6** | 0 | — | **真缺口最密** |
| particle | 10 | 2 | 2 | 6 | 力場 fold 假象 | ParticleSystem -11 是真大洞 |
| field | 43 | 11 | 3 | 29 | fold bug（成因 B）為主 | EXTRA 多假象，真缺少數 |
| mesh | 20 | 6 | 2 | 12 | fold bug（Vec 完全沒折） | 同 field |
| image | 76 | 17 | 1 | 58 | resolution trio（成因 A） | EXTRA 是慣例非缺口 |

## 真缺口清單（sw < TiXL，去假象後確定要補）

- **flow（first）**：`SetIntVar` -3（漏 MappedType LogLevels 日誌通道 + clamp/default）/
  `SetFloatVar` -2 / `SetVec3Var`·`SetBoolVar`·`GetIntVar`·`LogMessage` 各 -1。
  context-var 寫入端系統性漏 log-level/條件旋鈕。
- **particle**：`ParticleSystem` -11（lifetime/initial velocity/emit 控制，與 PARITY_GATE_PLAN §清單
  的 host-cut input 同一批；MaxParticleCount pool-recycle fork = `[?]` 卡柏為）/ `TurbulenceForce` -2。
- **field**：`TransformField` -5（Translation/Rotation/Scale Vec3 全沒接）/ `RaymarchField` -3。
- **mesh**：`DrawMeshUnlit` -11（材質/光照旋鈕幾乎沒 port）/ `CombineMeshes` -1。
- **string**：`BlendStrings` -2 / `BuildRandomString` -1。
- **image**：`RenderTarget` -6（真缺，與 resolution trio 無關）。

## 下批 fan-out 優先序

1. **flow Set*/Get*Var 族** — 真缺口最密，context-var 通道完整性，顆數小解鎖高。
2. **ParticleSystem -11** — 單一節點解鎖最多旋鈕；但需先讓柏為拍 pool-recycle fork（`[?]`）。
3. **field TransformField/RaymarchField + mesh DrawMeshUnlit** — 需先修 fold bug（工單 D）才能用閘驗。
4. image/string/多數 field-mesh 的 EXTRA **不要當缺口處理**。

## 閘擴充工單（`tools/nodespec_integrity.sh` 現只懂 generator 島）

- **D（最關鍵，先做）**：修 `--dump-nodespec` 的 field/mesh Vec fold——Vec3 input（`.x/.y/.z`
  分離 slot 命名）要像 point generator 一樣折成 head。不修就掃 field/mesh = 全假紅、閘失信號。
- **A**：island `.cs` 目錄解析。`.cs` 散在 `image/{fx,color,generate}`、`field/generate/sdf` 等子樹 →
  改成 `Lib/<island>/` 子樹 `find -name <cs> | grep -v _obsolete | head -1`。
- **B**：`cs_for_type` 覆寫表擴充 sw-fork rename（DoyleSpiralPoints2/CombineMaterialChannels2/
  AfterGlow2/DrawPoints2/RyojiPattern2/MunchingSquares2/RepeatField3/PointTrailFast…）。
  **更穩**：閘讀節點碼的 `// @tixl:` / `// TiXL authority:` header authority 宣告當權威來源，
  比手維護覆寫表不易 stale（census 已靠這個第四源對 fork）。
- **C**：image 島 known-EXTRA 排除——在 `dumpNodeSpec`（`app/src/selftests.cpp`）給
  Resolution/CustomW/CustomH 打 `output-format synthetic` tag、像 grid `Count` 一樣排除。

## 命名對照陷阱（census 活情報，sw type ≠ TiXL .cs 名）

sw 帶 `2` 後綴 / 改名的 fork：DoyleSpiralPoints→DoyleSpiralPoints2.cs、CombineMaterialChannels2、
AfterGlow2、DrawPoints2、RyojiPattern2、MunchingSquares2。census 已靠 `// @tixl:` header 對上
大小寫 fork（chromab→ChromaticAbberation），但閘沒讀此 header。

## 4 顆 SW_UNKNOWN（已查證 2026-06-29）

| 節點 | verdict | 證據 |
|---|---|---|
| EdgeRepeat | 走他路，閘 N/A 正常 | `registerTexOp`（texReg 單例，image-filter 族）非 NodeSpec |
| PolarCoordinates | 走他路，閘 N/A 正常 | 同上 `registerTexOp` |
| Switch | 走他路，閘 N/A 正常 | `registerCmdOp`（command-flow 收集器，同 Execute/Loop）；持久 runtime 無 NodeSpec |
| **Steps** | **census 假陽性，實際未 port** | sw 無此 node（無檔/無 register/無 cook）；census source#3 grep 撈到 `node_registry_math_anim.cpp:61` 插值 enum-label 字串 `"Steps"` lowercase 撞 TiXL `Steps.cs` 誤判。Steps（image/fx/stylize posterize op）**待 port**。 |

→ 閘擴島前要先**分類 cook 路徑**（NodeSpec / texReg / cmdReg），只對 NodeSpec-driven 跑 param 閘，
其餘標 `N/A (non-NodeSpec path)`，否則 texReg/cmdReg op 報 `sw=UNKNOWN` 淹沒信號。
→ census source#3 的 capitalized-string 掃描會把 enum-label 誤判成 node type（Steps 受害）→ census
done 數略有假陽性灌水。修法：source#3 只認真正 `register*Op("X")`/NodeSpec `type=` token，別撈裸字串陣列。

---

## ★閘擴多島修法 spec（2026-06-29 scout，下批工單，可直接實作）

### 工單 D（最關鍵）— fold-bug 真因 + generator-safe 修法
**真因**：`dumpNodeSpec`（`app/src/selftests.cpp:110-138`）用了一條跟全 codebase **發散**的 fold 規則
（line 115 靠 `widget==Vec && vecArity==1` 認 component）。但 Inspector（`ui/inspector.cpp:83-86`）與
`animGroupForSlot`（`runtime/node_registry.cpp:196-213`）都用 **positional consume-the-run**（head 在 i、
`widget==Vec && vecArity>=2` 就位置上吃掉接下來 N-1 個 port，不看 component widget）。field/mesh 很多 op
手寫 component port 時只設 def/minV/maxV 沒設 `widget=Vec`（鐵證 `field_ops_spheresdf.cpp:75-80` cy/cz；
全 mesh op）→ dumpNodeSpec 把 component 各算 1 → 假 EXTRA。

**修法（單一檔 dumpNodeSpec，建議）**：改 index loop，head 判定 `isInput && widget==Vec && vecArity>=2`
→ 印 VEC-HEAD、`++folded`、`int N=min(vecArity,4); i+=(N-1)`（位置吃 component，不計數）。移除 line 115 舊規則。
output 排除 + grid-`Count` 排除原樣保留。**更治本**：把這條 walk 抽成 `graph.h` 共用 helper `foldVecRun`，
Inspector/animGroup/dump 三處共用，杜絕再發散（graph.h 是 cook-core owner-lock，動前確認無並行 lane 寫它）。

**generator-safe 數學保證**：13 顆 generator 無 Vec head → 新 walk 對它們逐 port 等同舊 walk →
13/13 不可能回歸。已正確折的 op（raymarchfield 那批）兩 walk 同結果。已破的（spheresdf/cubemesh）位置
吃掉 component = 要的修復。

**守護斷言（防再退化）**：①釘已知真值 golden：SphereSDF==2 / BoxSDF==4 / RaymarchField（正確折哨兵）/
RadialPoints（無 Vec generator-safe 哨兵）。②**結構 invariant**：head 宣告 vecArity=N 卻沒吃到 N-1 個
component（踩到下個 head/output）→ 印 WARNING + 非零標記（把「作者忘排 component」從靜默假數變大聲報錯，
比逐顆維護 golden 耐 stale）。
**不採**「補 ~33 個破檔的 component widget」替代法（治標，下個手寫 op 又破；改 walk 才治本）。

### 工單 A（island .cs 子樹解析）+ B（fork rename）
`nodespec_integrity.sh:33` 硬編 `TIXL_GEN=point/generate`。改參數化島：field 在 `field/generate/sdf/`、
mesh 在 `mesh/generate/`、image 在 `image/fx/{distort,stylize,...}`。`cs_for_type` 硬編路徑換成子樹搜尋
`find external/tixl/Operators/Lib/<island> -name "<cs>" | grep -vE '_obsolete|/_' | head -1`。
**fork rename 別手維護覆寫表**（易 stale）→ 改讀節點碼 header 的 `// @tixl:`/`// TiXL authority:` 宣告當權威
（census source#4 已這樣對 fork，複用同 awk）。此項同時吃掉工單 B。

### 工單 C（image resolution-trio 排除）
與既有 grid-`Count` 排除同形。`dumpNodeSpec` 對 port id ∈ {Resolution,CustomW,CustomH}（或宣告處打
synthetic flag）給 `role="output-format synthetic"` + `continue` 不計數 → image 島 `sw=tixl+3` 假 EXTRA 歸零。

---

## ★閘擴多島實機結果 + refuter 更正（2026-06-29，`ca0972e` + refuter）

閘擴後實掃（fold/trio 假象部分清掉）：

| 島 | swept | MATCH | MISSING | EXTRA | 可信度 |
|---|---|---|---|---|---|
| field | 43 | 38 | 2（TransformField/RaymarchField） | 3 | ✅ 準 |
| mesh | 20 | 15 | 5（DrawMeshUnlit/CubeMesh/IcosahedronMesh/CombineMeshes/DeformMesh） | 0 | ✅ 準 |
| image | 73 | 29 | **34（地板）** | 10（**假帳**） | ⚠ MISSING 可信、EXTRA 是 bug |
| generator | 13 | 13 | 0 | 0 | ✅ 準（無回歸） |
| flow | 6 | 6 | 0 | 0 | ✅ 準（無回歸） |

**★refuter 抓到的 BREAK（image 島專屬，未修，已派 task）**：
- **image MISSING=34 是真缺口下界**：逐顆對 TiXL .cs 驗過（Blur sw5/tixl7 缺 Resolution+Wrap、RenderTarget sw2/tixl11 缺 9），無假象污染，**照這 34 顆補旋鈕安全**。
- **但 image EXTRA=10 是假帳，符號翻轉**：AfterGlow/Blend/BlendWithMask/BoxGradient/Combine3Images/CombineMaterialChannels2/DistortAndShade/LightRaysFx/MirrorRepeat/AfterGlow2 這 10 顆**其實也是 MISSING**，被 fold 守衛過度折開（Blend 真缺 2 報成 +2 EXTRA、AfterGlow 真缺 1 報成 +1）。**真 image 缺口 >34。**
- **根因二（都在 image 島）**：(a) `dumpNodeSpec` 的 positional walk 守衛跟 `inspector.cpp:83-86`/`node_registry.cpp:205-207` **不同源**——這 10 顆 op 把 `Vector4 Color` 拆成 `Color.x/.y/.z/.w` 且**每個 component 自己 tag widget==Vec、vecArity 遞減 4→3→2**，守衛看到 `Color.y` 也是 Vec head 就 break → 一個 Vector4 折成 3。(b) `nodespec_integrity.sh` 的 VEC-RUN-SHORT 偵測**在 shell 裡是死的**：`sw=$(...)` 在 subshell 跑、設的 `SW_VEC_RUN_SHORT` global 出 subshell 消失 → 守衛永不觸發 → 靜悄悄歸 EXTRA 而非大聲報結構 bug。
- **修法（task 派出）**：(a) dumpNodeSpec 守衛改成「component 自身 tag Vec 且 arity 遞減＝仍屬同一 head 的 run，繼續吃不 break」；(b) shell 改 subshell 變數傳遞（temp-file 或 exit-code 帶 VEC-SHORT 旗標）。修完 image EXTRA→正確 MISSING、真缺口全景才現。**field/mesh/generator/flow 不受影響（裸 component 無 chained-Vec-head，已驗準）。**

---

## ★兩個 fold 缺陷修復落地 + refuter 驗證（2026-06-29，本 session）

兩缺陷都堵上（`app/src/selftests.cpp` dumpNodeSpec 守衛 + `tools/nodespec_integrity.sh` VEC-SHORT 跨 subshell 傳遞）：
- **缺陷 1**：dumpNodeSpec 守衛改成鏡像權威 walk（`inspector.cpp:83`/`node_registry.cpp:203`）的**盲目位置消費**——head 宣告 arity=N 就吃 N-1 個 port，不看 component widget；只在撞 output / port 用盡時喊 VEC-RUN-SHORT。移除了「component 是 Vec head 就 break」的發散規則。
- **缺陷 2**：`sw="$(sw_folded_count …)"` 的 subshell 吃掉 `SW_VEC_RUN_SHORT` global → 改寫 temp-file（`VEC_SHORT_FLAG`）+ `sw_vec_run_short` reader，跨 subshell 存活。實證活了：`PickColor`/`BlendVector3`/`PickVector3`/`MaxInt2`（Vec head arity 撞 output 的真結構案例）現在大聲報 VEC-RUN-SHORT 而非靜默歸帳。

**閘擴後 image 真缺口全景（refuter 逐顆對 .cs 驗過，NOT REFUTED）**：

| 島 | swept | MATCH | MISSING | EXTRA | VEC-SHORT | 可信度 |
|---|---|---|---|---|---|---|
| image | 73 | 31 | **42（真缺口，地板已破）** | **0** | 0 | ✅ refuter 驗準（符號翻轉已修） |
| field | 43 | 38 | 2 | 3 | 0 | ✅ 數字未動（無回歸） |
| mesh | 20 | 15 | 5 | 0 | 0 | ✅ 數字未動（無回歸） |
| generator | 13 | 13 | 0 | 0 | 0 | ✅ 13/13（無回歸） |
| flow | 6 | 6 | 0 | 0 | 0 | ✅ 6/6（無回歸） |

10 顆假 EXTRA 翻正：**7 顆→MISSING**（AfterGlow −1·Blend −2·BlendWithMask −1·BoxGradient −1·Combine3Images −1·CombineMaterialChannels2 −1·MirrorRepeat −1）+ **3 顆→MATCH**（AfterGlow2·DistortAndShade·LightRaysFx）。image MISSING `34→42`（先前 34 是地板，多出的 8 是被過度折開藏住的真缺口）。EXTRA `10→0`。

refuter 確認（獨立數 `[Input(`）：Blend sw8/TiXL10 缺 GenerateMips+Resolution；AfterGlow/BoxGradient/MirrorRepeat 各缺 Resolution（TiXL 真 Int2 [Input]，非 sw synthetic trio）。**最硬 break-test：Rings（3 連續 Vec4 + 6 連續 Vec2 head）9 顆全各折一顆、零塌陷** → 盲目位置消費不會把相鄰獨立 Vec 併掉，無新增假 MISSING。

**守護牙**：`--selftest-nodespecfold` 新增 `Blend==8` chained-Vec golden（-bug 下每個 Vector4 過折成 3 → 12 → RED），與 SphereSDF/BoxSDF（裸 component 形）兩種 fold-bug 形都釘住。`--bite`：PASS=530 無 failure。

---

## ★閘擴後 fan-out 目標 ground-truth（2026-06-29 scout，難度排序）

**★★承重發現：剩餘大宗 param-completion 不是逐顆 leaf，是兩條共享 seam 卡住**——
排 seam-build 先，3/4 顆才變乾淨 fan-out：
- **PF-0d float4x4 param-spine**：現 float-spine `map<string,float>` 載不動矩陣 → 凡需 matrix 的 param
  全卡（TransformField 全 5 顆）。
- **render-state + texture-asset-bind infra**：pipeline-state（rasterizer/depth/blend）+ Texture2D
  sampler 綁定，是 codebase-wide infra gap（DrawPoints/Lines/ScreenQuad/DrawMeshUnlit 共享延後）。

| 節點 | 缺 | 乾淨/卡 | 下批序 |
|---|---|---|---|
| **RaymarchField** | 6（Color/TextureScale/NormalSamplingD/SpecularAA/WriteDepth/UVMapping） | 5 乾淨（騎現有 FloatsToBuffer float-pack pipe，MaxSteps 等已證）+ UVMapping 輕 #define-seam | **① 先採**（最高乾淨產出、零新 seam，UVMapping 那顆延後） |
| **RenderTarget** | 6（Clear/GenerateMips/EnableUpdate 乾淨；Multisampling/TextureFormat/WithDepth/WithNormalBuffer 卡） | 3 toggle 乾淨（gate 既有機制）+ 餘卡 texture-alloc/MSAA-resolve/multi-attach seam | **② split-node**：先採 3 toggle，餘延後 |
| **TransformField** | 5（Translation/Rotation/Scale/Shear/Pivot 全 Vec3，純 host input 無中間 routing） | 單一 seam：T/R/S/Shear/Pivot→float4x4（TiXL transpose+invert+yaw/pitch/roll 序）+ **PF-0d float4x4 param-apply** | **③ 等 PF-0d**（self-contained 但卡 param-spine） |
| **DrawMeshUnlit** | 11（BlendMode/FillMode/Culling/ZTest/ZWrite/Texture/UseCubeMap/AlphaCutOff/BlurLevel/TextureWrap/UseVertexColor） | 跨兩條共享 seam：render-state（5）+ texture-asset-bind（6） | **④ 等 shared seam**（最重，與 Draw* 家族同解鎖） |

routing 註：TransformField/RenderTarget 的 .t3 都 empty Children/Connections=純 host input 無中間節點
（無 silent routing trap，trap 在 matrix 數學）；RaymarchField/DrawMeshUnlit 的缺 param 多經
FloatsToBuffer/BlendColors/RasterizerState 子節點 routing（補時 backward-trace）。

---

## ★307 真原子 param 全掃（2026-06-29，fold-fix c0251ea 後全島可信）

對 `node_health.sh --class atom` 的 307 真原子（done ∧ TiXL 原子）逐顆跑 `nodespec_integrity`（sw-folded vs TiXL [Input]）。
快照 `--tsv --params`；KPI 進 node_health.html「真原子旋鈕不全」欄。

| 結果 | 顆數 | 意義 |
|---|---|---|
| match（旋鈕齊） | 246 | sw 暴露的 param 數 == TiXL [Input] |
| **MISSING（旋鈕不全）** | **38** | **baked 寫死沒暴露成 inspector 旋鈕 = 補 backlog** |
| EXTRA | 5 | 多為 float4x4 fold 上限假象（見下）|
| no-cs | 11 | 閘解析不到 .cs（fork/rename 待補 header 或 sw-internal 無 TiXL 對應）|
| vec-short | 4 | Vec head 宣告 arity 但 component 沒鋪滿（author 結構 bug，閘大聲報）|
| unregistered | 3 | atom 名非已註冊 NodeSpec（走他路 / 名字對不上）|

### ★38 真原子旋鈕不全清單（補 baked-param backlog；缺的 TiXL [Input] 已對 .cs）

| 算子 | 島 | 缺 | 缺哪些（對 TiXL .cs [Input]）|
|---|---|---|---|
| AnimFloatList | numbers | 1 param | OverrideTime |
| AnimVec2 | numbers | 1 param | AllowSpeedFactor |
| AnimVec3 | numbers | 1 param | OverrideTime |
| BlendStrings | string | 2 param | InputTextA, InputTextB |
| BuildRandomString | string | 1 param | OverrideBuilder |
| CubeMesh | mesh | 3 param | Margin2, TexCoord, TexCoord2 |
| Damp | numbers | 1 param | UseAppRunTime |
| DampAngle | numbers | 1 param | UseAppRunTime |
| Ease | numbers | 1 param | UseAppRunTime |
| EaseVec2 | numbers | 1 param | UseAppRunTime |
| EaseVec3 | numbers | 1 param | UseAppRunTime |
| GradientsToTexture | numbers | 1 param | Resolution |
| IcosahedronMesh | mesh | 2 param | TexCoord, TexCoord2 |
| Lerp | numbers | 1 param | Clamp |
| LoadImage | image | 1 param | CacheResources, SourcePathSlot |
| MergeFloatLists | numbers | 1 param | （名稱對不上：count 差 1，sw-fold 與 TiXL 命名歧義，補時逐顆 trace） |
| MergeIntLists | numbers | 1 param | （名稱對不上：count 差 1，sw-fold 與 TiXL 命名歧義，補時逐顆 trace） |
| OscillateVec2 | numbers | 1 param | OverrideTime |
| OscillateVec3 | numbers | 1 param | OverrideTime |
| PerlinNoise | numbers | 1 param | OverrideTime |
| PerlinNoise2 | numbers | 1 param | OverrideTime |
| PerlinNoise3 | numbers | 1 param | OverrideTime |
| PickVector2 | numbers | 1 param | Index |
| PointToMatrix | point | 10 param | AlsoOffsetTarget, AspectRatio, ClipPlanes, FieldOfView, LensShift, PositionOffset, Roll, RotationOffset, SamplePos, Up |
| PointsToCPU | point | 3 param | Async, TriggerUpdate, UpdateContinuously |
| ReadPointColors | point | 1 param | Async |
| Remap | numbers | 2 param | BiasAndGain, Mode |
| RemapValues | numbers | 1 param | InputValue |
| RgbaToColor | numbers | 3 param | A, B, G |
| SetBpm | numbers | 1 param | SubGraph |
| SetPlaybackSpeed | numbers | 1 param | SubGraph |
| SetPlaybackTime | numbers | 2 param | ShowLogMessages, SubGraph |
| SetStringVar | flow | 2 param | ClearAfterExecution, SubGraph |
| Spring | numbers | 1 param | UseAppRunTime |
| SpringVec2 | numbers | 1 param | UseAppRunTime |
| SpringVec3 | numbers | 1 param | UseAppRunTime |
| TransformField | field | 5 param | Pivot, Rotation, Scale, Shear, Translation |
| Trigger | numbers | 1 param | ColorInGraph |

### 系統性 pattern（補 backlog 的槓桿點）

- **時間源旋鈕**（最大宗，~15 顆）：`UseAppRunTime`（Spring/Damp/Ease 家族 8 顆）+ `OverrideTime`（PerlinNoise/Anim/Oscillate 7 顆）全沒暴露。
  **這正是 MEMORY 警告的 wall-clock 綁定點**——離線決定性 render（MV 目標）要靠這顆把 anim 從 wall-clock 切到 app-time。補一顆共通 time-source 旋鈕家族 = 一次解鎖多顆。
- **Command-rail fork 疑似假 MISSING**（SetBpm/SetPlaybackTime/SetPlaybackSpeed/SetStringVar 的 `SubGraph`+`ClearAfterExecution`）：
  與 `known_fork_count` 已記錄的 Set{Int,Float,Vec3,Bool}Var **同形**（value-rail node 故意不掛 SubGraph/Command；SubGraph 那半在 Set*VarCmd）。
  **這 4 顆很可能該進 known_fork_count 而非當缺口補**——待對 op-source header 驗證（同 SetIntVar 註解）。先標疑似 fork，別盲補。
- **matrix fold 上限假象（EXTRA）**：TransformVec3（+12）/MulMatrix（+24）的 `Matrix` 是 float4x4 攤成 16 個 `m11..m44` flat port；
  dumpNodeSpec 的 Vec fold `min(vecArity,4)` 對 arity16 折不動（折 4 顆、剩 12 顆各算 1）→ 假 EXTRA。**非真分歧，是 PF-0d float4x4 param-spine 缺**（同 TransformField -5）。
  CombineSDF/StairCombineSDF/CombineFieldColor 各 +1 EXTRA = field combiner 多輸入 fold，待逐顆 trace（小量）。
- **TransformField -5**（Translation/Rotation/Scale/Shear/Pivot 全 Vec3）= PF-0d float4x4 spine 卡住（已在上方 fan-out 目標表記）。
