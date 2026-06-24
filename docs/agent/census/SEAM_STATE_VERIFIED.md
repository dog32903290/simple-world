# SEAM_STATE_VERIFIED — 引擎剩餘 seam 真實狀態（2026-06-25 read-only set-diff 驗證）

> Scout a440c967 read-only set-diff（TiXL `external/tixl/Operators/Lib/` vs sw registries）+ 讀 cook-core 定 blast。**糾正 OP_BACKLOG bucket C「BLOCKED」counts 嚴重高估**——5 個「BLOCKED」seam 其實**早已建**（同 list-seam 誤分類）。**∴引擎剩餘有 ~150+ op 的 LOW-blast 安全 fan-out 燃料，非全高 blast 承重。**

## 真實狀態表
| seam | 真實狀態 | 真實未 port 數 | blast | 需柏為? | 證據 |
|---|---|---|---|---|---|
| **list value-type** | **BUILT** | ~6 stateful（需 state slot） | LOW | no | LIST_SEAM_BLUEPRINT.md |
| **point-buffer** | **BUILT**（誤分類，最大） | **~84 fan-out** | **LOW** | **no** | `registerPointOp("X",cookX)` per-leaf；`cookReg()` map @ point_graph_registry.cpp:29；PointCookCtx 完整 |
| **field/shader-graph** | **BUILT**（誤分類） | ~20（40/60 已 port） | **LOW** | no | 40 field_ops_*.cpp；fieldSpecSink live；field_render.cpp Render2dField BUILT |
| **mesh-pipeline** | **BUILT**（誤分類） | ~27 gen/mod（~7 Draw* 需 camera3d） | LOW(gen/mod)/MED(draw) | no(gen/mod) | 17 mesh_ops_*.cpp；meshSpecSink live；findMeshOp lookup 非 branch |
| **gradient-widget** | **BUILT**（誤分類） | ~4-8 image-gradient | LOW | no | gradient_op_registry=第8 cook flow，Define/Blend/Pick/Iq+Box/Linear/Radial/NGon+GradientsToTexture live |
| **cpu-upload-texture** | **BUILT** | 0 | LOW | no | ValuesToTexture/2/GradientsToTexture/CurvesToTexture 全 registered |
| **context-var** | **PARTIAL** | **9/15**（Vec3/String/Matrix/Object Var+GetForegroundColor/GetPosition） | LOW | no | Float/Int/Bool Get+Set BUILT (node_registry_math_contextvar.cpp) |
| **camera3d** | PARTIAL | ~29-50（core bridge live，gizmo 0/10+variants ~2/11） | **MED-HIGH**（動 flat+resident 兩 leg） | **yes**（新 infra；clean consumer leaf 如 SortPoints=LOW） | C1 camera-scope+CmdCookCtx 在 point_graph.cpp+_resident.cpp |
| **Layer2d+Execute** | PARTIAL | ~25/37 | MED | mixed | Execute/Loop/Switch/Bloom/AfterGlow/LightRaysFx BUILT；Glow/Sketch/AsciiRender/GlitchDisplace/WaveForm/ScreenCloseUp/DetectMotion/FieldToImage NOT FOUND |
| **compute-readback** | PARTIAL | ~10 | LOW | no | PointsToCPU/ReadPointColors BUILT；GetImageBrightness/PickColorFromImage/JumpFloodFill 未建 |
| **feedback** | PARTIAL | ~14 | MED | maybe（point ring） | KeepPreviousFrame/SwapTextures/AfterGlow pingpong BUILT；KeepPreviousPointBuffer 未建 |
| **FloatList-resident-state**（新） | **UNBUILT** | 6 op | **LOW-MED**（加 state slot 到 FloatListCookCtx，不碰 spine） | no | FloatListCookCtx 無 state slot |
| **datetime** | UNBUILT | 6（DateTimeToFloat/ToString/NowAsDateTime/StringToDateTime/TimeToString/CountDown） | LOW | no | cheap value-graph leaves |
| **network-io** | UNBUILT | 9（+UDP） | LOW | no（柏為域 perf I/O） | 零 Tcp/Udp/WebSocket in app/src |
| **dx11-api-wrapper** | UNBUILT/N-A | ~25（likely SKIP-on-Metal） | LOW | no | render_command.h DrawKind 吸收等價物 |

## ★ 安全可採（LOW blast，orchestrator 自走 bounded，無柏為）— 推薦順序
1. **point-buffer fan-out（~84）** — 最高 unlock 最低險，proven，self-register leaf 零 point_graph 編輯。**頂級燃料。**
2. **field/shader-graph fan-out（~20）** — 自含島，renderer live。
3. **mesh gen/modifier fan-out（~27）** — 純 CPU geometry，self-register。
4. **context-var remainder（9）** — cheap，pattern 在 node_registry_math_contextvar.cpp。
5. **datetime（6）** — cheap value-graph leaves。
6. **gradient image-op（~4-8）** — rail exists。
7. **FloatList-resident-state seam（6 op）** — 小 bounded seam：加 state slot 到 FloatListCookCtx（KeepColors precedent），fan out Amplify/Keep/Damp/Merge/DampPeakDecay/Delta。不碰 cook spine。

## 需柏為在場（HIGH blast，動 cook-core spine flat+resident）
8. camera3d 剩餘 infra（gizmo/variants，動兩 leg）。clean consumer leaf（如 SortPoints）=LOW 可進 orchestrator 批。
9. Layer2d/Execute 剩 + feedback point-ring（render-command+resident pingpong，MED-HIGH）。
10. network-io（新 platform I/O，自含但柏為域 perf I/O）。

## 誠實不確定
- point-buffer 精確數：state=BUILT/~84 minable 可靠；精確 int 需 `comm` TiXL point/**/*.cs basenames vs sw registered（scout 數 .t3 非 .cs registration）。
- mesh Draw*（7）：camera3d-dependent=MED，未逐顆確認 coupling。
- dx11-wrapper：fairly confident SKIP-on-Metal 未逐顆 trace。
- **方法警示**：sub-agent 初報 Layer2d="BUILT/0" 過樂觀→scout 直接 grep 糾正（Glow/Sketch 等 NOT FOUND）。raw counts 偏樂觀，每顆採前仍 grep 確認 un-ported + backward-trace。
