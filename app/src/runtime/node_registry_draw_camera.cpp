// runtime/node_registry_draw_camera — NodeSpec rows for the CAMERA family: the render.camera ops that
// wrap a Command subtree and render it through an explicit camera projection (Camera / OrthographicCamera).
// Peeled out of node_registry_draw.cpp so the camera lane can extend this family without touching
// drawSpecs()'s shared table (parallel-lane peel — no merge conflict with render/flow/data lanes). These
// rows moved VERBATIM from node_registry_draw.cpp; table order unchanged (drawSpecs() appends in source order).
#include "runtime/node_registry_draw.h"
#include "runtime/graph.h"

namespace sw {

const std::vector<NodeSpec>& drawCameraSpecs() {
  static const std::vector<NodeSpec> specs = {
      // Camera (TiXL Lib.render.camera.Camera): wraps a Command subtree and renders it through an
      // explicit camera (Position/Target/Up/FieldOfView/ClipPlanes) instead of the driver-local default
      // (Camera.cs push/pop). Command in → Command out (the op stamps its camera onto every subtree item;
      // the RenderTarget executor builds WorldToCamera/CameraToClipSpace from those params). FORK (named):
      // offset/roll/lensShift dropped (Camera.cs:82-103 commented embellishments); AspectRatio default 0
      // → the output (RequestedResolution) aspect (Camera.cs:53-55). v1: no OrbitCamera/ActionCamera, no depth.
      {"Camera", "Camera",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"Position.x", "Position", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Position.y", "Position.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Position.z", "Position.z", "Float", true, 2.4142135f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Target.x", "Target", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Target.y", "Target.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Target.z", "Target.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Up.x", "Up", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"Up.y", "Up.y", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Up.z", "Up.z", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"FieldOfView", "FieldOfView", "Float", true, 45.0f, 1.0f, 179.0f},
        {"ClipPlanes.x", "ClipPlanes", "Float", true, 0.01f, 0.0001f, 1000.0f, Widget::Vec, {}, true, 2},
        {"ClipPlanes.y", "ClipPlanes.y", "Float", true, 1000.0f, 0.0001f, 100000.0f, Widget::Vec, {}, true, 1},
        {"AspectRatio", "AspectRatio", "Float", true, 0.0f, 0.0f, 10.0f},
        // Reference OUTPUT (TiXL Camera.cs:16-17 Slot<Object> Reference): the camera-instance handle a
        // ReuseCamera's CameraReference / BlendCameras' CameraReferences / ActionCamera's
        // ReferenceCamera wire consumes (the cook drivers resolve the SOURCE node's identity+params —
        // no value flows through this pin; it is the wire's anchor). ONE port serves both camera
        // lanes' consumers (merge unification: Object = the shared camera-ref wire currency).
        {"Reference", "Reference", "Object", false}},
       nullptr,
       "render.camera"},
      // OrthographicCamera (TiXL Lib.render.camera.OrthographicCamera): the perspective Camera's twin — wraps
      // a Command subtree and renders it through an ORTHOGRAPHIC projection (no perspective foreshortening; a
      // farther eye does NOT shrink the view). Command in → Command out (the op stamps its ortho camera onto
      // every subtree item; the RenderTarget executor builds CameraToClipSpace = orthoRH(size) where
      // size = Stretch·Scale·(aspect,1), OrthographicCamera.cs:33). FORKS (named): Roll dropped
      // (OrthographicCamera.cs:30 rollRotation); Position default = TiXL's exact .t3 value (NOT rounded); no
      // Reference output; AspectRatio default 0 → output (RequestedResolution) aspect (cs:25-28). v1: Scale +
      // eye/target/up + near/far shipped (Stretch defaults (1,1)); no OrbitCamera/ActionCamera, no depth.
      {"OrthographicCamera", "OrthographicCamera",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"Position.x", "Position", "Float", true, -0.0015059264f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Position.y", "Position.y", "Float", true, 0.0014562709f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Position.z", "Position.z", "Float", true, 10.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Target.x", "Target", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Target.y", "Target.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Target.z", "Target.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Up.x", "Up", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"Up.y", "Up.y", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Up.z", "Up.z", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Scale", "Scale", "Float", true, 1.0f, 0.0001f, 100.0f},
        {"Stretch.x", "Stretch", "Float", true, 1.0f, 0.0001f, 100.0f, Widget::Vec, {}, true, 2},
        {"Stretch.y", "Stretch.y", "Float", true, 1.0f, 0.0001f, 100.0f, Widget::Vec, {}, true, 1},
        {"NearFarClip.x", "NearFarClip", "Float", true, 0.1f, 0.0001f, 1000.0f, Widget::Vec, {}, true, 2},
        {"NearFarClip.y", "NearFarClip.y", "Float", true, 1000.0f, 0.0001f, 100000.0f, Widget::Vec, {}, true, 1},
        {"AspectRatio", "AspectRatio", "Float", true, 0.0f, 0.0f, 10.0f}},
       nullptr,
       "render.camera"},
      // CamPosition (TiXL Lib.render.camera.CamPosition, camera-A lane): emits the AMBIENT camera's
      // world-space Position / (unnormalized) view Direction / projection AspectRatio — CamPosition.cs:
      // 29-38 (invert context.WorldToCamera; transform (0,0,0,1) and (0,0,1,1); M22/M11). OUTPUT-ONLY
      // (CamPosition.cs has no Input slots). CONTEXT-reading → evaluate==nullptr; the values are cooked
      // once per frame by cookCameraValueOutputNodes (resident_camera_value_cook.cpp) onto extOut[1..7],
      // resolving the enclosing camera STRUCTURALLY (fork-camera-value-structural-enclosing-walk, named
      // in that header). The Command output is the TiXL execution-path hook (draws nothing; registered
      // as a no-op cmd op so a Command chain stays walkable through it).
      // OUTPUT PORTS ONLY, extOut index = port index: [0]=Command (no float slot), [1..3]=Position,
      // [4..6]=Direction, [7]=AspectRatio (exactly fills extOut[8]).
      // FORK fork-vec-output-as-n-scalar-ports: TiXL's Position/Direction are ONE Slot<Vector3> each;
      // here 3 Float ports each (the established output-side scalar-pack fork, RequestedResolution).
      {"CamPosition", "CamPosition",
       {{"Command", "Command", "Command", false},
        {"Position.x", "Position", "Float", false},
        {"Position.y", "Position.y", "Float", false},
        {"Position.z", "Position.z", "Float", false},
        {"Direction.x", "Direction", "Float", false},
        {"Direction.y", "Direction.y", "Float", false},
        {"Direction.z", "Direction.z", "Float", false},
        {"AspectRatio", "AspectRatio", "Float", false}},
       nullptr,
       "render.camera"},
      // CurrentCamMatrices (TiXL Lib.render.camera.CurrentCamMatrices, camera-A lane): emits the AMBIENT
      // camera's WorldToClipSpace as a 4-element Vector4[] — CurrentCamMatrices.cs:28-42 (worldToClip =
      // context.WorldToCamera * context.CameraToClipSpace; Transpose; rows M11-14/M21-24/M31-34/M41-44).
      // OUTPUT-ONLY (cs has no Input slots). CONTEXT-reading → evaluate==nullptr; cooked once per frame by
      // cookCameraValueOutputNodes (resident_camera_value_cook.cpp) onto extColorOut[1] — the matrix rides
      // the vec4-list channel (FORK fork-matrix-as-4-vec4-on-extColorOut, the TransformMatrix precedent:
      // TiXL wires ONE Slot<Vector4[]>; sw emits the SAME 4 float4 rows onto the ColorList channel).
      // Ambient camera = the structural enclosing-walk (fork-camera-value-structural-enclosing-walk).
      // The Command output is the TiXL execution-path hook (draws nothing; no-op cmd op).
      {"CurrentCamMatrices", "CurrentCamMatrices",
       {{"Command", "Command", "Command", false},
        {"WorldToClipSpace", "WorldToClipSpace", "ColorList", false}},
       nullptr,
       "render.camera"},
      // CameraWithRotation (TiXL Lib.render.camera.CameraWithRotation, camera-A lane): the
      // ROTATION-driven camera push — worldToCamera = T(−Position)·R (cs:114-115), R from Euler
      // (heading·pitch·roll, cs:78-84; euler = Rotation·RotationFactor + RotationOffset2, cs:66) or a
      // Quaternion (cs:87-88); camToClipSpace = PerspectiveFovRH + LensShift M31/M32 (cs:110-112).
      // Command in → Command out (per-item stamp via the exact LookAtRH decomposition of T(−pos)·R —
      // point_ops_camerawithrotation.h has the proof + the named forks: lensshift-drawrail-drop,
      // nonunit-quat-renormalized, no-point-rail-scope). Defaults = CameraWithRotation.t3 (AspectRatio
      // -1 → output aspect, Position.z 2.4141 — TiXL's exact value, NOT the Camera default).
      // Up / PositionOffset / AlsoOffsetTarget / RotationOffset are DEAD for the pushed matrices in
      // TiXL itself (cs:114-115) — carried for interface parity + the future CameraDefinition rail.
      {"CameraWithRotation", "CameraWithRotation",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"Position.x", "Position", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Position.y", "Position.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Position.z", "Position.z", "Float", true, 2.4141f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"RotationMode", "RotationMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"Euler", "Quaternion"}, true},
        {"Rotation.x", "Rotation", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"Rotation.y", "Rotation.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"Rotation.z", "Rotation.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"RotationFactor.x", "RotationFactor", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"RotationFactor.y", "RotationFactor.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"RotationFactor.z", "RotationFactor.z", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"RotationOffset2.x", "RotationOffset2", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"RotationOffset2.y", "RotationOffset2.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"RotationOffset2.z", "RotationOffset2.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"RotationQuaternion.x", "RotationQuaternion", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"RotationQuaternion.y", "RotationQuaternion.y", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"RotationQuaternion.z", "RotationQuaternion.z", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"RotationQuaternion.w", "RotationQuaternion.w", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"FOV", "FOV", "Float", true, 45.0f, 1.0f, 179.0f},
        {"PositionOffset.x", "PositionOffset", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"PositionOffset.y", "PositionOffset.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"PositionOffset.z", "PositionOffset.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"AlsoOffsetTarget", "AlsoOffsetTarget", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Enum,
         {"Off", "On"}, true},
        {"RotationOffset.x", "RotationOffset", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"RotationOffset.y", "RotationOffset.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"RotationOffset.z", "RotationOffset.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"LensShift.x", "LensShift", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
        {"LensShift.y", "LensShift.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ClipPlanes.x", "ClipPlanes", "Float", true, 0.01f, 0.0001f, 1000.0f, Widget::Vec, {}, true, 2},
        {"ClipPlanes.y", "ClipPlanes.y", "Float", true, 1000.0f, 0.0001f, 100000.0f, Widget::Vec, {}, true, 1},
        {"AspectRatio", "AspectRatio", "Float", true, -1.0f, -1.0f, 10.0f},
        {"Up.x", "Up", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"Up.y", "Up.y", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Up.z", "Up.z", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        // Reference output (CameraWithRotation.cs:13-14 Slot<Object> Reference; appended at the tail —
        // same rationale as Camera's Reference above).
        {"Reference", "Reference", "Object", false}},
       nullptr,
       "render.camera"},
      // BlendCameras (TiXL Lib.render.camera.BlendCameras, camera-A lane): slerp-blend N referenced
      // cameras by a float Index — BlendCameras.cs:24-106 + CameraDefinition.Blend (ICamera.cs:38-73:
      // lerp position/scalars, slerp the extracted orientations shortest-path, rebuild Target/Up from
      // the blended quaternion). Command in → Command out (per-item stamp from the blended camera's
      // rigid worldToCamera via the inverse-decomposition; point_ops_blendcameras.h has the forks:
      // ref-types-v1 Camera/CameraWithRotation only, mixed-aspect-fallback, lensshift-drawrail-drop).
      // CameraReferences = MultiInput CameraRef (the camera-A gather seam; = TiXL MultiInputSlot<Object>
      // GetCollectedTypedInputs). TiXL's error legs (no/invalid refs) do NOT evaluate the subtree →
      // empty chain. Defaults = BlendCameras.t3 (Index 0).
      {"BlendCameras", "BlendCameras",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"CameraReferences", "CameraReferences", "Object", true, 0.0f, 0.0f, 1.0f, Widget::Slider,
         {}, false, 1, true},
        {"Index", "Index", "Float", true, 0.0f, 0.0f, 10.0f},
        // CameraReference output (BlendCameras.cs:12-13): BlendCameras is itself an ICamera in TiXL
        // (nested blending). v1 the ref GATHER does not resolve a BlendCameras upstream
        // (fork-blendcameras-ref-types-v1) — the port exists for interface parity.
        {"CameraReference", "CameraReference", "Object", false}},
       nullptr,
       "render.camera"},
      // ActionCamera (TiXL Lib.render.camera.ActionCamera, camera-A lane): the STATEFUL fly-camera
      // CameraDefinition provider — no Command flow (its only output is the Reference handle,
      // ActionCamera.cs:10-11). Each frame it blends toward its ReferenceCamera's definition
      // (BlendToReferenceCamera·dt·60, cs:44-52) then integrates Position/Target from
      // Forward/Sideways/UpDown/Yaw/Pitch scaled by Speed/RotationSpeed·dt (cs:54-79). The
      // integration lives in resolveActionCameraDefinition (point_ops_actioncamera.cpp), driven when
      // a consumer (BlendCameras) resolves the ref — once per frame, state keyed by node identity.
      // Named forks in the leaf header: clock-ctx-time, no-input-writeback (edge-detected reset),
      // drawrail-only. Defaults = ActionCamera.t3 (Speed 0.01, RotationSpeed 0.2, rest 0).
      {"ActionCamera", "ActionCamera",
       {{"Reference", "Reference", "Object", false},
        {"ReferenceCamera", "ReferenceCamera", "Object", true, 0.0f, 0.0f, 1.0f, Widget::Slider,
         {}, false, 1, false, "", true},
        {"BlendToReferenceCamera", "BlendToReferenceCamera", "Float", true, 0.0f, 0.0f, 1.0f},
        {"TriggerReset", "TriggerReset", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Speed", "Speed", "Float", true, 0.01f, 0.0f, 10.0f},
        {"Forward", "Forward", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Sideways", "Sideways", "Float", true, 0.0f, -10.0f, 10.0f},
        {"UpDown", "UpDown", "Float", true, 0.0f, -10.0f, 10.0f},
        {"RotationSpeed", "RotationSpeed", "Float", true, 0.2f, 0.0f, 10.0f},
        {"Yaw", "Yaw", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Pitch", "Pitch", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Roll", "Roll", "Float", true, 0.0f, -10.0f, 10.0f},
        {"FOV", "FOV", "Float", true, 0.0f, -10.0f, 10.0f}},
       nullptr,
       "render.camera"},
      // ShiftCamera (TiXL Lib.render.camera.ShiftCamera): additive nudge on the AMBIENT CameraToClipSpace —
      // M31/M32 += Translation.XY, M33 += Translation.Z/1000 (ShiftCamera.cs:34-36) — around its Command
      // subtree (lens-shift/stereo-offset style). Command in → Command out (cookShiftCamera ACCUMULATES the
      // delta onto every !hasCamera subtree item; the RenderTarget executor applies it after composing the
      // item's projection). FORK (named, point_ops_shiftcamera.h): Scale/UniformScale inputs are DEAD in the
      // live TiXL Update body (computed, never used; the Matrix.Transformation block :24-30 is commented
      // out) → dropped, same precedent as the SetW/Visibility dead-input drops. .t3 default Translation=(0,0,0).
      {"ShiftCamera", "ShiftCamera",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"Translation.x", "Translation", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Translation.y", "Translation.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Translation.z", "Translation.z", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Vec, {}, true, 1}},
       nullptr,
       "render.camera"},
      // ReuseCamera (TiXL Lib.render.camera.ReuseCamera): renders its Command subtree through ANOTHER
      // camera's matrices — CameraReference (Slot<Object>) wired from a Camera's Reference output
      // (ReuseCamera.cs:31-41 push/pop). The cook DRIVER resolves the wire's source node into raw camera
      // params (resolveReferencedCamera, both legs); cookReuseCamera stamps them like cookCamera. A
      // missing/invalid reference drops the subtree (cs:17-29 warn + no render). FORKS (named,
      // point_ops_reusecamera.h): v1 provider = Camera only; gather-side-effects; no point-rail scope.
      {"ReuseCamera", "ReuseCamera",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"CameraReference", "CameraReference", "Object", true}},
       nullptr,
       "render.camera"},
      // OrbitCamera (TiXL Lib.render.camera.OrbitCamera): the ANIMATED orbit/wobble camera — eye orbits
      // the origin at DistanceToTarget, spun by SpinRate·time (+ the constant per-seed Perlin yaw,
      // OrbitCamera.cs:85-88), pitched by OrbitAngleAndWobble (:89); aim yaw/pitch/roll rotate the view
      // direction/up (:100-119); every *AngleAndWobble = vec2 (angle°, Perlin wobble amplitude°,
      // :143-154). Stamps the standard camera fields per item (the cookCamera push). .t3 defaults on
      // every port. FORKS (named, point_ops_orbitcamera.h): Damping DROPPED (cross-frame state; .t3
      // default 0 = stateless byte-faithful); OverrideTime non-zero-overrides-clock; Seed/
      // WobbleComplexity int-on-float; no point-rail scope.
      {"OrbitCamera", "OrbitCamera",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"CameraTargetPosition.x", "CameraTargetPosition", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"CameraTargetPosition.y", "CameraTargetPosition.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"CameraTargetPosition.z", "CameraTargetPosition.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"FOV", "FOV", "Float", true, 45.0f, 1.0f, 179.0f},
        {"DistanceToTarget", "DistanceToTarget", "Float", true, 3.0f, 0.0f, 100.0f},
        {"SpinRate", "SpinRate", "Float", true, 0.05f, -10.0f, 10.0f},
        {"SpinOffset", "SpinOffset", "Float", true, 0.0f, -360.0f, 360.0f},
        {"OrbitAngleAndWobble.x", "OrbitAngleAndWobble", "Float", true, 30.0f, -180.0f, 180.0f, Widget::Vec, {}, true, 2},
        {"OrbitAngleAndWobble.y", "OrbitAngleAndWobble.y", "Float", true, 5.0f, 0.0f, 180.0f, Widget::Vec, {}, true, 1},
        {"AimRollAngleAndWobble.x", "AimRollAngleAndWobble", "Float", true, 0.0f, -180.0f, 180.0f, Widget::Vec, {}, true, 2},
        {"AimRollAngleAndWobble.y", "AimRollAngleAndWobble.y", "Float", true, 5.0f, 0.0f, 180.0f, Widget::Vec, {}, true, 1},
        {"AimPitchAngleAndWobble.x", "AimPitchAngleAndWobble", "Float", true, 0.0f, -180.0f, 180.0f, Widget::Vec, {}, true, 2},
        {"AimPitchAngleAndWobble.y", "AimPitchAngleAndWobble.y", "Float", true, 0.0f, 0.0f, 180.0f, Widget::Vec, {}, true, 1},
        {"AimYawAngleAndWobble.x", "AimYawAngleAndWobble", "Float", true, 0.0f, -180.0f, 180.0f, Widget::Vec, {}, true, 2},
        {"AimYawAngleAndWobble.y", "AimYawAngleAndWobble.y", "Float", true, 0.0f, 0.0f, 180.0f, Widget::Vec, {}, true, 1},
        {"SpinAngleAndWobble.x", "SpinAngleAndWobble", "Float", true, 0.0f, -180.0f, 180.0f, Widget::Vec, {}, true, 2},
        {"SpinAngleAndWobble.y", "SpinAngleAndWobble.y", "Float", true, 0.0f, 0.0f, 180.0f, Widget::Vec, {}, true, 1},
        {"WobbleSpeed", "WobbleSpeed", "Float", true, 0.2f, 0.0f, 10.0f},
        {"WobbleComplexity", "WobbleComplexity", "Float", true, 2.0f, 1.0f, 8.0f},
        {"Seed", "Seed", "Float", true, 0.0f, 0.0f, 10000.0f},
        {"NearFarClip.x", "NearFarClip", "Float", true, 0.01f, 0.0001f, 1000.0f, Widget::Vec, {}, true, 2},
        {"NearFarClip.y", "NearFarClip.y", "Float", true, 1000.0f, 0.0001f, 100000.0f, Widget::Vec, {}, true, 1},
        {"RotationOffset.x", "RotationOffset", "Float", true, 0.0f, -180.0f, 180.0f, Widget::Vec, {}, true, 3},
        {"RotationOffset.y", "RotationOffset.y", "Float", true, 0.0f, -180.0f, 180.0f, Widget::Vec, {}, true, 1},
        {"RotationOffset.z", "RotationOffset.z", "Float", true, 0.0f, -180.0f, 180.0f, Widget::Vec, {}, true, 1},
        {"PositionOffset.x", "PositionOffset", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"PositionOffset.y", "PositionOffset.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"PositionOffset.z", "PositionOffset.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"AspectRatio", "AspectRatio", "Float", true, 0.0f, 0.0f, 10.0f},
        {"Up.x", "Up", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"Up.y", "Up.y", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Up.z", "Up.z", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"OverrideTime", "OverrideTime", "Float", true, 0.0f, -10000.0f, 10000.0f},
        // Reference OUTPUT (TiXL OrbitCamera.cs:16-17 Slot<Object>): ReuseCamera wire anchor — the
        // driver resolves this node's params via resolveReferencedCamera's OrbitCamera branch.
        {"Reference", "Reference", "Object", false}},
       nullptr,
       "render.camera"},
  };
  return specs;
}

}  // namespace sw
