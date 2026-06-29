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
