# Point Operator Parity Ledger（對 TiXL 的逐顆對齊債務）

> 這是跨 session 的 point op 對 TiXL parity 債務清單。**設計權威 = `external/tixl`**，本檔只記「我們的 NodeSpec 跟 TiXL 差多少、缺的哪些有下游連鎖、為什麼會漏」。
> 下個 session 一開就看這張，不要再被 `--selftest-*` 綠燈騙——綠燈只證 position 對，不證 attribute 對。

## 鐵尺（怎麼量「一模一樣」）

對每顆 op，比三個權威來源 vs 我們的 `app/src/runtime/node_registry.cpp` NodeSpec：

| 驗什麼 | TiXL 權威來源 |
|---|---|
| port 集合 / type / 順序 / enum 選項 | `Operators/Lib/point/{cat}/{Node}.cs`（`[Input(Guid=...)]` + `private enum`） |
| 每個 port 的 default 值 | `Operators/Lib/point/{cat}/{Node}.t3`（`Inputs[].DefaultValue`，靠 Input Guid 對應，**不靠順序**） |
| 該 port 是否真影響輸出 | 對應 `.hlsl`（`Assets/shaders/points/...`）或 `.cs` cook —— 有些 input 是 metadata，shader 根本沒讀 |

## 總表（subagent 逐 port 驗證後修正）

「缺」只算 TiXL shader 真讀（USED）的 port；TiXL 自己 commented-out 的死碼 port 不算缺。
「default 錯」= port 在但預設值跟 TiXL `.t3` 不一樣（會讓柏為一接上去行為就不對）。

| # | 節點 | 類 | 缺 USED port | default 錯 | 判定 |
|---|---|---|---|---|---|
| 1 | RadialPoints | 生成 | 14（其中 StartAngle/Rotations/Scale 的 struct 欄位 shader 已接，只差 expose） | Count, Radius | 🔴 偷工最兇 |
| 2 | LinePoints | 生成 | 9（F1/F2/ColorA/ColorB/Orientation/Twist/OrientAxis/OrientAngle/AddSeparator） | Count, Length, Direction | 🔴 偷工重 |
| 3 | GridPoints | 生成 | 6（Tiling/F1/F2/Color/OrientAxis/OrientAngle） | CountZ | 🔴 偷工重 |
| 4 | SpherePoints | 生成 | 0 | **Count, Radius** | 🟡 port 全對、default 漂 |
| 5 | TransformPoints | 變換 | 1（StrengthFactor，其餘 5 個是 TiXL 死碼） | Space | 🟡 缺連鎖+Shearing(UNKNOWN) |
| 6 | OrientPoints | 變換 | 1（WIsWeight，半死碼） | 0 | 🟢 近忠實 |
| 7 | RandomizePoints | modify | 0 | **OffsetMode, Interpolation, ClampColorsEtc** | 🟡 port 全對、3 default 漂 |
| 8 | SetPointAttributes | modify | 0 | **RotationAxis, Fx1, Fx2** + 命名 SetExtend/Extend→SetStretch/Stretch | 🟡 命名+3 default 漂 |
| 9 | CombineBuffers | combine | 0（結構簡化：固定 4 路 vs MultiInput 動態 N） | — | 🟡 設計簡化 |
| 10 | AddNoise | modify | 0 | 0（全對齊） | 🟢 近忠實（fork: MSL matrix 無需 transpose） |
| 11 | FilterPoints | modify | 0 | Count type: Int→Float（fork） | 🟢 近忠實（fork: Count Float vs Int） |

**關鍵訊息：沒有一顆是 100% 一模一樣。** 連我們以為「忠實」的 Sphere/Randomize/SetPointAttributes 都有 default 偷偷漂掉——這種漂移最陰，因為 port 在、selftest 綠、但柏為一拉就是錯的手感（球生成 20 倍點、Randomize 預設插值/模式不對、SetAttr 旋轉軸轉錯方向）。

## 逐顆缺項（戰略層）

**1. RadialPoints**（最早的範本，砍最兇）
缺：`OffsetRadius` `Axis(v3)` `OffsetCenter(v3)` `StartAngle` `Rotations` `GainAndBias(v2)` `CloseCircleLine(bool)` `Scale(v2)` `F1(v2)` `F2(v2)` `Color(v4)` `OrientationAxis(v3)` `OrientationAngle` `OrientationMode(enum)`。
只剩 Count/Radius/Center → 是個死圓環，連「轉幾圈/起始角/螺旋分佈」都沒有。

**2. LinePoints**
缺：`F1(v2)` `F2(v2)` `ColorA(v4)` `ColorB(v4)` `Orientation(enum)` `Twist` `OrientationAxis(v3)` `OrientationAngle` `AddSeparator(bool)` `W` `WOffset`。
少了沿線漸層色（ColorA→ColorB）、扭轉（Twist）、W 維。

**3. GridPoints**（已詳查，見 git log d91883b 後的分析）
缺：`Scale(f)` `Tiling(enum 4種網格)` `F1` `F2` `Color(v4)` `OrientationAxis(v3)` `OrientationAngle` `W`。
HLSL 證據：`ResultPoints[index].{Color,FX1,FX2,Rotation}` 每點都寫，我們整段烤死。Tiling 少 3/4 佈局（Triangular/HoneyComb/Diagonal）。

**5. TransformPoints**
缺：`UpdateRotation(bool)` `Shearing(v3)` `StrengthFactor(enum)` `ScaleW` `OffsetW` `WIsWeight(bool)`。
`Shearing` 是真幾何功能漏掉；`StrengthFactor` 有連鎖（見下）。

**6. OrientPoints**
缺：`WIsWeight(bool)` 一個。近乎忠實。

**8. SetPointAttributes**
技術不缺，但我們把 TiXL 的 `SetExtend`/`Extend` **改名**成 `SetStretch`/`Stretch`。功能同，但對 `.t3` 檔會對不上 → 命名 drift 要正名回 Extend。

**9. CombineBuffers**
TiXL 是 `MultiInputSlot`（動態 N 路），我們寫死 4 路。夠用但不是真 MultiInput。

## 兩條跨節點連鎖（真痛點，不只單顆少功能）

**鏈一 — F1/F2 屬性**：生成器（Radial/Line/Grid）對每個點寫 `FX1`/`FX2` 屬性。下游 `RandomizePoints.StrengthFactor` / `TransformPoints.StrengthFactor` / `OrientPoints.AmountFactor` 的 enum `{None, F1, F2}` **讀的就是這個**。生成器烤死 FX1/FX2=0 → 下游選 F1/F2 全變死路。**modifier 端口在，生成器端不餵 → 接縫斷在中間。**

**鏈二 — W 維權重**：`W`/`WOffset`/`ScaleW`/`OffsetW`/`WIsWeight` 是點的第四維權重。生成器不寫 W → TransformPoints/OrientPoints 的 `WIsWeight` 模式沒 weight 可讀。

## 為什麼是這個 pattern（生成器全偷、modifier 忠實）

- **modifier 被迫忠實**：它「逐點讀屬性→改→寫回」，不抄屬性根本動不了 → RandomizePoints/SetPointAttributes 移植時必須抄全。
- **生成器可以偷**：它「無中生有」，只抄 position 點就看得見、`--selftest` 就綠，attribute-write（Color/FX/Rotation/Tiling）整段烤死也跑得過。**綠燈騙過了人——這是偷工沒被擋下的機制。**
- 教訓：生成器的 golden selftest 只驗 position，沒驗 attribute。補洞時要連 selftest 一起補（驗 FX1/FX2/Color/Rotation 真的寫進點）。

## 逐 port 精確設計表（default / type / enum 一模一樣驗證）

> 由 9 個 subagent 逐顆對 `.cs`(type/enum/順序) + `.t3`(default，GUID 對應) + `.hlsl`(實際使用) 驗證。**驗證完成 2026-06-10。**
> used 標註來自 TiXL shader：USED=kernel 真讀；UNUSED=TiXL 自己 commented-out 死碼（我們省略無害）；UNKNOWN=透過子 op 傳遞、本 kernel 沒直接讀。

### 1. RadialPoints（生成）— 🔴 MAJOR_GAP
缺 USED port（TiXL shader 全部真讀）：
`OffsetRadius`(f,0) `Axis`(v3,0,0,1) `OffsetCenter`(v3,0) `StartAngle`(f,0)* `Rotations`(f,1)* `GainAndBias`(v2,.5,.5) `CloseCircleLine`(bool,false) `Scale`(v2,1,0)* `F1`(v2,1,0) `F2`(v2,1,0) `Color`(v4,1,1,1,1) `OrientationAxis`(v3,0,0,1) `OrientationAngle`(f,0) `OrientationMode`(enum[Classic,AlignedToCurvature],0)
- `*` = StartAngle/Rotations(shader 名 Cycles)/Scale 的欄位**已在 `RadialParams` C++ struct、metal shader 已接**，但餵 baked 值（0°/1turn/scale 0）且沒 expose port → 補工 = 加 NodeSpec port + 接 struct 欄位，不是從零。
- DEFAULT 錯：`Count` ours=2048 vs tixl=**100**；`Radius` ours=2.0 vs tixl=**1.0**。
- CHAIN：metal `radial_points.metal` 把 `p.FX1/FX2` baked=0（TiXL 是 `FX1.x+FX1.y·f` ramp）、`p.Color` baked 白。

### 2. LinePoints（生成）— 🔴 MAJOR_GAP
缺 USED port：`F1`(v2,1,0) `F2`(v2,1,0) `ColorA`(v4,1,1,1,1) `ColorB`(v4,1,1,1,1) `Orientation`(enum[UsingUpVector,Simple],**1**) `Twist`(f,0) `OrientationAxis`(v3,0,0,1) `OrientationAngle`(f,0) `AddSeparator`(bool,false)
- DEFAULT 錯：`Count` 64 vs **100**；`Length` 5.0 vs **1.0**；`Direction` ours=(0,1,0) vs tixl=**(1,0,0)**。
- `W`/`WOffset`：TiXL shader commented-out 死碼 → 我們省略**正確**，不補。
- CHAIN：TiXL shader 寫 Color(lerp A→B)/FX1/FX2/Rotation 全餵 port；我們烤死。

### 3. GridPoints（生成）— 🔴 MAJOR_GAP
缺 USED port：`Tiling`(enum[Cartesian,Triangular,HoneyCombs,Diagonal],0) `F1`(f,1) `F2`(f,1) `Color`(v4,1,1,1,1) `OrientationAxis`(v3,1,0,0) `OrientationAngle`(f,0)
- `Scale`(f,0.1)=UNUSED（TiXL 用 child node 乘 Size，kernel 沒讀）、`W`(f,1)=UNKNOWN（不在 kernel）→ 這兩個非 shader 參數，低優先。
- TYPE 錯：`CountX/Y/Z` ours=Float vs tixl=**Int**；DEFAULT 錯：`CountZ` ours=1 vs tixl=**10**。
- `Count`(我們的 host-only 容量 port)=EXTRA，TiXL 無，保留。

### 4. SpherePoints（生成）— 🟡 port 全對、default 漂
port 集合/type/enum 全對齊。**只有 default 錯**：`Count` ours=2048 vs tixl=**100**；`Radius` ours=2.0 vs tixl=**1.0**。
- TiXL `Count` 是 Int + ClampInt(min=2)，我們 Float 無 clamp（shader 用 GetDimensions 拿真 count，不破壞行為，但語意漂）。
- FX1/FX2 TiXL 自己也硬碼=1（沒 expose port）→ 我們不缺。

### 5. TransformPoints（變換）— 🟡 缺 1 USED + Shearing UNKNOWN
缺 USED port：`StrengthFactor`(enum[None,F1,F2],0) — shader `Strength * (==0?1 : ==1?p.FX1 : p.FX2)`，**真讀 FX1/FX2，真缺口**。
- `Shearing`(v3,0)=UNKNOWN（餵 TiXL `TransformMatrix` 子 op，不在本 .hlsl）→ 看我們 Metal 變換矩陣有沒有 shear。
- `UpdateRotation`/`ScaleW`/`OffsetW`/`WIsWeight`=UNUSED（TiXL cbuffer 全 commented-out）→ 省略無害。
- DEFAULT 錯：`Space` ours=0(Point) vs tixl=**1(Object)**。

### 6. OrientPoints（變換）— 🟢 近忠實
port/default/enum 全對齊。唯一缺：`WIsWeight`(bool,false) — TiXL cbuffer `UseWAsWeight` 有宣告但 body 沒見用（半死碼，UNKNOWN）。
- 註：TiXL enum[0] 原始碼 typo `"LootAtTarget"`，我們拼對成 `LookAtTarget`——index 一致，**保持我們的正確拼法，不要為對齊抄錯字**。

### 7. RandomizePoints（modify）— 🟡 port 全對、3 default 漂
port 集合/type/enum 全對齊。**3 個 default 錯**：
`OffsetMode` ours=0(Add) vs tixl=**1(Scatter)**；`Interpolation` ours=1(Linear) vs tixl=**2(Smooth)**；`ClampColorsEtc` ours=false vs tixl=**true**。
- enum label drift：`Space` ours=[Point,Object] vs tixl=[PointSpace,ObjectSpace]（index 同，純顯示）。

### 8. SetPointAttributes（modify）— 🟡 命名 + 3 default 漂
**命名**：我們 `SetStretch`/`Stretch` → TiXL `.cs` 權威名是 `SetExtend`/`Extend`（HLSL 內部才叫 Stretch；對使用者/`.t3` 的名是 Extend）。正名回 Extend。
- DEFAULT 錯：`RotationAxis` ours=(0,1,0) vs tixl=**(0,0,1)**；`Fx1` ours=0 vs tixl=**1.0**；`Fx2` ours=0 vs tixl=**1.0**。
- CHAIN：這顆同時**寫** FX1/FX2（SetFx1/SetFx2 gate）又**讀** FX1/FX2（AmountFactor）。

### 9. CombineBuffers（combine）— 🟡 結構簡化
TiXL = 1 個 `MultiInputSlot`（動態 N 路，無上限）；我們 = 固定 input0..3（4 路上限）。語意等價（純 concat 串接，count=Σ inputs）。4 路夠用，要 5+ 路時得改成動態 port 列表（別硬加 input4/5）。TiXL 有 stride 校驗+skip warning，我們 CPU blit 無（SwPoint 固定 stride，暫不追）。

### 10. AddNoise（modify）— 🟢 近忠實（batch 15）
TiXL parity: `Operators/Lib/point/modify/AddNoise.cs` + `Assets/shaders/points/modify/AddNoise.hlsl`
port 集合 / type / enum 全對齊（.cs 對照）：Points / Strength / StrengthFactor(enum None/F1/F2) / Frequency / Phase / Variation / AmountDistribution(v3) / RotationLookupDistance / NoiseOffset(v3)。
defaults 全對（.t3 GUID 對照）：Strength=1.0 Frequency=1.0 Phase=0.0 Variation=0.0 AmountDistribution=(1,1,1) RotationLookupDistance=0.25 NoiseOffset=(0,0,0) StrengthFactor=0(None)。
**Fork（具名）：** MSL `float3x3(ex,ey,ez)` 是 column-major（三個 cols）；HLSL `float3x3(ex,ey,ez)` 是 row-major（三個 rows）+ `transpose()`；兩者等價，不需要在 MSL 做 transpose。addnoise.metal:93-96 行有 comment 說明。
牙：`--selftest-addnoise` + `--selftest-addnoise-bug`（PASS/FAIL 已驗）。

### 11. FilterPoints（modify）— 🟢 近忠實（batch 15）
TiXL parity: `Operators/Lib/point/modify/FilterPoints.cs` + `Assets/shaders/points/generate/FilterPoints.hlsl`
port 集合 / type 全對齊：Points / Count(Int→見 fork) / StartIndex / Step / ScatterSelect / Seed。
defaults 全對（.t3 GUID 對照）：Count=1 StartIndex=0 Step=1.0 ScatterSelect=0.0 Seed=0。
**Fork（具名）：** TiXL Count 是 `InputSlot<int>` 通過 `ClampInt(0,1000000)` 再進 shader。我們 Count 是 Float port（resolved-param 脊椎合約要求）；`cookFilterPoints` cast 成 `int32_t` 傳 shader，範圍由 NodeSpec max=8192 限制（不是 TiXL 的 1000000）。語意等價，最大值偏保守。
牙：`--selftest-filterpoints` + `--selftest-filterpoints-bug`（PASS/FAIL 已驗）。

---

## 建議施工批次（給柏為決策）

**批 A — default / 命名修正（葉檔內小改，零 shader，低風險高價值）**
SpherePoints(Count→100,Radius→1) · RandomizePoints(OffsetMode→1,Interp→2,Clamp→true) · SetPointAttributes(正名 Extend,RotationAxis→(0,0,1),Fx1/Fx2→1) · LinePoints(Count→100,Length→1,Direction→(1,0,0)) · TransformPoints(Space→1) · RadialPoints(Count→100,Radius→1) · GridPoints(CountZ→10)。
→ 改 `node_registry.cpp` default 值 + enum label，不動 kernel。一輪可清完。

**批 B — 補真 USED 缺口（要動 shader + NodeSpec + selftest 驗 attribute）**
生成器寫屬性：Radial/Line/Grid 的 `F1/F2/Color/Orientation(Axis/Angle)`、Grid 的 `Tiling`(4 佈局) → 解鏈一（下游 F1/F2 factor）。
變換：TransformPoints `StrengthFactor`、OrientPoints `WIsWeight`。
→ 每顆是葉檔 fan-out，但 selftest 要加驗「FX1/FX2/Color/Rotation 真寫進點」（現有 selftest 只驗 position，才會漏掉）。

**批 C — 型別 / 結構（低優先，不影響當下手感）**
GridPoints CountX/Y/Z Float→Int · CombineBuffers 4 路→動態 MultiInput · enum label 正名（PointSpace/ObjectSpace）。
