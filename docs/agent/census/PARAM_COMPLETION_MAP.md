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

## 4 顆 SW_UNKNOWN（census done 與 NodeSpec registry 對不上）

`EdgeRepeat` / `PolarCoordinates` / `Steps` / `Switch` —— census 報已 port 但 `--dump-nodespec`
回空（type 沒註冊進 NodeSpec 或走非 NodeSpec 路徑）→ param 完整性閘根本看不到。需單獨查它們
是不是不走 NodeSpec。
