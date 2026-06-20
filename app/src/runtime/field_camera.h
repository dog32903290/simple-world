// runtime/field_camera — PURE-MATH camera matrices for the 3D raymarch render path.
//
// ZONE: runtime (pure computation). NO Metal / platform include — this is plain float[16] math,
// the one real parity risk of raymarch3D isolated into a deterministically-testable leaf.
//
// PARITY AUTHORITY: external/tixl/Core/Utils/Geometry/GraphicsMath.cs (LookAtRH / PerspectiveFovRH)
// and Core/Rendering/TransformBufferLayout.cs (the Transforms cbuffer assembly). The native clone of
// TiXL's DEFAULT camera (no camera op connected) is EvaluationContext.SetDefaultCamera():
//   WorldToCamera   = LookAtRH((0,0,DefaultCameraDistance), origin, up=(0,1,0))
//   CameraToClipSpace= PerspectiveFovRH(45°.toRad, aspect, 0.01, 1000)
// with DefaultCameraDistance = 1/tan(45°·π/360) = 2.4142135 (GraphicsMath.cs:114). The raymarch
// shader unprojects rays through ClipSpaceToWorld and reads CameraToWorld._31.._33 for the view dir,
// so this header builds exactly those two derived matrices.
//
// CONVENTION (locked, mirrors the codebase's existing TM4/tXform precedent in
// point_ops_transformpoints.cpp): ROW-MAJOR storage, ROW-VECTOR transform `v·M` (= HLSL
// `mul(rowVec, M)` = System.Numerics `Vector4.Transform(v, M)`). m[row*4 + col]. LookAtRH /
// PerspectiveFovRH are transcribed element-for-element from GraphicsMath.cs (which is itself a
// System.Numerics reimplementation of SharpDX), so the matrices are byte-faithful to TiXL's. The
// HLSL cbuffer transpose dance in TransformBufferLayout.cs is a column-major-PACKING artifact (HLSL
// reads cbuffer float4s as columns by default); we DO NOT transpose here — instead the MSL side
// constructs its float4x4 from these rows so that `M_msl * v` reproduces `v·M_numerics` (see
// field_raymarch_template.metal). Net: identical math, no double-transpose. This convention is
// pinned by --selftest-field-raymarch (a wrong handedness / multiply-order flips its silhouette and
// depth teeth).
#pragma once

#include <cstdint>

namespace sw {

// TiXL GraphicsMath.cs:113-114 — the default camera fov + the derived default distance.
constexpr float kDefaultCamFovDegrees = 45.0f;
// 1 / tan(45° · π/360) = 2.41421356. Distance the default camera sits from the origin on +z.
float defaultCameraDistance();

// A 4x4 matrix, ROW-MAJOR, row-vector convention (m[r*4+c]). Plain POD so it has zero platform deps.
struct Mat4 {
  float m[16];  // row-major: m[r*4 + c]
};

// Identity matrix.
Mat4 mat4Identity();

// Row-major matrix multiply: r = a·b in row-vector convention (apply a then b), so
//   (v·r) = ((v·a)·b). Mirrors System.Numerics Matrix4x4.Multiply(a,b) and the codebase's tMatMul.
Mat4 mat4Mul(const Mat4& a, const Mat4& b);

// Transform a point (w=1) by the row-vector convention `v·M`, then perspective-divide by w.
// Mirrors GraphicsMath.TransformCoordinate. out[0..2] = transformed xyz / w.
void mat4TransformPointDivW(const Mat4& m, float x, float y, float z, float out[3]);

// LookAtRH — verbatim port of GraphicsMath.LookAtRH (right-handed, row-vector). Result is
// WorldToCamera: transforms a WORLD row-vector into CAMERA space via `v·M`.
//   zAxis = normalize(eye - target); xAxis = normalize(cross(up, zAxis)); yAxis = cross(zAxis, xAxis).
Mat4 lookAtRH(const float eye[3], const float target[3], const float up[3]);

// PerspectiveFovRH — verbatim port of GraphicsMath.PerspectiveFovRH (right-handed, row-vector,
// D3D-style clip z in [0,1]). Result is CameraToClipSpace. fovY in RADIANS.
//   yScale = 1/tan(fov/2); xScale = yScale/aspect; M33 = far/(near-far); M34 = -1;
//   M43 = near·far/(near-far).
Mat4 perspectiveFovRH(float fovY, float aspect, float zNear, float zFar);

// General 4x4 inverse (Gauss–Jordan / cofactor). Returns true on success; on a singular matrix
// returns false and leaves `out` = identity. Used to derive CameraToWorld = inverse(WorldToCamera)
// and ClipSpaceToCamera = inverse(CameraToClipSpace) — exactly TiXL's Matrix4x4.Invert calls in
// TransformBufferLayout.cs.
bool mat4Inverse(const Mat4& in, Mat4& out);

// The two derived matrices the raymarch shader actually consumes, packed for [[buffer(2)]].
// Built from a WorldToCamera + CameraToClipSpace pair, following TransformBufferLayout.cs:
//   clipSpaceToCamera = inverse(cameraToClipSpace)
//   cameraToWorld     = inverse(worldToCamera)
//   clipSpaceToWorld  = clipSpaceToCamera · cameraToWorld   (row-vector: clip→camera→world)
// Stored ROW-MAJOR (no cbuffer transpose — see header note). The shader uses clipSpaceToWorld for
// ray unproject and cameraToWorld._31.._33 for the camera forward axis.
struct RaymarchTransforms {
  Mat4 clipSpaceToWorld;  // unproject a clip-space ray endpoint to world (row-vector v·M)
  Mat4 cameraToWorld;     // camera→world; row 2 (m[8..10]) is the camera +z axis in world space
};

// Build the RaymarchTransforms for TiXL's DEFAULT camera (no camera op connected) at the given
// output aspect ratio (= width/height). This IS the parity target: eye=(0,0,DefaultCameraDistance),
// target=origin, up=(0,1,0), fov=45°, near=0.01, far=1000.
RaymarchTransforms defaultRaymarchTransforms(float aspect);

// Build the RaymarchTransforms for an arbitrary camera (eye/target/up + fov/near/far + aspect).
// Used by the golden to drive a known fixed camera. fovYDegrees in DEGREES (converted internally).
RaymarchTransforms raymarchTransforms(const float eye[3], const float target[3], const float up[3],
                                      float fovYDegrees, float aspect, float zNear, float zFar);

// objectToClipSpace — TransformBufferLayout.cs:13-16 multiply order (row-vector v·M):
//   ObjectToCamera    = objectToWorld · worldToCamera
//   ObjectToClipSpace = ObjectToCamera · cameraToClipSpace
// = objectToWorld · worldToCamera · cameraToClipSpace. Stored ROW-MAJOR (no cbuffer transpose —
// the MSL VS rebuilds its float4x4 so `mul4(M,v)` == `v·M_rowmajor`, mirroring field_raymarch's
// convention; see draw_quad_xf.metal). This is the ONE matrix the Layer2d quad VS reads
// (draw-Quad-vs.hlsl:54 `mul(float4(quadVertexInObject,0,1), ObjectToClipSpace)`).
Mat4 objectToClipSpace(const Mat4& objectToWorld, const Mat4& worldToCamera,
                       const Mat4& cameraToClipSpace);

// ── Layer2d transform-stack (Cut 2) ──────────────────────────────────────────────────────────────
// PARITY AUTHORITY: external/tixl/Operators/Lib/render/_/_ProcessLayer2d.cs + GraphicsMath.cs
// CreateTransformationMatrix + ApplyTransformMatrix.cs.
//
// NET ObjectToWorld delivered to the quad VS = S·R·T (row-vector v·M), where S=scale, R=rotate-Z,
// T=translate. BACKWARD-TRACE of the double-transpose (the trap the blueprint flagged):
//   _ProcessLayer2d builds M = CreateTransformationMatrix(...)  (with scalingCenter=rotationCenter=0,
//     scalingRotation=Identity → M = scale·rotation·translation in System.Numerics row-vector order),
//     then `.Transpose()`s it purely to pack the HLSL cbuffer column-major (its _matrix[] rows are the
//     COLUMNS of M).
//   ApplyTransformMatrix reads those rows back (ToMatrixFromRows = Mᵀ), `.Transpose()`s AGAIN (→ M),
//     and sets context.ObjectToWorld = Multiply(M, prevObjectToWorld=Identity) = M.
//   → the two transposes CANCEL; the net ObjectToWorld is plain M = S·R·T. SW stores matrices
//     ROW-MAJOR row-vector already (field_camera convention), so we build S·R·T directly via mat4Mul
//     — NO transpose dance (it would be a double-cancel of a cancel).
//
// ScaleMode aspect coupling (_ProcessLayer2d.cs:37,49-101): viewAspect = CameraToClipSpace.M22/M11
//   (= aspect, since M11=yScale/aspect, M22=yScale). imageAspect = srcW/srcH. The mode scales scale.X
//   (and sometimes scale.Y) BEFORE the SRT is built. layer2dScaleModeApply applies the mode in place.
enum class Layer2dScaleMode {
  FitHeight = 0,
  FitWidth = 1,
  FitBoth = 2,
  Cover = 3,
  Stretch = 4,
  MatchPixelResolution = 5,  // DEFERRED (needs RequestedResolution context) — see field_camera.cpp
};

// viewAspect = M22/M11 of a CameraToClipSpace (PerspectiveFovRH) — _ProcessLayer2d.cs:37, faithful.
float viewAspectFromClip(const Mat4& cameraToClipSpace);

// Apply the ScaleMode to (scaleX,scaleY) in place — verbatim _ProcessLayer2d.cs:49-101 (the implemented
// modes; MatchPixelResolution is a named DEFER, treated as Stretch). FitBoth resolves to FitHeight or
// FitWidth by imageAspect vs viewAspect first (cs:50-53).
void layer2dScaleModeApply(Layer2dScaleMode mode, float imageAspect, float viewAspect,
                           float& scaleX, float& scaleY);

// Build the Layer2d net ObjectToWorld = S·R·T (row-vector). scale already includes the Stretch×Scale
// product AND the ScaleMode aspect adjustment (call layer2dScaleModeApply first). rotateZdeg = TiXL
// Rotate (degrees → radians internally, named fork). translate = (posX,posY,posZ).
//   S = scale(scaleX,scaleY,1); R = rotation about +Z by roll (CreateFromYawPitchRoll(0,0,roll) =
//       CreateRotationZ(roll), row-vector); T = translate(tx,ty,tz). M = mat4Mul(mat4Mul(S,R),T).
Mat4 layer2dObjectToWorld(float scaleX, float scaleY, float rotateZdeg, float tx, float ty, float tz);

// The DEFAULT-camera FORWARD matrices Layer2d needs (the inverses live in RaymarchTransforms; the
// quad VS needs WorldToCamera + CameraToClipSpace forward). Same default as defaultRaymarchTransforms:
// eye=(0,0,DefaultCameraDistance), target=origin, up=(0,1,0), fov=45°, near=0.01, far=1000.
// EvaluationContext.SetDefaultCamera. `aspect` = output width/height (the RESOLUTION-pin aspect).
struct LayerCameraForward {
  Mat4 worldToCamera;
  Mat4 cameraToClipSpace;
};
LayerCameraForward defaultLayerCameraForward(float aspect);

// --selftest-field-camera entry (field_camera_selftest.cpp). PURE MATH, zero GPU.
int runFieldCameraSelfTest(bool injectBug);

// --selftest-layer2d entry (point_ops_layer2d.cpp). RENDER tooth (GPU): a solid-color source
// through Layer2d at default camera; asserts center=clamp(Color*tex) + far-corner=background.
// injectBug = drop the ObjectToClipSpace mul (raw-clip VS) → quad mis-placed → corner reads quad
// color → RED. Declared here (shares the field_camera convention) but defined in point_ops_layer2d.cpp.
int runLayer2dSelfTest(bool injectBug);

}  // namespace sw
