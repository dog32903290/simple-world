// repetitionpoints — GPU NAMED FORK of external/tixl
// .../point/generate/RepetitionPoints.cs (a CPU StructuredList generator; TiXL has NO .hlsl
// for this op). thread i computes the i-th point by the exact per-point recipe in
// RepetitionPoints.cs Update() (lines 40-71). No input bag.
//
// PER-POINT RECIPE (RepetitionPoints.cs:42-69, verbatim):
//   u            = i + 1 + Phase                                                    (.cs:44)
//   translation  = Translate * u + StartPosition                                    (.cs:46)
//   rotation     = Quaternion.CreateFromYawPitchRoll(Rotate.X/360*2pi*u,            (.cs:47-49)
//                                                    Rotate.Y/360*2pi*u,            // yaw=X
//                                                    Rotate.Z/360*2pi*u)            // pitch=Y roll=Z
//   scale        = (Vector3.One - Vector3(Scale)) * u + Vector3.One                 (.cs:50)
//   transform    = GraphicsMath.CreateTransformationMatrix(
//                      scalingCenter:0, scalingRotation:identity, scaling:scale,
//                      rotationCenter:Pivot, rotation:rotation, translation:translation)  (.cs:52-57)
//   Position     = Vector4.Transform((0,0,0,1), transform).xyz                      (.cs:61-62)
//   F1           = scale.Length() / sqrt(3) + StartW                               (.cs:65, Vector3.One.Length()==sqrt(3))
//   Orientation  = rotation;  F2 = 1;  Color = white;  Scale-attr = (1,1,1)        (.cs:66-69)
//
// AddSeparator (RepetitionPoints.cs:72-75 + Point.Separator() in Core/DataTypes/Point.cs:37-48):
//   appends ONE Point.Separator() at index Count: Position 0, F1 1, Orientation identity,
//   Color white, Scale = (NaN,NaN,NaN), F2 1. The host sizes the bag to Count+1 in that case.
//
// ============================ CONVENTION FORK (NAMED) ============================
// TiXL runs CreateTransformationMatrix in System.Numerics ROW-VECTOR convention:
//   - matrices are row-major, translation in M41/M42/M43 (row 4),
//   - Matrix A*B multiplies left-to-right in that order,
//   - Vector4.Transform(v, M) computes  v * M  (row vector on the LEFT).
// Metal float4x4 is column-major with M*v semantics — a naive port would silently transpose.
// So we DO NOT use float4x4: we replicate System.Numerics directly with a row4x4 struct
// (rows[0..3] are matrix rows) and a left-multiply transform. Matrix factor order, the
// conjugate-of-identity term, and the M41-translation layout are copied VERBATIM from
// external/tixl/Core/Utils/Geometry/GraphicsMath.cs:56-97. Nothing is reordered or invented.
// ================================================================================
#include <metal_stdlib>
#include "tixl_point.h"             // SwPoint (64B)
#include "repetitionpoints_params.h" // RepetitionPointsParams, RepetitionPointsBinding
using namespace metal;

// Row-major 4x4 in System.Numerics layout: rows[r] = (M[r][0..3]); translation in rows[3].xyz.
struct row4x4 { float4 r0, r1, r2, r3; };

static row4x4 rowIdentity() {
  return row4x4{ float4(1,0,0,0), float4(0,1,0,0), float4(0,0,1,0), float4(0,0,0,1) };
}

// System.Numerics  C = A * B   (row-major). C[i][j] = sum_k A[i][k] * B[k][j].
static row4x4 rowMul(row4x4 a, row4x4 b) {
  // columns of b
  float4 bc0 = float4(b.r0.x, b.r1.x, b.r2.x, b.r3.x);
  float4 bc1 = float4(b.r0.y, b.r1.y, b.r2.y, b.r3.y);
  float4 bc2 = float4(b.r0.z, b.r1.z, b.r2.z, b.r3.z);
  float4 bc3 = float4(b.r0.w, b.r1.w, b.r2.w, b.r3.w);
  row4x4 c;
  c.r0 = float4(dot(a.r0, bc0), dot(a.r0, bc1), dot(a.r0, bc2), dot(a.r0, bc3));
  c.r1 = float4(dot(a.r1, bc0), dot(a.r1, bc1), dot(a.r1, bc2), dot(a.r1, bc3));
  c.r2 = float4(dot(a.r2, bc0), dot(a.r2, bc1), dot(a.r2, bc2), dot(a.r2, bc3));
  c.r3 = float4(dot(a.r3, bc0), dot(a.r3, bc1), dot(a.r3, bc2), dot(a.r3, bc3));
  return c;
}

// System.Numerics  Vector4.Transform(v, M) = v * M  (row vector on the left).
static float4 rowTransform(float4 v, row4x4 m) {
  return float4(v.x * m.r0.x + v.y * m.r1.x + v.z * m.r2.x + v.w * m.r3.x,
                v.x * m.r0.y + v.y * m.r1.y + v.z * m.r2.y + v.w * m.r3.y,
                v.x * m.r0.z + v.y * m.r1.z + v.z * m.r2.z + v.w * m.r3.z,
                v.x * m.r0.w + v.y * m.r1.w + v.z * m.r2.w + v.w * m.r3.w);
}

// Matrix4x4.CreateTranslation(t) — System.Numerics: translation in row 4 (M41,M42,M43).
static row4x4 rowTranslation(float3 t) {
  row4x4 m = rowIdentity();
  m.r3 = float4(t.x, t.y, t.z, 1.0f);
  return m;
}

// Matrix4x4.CreateScale(s) — diagonal.
static row4x4 rowScale(float3 s) {
  return row4x4{ float4(s.x,0,0,0), float4(0,s.y,0,0), float4(0,0,s.z,0), float4(0,0,0,1) };
}

// Matrix4x4.CreateFromQuaternion(q) — VERBATIM System.Numerics (row-major).
// (System.Numerics Matrix4x4.CreateFromQuaternion source.) q assumed unit.
static row4x4 rowFromQuaternion(float4 q) {
  float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
  float xy = q.x * q.y, wz = q.z * q.w, xz = q.z * q.x, wy = q.y * q.w;
  float yz = q.y * q.z, wx = q.x * q.w;
  row4x4 m = rowIdentity();
  m.r0 = float4(1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz),        2.0f * (xz - wy),        0.0f);
  m.r1 = float4(2.0f * (xy - wz),        1.0f - 2.0f * (zz + xx), 2.0f * (yz + wx),        0.0f);
  m.r2 = float4(2.0f * (xz + wy),        2.0f * (yz - wx),        1.0f - 2.0f * (xx + yy), 0.0f);
  m.r3 = float4(0.0f, 0.0f, 0.0f, 1.0f);
  return m;
}

// Quaternion.CreateFromYawPitchRoll(yaw, pitch, roll) — VERBATIM System.Numerics.
// roll(Z) around Z, pitch(X) around X, yaw(Y) around Y; composed as in the .NET source.
static float4 quatFromYawPitchRoll(float yaw, float pitch, float roll) {
  float halfRoll = roll * 0.5f;
  float sr = sin(halfRoll), cr = cos(halfRoll);
  float halfPitch = pitch * 0.5f;
  float sp = sin(halfPitch), cp = cos(halfPitch);
  float halfYaw = yaw * 0.5f;
  float sy = sin(halfYaw), cy = cos(halfYaw);
  float4 q;
  q.x = cy * sp * cr + sy * cp * sr;
  q.y = sy * cp * cr - cy * sp * sr;
  q.z = cy * cp * sr - sy * sp * cr;
  q.w = cy * cp * cr + sy * sp * sr;
  return q;
}

// GraphicsMath.CreateTransformationMatrix — VERBATIM factor order from
// external/tixl/Core/Utils/Geometry/GraphicsMath.cs:56-97. scalingCenter is 0 and
// scalingRotation is identity here (RepetitionPoints.cs:52-54), but we keep the full chain
// so the port is the source recipe, not a hand-simplified one.
static row4x4 createTransformationMatrix(float3 scalingCenter, float4 scalingRotation,
                                         float3 scaling, float3 rotationCenter,
                                         float4 rotation, float3 translation) {
  row4x4 scalingRotationMatrix        = rowFromQuaternion(scalingRotation);
  row4x4 rotationMatrix               = rowFromQuaternion(rotation);
  // Quaternion.Conjugate(q) = (-x,-y,-z, w)
  row4x4 inverseScalingRotationMatrix = rowFromQuaternion(float4(-scalingRotation.xyz, scalingRotation.w));
  row4x4 scalingCenterTranslation         = rowTranslation(-scalingCenter);
  row4x4 rotationCenterTranslation        = rowTranslation(-rotationCenter);
  row4x4 inverseScalingCenterTranslation  = rowTranslation(scalingCenter);
  row4x4 inverseRotationCenterTranslation = rowTranslation(rotationCenter);
  row4x4 scalingMatrix                = rowScale(scaling);
  row4x4 finalTranslationMatrix       = rowTranslation(translation);

  // scalingCenterTranslation * inverseScalingRotationMatrix * scalingMatrix * scalingRotationMatrix
  //  * inverseScalingCenterTranslation * rotationCenterTranslation * rotationMatrix
  //  * inverseRotationCenterTranslation * finalTranslationMatrix
  row4x4 m = rowMul(scalingCenterTranslation, inverseScalingRotationMatrix);
  m = rowMul(m, scalingMatrix);
  m = rowMul(m, scalingRotationMatrix);
  m = rowMul(m, inverseScalingCenterTranslation);
  m = rowMul(m, rotationCenterTranslation);
  m = rowMul(m, rotationMatrix);
  m = rowMul(m, inverseRotationCenterTranslation);
  m = rowMul(m, finalTranslationMatrix);
  return m;
}

kernel void repetitionpoints(device SwPoint*                  pts [[buffer(REPETITION_Points)]],
                             constant RepetitionPointsParams&  P   [[buffer(REPETITION_Params)]],
                             uint3                             tid [[thread_position_in_grid]]) {
  uint count = P.Count;
  bool addSep = P.AddSeparator > 0.5f;
  uint total = count + (addSep ? 1u : 0u);
  if (tid.x >= total) return;
  uint i = tid.x;

  // Separator slot (RepetitionPoints.cs:72-75 -> Point.Separator(), Point.cs:37-48).
  if (addSep && i == count) {
    SwPoint sep;
    sep.Position = float3(0.0f, 0.0f, 0.0f);
    sep.FX1      = 1.0f;
    sep.Rotation = float4(0.0f, 0.0f, 0.0f, 1.0f);             // Quaternion.Identity
    sep.Color    = float4(1.0f, 1.0f, 1.0f, 1.0f);
    sep.Scale    = float3(NAN, NAN, NAN);                      // Separator marker
    sep.FX2      = 1.0f;
    pts[i] = sep;
    return;
  }

  const float toRadStep = (1.0f / 360.0f) * (2.0f * M_PI_F);   // /360 * 2pi  (RepetitionPoints.cs:47-49)

  float u = float(i) + 1.0f + P.Phase;                          // .cs:44

  float3 translateStep = float3(P.TranslateX, P.TranslateY, P.TranslateZ);
  float3 startPos      = float3(P.StartPosX, P.StartPosY, P.StartPosZ);
  float3 translation   = translateStep * u + startPos;          // .cs:46

  float4 rotation = quatFromYawPitchRoll(P.RotateX * toRadStep * u,   // yaw  = Rotate.X
                                         P.RotateY * toRadStep * u,   // pitch= Rotate.Y
                                         P.RotateZ * toRadStep * u);  // roll = Rotate.Z

  float3 scale = (float3(1.0f, 1.0f, 1.0f) - float3(P.Scale, P.Scale, P.Scale)) * u
                 + float3(1.0f, 1.0f, 1.0f);                    // .cs:50

  float3 pivot = float3(P.PivotX, P.PivotY, P.PivotZ);

  row4x4 transform = createTransformationMatrix(/*scalingCenter*/ float3(0.0f, 0.0f, 0.0f),
                                                /*scalingRotation*/ float4(0.0f, 0.0f, 0.0f, 1.0f),
                                                /*scaling*/ scale,
                                                /*rotationCenter*/ pivot,
                                                /*rotation*/ rotation,
                                                /*translation*/ translation);

  float4 pos = rowTransform(float4(0.0f, 0.0f, 0.0f, 1.0f), transform);  // .cs:61-62

  SwPoint p;
  p.Position = float3(pos.x, pos.y, pos.z);
  p.FX1      = length(scale) / sqrt(3.0f) + P.StartW;           // .cs:65 (Vector3.One.Length()==sqrt(3))
  p.Rotation = rotation;                                        // .cs:66 Orientation = rotation
  p.Color    = float4(1.0f, 1.0f, 1.0f, 1.0f);                  // .cs:67
  p.Scale    = float3(1.0f, 1.0f, 1.0f);                        // .cs:69 Scale attr = One
  p.FX2      = 1.0f;                                            // .cs:68
  pts[i] = p;
}
