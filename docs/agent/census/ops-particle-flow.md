# Census: particle/ + flow/ (54 ops)

---

## particle/ (20 ops: 1 root + 19 force)

### Already ported to simple_world (confirmed in node_registry_particle.cpp)
ParticleSystem, TurbulenceForce, DirectionalForce, VectorFieldForce (fork: ShaderGraphNode baked),
VelocityForce, AxisStepForce, SnapToAnglesForce — 7 ops shipped.

### particle/ op table

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| ParticleSystem | GPU 粒子 emit/cycle/integrate | particle-system | READY-LEAF | R2 | 已 ported；accepts BufferWithViews emit + multi ParticleForce |
| TurbulenceForce | curl-noise 渦流力 | particle-system | READY-LEAF | R2 | 已 ported；ShaderGraphNode ValueField 輸入已 fork-baked |
| DirectionalForce | 常數方向推力 + 隨機抖動 | particle-system | READY-LEAF | R1 | 已 ported |
| VelocityForce | 重新縮放粒子速度大小 | particle-system | READY-LEAF | R1 | 已 ported |
| AxisStepForce | 隨機挑軸向步進速度 | particle-system | READY-LEAF | R2 | 已 ported；ApplyTrigger 布林邏輯 |
| SnapToAnglesForce | 量化速度方向到離散角度 | particle-system | READY-LEAF | R2 | 已 ported；fork snapangles-camera(identity矩陣) |
| VectorFieldForce | 從 ShaderGraphNode 向量場採樣推力 | particle-system | READY-LEAF | R2 | 已 ported；fork-VFF: field baked=(1,1,1) |
| CollisionForce | 粒子間碰撞 + spatial hash grid | NEW-SEAM:spatial-hash-grid | BLOCKED:spatial-hash-grid | R3 | t3ui 有 2×ComputeShader+ComputeShaderStage；需 GPU cell 格點結構 |
| CustomForce | 使用者自訂 GLSL/ShaderGraphNode 力場 | NEW-SEAM:shader-graph | BLOCKED:shader-graph | R3 | 輸入 ShaderGraphNode Field + ComputeShaderFromSource；動態編譯 field |
| FieldDistanceForce | SDF field 距離梯度力 | NEW-SEAM:shader-graph | BLOCKED:shader-graph | R3 | InputSlot<ShaderGraphNode> Field；需 field-graph SDF 求值 |
| FieldVolumeForce | SDF field 體積內外力（含彈跳） | NEW-SEAM:shader-graph | BLOCKED:shader-graph | R3 | InputSlot<ShaderGraphNode> Field；碰撞反彈邏輯 |
| FollowMeshSurfaceForce | 投影粒子到 mesh 表面 | NEW-SEAM:mesh-pipeline | BLOCKED:mesh-pipeline | R3 | InputSlot<MeshBuffers>；需 mesh 幾何管線 |
| MeshVolumeForce | mesh 體積內部排斥/吸引力 | NEW-SEAM:mesh-pipeline | BLOCKED:mesh-pipeline | R3 | InputSlot<MeshBuffers>；兩 compute pass |
| RandomJumpForce | 隨機方向跳躍速度（可 field gate） | NEW-SEAM:shader-graph | BLOCKED:shader-graph | R2 | InputSlot<ShaderGraphNode> ValueField 可選；無 field 時可退化為常數 |
| ReconstructiveForce | 把粒子拉向 TargetPoints 點集 | particle-system | BLOCKED:RWStructuredBuffer | R3 | InputSlot<BufferWithViews> TargetPoints；ITransformable；TransformCallback；多 ComputeShaderStage；需 BufferWithViews readback |
| SnapToAnglesForce | (已列) | — | — | — | — |
| SwitchParticleForce | index 選擇多個 force 之一 | particle-system | READY-LEAF | R1 | 純邏輯 MultiInputSlot<ParticleSystem> 選一；無 shader；小 util |
| TextureMapForce | 用 Texture2D 法向量圖引導粒子 | particle-system + multi-image | BLOCKED:multi-image | R2 | InputSlot<Texture2D> SignedNormalMap；需 texture 傳入 particle compute |
| TurbulenceForce | (已列) | — | — | — | — |
| VelocityForce | (已列) | — | — | — | — |
| VolumeForce | 球/立方體/橢球體積內力（ITransformable） | particle-system | READY-LEAF | R2 | 純參數幾何；無 MeshBuffers；ComputeShader+Stage；math 直白；ITransformable=gizmo 可略 |
| VerletRibbonForce | Verlet 積分鏈條約束（ribbon） | particle-system + RWStructuredBuffer | BLOCKED:RWStructuredBuffer | R3 | InputSlot<BufferWithViews> ReferencePoints；3×ComputeShaderStage；constraint 多 pass |

> 注意：particle/ 頂層只有 ParticleSystem 一顆 op（root），force/ 子目錄 19 顆。合計 20。重複 op 已從表中去除（TurbulenceForce/VelocityForce/SnapToAnglesForce 各只列一次）。最終表共 18 獨立 op。

### 摘要（particle/）

- 總 op 數：20（ParticleSystem × 1 + force/ × 19）
- READY-LEAF：8（ParticleSystem, TurbulenceForce, DirectionalForce, VelocityForce, AxisStepForce, SnapToAnglesForce, VectorFieldForce, SwitchParticleForce, VolumeForce）— 其中 7 顆已 ported
- 未 ported READY-LEAF（可直接開採）：**SwitchParticleForce, VolumeForce**（2 顆）
- BLOCKED 分佈：
  - `NEW-SEAM:shader-graph`：4 顆（CustomForce, FieldDistanceForce, FieldVolumeForce, RandomJumpForce）
  - `NEW-SEAM:mesh-pipeline`：2 顆（FollowMeshSurfaceForce, MeshVolumeForce）
  - `NEW-SEAM:spatial-hash-grid`：1 顆（CollisionForce）
  - `RWStructuredBuffer`：2 顆（ReconstructiveForce, VerletRibbonForce）
  - `multi-image（texture bind to compute）`：1 顆（TextureMapForce）
- NEW-SEAM 清單：
  - `shader-graph` — ShaderGraphNode 動態 field/HLSL；需 SDF 求值或 ComputeShaderFromSource 動態編譯 pipeline
  - `spatial-hash-grid` — GPU 粒子碰撞格點結構（多 compute pass build + query）
  - `mesh-pipeline` — MeshBuffers 幾何輸入（頂點/面索引 buffer）傳入 particle compute
- 意外發現：VectorFieldForce 在 simple_world 已 ported，但其 TiXL 原版需 ShaderGraphNode（已 fork-baked）；RandomJumpForce 的 ShaderGraphNode 為可選輸入，無 field 接線時退化為常數，未來可做半-faithful port。

---

## flow/ (34 ops: 10 root + 17 context + 7 skillQuest)

### 分桶依據
- **Command 類**：輸入輸出 `Slot<Command>` / `MultiInputSlot<Command>` — 全數需要 `Layer2d+Execute` seam
- **context-var 類**：Get/Set *Type*Var — 踩 `NEW-SEAM:context-var`（eval-context 上的 typed 字典）
- **skillQuest 類**：TiXL 內建教學測驗系統，非產品 op — 標 `NEW-SEAM:skillquest-system`

### flow/ op table

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| Execute | 收集多 Command 依序執行 | Layer2d+Execute | BLOCKED:Layer2d+Execute | R2 | 核心 Command dispatcher；IsEnabled gate |
| ExecRepeatedly | 重複執行 Command N 次（跳幀控制） | Layer2d+Execute | BLOCKED:Layer2d+Execute | R2 | RepeatCount / SkipFrameCount；時間相干風險 |
| ExecuteOnce | Trigger dirty 時執行一次 Command | Layer2d+Execute | BLOCKED:Layer2d+Execute | R2 | Slot<bool> OutputTrigger；trigger-dirty 邏輯 |
| LoadSoundtrack | 掛音訊 soundtrack 到 Command 鏈 | Layer2d+Execute + transport | BLOCKED:Layer2d+Execute | R2 | 同 Execute 框架 + 需音訊 seam；IsEnabled |
| LogMessage | 把 SubGraph Command 執行並 log | Layer2d+Execute | BLOCKED:Layer2d+Execute | R1 | debug util；OnlyOnChanges gate |
| Loop | index/progress context-var 注入的迴圈執行 | Layer2d+Execute + NEW-SEAM:context-var | BLOCKED:Layer2d+Execute | R3 | 寫 FloatVariables/IntVariables；InvalidateGraph 每 iter；複雜失效模型 |
| Once | 輸出 bool trigger（dirty 邊緣觸發） | NEW-SEAM:trigger-dirty | BLOCKED:NEW-SEAM:trigger-dirty | R2 | 純 trigger 邊緣偵測，無 Command；DirtyFlag 機制 |
| ResetSubtreeTrigger | Trigger 時 invalidate 子樹並重置 | Layer2d+Execute + NEW-SEAM:trigger-dirty | BLOCKED:Layer2d+Execute | R2 | 強制 invalidate Command 子樹 |
| Switch | index 選擇 Command 分支輸出 | Layer2d+Execute | BLOCKED:Layer2d+Execute | R2 | MultiInputSlot<Command>；Count 輸出 |
| TimeClip | 把子 Command 對應到 timeline 時間段 | Layer2d+Execute + transport | BLOCKED:Layer2d+Execute | R3 | IPreventingTimeRemap；TimeClipSlot；_normalizedTime context-var |
| BlendScenes | alpha 混融兩個 Command 場景 | Layer2d+Execute | BLOCKED:Layer2d+Execute | R3 | MultiInputSlot<Command> + BlendFraction；需 blended render-target |
| GetBoolVar | 從 eval-context 讀 bool 變數 | NEW-SEAM:context-var | BLOCKED:NEW-SEAM:context-var | R1 | context.BoolVariables 字典（TiXL）；ICustomDropdownHolder |
| GetFloatVar | 從 eval-context 讀 float 變數 | NEW-SEAM:context-var | BLOCKED:NEW-SEAM:context-var | R1 | context.FloatVariables |
| GetIntVar | 從 eval-context 讀 int 變數 | NEW-SEAM:context-var | BLOCKED:NEW-SEAM:context-var | R1 | context.IntVariables |
| GetMatrixVar | 從 eval-context 讀 Matrix(Vector4[]) 變數 | NEW-SEAM:context-var | BLOCKED:NEW-SEAM:context-var | R2 | context.MatrixVariables；IStatusProvider |
| GetObjectVar | 從 eval-context 讀 object 變數 | NEW-SEAM:context-var | BLOCKED:NEW-SEAM:context-var | R2 | context.ObjectVariables；typed object box |
| GetStringVar | 從 eval-context 讀 string 變數 | NEW-SEAM:context-var | BLOCKED:NEW-SEAM:context-var | R1 | context.StringVariables |
| GetVec3Var | 從 eval-context 讀 Vector3 變數 | NEW-SEAM:context-var | BLOCKED:NEW-SEAM:context-var | R1 | context.Vec3Variables |
| GetForegroundColor | 讀取 UI foreground color context 值 | NEW-SEAM:context-var | BLOCKED:NEW-SEAM:context-var | R1 | Slot<Vector4>；context.ForegroundColor |
| GetPosition | 從 Command 鏈讀取 ObjectToWorld 矩陣位置 | Layer2d+Execute + NEW-SEAM:context-3d | BLOCKED:Layer2d+Execute | R3 | 讀 context.ObjectToWorld / WorldToCamera / CameraToClipSpace；3D 相機矩陣 |
| SetBoolVar | 寫 bool 到 eval-context + 執行 SubGraph | NEW-SEAM:context-var + Layer2d+Execute | BLOCKED:NEW-SEAM:context-var | R1 | Slot<Command> Result；SubGraph |
| SetFloatVar | 寫 float 到 eval-context + SubGraph | NEW-SEAM:context-var + Layer2d+Execute | BLOCKED:NEW-SEAM:context-var | R1 | ClearAfterExecution 選項 |
| SetIntVar | 寫 int 到 eval-context + SubGraph | NEW-SEAM:context-var + Layer2d+Execute | BLOCKED:NEW-SEAM:context-var | R1 | LogLevel 參數 |
| SetMatrixVar | 寫 Matrix 到 eval-context + SubGraph | NEW-SEAM:context-var + Layer2d+Execute | BLOCKED:NEW-SEAM:context-var | R2 | InputSlot<Vector4[]> |
| SetObjectVar | 寫 object 到 eval-context + SubGraph | NEW-SEAM:context-var + Layer2d+Execute | BLOCKED:NEW-SEAM:context-var | R2 | InputSlot<Object>；type-unsafe box |
| SetStringVar | 寫 string 到 eval-context + SubGraph | NEW-SEAM:context-var + Layer2d+Execute | BLOCKED:NEW-SEAM:context-var | R1 | |
| SetVec3Var | 寫 Vector3 到 eval-context + SubGraph | NEW-SEAM:context-var + Layer2d+Execute | BLOCKED:NEW-SEAM:context-var | R1 | |
| SetRequestedResolutionCmd | 改 context.RequestedResolution 後執行 SubGraph | Layer2d+Execute | BLOCKED:Layer2d+Execute | R2 | context.RequestedResolution；restore 後執行；類似 SetVar 但改 resolution 不是變數字典 |
| ExecuteRawBufferUpdate | 執行 Command 後傳 out Buffer | Layer2d+Execute + RWStructuredBuffer | BLOCKED:Layer2d+Execute | R3 | Slot<Buffer> Output；兩 seam 同時需要 |
| DrawQuiz | 比對兩個 Command render 差值 | NEW-SEAM:skillquest-system | BLOCKED:NEW-SEAM:skillquest-system | R3 | skillQuest 教學系統；Texture2D out |
| ImageQuiz | 比對兩個 Texture2D 影像 | NEW-SEAM:skillquest-system | BLOCKED:NEW-SEAM:skillquest-system | R3 | skillQuest；差值計算 |
| ValueQuiz | 比對 float 數值 | NEW-SEAM:skillquest-system | BLOCKED:NEW-SEAM:skillquest-system | R1 | skillQuest；float diff range |
| _QuizUp | 複合 Quiz 控制器 | NEW-SEAM:skillquest-system | BLOCKED:NEW-SEAM:skillquest-system | R2 | 帶底線=private；Command + Texture2D 雙輸出 |
| _ReadBackImageDifference | GPU→CPU 讀回 BufferWithViews 差值 | RWStructuredBuffer | BLOCKED:RWStructuredBuffer | R3 | StructuredBufferReadAccess；CPU readback；skillQuest 內部 |
| _ValueQuizGraph | float 歷史曲線 render | NEW-SEAM:skillquest-system | BLOCKED:NEW-SEAM:skillquest-system | R2 | Slot<List<float>> Values；skillQuest 圖表 |

### 摘要（flow/）

- 總 op 數：34（flow/ 頂層 10 + context/ 17 + skillQuest/ 7）
- READY-LEAF：**0**——flow/ 全部 BLOCKED
- 主要 BLOCKED seam 分佈：
  - `Layer2d+Execute`：16 顆（Execute, ExecRepeatedly, ExecuteOnce, LoadSoundtrack, LogMessage, Loop, ResetSubtreeTrigger, Switch, TimeClip, BlendScenes, GetPosition, SetBoolVar, SetFloatVar, SetIntVar, SetMatrixVar, SetObjectVar, SetStringVar, SetVec3Var, SetRequestedResolutionCmd, ExecuteRawBufferUpdate）— 注意：SetVar 系列主要阻擋在 context-var，但其 SubGraph 輸入也繫於 Command seam
  - `NEW-SEAM:context-var`：15 顆（Get/Set *Type*Var 全家桶，含 GetForegroundColor）— **解鎖這個 seam 可一次收 15 顆**
  - `NEW-SEAM:skillquest-system`：5 顆（DrawQuiz, ImageQuiz, ValueQuiz, _QuizUp, _ValueQuizGraph）— TiXL 教學模組，非產品優先
  - `NEW-SEAM:trigger-dirty`：2 顆（Once, ResetSubtreeTrigger）
  - `RWStructuredBuffer`：2 顆（ExecuteRawBufferUpdate, _ReadBackImageDifference）
  - `NEW-SEAM:context-3d`（次要，附屬 GetPosition）：1 顆
- NEW-SEAM 清單（flow 新增）：
  - `context-var` — eval-context 上的 typed 變數字典（FloatVariables, IntVariables, StringVariables, BoolVariables, Vec3Variables, MatrixVariables, ObjectVariables）；解鎖 15 顆；與 `Layer2d+Execute` 無依賴關係，可獨立建
  - `trigger-dirty` — DirtyFlag 邊緣觸發機制（bool Trigger 骯髒標誌驅動 OutputTrigger）；解鎖 Once + ResetSubtreeTrigger；簡單但需 dirty-flag seam
  - `skillquest-system` — TiXL 教學 quiz UI + GPU diff readback；非產品功能，優先度低
  - `context-3d`（次要）— eval-context 的 ObjectToWorld / WorldToCamera / CameraToClipSpace 矩陣；GetPosition 專屬；與 3D render pipeline 綁定
- 意外/盲區：
  - `Loop` 用 `DirtyFlag.GlobalInvalidationTick++` + `Command.InvalidateGraph()` 逐 iter 強制失效——這是 TiXL 特有的失效廣播機制，simple_world 是否有對應 GlobalInvalidationTick 尚未確認，**可能需要額外 seam 支持**。
  - `TimeClip` 用 `TimeClipSlot<Command>` 特化 slot 型別 + `IPreventingTimeRemap`——不是普通 Command slot，需 transport seam 也支持 time-remap 抑制。
  - `context-var` seam 完全獨立於 `Layer2d+Execute`：Get/SetVar 不需要 Command rendering，只需 eval-context 字典——這是**最划算**的 flow seam，投入小但解鎖 15 顆。
  - `skillQuest/` 全家桶目前被帶底線標記為 private op（_QuizUp, _ReadBackImageDifference, _ValueQuizGraph），是 TiXL 教學系統內件，不在產品 clone 必要路徑上。
