# Census: numbers/ (236 ops, 13 obsolete → 223 active)

> 分析方法：桶分類法（grep 關鍵字分桶 → 每桶取代表確認 → 批次標分類）。
> _obsolete/ 下 13 顆（Counter/_Jitter/_Jitter2d/_AnimValueOld/__ObsoletePulse/__ObsoletePulsate/_Time_old/ExportPointList/GetIteratedFloat/GetIteratedVec3/GetIteration/IterateList/Iterator）統一標 SKIP，不計入開採 backlog。

---

## 一、TRIVIAL 大桶（value-graph，純算術 / 布林 / 向量 / 整數）

這 **131 顆** op 全部純值運算，踩 `value-graph`，無 shader、無 buffer 輸出、無時間狀態依賴。

### float/（43 顆）
`Abs` `Ceil` `Clamp` `Floor` `InvertFloat` `Remap` `Round` `Sigmoid`
`Add` `Div` `Log` `Modulo` `Multiply` `Pow` `Sqrt` `Sub` `Sum`
`Compare` `HasValueChanged` `HasValueIncreased` `IsGreater` `PickFloat` `TryParse` `ValueToRate`
`BlendValues` `Damp` `DampAngle` `DetectPulse` `Ease` `EaseKeys` `FreezeValue` `HasValueDecreased` `Lerp` `PeakLevel` `RemapValues` `SmoothStep` `Spring`
`FloatHash` `PerlinNoise` `Random`
`Atan2` `Cos` `Sin`

| op | 狀態 | 風險 |
|----|------|------|
| 以上 43 顆 | TRIVIAL | R1 |

### bool/（13 顆）
`All` `And` `Any` `Or`
`BoolToFloat` `BoolToInt`
`FlipBool` `FlipFlop` `HasBooleanChanged` `Not` `PickBool` `ToggleBoolean` `Xor`
`CacheBoolean` `DelayBoolean` `DelayTriggerChange` `KeepBoolean`

> Note: `Trigger` `WasTrigger` 同屬此桶（見下方 Trigger 桶說明）。

| op | 狀態 | 風險 |
|----|------|------|
| 以上 13 顆 + Trigger + WasTrigger | TRIVIAL | R1 |

### int/（18 顆）
`AddInts` `IntAdd` `IntDiv` `IntToFloat` `ModInt` `MultiplyInt` `MultiplyInts` `SubInts` `SumInts`
`CompareInt` `CountInt` `HasIntChanged` `IsIntEven` `PickInt`
`ClampInt` `FloatToInt` `GetAPrime` `KeepInts` `MaxInt` `MinInt` `TryParseInt`

| op | 狀態 | 風險 |
|----|------|------|
| 以上 21 顆（含 ClampInt/FloatToInt/GetAPrime/KeepInts/MaxInt/MinInt/TryParseInt） | TRIVIAL | R1 |

### int2/（5 顆）
`AddInt2` `Int2Components` `MakeResolution` `MaxInt2` `ScaleResolution` `ScaleSize`

| op | 狀態 | 風險 |
|----|------|------|
| 以上 6 顆 | TRIVIAL | R1 |

### ints/（5 顆）
`IntListLength` `IntsToList` `MergeIntLists` `PickIntFromList` `SetIntListValue`

| op | 狀態 | 風險 |
|----|------|------|
| 以上 5 顆 | TRIVIAL | R1 |

### vec2/（16 顆）
`AddVec2` `DampVec2` `DivideVector2` `DotVec2` `GridPosition` `HasVec2Changed` `Int2ToVector2`
`PadVec2Range` `PerlinNoise2` `PickVector2` `RemapVec2` `ScaleVector2` `Vec2ToVec3` `Vector2Components`
`EaseVec2` `EaseVec2Keys` `SpringVec2`

| op | 狀態 | 風險 |
|----|------|------|
| 以上 17 顆 | TRIVIAL | R1 |

### vec3/（23 顆）
`AddVec3` `BlendVector3` `CrossVec3` `DampVec3` `DotVec3` `EulerToAxisAngle` `HasVec3Changed`
`LerpVec3` `Magnitude` `NormalizeVector3` `PerlinNoise3` `PickVector3` `RotateVector3` `RoundVec3`
`ScaleVector3` `SubVec3` `TransformVec3` `Vec2Magnitude` `Vec3Distance` `Vector3Components`
`EaseVec3` `EaseVec3Keys` `SpringVec3`

> `MulMatrix`: 輸入/輸出純 vec3+Matrix4x4，仍屬 value-graph。`Vector3Gizmo`: 實作 ITransformable，用 TransformCallback，但輸出仍 Vector3，value-graph + 編輯器 gizmo callback，不需新 seam。

| op | 狀態 | 風險 |
|----|------|------|
| 以上 23 顆 + MulMatrix + Vector3Gizmo | TRIVIAL | R1 |

### vec4/（4 顆）
`DotVec4` `PickColor` `RgbaToColor` `Vector4Components`

| op | 狀態 | 風險 |
|----|------|------|
| 以上 4 顆 | TRIVIAL | R1 |

---

## 二、transport 桶（anim/time — 讀播放頭 / 設播放頭）

這些踩 `transport` seam（simple_world 已建成）。

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| Time | 輸出 fxTime/bars/bpm | transport | READY-LEAF | R1 | 讀 context.LocalFxTime |
| ClipTime | 輸出 context.LocalTime | transport | READY-LEAF | R1 | |
| RunTime | 輸出 app 執行秒數 | transport | READY-LEAF | R1 | Playback.RunTimeInSecs |
| GetFrameSpeedFactor | 取當前速度因子 | transport | READY-LEAF | R1 | |
| LastFrameDuration | 上一幀耗時 | transport | READY-LEAF | R1 | 輕度 temporal-state |
| HasTimeChanged | 時間是否改變 | transport | READY-LEAF | R1 | |
| ConvertTime | beats↔secs 換算 | transport | READY-LEAF | R1 | |
| DateTimeInSecs | 系統時間轉秒 | transport | READY-LEAF | R1 | |
| SetPlaybackTime | 設播放頭位置 | transport | READY-LEAF | R2 | 輸出 Command slot，需 transport 控制 |
| SetPlaybackSpeed | 設播放速率 | transport | READY-LEAF | R2 | 同上 |
| SetTime | 設時間(alias) | transport | READY-LEAF | R2 | |
| StopWatch | 計時器（累積時間差） | transport | READY-LEAF | R2 | frame-level 狀態，但不需新 seam |

---

## 三、anim/animators 桶（Curve anim，踩 transport + value-graph）

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| AnimValue | 波形動畫（曲線形狀×時間） | transport | READY-LEAF | R2 | AnimMath.Shapes；SpeedFactor context |
| AnimVec2 | Vec2 分量動畫 | transport | READY-LEAF | R2 | |
| AnimVec3 | Vec3 分量動畫 | transport | READY-LEAF | R2 | |
| AnimInt | Int 動畫 | transport | READY-LEAF | R1 | |
| AnimBoolean | Bool 動畫 | transport | READY-LEAF | R1 | |
| AnimFloatList | FloatList 動畫 | transport | READY-LEAF | R2 | |
| OscillateVec2 | Vec2 震盪 | transport | READY-LEAF | R1 | |
| OscillateVec3 | Vec3 震盪 | transport | READY-LEAF | R1 | |
| SequenceAnim | 步進序列動畫（自訂 UI） | transport | READY-LEAF | R2 | 有自訂 UI 邏輯，但輸出純 float/bool |
| TriggerAnim | 觸發後播曲線 | transport | READY-LEAF | R2 | 內含 frame-level state |
| AdsrEnvelope | ADSR 包絡調製 | transport | READY-LEAF | R2 | 2025 新增；AudioAnalysis import 僅用 LocalFxTime |

---

## 四、anim/utils 桶（keyframe 編輯工具）

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| SetKeyframes | 程序化設置 keyframe | NEW-SEAM:keyframe-edit | BLOCKED:keyframe-edit | R3 | 讀寫 Instance 的 Curve keyframe，需 animation-system 編輯 API |
| FindKeyframes | 查詢 keyframe 值/時間 | NEW-SEAM:keyframe-edit | BLOCKED:keyframe-edit | R3 | 同上，需 animation-system 讀取 |

---

## 五、anim/vj 桶（VJ 節拍/速度控制）

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| GetBpm | 讀取當前 BPM | transport | READY-LEAF | R1 | Playback.Bpm |
| SetBpm | 設置 BPM（輸出 Command） | transport | READY-LEAF | R2 | Command slot |
| ForwardBeatTaps | 轉發 beat-tap 觸發 | transport | READY-LEAF | R2 | TapProvider singleton，Command subtree |
| SetSpeedFactors | 設子節點速度因子（輸出 Command + Texture2D） | Layer2d+Execute | BLOCKED:Layer2d+Execute | R3 | 輸出 Command+Texture2D 同時；內部無 shader 邏輯，但 Command slot 需 Execute seam |
| AbletonLinkSync | Ableton Link UDP 同步節拍 | NEW-SEAM:ableton-link | BLOCKED:ableton-link | R3 | DllImport AbletonLinkDLL（Windows-only native DLL）；macOS 需獨立 native 層 |

---

## 六、floats/ 桶（float list 處理）

多數純值 list 操作，踩 value-graph。少數有 Texture2D 輸出。

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| FloatListLength | float list 長度 | value-graph | TRIVIAL | R1 | |
| FloatsToList | 多 float → list | value-graph | TRIVIAL | R1 | |
| ColorsToList | 多 color → float list | value-graph | TRIVIAL | R1 | |
| SetFloatListValue | 設置 list 指定位置 | value-graph | TRIVIAL | R1 | |
| FloatListToIntList | 型別轉換 | value-graph | TRIVIAL | R1 | |
| IntListToFloatList | 型別轉換 | value-graph | TRIVIAL | R1 | |
| ComposeVec3FromList | 從 list 組 Vec3 | value-graph | TRIVIAL | R1 | |
| PickFloatFromList | 從 list 取值 | value-graph | TRIVIAL | R1 | |
| PickFloatList | 條件選 list | value-graph | TRIVIAL | R1 | |
| AmplifyValues | 放大 list 值 | value-graph | TRIVIAL | R1 | |
| AnalyzeFloatList | 分析 list 統計 | value-graph | TRIVIAL | R1 | |
| ColorListToInts | color list → int list | value-graph | TRIVIAL | R1 | |
| CombineFloatLists | 合併多個 list | value-graph | TRIVIAL | R1 | |
| CompareFloatLists | 逐元素比較 | value-graph | TRIVIAL | R1 | |
| DampFloatList | 整個 list 阻尼平滑 | value-graph | TRIVIAL | R2 | frame-level state，但仍純值 |
| DampPeakDecay | peak+decay 阻尼 | value-graph | TRIVIAL | R2 | frame-level state |
| DeltaSinceLastFrame | 幀間差值 | value-graph | TRIVIAL | R2 | frame-level state |
| KeepFloatValues | 保持 list 上一值 | value-graph | TRIVIAL | R2 | frame-level state |
| MergeFloatLists | 合併（不同於 Combine） | value-graph | TRIVIAL | R1 | |
| RemapFloatList | remap list | value-graph | TRIVIAL | R1 | |
| SmoothValues | 平滑 list | value-graph | TRIVIAL | R2 | |
| SumRange | 區間加總 | value-graph | TRIVIAL | R1 | |
| DefineIqGradient | IQ 公式生成 Gradient | value-graph | READY-LEAF | R1 | 輸出 Gradient 型別（value-graph 已有 Gradient） |
| ValuesToTexture | float list → R32 Texture2D | NEW-SEAM:cpu-upload-texture | BLOCKED:cpu-upload-texture | R2 | CPU 上傳 float → Texture2D；需 MTLTexture CPU-write 路徑 |
| ValuesToTexture2 | float list → R32 Texture2D（加 remap） | NEW-SEAM:cpu-upload-texture | BLOCKED:cpu-upload-texture | R2 | 同上；ValuesToTexture 的 remap 變體 |
| PlaybackFFT | 音訊 FFT 分析頻帶 → float list | NEW-SEAM:audio-analysis | BLOCKED:audio-analysis | R3 | 讀 AudioAnalysis.FftGainBuffer 等；需 audio 分析子系統 |

---

## 七、color/ 桶（顏色 / 漸層操作）

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| BlendColors | 兩色 lerp | value-graph | TRIVIAL | R1 | |
| HSBToColor | HSB→RGBA | value-graph | TRIVIAL | R1 | |
| HSLToColor | HSL→RGBA | value-graph | TRIVIAL | R1 | |
| OKLChToColor | OKLCh→RGBA | value-graph | TRIVIAL | R1 | |
| RgbaToColor | float4→Color | value-graph | TRIVIAL | R1 | （在 vec4/ 下） |
| PickColorFromList | 從 list 取 color | value-graph | TRIVIAL | R1 | |
| CombineColorLists | 合併 color list | value-graph | TRIVIAL | R1 | |
| KeepColors | 保持上一幀 color | value-graph | TRIVIAL | R2 | frame-level state |
| DefineGradient | 定義最多 4 色的 Gradient | value-graph | READY-LEAF | R1 | 輸出 Gradient 型別 |
| BuildGradient | 從 color list + position list 建 Gradient | value-graph | READY-LEAF | R1 | |
| BlendGradients | 兩 Gradient lerp | value-graph | READY-LEAF | R1 | |
| PickGradient | 條件選 Gradient | value-graph | READY-LEAF | R1 | |
| SampleGradient | 取樣 Gradient 某位置顏色 | value-graph | READY-LEAF | R1 | |
| PickColorFromImage | 從 Texture2D CPU readback 取像素色 | NEW-SEAM:cpu-readback-texture | BLOCKED:cpu-readback-texture | R2 | CopyResource+MapSubresource；需 GPU→CPU readback 路徑（Metal 有，但需平台封裝） |
| GradientsToTexture | Gradient list → RGBA32F Texture2D | NEW-SEAM:cpu-upload-texture | BLOCKED:cpu-upload-texture | R2 | 同 ValuesToTexture，CPU 上傳 RGBA float array |

---

## 八、curve/ 桶（Curve 取樣 / 轉貼圖）

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| SampleCurve | 給定 t 取樣 Curve | value-graph | READY-LEAF | R1 | 純 CPU Curve 取樣 |
| CurvesToTexture | Curve list → R32/RGBA32F Texture2D | NEW-SEAM:cpu-upload-texture | BLOCKED:cpu-upload-texture | R2 | CPU 上傳 curve samples |

---

## 九、int/ buffer 桶（整數 → GPU buffer）

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| IntsToBuffer | 多 int 參數 → constant buffer (Buffer) | NEW-SEAM:int-cbuffer | BLOCKED:int-cbuffer | R2 | 輸出 `Buffer`（constant buffer）而非 Texture；動態常數緩衝區上傳 |
| IntListToBuffer | int list → StructuredBuffer（含 SRV+UAV） | RWStructuredBuffer | BLOCKED:RWStructuredBuffer | R2 | 使用 `StructuredList<int>` + SetupStructuredBuffer + CreateStructuredBufferSrv/Uav |
| RandomChoiceIndex | 隨機選整數（有 frame state） | value-graph | TRIVIAL | R1 | 純雜湊，無 buffer |

---

## 十、data/utils 桶（StructuredList / Dict 工具）

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| GetListItemAttribute | 從 StructuredList 反射取 float 屬性 | NEW-SEAM:structured-list-cpu | BLOCKED:structured-list-cpu | R2 | 踩 StructuredList 型別 + Reflection |
| GetPointDataFromList | 從 StructuredList<Point> 取位置/W/orientation | NEW-SEAM:structured-list-cpu | BLOCKED:structured-list-cpu | R2 | 踩 Point struct；StructuredList 讀取 |
| JoinLists | 合併多個 StructuredList | NEW-SEAM:structured-list-cpu | BLOCKED:structured-list-cpu | R2 | StructuredList.Join；泛型 clone |
| SelectFloatFromDict | 從 Dict<float> 用字串 key 取值 | NEW-SEAM:dict-context | BLOCKED:dict-context | R2 | 踩 Dict<T> + ICustomDropdownHolder + IStatusProvider |
| SelectVec2FromDict | Dict<Vec2> key 取值 | NEW-SEAM:dict-context | BLOCKED:dict-context | R2 | 同上 |
| SelectVec3FromDict | Dict<Vec3> key 取值 | NEW-SEAM:dict-context | BLOCKED:dict-context | R2 | 同上 |
| SelectBoolFromFloatDict | Dict<float> 閾值 → bool | NEW-SEAM:dict-context | BLOCKED:dict-context | R2 | 同上 |

---

## 摘要

- **總 active op 數**：223（236 - 13 obsolete）
- **TRIVIAL**：約 131 顆（float/bool/int/int2/ints/vec2/vec3/vec4 純算術 + 純值 floats list 操作）
- **READY-LEAF**：約 35 顆（transport/anim animators/vj transport、color Gradient ops、curve SampleCurve、DefineIqGradient/DefineGradient/BuildGradient 等）
- **BLOCKED 分佈**：

| seam | 擋住的 op 數 | 說明 |
|------|------------|------|
| `value-graph`（已建） | 131 | TRIVIAL 全可開採 |
| `transport`（已建） | ~19 | READY-LEAF |
| `NEW-SEAM:cpu-upload-texture` | 4 | ValuesToTexture / ValuesToTexture2 / GradientsToTexture / CurvesToTexture — CPU float array → MTLTexture |
| `NEW-SEAM:cpu-readback-texture` | 1 | PickColorFromImage — CopyResource+Map → CPU readback |
| `NEW-SEAM:structured-list-cpu` | 3 | GetListItemAttribute / GetPointDataFromList / JoinLists — StructuredList CPU-side 操作 |
| `NEW-SEAM:dict-context` | 4 | SelectFloatFromDict / SelectVec2FromDict / SelectVec3FromDict / SelectBoolFromFloatDict — Dict<T> context 子系統 |
| `NEW-SEAM:keyframe-edit` | 2 | SetKeyframes / FindKeyframes — 程序化讀寫 Curve keyframe |
| `NEW-SEAM:ableton-link` | 1 | AbletonLinkSync — macOS 需 native Ableton Link DLL |
| `NEW-SEAM:int-cbuffer` | 1 | IntsToBuffer — dynamic constant buffer |
| `RWStructuredBuffer`（已知未建） | 1 | IntListToBuffer — StructuredBuffer SRV+UAV |
| `Layer2d+Execute`（已知未建） | 1 | SetSpeedFactors — 輸出 Command slot |
| `NEW-SEAM:audio-analysis` | 1 | PlaybackFFT — FFT 頻帶分析子系統 |

- **冒出的 NEW-SEAM 清單**：
  - `cpu-upload-texture` — CPU float/RGBA array 批次上傳至 MTLTexture（R32Float 或 RGBA32Float），不走 shader。共 4 顆阻擋。**投資小，解鎖 ValuesToTexture/GradientsToTexture/CurvesToTexture，可作 audio-vis 視覺工具。**
  - `cpu-readback-texture` — GPU Texture → CPU MapSubresource 讀回像素。共 1 顆（PickColorFromImage）。Metal 有對應 API，需平台封裝。
  - `structured-list-cpu` — StructuredList<T> CPU 端讀取/合併（含 Reflection）。共 3 顆。此 seam 在 point/ 類別的 op 也會出現，建議統一命名。
  - `dict-context` — Dict<T>（TiXL 自定義字典型別）+ context 子系統 + ICustomDropdownHolder。共 4 顆。相對小眾，先排低。
  - `keyframe-edit` — 程序化存取並修改 Instance.Parameter 的 Curve 資料。共 2 顆。需 animation-system 暴露 keyframe API，投資大。
  - `ableton-link` — macOS 需 Ableton Link native library（開源但需編譯 mac dylib）。共 1 顆，低優先。
  - `int-cbuffer` — dynamic constant buffer 寫入整數陣列（Metal MTLBuffer）。共 1 顆，較小。
  - `audio-analysis` — 即時 FFT 頻帶分析（AudioAnalysis 子系統）。共 1 顆（PlaybackFFT），屬音訊基礎建設。

- **意外/盲區**：
  1. `SetSpeedFactors` 同時輸出 `Command` 和 `Texture2D` 但內部沒有任何實作（.cs 裡只有欄位定義，無 Update logic）——疑似 TiXL WIP/skeleton stub，暫標 BLOCKED:Layer2d+Execute。
  2. `AdsrEnvelope` 是 simple_world 自行新增的 op（Guid 格式 UUID 不符 TiXL 風格，且 AdsrCalculator 來自 `T3.Core.Audio`），已在 numbers/anim 下，符合 READY-LEAF（只踩 transport + value-graph），但須確認 simple_world 已有 AdsrCalculator。
  3. `floats/process/DefineIqGradient` 輸出 `Gradient` 型別：simple_world 需確認 Gradient 型別已實作（為 color/ 系列 op 的基礎）。
  4. `Vector3Gizmo` 實作 `ITransformable`，依賴 editor-side `TransformCallback`；若無 gizmo render 子系統，此 op 退化為 identity（直通 Position）。功能損失可接受，暫標 READY-LEAF。
  5. `SequenceAnim` 有自訂 UI 邏輯（`SetStepValue`、公開 `CurrentSequence` 供 inspector 讀取），純數值輸出 float/bool，但 UI 編輯體驗需要對應 inspector panel，否則只能用預設字串參數驅動。
