// runtime/field_camera.cpp — see field_camera.h. Pure float[16] camera math; ZERO Metal.
//
// Every matrix routine is transcribed element-for-element from
//   external/tixl/Core/Utils/Geometry/GraphicsMath.cs   (LookAtRH / PerspectiveFovRH)
//   external/tixl/Core/Rendering/TransformBufferLayout.cs (the derived-matrix assembly)
// in ROW-MAJOR storage / ROW-VECTOR convention (m[r*4+c]; transform = v·M). See header for the
// convention lock and why we do NOT replay TiXL's HLSL-cbuffer transpose.
#include "runtime/field_camera.h"

#include <cmath>
#include <cstring>

namespace sw {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kFovEpsilon = 0.0001f;  // GraphicsMath.cs:109
const float kMaxFov = kPi - kFovEpsilon;

void normalize3(float v[3]) {
  float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (len > 0.0f) { v[0] /= len; v[1] /= len; v[2] /= len; }
}
void cross3(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}
float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
}  // namespace

float defaultCameraDistance() {
  // GraphicsMath.cs:114 — 1 / tan(DefaultCamFovDegrees · π / 360).
  return 1.0f / std::tan(kDefaultCamFovDegrees * kPi / 360.0f);
}

Mat4 mat4Identity() {
  Mat4 r{};
  r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
  return r;
}

Mat4 mat4Mul(const Mat4& a, const Mat4& b) {
  // Row-major a·b: r[i][j] = Σ_k a[i][k]·b[k][j]. Matches System.Numerics Matrix4x4.Multiply and the
  // codebase's tMatMul (point_ops_transformpoints.cpp:258).
  Mat4 r{};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) {
      float s = 0.0f;
      for (int k = 0; k < 4; ++k) s += a.m[i * 4 + k] * b.m[k * 4 + j];
      r.m[i * 4 + j] = s;
    }
  return r;
}

void mat4TransformPointDivW(const Mat4& m, float x, float y, float z, float out[3]) {
  // Row-vector (x,y,z,1)·M, then divide by w. Mirrors GraphicsMath.TransformCoordinate.
  float in[4] = {x, y, z, 1.0f};
  float o[4];
  for (int j = 0; j < 4; ++j) {
    float s = 0.0f;
    for (int i = 0; i < 4; ++i) s += in[i] * m.m[i * 4 + j];
    o[j] = s;
  }
  float w = (o[3] != 0.0f) ? o[3] : 1.0f;
  out[0] = o[0] / w;
  out[1] = o[1] / w;
  out[2] = o[2] / w;
}

Mat4 lookAtRH(const float eye[3], const float target[3], const float up[3]) {
  // GraphicsMath.cs:10-22 verbatim (System.Numerics impl of SharpDX LookAtRH).
  float zAxis[3] = {eye[0] - target[0], eye[1] - target[1], eye[2] - target[2]};
  normalize3(zAxis);
  float xAxis[3];
  cross3(up, zAxis, xAxis);
  normalize3(xAxis);
  float yAxis[3];
  cross3(zAxis, xAxis, yAxis);

  Mat4 r{};
  // The C# constructor fills ROWS in this order (each line = one matrix row):
  //   xAxis.X, yAxis.X, zAxis.X, 0
  //   xAxis.Y, yAxis.Y, zAxis.Y, 0
  //   xAxis.Z, yAxis.Z, zAxis.Z, 0
  //   -dot(x,eye), -dot(y,eye), -dot(z,eye), 1
  r.m[0] = xAxis[0]; r.m[1] = yAxis[0]; r.m[2] = zAxis[0]; r.m[3] = 0.0f;
  r.m[4] = xAxis[1]; r.m[5] = yAxis[1]; r.m[6] = zAxis[1]; r.m[7] = 0.0f;
  r.m[8] = xAxis[2]; r.m[9] = yAxis[2]; r.m[10] = zAxis[2]; r.m[11] = 0.0f;
  r.m[12] = -dot3(xAxis, eye); r.m[13] = -dot3(yAxis, eye); r.m[14] = -dot3(zAxis, eye); r.m[15] = 1.0f;
  return r;
}

Mat4 perspectiveFovRH(float fovY, float aspect, float zNear, float zFar) {
  // GraphicsMath.cs:27-54 verbatim. NOTE the M.. names there are 1-based row.col (M11=m[0]).
  fovY = std::fmax(kFovEpsilon, std::fmin(fovY, kMaxFov));
  aspect = std::fmax(kFovEpsilon, aspect);
  zNear = std::fmax(kFovEpsilon, zNear);
  zFar = std::fmax(zNear + kFovEpsilon, zFar);

  float yScale = 1.0f / std::tan(fovY * 0.5f);
  float xScale = yScale / aspect;
  float diff = zNear - zFar;

  Mat4 r{};
  r.m[0] = xScale;                    // M11
  r.m[5] = yScale;                    // M22
  r.m[10] = zFar / diff;              // M33
  r.m[11] = -1.0f;                    // M34
  r.m[14] = zNear * zFar / diff;      // M43
  // M44 = 0 (left zero) — this is a perspective matrix, w' = -z.
  return r;
}

Mat4 orthoRH(float width, float height, float zNear, float zFar) {
  // System.Numerics Matrix4x4.CreateOrthographic verbatim (the .NET reference impl TiXL's
  // OrthographicCamera.cs:36 calls). RH, row-vector, clip z in [0,1]. NO fov/aspect epsilon clamps —
  // .NET's CreateOrthographic does none (it is a pure element fill); we mirror that exactly so the
  // matrix is byte-faithful. A degenerate width/height/zRange would divide-by-zero, identical to .NET.
  Mat4 r{};
  r.m[0] = 2.0f / width;                    // M11
  r.m[5] = 2.0f / height;                   // M22
  r.m[10] = 1.0f / (zNear - zFar);          // M33
  r.m[14] = zNear / (zNear - zFar);         // M43
  r.m[15] = 1.0f;                           // M44 = 1 (orthographic: w stays 1, no perspective divide)
  return r;
}

bool mat4Inverse(const Mat4& in, Mat4& out) {
  // Standard cofactor inverse (the same one most engines ship; numerically fine for camera matrices).
  const float* m = in.m;
  float inv[16];
  inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
  inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
  inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
  inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
  inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
  inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
  inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
  inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
  inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
  inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
  inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
  inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
  inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
  inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
  inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
  inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];

  float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
  if (det == 0.0f) { out = mat4Identity(); return false; }
  float invDet = 1.0f / det;
  for (int i = 0; i < 16; ++i) out.m[i] = inv[i] * invDet;
  return true;
}

namespace {
RaymarchTransforms buildTransforms(const Mat4& worldToCamera, const Mat4& cameraToClipSpace) {
  // TransformBufferLayout.cs:10-15 — derive the inverses, compose ClipSpaceToWorld.
  Mat4 clipSpaceToCamera, cameraToWorld;
  mat4Inverse(cameraToClipSpace, clipSpaceToCamera);
  mat4Inverse(worldToCamera, cameraToWorld);
  // ClipSpaceToWorld = clipSpaceToCamera · cameraToWorld (row-vector: clip→camera→world).
  RaymarchTransforms t;
  t.clipSpaceToWorld = mat4Mul(clipSpaceToCamera, cameraToWorld);
  t.cameraToWorld = cameraToWorld;
  return t;
}
}  // namespace

RaymarchTransforms raymarchTransforms(const float eye[3], const float target[3], const float up[3],
                                      float fovYDegrees, float aspect, float zNear, float zFar) {
  Mat4 worldToCamera = lookAtRH(eye, target, up);
  Mat4 cameraToClipSpace = perspectiveFovRH(fovYDegrees * kPi / 180.0f, aspect, zNear, zFar);
  return buildTransforms(worldToCamera, cameraToClipSpace);
}

RaymarchTransforms defaultRaymarchTransforms(float aspect) {
  // EvaluationContext.SetDefaultCamera() (EvaluationContext.cs:89-95): eye=(0,0,DefaultCameraDistance),
  // target=origin, up=(0,1,0), fov=45°, near=0.01, far=1000.
  float eye[3] = {0.0f, 0.0f, defaultCameraDistance()};
  float target[3] = {0.0f, 0.0f, 0.0f};
  float up[3] = {0.0f, 1.0f, 0.0f};
  return raymarchTransforms(eye, target, up, kDefaultCamFovDegrees, aspect, 0.01f, 1000.0f);
}

Mat4 objectToClipSpace(const Mat4& objectToWorld, const Mat4& worldToCamera,
                       const Mat4& cameraToClipSpace) {
  // TransformBufferLayout.cs:13-16. System.Numerics Multiply(a,b)=a·b (row-vector), so
  // objectToWorld · worldToCamera · cameraToClipSpace. mat4Mul is left-to-right associative; the
  // grouping is irrelevant (matrix mul is associative) but we mirror TiXL's two-step form.
  Mat4 objectToCamera = mat4Mul(objectToWorld, worldToCamera);
  return mat4Mul(objectToCamera, cameraToClipSpace);
}

float viewAspectFromClip(const Mat4& cameraToClipSpace) {
  // _ProcessLayer2d.cs:37 — viewAspect = M22/M11. For PerspectiveFovRH, M11=yScale/aspect (m[0]),
  // M22=yScale (m[5]), so M22/M11 == aspect. Faithful (derive, don't hardcode).
  float m11 = cameraToClipSpace.m[0];
  return (m11 != 0.0f) ? cameraToClipSpace.m[5] / m11 : 1.0f;
}

void layer2dScaleModeApply(Layer2dScaleMode mode, float imageAspect, float viewAspect, float& scaleX,
                           float& scaleY) {
  // _ProcessLayer2d.cs:50-101 verbatim. FitBoth first resolves to FitHeight/FitWidth (cs:50-53).
  if (mode == Layer2dScaleMode::FitBoth)
    mode = (imageAspect < viewAspect) ? Layer2dScaleMode::FitHeight : Layer2dScaleMode::FitWidth;

  switch (mode) {
    case Layer2dScaleMode::FitHeight:
      scaleX *= imageAspect;  // cs:58
      break;
    case Layer2dScaleMode::FitWidth:
      scaleX *= viewAspect;                 // cs:62
      scaleY *= viewAspect / imageAspect;   // cs:63
      break;
    case Layer2dScaleMode::Cover: {
      float ratio = viewAspect / imageAspect;  // cs:67
      if (ratio > 1.0f) {
        scaleX *= viewAspect;                // cs:70
        scaleY *= viewAspect / imageAspect;  // cs:71
      } else {
        scaleX *= viewAspect / ratio;                // cs:75
        scaleY *= viewAspect / imageAspect / ratio;  // cs:76
      }
      break;
    }
    case Layer2dScaleMode::Stretch:
      scaleX *= viewAspect;  // cs:80
      break;
    case Layer2dScaleMode::MatchPixelResolution:
      // DEFERRED (named fork): needs context.RequestedResolution (cs:83-99), which the Cut-2 seam does
      // not thread. Treated as Stretch (the closest implemented mode) so the op stays defined; NOT
      // pixel-faithful for this mode. Camera op + RequestedResolution context = a later cut.
      scaleX *= viewAspect;
      break;
    case Layer2dScaleMode::FitBoth:
      break;  // unreachable (resolved above); explicit for -Wswitch
  }
}

Mat4 layer2dObjectToWorld(float scaleX, float scaleY, float rotateZdeg, float tx, float ty, float tz) {
  // GraphicsMath.CreateTransformationMatrix with scalingCenter=rotationCenter=0, scalingRotation=
  // Identity reduces to M = S·R·T (row-vector). Build each row-vector matrix, then mat4Mul left→right.
  Mat4 S = mat4Identity();
  S.m[0] = scaleX; S.m[5] = scaleY; S.m[10] = 1.0f;

  // R = rotation about +Z by roll (CreateFromYawPitchRoll(0,0,roll) == CreateRotationZ(roll),
  // System.Numerics row-vector): [[cos,sin,0,0],[-sin,cos,0,0],[0,0,1,0],[0,0,0,1]]. deg→rad (fork).
  float roll = rotateZdeg * kPi / 180.0f;
  float cz = std::cos(roll), sz = std::sin(roll);
  Mat4 R = mat4Identity();
  R.m[0] = cz;  R.m[1] = sz;
  R.m[4] = -sz; R.m[5] = cz;

  Mat4 T = mat4Identity();
  T.m[12] = tx; T.m[13] = ty; T.m[14] = tz;

  return mat4Mul(mat4Mul(S, R), T);
}

Mat4 groupObjectToWorld(float sx, float sy, float sz, float yawDeg, float pitchDeg, float rollDeg,
                        float tx, float ty, float tz) {
  // TiXL Group.cs:44 CreateTransformationMatrix(scalingCenter=0, scalingRotation=Identity,
  // scaling=(sx,sy,sz), rotationCenter=0, rotation=CreateFromYawPitchRoll(yaw,pitch,roll),
  // translation=(tx,ty,tz)). With centers=0 + scalingRotation=Identity the GraphicsMath.cs:84-96 product
  // collapses to M = S·R·T (row-vector v·M). Build each, then mat4Mul left→right (apply S, then R, then T).
  Mat4 S = mat4Identity();
  S.m[0] = sx; S.m[5] = sy; S.m[10] = sz;

  // R = CreateFromQuaternion(CreateFromYawPitchRoll(yaw,pitch,roll)). Transcribed element-for-element
  // from System.Numerics: Quaternion.CreateFromYawPitchRoll → (x,y,z,w), then Matrix4x4.CreateFromQuaternion
  // (ROW-MAJOR, row-vector convention — same storage as the rest of this file). deg→rad (named fork, like
  // Layer2d's rotateZdeg). yaw=Y, pitch=X, roll=Z (Group.cs:40-42).
  float yaw = yawDeg * kPi / 180.0f, pitch = pitchDeg * kPi / 180.0f, roll = rollDeg * kPi / 180.0f;
  float sy2 = std::sin(roll * 0.5f), cy2 = std::cos(roll * 0.5f);   // System.Numerics names: roll→sr/cr
  float sp = std::sin(pitch * 0.5f), cp = std::cos(pitch * 0.5f);
  float syaw = std::sin(yaw * 0.5f), cyaw = std::cos(yaw * 0.5f);
  // Quaternion.CreateFromYawPitchRoll (System.Numerics reference impl):
  float qx = cyaw * sp * cy2 + syaw * cp * sy2;
  float qy = syaw * cp * cy2 - cyaw * sp * sy2;
  float qz = cyaw * cp * sy2 - syaw * sp * cy2;
  float qw = cyaw * cp * cy2 + syaw * sp * sy2;
  // Matrix4x4.CreateFromQuaternion (System.Numerics, row-vector / row-major):
  float xx = qx * qx, yy = qy * qy, zz = qz * qz;
  float xy = qx * qy, wz = qz * qw, xz = qz * qx, wy = qy * qw, yz = qy * qz, wx = qx * qw;
  Mat4 R = mat4Identity();
  R.m[0] = 1.0f - 2.0f * (yy + zz); R.m[1] = 2.0f * (xy + wz);        R.m[2] = 2.0f * (xz - wy);
  R.m[4] = 2.0f * (xy - wz);        R.m[5] = 1.0f - 2.0f * (zz + xx); R.m[6] = 2.0f * (yz + wx);
  R.m[8] = 2.0f * (xz + wy);        R.m[9] = 2.0f * (yz - wx);        R.m[10] = 1.0f - 2.0f * (yy + xx);

  Mat4 T = mat4Identity();
  T.m[12] = tx; T.m[13] = ty; T.m[14] = tz;

  return mat4Mul(mat4Mul(S, R), T);
}

LayerCameraForward defaultLayerCameraForward(float aspect) {
  // Same default camera as defaultRaymarchTransforms — but the FORWARD pair the quad VS consumes
  // (WorldToCamera + CameraToClipSpace), not the unproject inverses.
  float eye[3] = {0.0f, 0.0f, defaultCameraDistance()};
  float target[3] = {0.0f, 0.0f, 0.0f};
  float up[3] = {0.0f, 1.0f, 0.0f};
  LayerCameraForward f;
  f.worldToCamera = lookAtRH(eye, target, up);
  f.cameraToClipSpace = perspectiveFovRH(kDefaultCamFovDegrees * kPi / 180.0f, aspect, 0.01f, 1000.0f);
  return f;
}

LayerCameraForward stampedCameraForward(const float eye[3], const float target[3], const float up[3],
                                        bool ortho, float fovDeg, float scale, const float stretch[2],
                                        float aspect, float zNear, float zFar) {
  LayerCameraForward f;
  f.worldToCamera = lookAtRH(eye, target, up);  // shared by both projection types
  if (ortho) {
    // OrthographicCamera.cs:33 size = Stretch · Scale · (aspect,1).
    float sizeX = stretch[0] * scale * aspect;
    float sizeY = stretch[1] * scale * 1.0f;
    f.cameraToClipSpace = orthoRH(sizeX, sizeY, zNear, zFar);
  } else {
    f.cameraToClipSpace = perspectiveFovRH(fovDeg * kPi / 180.0f, aspect, zNear, zFar);
  }
  return f;
}

void pointCameraMatrices(float aspect, float outObjectToCamera[16], float outCameraToWorld[16]) {
  // v1 fork (default camera + identity ObjectToWorld) — see header. WorldToCamera = LookAtRH(default).
  LayerCameraForward fwd = defaultLayerCameraForward(aspect);  // reuses the SetDefaultCamera params
  // ObjectToCamera = ObjectToWorld(Identity) · WorldToCamera = WorldToCamera. Copy row-major.
  std::memcpy(outObjectToCamera, fwd.worldToCamera.m, 16 * sizeof(float));
  // CameraToWorld = inverse(WorldToCamera) (TransformBufferLayout.cs Matrix4x4.Invert). On a singular
  // matrix mat4Inverse leaves identity (never happens for a valid LookAtRH).
  Mat4 cameraToWorld;
  mat4Inverse(fwd.worldToCamera, cameraToWorld);
  std::memcpy(outCameraToWorld, cameraToWorld.m, 16 * sizeof(float));
}

}  // namespace sw
