# CAMERA3D BLUEPRINT — the camera/view/projection seam

> Read-only Plan scout, 2026-06-23 (HEAD 8a479fd). file:line cited at authoring time — re-confirm before editing.
> Mirror-structure target: `S2_RENDERGRAPH_BLUEPRINT.md` / `S3_FLOW_BLUEPRINT.md`.

## 0. Headline finding (changes the premise)

The camera3d seam is **NOT greenfield**. Since the S2 spine landed, the core view/projection machinery shipped on top of it and is **already built, registered for production, and golden-proven on the flat leg**. In-tree today:

- `field_camera.{h,cpp}` (308 lines) — `lookAtRH` / `perspectiveFovRH` / `mat4Mul` / `mat4Inverse` / `objectToClipSpace` / `defaultLayerCameraForward` / `pointCameraMatrices`, row-major/row-vector locked, `--selftest-field-camera` green.
- `point_ops_camera.cpp` — **`Camera` op registered** (`registerCameraOp` → `registerCmdOp("Camera", cookCamera)`, point_ops_register_draw.cpp:35), per-item stamp + `runCameraSelfTest` (eye-distance flip tooth + math tooth).
- executor `point_ops_rendertarget.cpp:374-486` composes `ObjectToClipSpace = objectToWorld · worldToCamera · cameraToClipSpace` per item, reading `it.hasCamera`/`camEye`/`camFovDeg`/… for BOTH Layer2d and Mesh, Group SRT right-multiplied.
- `render_command.h:138-197` carries the full camera stamp (hasCamera, camEye/Target/Up, camFovDeg, camNear/Far, camAspect) + Group SRT — per-item.
- Registered consumers: Layer2d, Group, Transform, RotateAroundAxis, Shear, DrawScreenQuad, DrawMeshUnlit, RenderTarget + point-camera ops TransformPointsFromClipspace, SamplePointsByCameraDistance, PointsOnMesh.

The seam-defining decision was already made = **per-item camera STAMP** (not a runtime scope stack), the analogue of S2b Group SRT + S3a context-var host-push. The Command-rail is **already mirrored on both legs** — resident `cookCommand` dispatches the identical registered `cookCamera` via `cm->second(cc)` (point_graph_resident.cpp:603-611), camera fields travel by value in `RenderDrawItem`.

**Therefore this is a HARDEN-AND-EXTEND plan, not a build plan.** Value = (1) name the one residual seam hole, (2) stage the genuinely-unbuilt ops, (3) bake flat-vs-resident mirror discipline into the residual hole before it ships a black-hole.

## 1. The real seam + the one residual hole

**The seam (shipped):** a Camera/Group/Transform Cmd op copies its subtree's `RenderCommand.items` and stamps raw camera/SRT params onto every item where `!hasCamera` (innermost-wins = push/pop without a scope stack, Camera.cs:36-45). The executor builds `worldToCamera = lookAtRH(eye,target,up)` and `cameraToClipSpace = perspectiveFovRH(fov,aspect,near,far)` from the stamp (or `defaultLayerCameraForward(aspect)` if `!hasCamera`).

**THE RESIDUAL HOLE (S2c/S3 mirror-class, live + unclosed):** the **point-camera leg ignores the Camera op.** `fillPointCamera` (point_graph_internal.h:75-82) is correctly shared by both cooks (point_graph.cpp:354 + point_graph_resident.cpp:341 — NOT a flat-vs-resident asymmetry) but it **always fills the DEFAULT camera**:
```cpp
inline void fillPointCamera(PointCookCtx& cc, const NodeSpec& spec, float aspect) {
  ... if (port.dataType == "Camera") wantsCamera = true; ...
  if (!wantsCamera) return;
  pointCameraMatrices(aspect, cc.objectToCamera, cc.cameraToWorld);  // ← ALWAYS default
  cc.hasCamera = true;
}
```
So SamplePointsByCameraDistance / TransformPointsFromClipspace / (future SortPoints) sort/unproject against the **default camera regardless of any upstream Camera op**. Command-rail draws respect the Camera op; point-rail silently does not. Same class as the S2c Texture2D black-hole + S3 var-write (a real consumer reading stale/default context) — here **default-vs-pushed**, not flat-vs-resident. Masked because no production graph yet wires a non-default camera into a point op + the point goldens use default.

**Minimal cook-core change to close it:** thread the active stamped camera into `PointCookCtx` so `fillPointCamera` builds from it instead of unconditional `defaultLayerCameraForward`. Point ops are evaluated inside a Camera-op-wrapped subtree → make the camera available the same way S3a made the context-var scope (LiveCtxVarScope / host "current camera" the Camera op sets around its subtree cook, point cook reads). **Copy the S3a pattern verbatim — do not invent a new mechanism.**

## 2. Staging (value ÷ risk descending)

| Stage | What | Risk | Unlock |
|---|---|---|---|
| **C0** | Resident-leg ratify of shipped Camera→Layer2d (golden through resident terminal, not the by-hand `runCameraSelfTest` harness) | R1 | correctness lock, 0 new ops, de-risks ~all remaining |
| **C1** | Close the point-camera hole (PointCookCtx active camera, S3a pattern) | R2 | ~5-7 point/screen ops (SamplePointsByCameraDistance, TransformPointsFromClipspace, SortPoints, GetScreenPos, GetPosition, CpuPointToCamera, PointToMatrix) |
| **C2** | OrthographicCamera + `orthoRH` matrix | R1 | camera-family ~6-8 (CamPosition, CurrentCamMatrices, BlendCameras, ShiftCamera, CameraWithRotation, ReuseCamera) |
| **C3** | Gizmo draw family | R2-R3 | ~10-15 (DrawLineGrid/Locator/ConeGizmo/DrawBoxGizmo/DrawSphereGizmo/GridPlane/DrawCamGizmos/PlotValueCurve/VisibleGizmos/DrawSpatialAudioGizmos) |

**C0 proving golden:** Camera eye=(0,0,5), fov 45°, square → Layer2d quad half-extent 0.6 projects x-half to NDC `0.6/(5·tan22.5°)=0.290`. Probe NDC 0.45 = background under eye=5 / quad under default; NDC 0.145 = quad (shrunk-not-vanished). Compute boundary host-side via `mat4TransformPointDivW`, cook through **resident** terminal. 
**C1 proving golden:** Camera(eye=(0,0,5)) → 2 points at world z=0,z=-1 → SamplePointsByCameraDistance; camera-space z = `-(d - z_world)`, default d=2.4142 vs eye=5 give different depth ordering → assert selection flips when camera moves; injectBug = revert to unconditional default → flip disappears → RED. BOTH legs byte-identical.
**C2:** ortho NDC.x is distance-invariant (the discriminator vs perspective) — same quad through Camera(persp,eye=5) vs OrthographicCamera: NDC-0.45 probe background under persp, quad under ortho. 
**C3:** DrawLineGrid line at world x=k → NDC `k/(d·tan(fov/2))`; assert two adjacent line centers (never edges).

## 3. Flat-vs-resident mirror checklist

**Command-rail (Camera/Group/Layer2d/Mesh) — ALREADY MIRRORED, re-verify don't rebuild:**
- [ ] `cookCamera` runs flat (point_graph.cpp cookCommand) AND resident (point_graph_resident.cpp:611 `cm->second(cc)`) — same fn ✅
- [ ] Camera fields by value in RenderDrawItem → resident composite (point_ops_rendertarget.cpp:415-422) reads identically ✅
- [ ] **C0 golden MUST cook through the resident terminal**, not the by-hand `runCameraSelfTest` (which bypasses `cookCommand`) — the only way to prove resident dispatch isn't a black-hole.

**Point-rail (fillPointCamera) — the live hole, mirror CORRECTLY when fixing C1:**
- [ ] `fillPointCamera` shared by both cooks (point_graph.cpp:354, resident:341) ✅ — do NOT fork it.
- [ ] The new "active camera" host-context the Camera op sets MUST be set/read on BOTH legs' cook recursion. Copy S3a `ctxVars`/`LiveCtxVarScope` verbatim (it already crosses both legs). Trap: setting active camera only in flat cookCommand's Camera branch → resident point ops read default → **resident-only black-hole = S2c**.
- [ ] C1 golden cooks identical graph through `cook()` AND resident terminal; assert byte-identical point selection.

## 4. TiXL faithfulness anchors
- Push/pop: `external/tixl/Operators/Lib/render/camera/Camera.cs:36-45` (save prev → set context → Command.GetValue → restore). SW = per-item stamp innermost-wins (`!hasCamera`, point_ops_camera.cpp:88-98).
- Projection: `Core/Utils/Geometry/GraphicsMath.cs:10-23` (LookAtRH), `:27-52` (PerspectiveFovRH: M33=zFar/(zNear-zFar), M43=zNear·zFar/(zNear-zFar), M34=-1 = **D3D NDC z∈[0,1]**, matches Metal), DefaultCameraDistance=2.4142135 (`:114`).
- Multiply order + transpose cancel: `Core/Rendering/TransformBufferLayout.cs:14-17` ObjectToCamera=Multiply(objectToWorld,worldToCamera), ObjectToClipSpace=Multiply(ObjectToCamera,cameraToClipSpace). Lines 19-30 transpose only to pack HLSL cbuffer column-major — **SW stores row-major row-vector, does NOT transpose** (double-transpose cancels; field_camera.h:92-100 note). **Same class as S2b multiply-order check — verify against locked convention, never re-derive.**
- EvaluationContext: `Core/Operator/EvaluationContext.cs:123-125` (CameraToClipSpace/WorldToCamera/ObjectToWorld = Identity default), `:89-95` (SetDefaultCamera), `:78` (**aspect = RequestedResolution W/H = the S1 seam**).
- Camera v1 fork: Camera.cs:52-70 LensShift/PositionOffset/Roll/RotationOffset dropped (named, point_ops_camera.cpp:13-19).
- Ortho (C2): `external/tixl/Operators/Lib/render/camera/OrthographicCamera.cs` + GraphicsMath ortho (transcribe element-for-element, same row-major/z[0,1] lock).

## 5. Risk list (silent-corruption)
1. **Point-camera default-vs-pushed hole (LIVE §1)** — ships correct-looking default-camera point sorting while draws use the real camera. Caught only by C1 golden wiring non-default Camera into a point op + asserting output changed. Until C1: treat SamplePointsByCameraDistance/TransformPointsFromClipspace/SortPoints as **default-camera-only** (name fork).
2. **Projection handedness / multiply-order** — wrong order moves origin off NDC-center; caught by math tooth (origin→(0,0)) not render tooth. Keep both.
3. **NDC z-range** — TiXL D3D[0,1] = Metal aligned; if C3 adds depth-tested gizmos re-confirm LessEqual against this z range.
4. **Transpose double-cancel** — field_camera does NOT transpose; new matrices (ortho C2) MUST follow or output silently mirrors.
5. **Aspect from S1 RequestedResolution** — camera aspect from output resolution; keep goldens square unless testing aspect.
6. **View-matrix inversion** — cameraToWorld=inverse(worldToCamera); C1 TransformPointsFromClipspace exercises it; probe an off-axis point (on-axis is singular-silent).
7. **Resident-leg golden bypass** — `runCameraSelfTest` calls cookRenderTarget directly, does NOT prove resident cookCommand dispatches cookCamera. C0 closes this; don't count the flat golden as resident coverage.

## 6. Ops per stage
OP_BACKLOG §camera3d (~50): render camera 11 / gizmo 15 / transform 8 / Apply*/GetScreenPos/TransformsConstBuffer; point CpuPointToCamera/PointToMatrix; field render RaymarchField/Render2dField/VisualizeFieldDistance.
- C0: 0 new but de-risks all ~50 (the gate downstream trusts).
- C1: SamplePointsByCameraDistance, TransformPointsFromClipspace, SortPoints, GetScreenPos, GetPosition, CpuPointToCamera, PointToMatrix (~5-7).
- C2: OrthographicCamera + CamPosition/CurrentCamMatrices/BlendCameras/ShiftCamera/CameraWithRotation/ReuseCamera (~6-8).
- C3: gizmo 15.
- **Already landed (don't re-count):** Camera, Layer2d, Group, Transform, RotateAroundAxis, Shear, DrawScreenQuad, DrawMeshUnlit, RenderTarget, SetRequestedResolution. **★Re-baseline the ~50 to ~25-30 remaining** — half shipped under the S2 spine. Update OP_BACKLOG when acted on.
- **Downstream-of-camera3d (gated, NOT in scope):** pbr-material(~10)/lighting(~8 PointLight/SetFog)/depth-buffer(~8 GodRays/SSAO/DoF/MotionBlur)/shadow-map(~5)/lens-flare(~9)/bitmapfont(~7) — need camera3d PLUS own seam. DrawMesh/DrawMeshCelShading/...WithShadow need pbr-material+lighting (separate blueprints); DrawMeshUnlit (unlit base) shipped.

## 7. Critical files
sw (read first; they ARE the seam):
- `app/src/runtime/point_graph_internal.h` — fillPointCamera (75-82): **the C1 hole** + host-camera scope it reads.
- `app/src/runtime/point_ops_camera.cpp` — cookCamera (65-100) + runCameraSelfTest: stamp mechanism (C0/C1 set point-rail active camera here) + golden template.
- `app/src/runtime/point_ops_rendertarget.cpp` — executor camera composition (374-486 Layer2d+Mesh): ortho (C2) branch + C0 resident golden lands.
- `app/src/runtime/field_camera.{h,cpp}` — pointCameraMatrices (h:194), objectToClipSpace (h:99), defaultLayerCameraForward (h:181): add orthoRH (C2) same convention lock.
- `app/src/runtime/point_graph_resident.cpp` — cookCommand (603-611) + fillPointCamera call (341): resident mirror surface; C0/C1 goldens route through here.
tixl (ground-truth, read-only): Camera.cs:36-77 + OrthographicCamera.cs; GraphicsMath.cs:10-52,114; TransformBufferLayout.cs:10-30; EvaluationContext.cs:78,89-95,123-125; render/gizmo/DrawLineGrid.cs (C3 proving op).
