// runtime/point_ops_orbitcamera — OrbitCamera command op (camera-B lane).
// TiXL authority: external/tixl/Operators/Lib/render/camera/OrbitCamera.cs (GUID 6415ed0e) +
// OrbitCamera.t3 + Core/Utils/MathUtils.cs:16-41 (PerlinNoise) — the animated orbit/wobble camera.
//
// OrbitCamera.cs:50-155 UpdateCameraDefinition (the angle→position pipeline, all angles degrees→rad):
//   time      = OverrideTime wired ? OverrideTime : context.LocalFxTime (bars)            :55-56
//   radius    = DistanceToTarget == 0 ? 0.0001 : DistanceToTarget                          :71
//   orbitYaw  = ComputeAngle(SpinAngleAndWobble,1)
//             + ToRad·((SpinRate·time)·360 + SpinOffset + PerlinNoise(0,1,6,Seed)·360)     :85-88
//   orbitPitch= -ComputeAngle(OrbitAngleAndWobble,2)                                       :89
//   eye       = (0,0,radius) rotated by CreateFromYawPitchRoll(orbitYaw,orbitPitch,0)      :90-95
//   viewDir   = CameraTargetPosition - eye                                                 :98
//   adjDir    = normalize(viewDir rotated by YawPitch(ComputeAngle(AimYaw,3)+RotOff.X·ToRad,
//                                                     ComputeAngle(AimPitch,4)+RotOff.Y·ToRad)) :100-108
//   roll      = ComputeAngle(AimRollAngleAndWobble,5) + RotOff.Z·ToRad                     :115
//   up        = normalize(Up rotated around adjDir by roll)  (axis-angle)                  :117-119
//   eye      += PositionOffset rotated by the ORBIT rotation                               :121
//   target    = eye + adjDir                                                               :123
//   WorldToCamera = LookAtRH(eye, target, up); CameraToClipSpace = PerspectiveFovRH(fov)   :69,:138
//   ComputeAngle(slot,i) = ToRad·(X + wobble), wobble = |Y|<0.001 ? 0
//             : (PerlinNoise(time·WobbleSpeed, 1, WobbleComplexity, Seed+123·i)-0.5)·2·Y   :143-154
// The op stamps eye/target/up/FOV/NearFarClip/AspectRatio per item (the cookCamera stamp — the
// executor rebuilds the SAME LookAtRH/PerspectiveFovRH pair). resolveOrbitCamera is the ONE formula
// source: cookOrbitCamera stamps it AND resolveReferencedCamera serves it to a ReuseCamera wire.
//
// FORKS (named):
//   - fork-orbitcamera-damping-dropped: Damping lerps against cross-frame _dampedEye/_dampedTarget
//     state (:125-126) — the Command cook rail has no per-node persistent state. The .t3 default
//     Damping=0 collapses the lerp to identity (Lerp(x,prev,0)=x), so the DEFAULT path is byte-faithful
//     and stateless; the knob is dropped from the spec until a Command-rail state seam exists.
//   - fork-orbitcamera-overridetime-nonzero: TiXL branches on OverrideTime.HasInputConnections (:55);
//     the established single-clock convention (fork-perlin2-overridetime-nonzero-single-clock) maps it
//     to "non-zero constant overrides the bars clock". .t3 default 0 → LocalFxTime, byte-faithful.
//   - fork-orbitcamera-int-on-float-port: Seed/WobbleComplexity are InputSlot<int>; stored Float,
//     truncated before use (the fork-perlin2-seed-octaves precedent). WobbleComplexity clamps [1,8] (:76).
//   - fork-orbitcamera-no-point-scope: draw-rail stamp only (the OrthographicCamera precedent); the C1
//     point-rail LiveCameraScope bridge is a follow-up seam.
#pragma once
#include <map>
#include <string>

#include "runtime/point_ops_camera_scope.h"  // ActiveCamera (the resolved camera currency)

namespace sw {

struct CmdCookCtx;      // runtime/point_graph_cook_ctx.h
struct RenderCommand;   // runtime/render_command.h

// The single formula source (OrbitCamera.cs:50-155 at Damping=0). `localFxTime` = bars (TiXL
// LocalFxTime); params by the spec's .x/.y/.z suffix convention with the .t3 defaults as fallbacks.
ActiveCamera resolveOrbitCamera(const std::map<std::string, float>& params, float localFxTime);

// The cmd cook (resolveOrbitCamera → the cookCamera per-item stamp). Exposed for the golden's
// bug-wrapper override (the rotatetowards-golden precedent).
RenderCommand cookOrbitCamera(CmdCookCtx& c);

void registerOrbitCameraOp();

// --selftest-orbitcamera golden. TOOTH B (math): the full angle→position pipeline at a mid-range,
// all-wobbles-active param set vs an INDEPENDENT golden-local transcription of .cs:50-155 (own Perlin
// copy anchored by a hand-computed integer-hash value), PLUS impl-independent geometric invariants at a
// non-trivial orbit position (|eye| == radius; |target-eye| == 1; aim=0 → camera looks at the orbit
// center; roll=0 → up unchanged). TOOTH A (render, production resident terminal): AimYaw=20° displaces
// the projected orbit center off NDC(0,0) — probe at the oracle-projected center = quad color, probe at
// (0,0) = background (the quad slid away). injectBug: drop the stamp in the real cook → default camera
// → both probes flip AND the math tooth loses the stamp → RED. did-not-trip → return 0.
int runOrbitCameraSelfTest(bool injectBug);

}  // namespace sw
