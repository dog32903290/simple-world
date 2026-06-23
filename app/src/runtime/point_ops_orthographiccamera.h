// runtime/point_ops_orthographiccamera — OrthographicCamera command op (camera3d C2).
//
// TiXL Operators/Lib/render/camera/OrthographicCamera.cs. Command subtree in → Command out, exactly like
// the perspective Camera op (point_ops_camera.cpp), but it pushes an ORTHOGRAPHIC projection instead of a
// perspective one. Same per-item STAMP mechanism (Option a): cookOrthographicCamera stamps its raw camera
// params onto every subtree item where !hasCamera (innermost wins), and the RenderTarget executor builds
// CameraToClipSpace = orthoRH(size) (field_camera.h) instead of perspectiveFovRH. WorldToCamera is the SAME
// LookAtRH(eye,target,up) as the perspective camera (shared eye/target/up + near/far fields on the item).
//
// PROJECTION (OrthographicCamera.cs:28-36): size = Stretch · Scale · (aspectRatio, 1);
//   CameraToClipSpace = Matrix4x4.CreateOrthographic(size.X, size.Y, NearFarClip.X, NearFarClip.Y).
// aspectRatio default 0 → RequestedResolution aspect (cs:25-28) → the executor's output aspect (the same
// fallback the perspective Camera's AspectRatio uses). So the RAW Scale/Stretch travel on the item and the
// executor finalizes size with the output aspect (render_command.h camOrtho/camOrthoScale/camOrthoStretch).
//
// FORKS (named, mirror the perspective Camera v1 scope): Roll dropped (OrthographicCamera.cs:30-31
// rollRotation — the WorldToCamera×rollRotation term; SW WorldToCamera = LookAtRH only, no roll). Position
// default is TiXL's exact .t3 value (-0.0015059264, 0.0014562709, 10.0) — kept faithful (NOT rounded to
// (0,0,10)). No Reference output (SW has no ICamera reflection rail). No depth/OrbitCamera/ActionCamera.
//
// runtime leaf: pure CPU stamp + the field_camera ortho math (the GPU golden routes through the real
// RenderTarget executor). Own header (NOT the at-cap point_ops.h, which is on the line-count ratchet) so the
// registrar + the selftest router reach it without bloating the god-header.
#pragma once

namespace sw {

// Register "OrthographicCamera" as a Command→Command Cmd op (point_ops_register_draw.cpp). Stamps the ortho
// camera onto its subtree items; the executor builds the orthoRH projection.
void registerOrthographicCameraOp();

// --selftest-orthographiccamera (camera3d C2 GOLDEN; routes through the production RESIDENT terminal so BOTH
// the flat and resident draw legs are proven — the resident cookCommand dispatches the SAME registered op).
//   TOOTH A (the ortho-vs-perspective render flip): an OrthographicCamera(Scale=2, eye=10) projects a
//     world x-half 0.6 quad to NDC 0.6 (distance-INVARIANT: NDC.x = 0.6·2/2 = 0.6). A perspective Camera at
//     the SAME eye=10 would shrink it to 0.6/(10·tan22.5°)=0.145. A probe at NDC 0.45 is INSIDE the ortho
//     quad (quad color) but OUTSIDE the perspective one (background) → the discriminator. injectBug = build
//     perspectiveFovRH instead of orthoRH (drop the ortho flag) → the quad shrinks past the probe → the
//     NDC0.45 probe reads background → RED.
//   TOOTH B (the ortho DISTANCE-INVARIANCE signature, host math): the ortho projected x-half is IDENTICAL at
//     eye=10 and eye=20 (perspective would differ). Asserts |proj(10) - proj(20)| ≈ 0 AND proj == 0.6·2/size
//     closed form; injectBug breaks the equality (a perspective matrix is distance-dependent).
int runOrthographicCameraSelfTest(bool injectBug);

}  // namespace sw
