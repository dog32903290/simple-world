# camera→CmdCookCtx bridge Build Plan (camera3d-remaining #1)

> HEAD 7409fe3. Surfaces the already-live active camera (C1 LiveCameraScope) onto CmdCookCtx so Command-rail ops read WorldToCamera/CameraToWorld at cook time. Closes RotateTowards FORK#2 + unblocks GetScreenPos/GetPosition. ADDITIVE cook-core edit (NOT the S4 split).

## Premise (scout-confirmed)
The C1 LiveCameraScope ALREADY exists and is set on BOTH cook legs around the SubGraph cook (flat `point_graph.cpp:488-495`, resident `point_graph_resident.cpp:528-535`); `liveActiveCamera()` (point_ops_camera_scope.h:76) returns the stamped camera while a Command op inside a Camera subtree cooks. The point-rail already reads it (point_graph_internal.h:83-84). MISSING = surfacing it onto `CmdCookCtx` for Command-rail readers. The hard part (mirror-correct live scope) is DONE → this is additive.

## Changes
1. **CmdCookCtx** (point_graph.h:136-174): add `bool hasCamera=false; float cameraToWorld[16]; float worldToCamera[16];` (mirror PointCookCtx::cameraToWorld at :108-109). Default-initialized → **every existing Command op ignores them → byte-identical**.
2. **Two mirror populate sites** (the flat-resident gate — same discipline as C1; the read-source `liveActiveCamera` is ALREADY mirror-correct so black-hole risk is retired): where `cc` is filled before `cm->second(cc)`:
   - flat: point_graph.cpp:~564-572
   - resident: point_graph_resident.cpp:~623-626
   At both: consult `liveActiveCamera()`; if non-null → `cc.hasCamera=true` + fill `cc.cameraToWorld`/`cc.worldToCamera` via `activeCameraMatrices()` (point_ops_camera_scope.h:84; add a worldToCamera accessor if needed); else leave default-false. **MUST populate on BOTH legs identically** (resident is production).
3. **RotateTowards FORK#2** (point_ops_rotatetowards.cpp:115-119): replace the hardcoded default-camera branch with `if (c.hasCamera) camWorld = transform((0,0,0), c.cameraToWorld); else <existing default (0,0,defaultCameraDistance())>`.

## Golden — `--selftest-rotatetowards-camera` (or extend rotatetowards golden), closed-form, BOTH legs
- Graph: `EmitItemCmd(identity) → Camera(eye=(0,0,5)) → RotateTowards(LookTowards=0/TowardsCamera) → terminal`, cooked flat `cook()` AND resident.
- Closed form: camera world pos = Transform((0,0,0), inverse(LookAtRH((0,0,5),0,up))) = (0,0,5). M = rotateOffset·inverse(LookAtRH(0, sourcePos−(0,0,5), up)) — host-derive (reuse point_ops_rotatetowards.cpp:89-96 with target=(0,0,5)).
- **Discriminator**: assert stamped item matrix == the eye=5 derivation AND DIFFERS from the eye=2.4142 (default) derivation → moving the Camera changes RotateTowards output. **injectBug** = force `cc.hasCamera=false` (skip the populate) → falls back to default camera → the eye=5-vs-default difference vanishes → RED both legs.
- **Mirror gate**: assert flat and resident produce byte-identical stamps (C1 runCameraScopeSelfTest/runCameraResidentSelfTest precedent).

## NON-REGRESSION (make-or-break — additive fields must not change anything)
- ALL existing render/Command goldens byte-identical: RotateTowards TowardsPosition golden (the existing one), Group/Shear/Transform/RotateAroundAxis, layercompose, Execute, Loop, Switch — all green, --bite UNCHANGED at 422.
- The additive CmdCookCtx fields default-false → every Command op that doesn't read them is byte-identical. Confirm no existing op's cook path changed.

## Owner-lock / S4
Edits point_graph.cpp + point_graph_resident.cpp in the cc-fill region ONLY (additive field assignment, ~3 lines each), NOT the recursion/collector logic. NOT owner-lock-class. S4 (point_graph split) is LAST in 柏為's queue, not concurrent → no collision; flag the additive insertion for the eventual S4 owner (mechanical rebase). Keep files ≤400 (point_graph.cpp/.h are grandfathered — the additive fields/lines must stay within the grandfather cap; peel a comment if needed, NO bump).

## TiXL refs
RotateTowards.cs:12-13,17-51 (Invert(context.WorldToCamera) camera-world derivation), EvaluationContext.cs:123-125 (WorldToCamera default identity), GraphicsMath.cs:10-23 (LookAtRH).

## Unlock + risk
Unlocks: RotateTowards TowardsCamera (the .t3 DEFAULT — closes FORK#2), GetScreenPos (OP_BACKLOG:92), GetPosition (:150), future CamPosition/CurrentCamMatrices/DrawCamGizmos. Risk R1-R2 (scope already proven by C1; new surface = 2 additive fields + 2 populate + 1 branch). Watch: the mirror gate (both legs populate) — caught by the resident golden leg.

## Critical files
- point_graph.h (CmdCookCtx :136-174 — add fields), point_graph.cpp (:564-572 flat populate), point_graph_resident.cpp (:623-626 resident populate), point_ops_camera_scope.h (liveActiveCamera/activeCameraMatrices + worldToCamera accessor), point_ops_rotatetowards.cpp (:115-119 FORK#2 branch).
