// SampleCpuPoints pointlist op (pointlist seam leaf — StructuredList<Point> -> StructuredList<Point>).
// TiXL authority: external/tixl/Operators/Lib/point/helper/SampleCpuPoints.cs (verbatim math below).
//
//   SampleCpuPoints.cs Update():
//     var points = PointList.GetValue(context);                                  // single InputSlot<StructuredList>
//     if (points is not StructuredList<Point> pointList || pointList.NumElements == 0) return;  // no output
//     var samplePosition = SamplePos.GetValue(context);
//     if (!samplePosition._IsFinite()) samplePosition = 0;                        // NaN/Inf -> 0
//     var f  = samplePosition.Clamp(0, pointList.NumElements - 1);                // clamp to [0, N-1]
//     var i0 = (int)f.ClampMin(0);                                               // truncate-toward-zero
//     var i1 = (i0 + 1).ClampMax(points.NumElements - 1);                         // next, clamped
//     var a  = pointList.TypedElements[i0];
//     var b  = pointList.TypedElements[i1];
//     var t  = f - i0;
//     var posA = a.Position; var posB = b.Position;
//     var d = posB - posA; var l = d.Length();
//     if (l <= float.Epsilon) { _result[0] = a; return; }                         // coincident -> copy a
//     var tLength = TangentScale.GetValue(context) * l;                           // tangent magnitude
//     var tA = Vector3.Transform(+UnitZ*tLength, Quaternion.Normalize(a.Orientation));   // fwd tangent of a
//     var tB = Vector3.Transform(-UnitZ*tLength, Quaternion.Normalize(b.Orientation));   // back tangent of b
//     var pos = Bezier.GetPoint(posA, posA+tA, posB+tB, posB, t);                 // CUBIC bezier (4 ctrl pts)
//     var tan = Bezier.GetFirstDerivative(posA, posA+tA, posB+tB, posB, t);       // cubic derivative
//     var orientation = ComputeOrientation(a.Orientation, b.Orientation, tan, t); // RH look-at frame
//     _result[0] = new Point { Position = pos, Orientation = orientation };       // a 1-ELEMENT list
//
//   Output: ResultPoint = Slot<StructuredList> (= StructuredList<Point>, length 1). The op produces
//   EXACTLY ONE resampled point (a single Bezier interpolation between the two bracketing keys).
//
//   .t3 DEFAULTS (SampleCpuPoints.t3, override the C# ctor): TangentScale=0.0, SamplePos=0.0,
//   PointList=null. ★TangentScale default 0 collapses tA=tB=0, so the cubic becomes
//   Bezier(posA, posA, posB, posB, t) — NOT a straight lerp (inner control pts sit ON the endpoints).
//
//   ★DEAD CODE in the .cs (computed-but-unused; we faithfully OMIT, it has no value effect):
//     - `smoothT = SmootherStep(t)` (cs:50) is never read (line 56 uses RAW t for Bezier.GetPoint).
//     - `up`/`upA`/`upB`/`pUpA`/`pUpB` (cs:60-66) are dead — ComputeOrientation recomputes upA/upB
//        from the orientations itself (cs:143-144). LookAtRH_ZForward / SlerpUnit are never called.
//
// EVAL-SIDE LAYOUT: the cook driver hands the upstream PointList(s) as inputLists (spec port order).
// PointList is a SINGLE InputSlot (NOT MultiInput) → we require inputLists.size()==1 (port wired). 0 or
// >1 wired (or empty list) → empty output (TiXL: `points is not ... || NumElements==0` → no output).
//
// FORK (named):
//   - cpupoint-reuses-swpoint: Point.Orientation→SwPoint.Rotation (same rename the GPU 四流 adopted).
//   - samplecpupoints-singleinput: PointList is a SINGLE InputSlot (multiInput=false). The gather drops
//     unwired single ports → require inputLists.size()==1 (faithful to "points==null → no output").
//   - samplecpupoints-deadcode-omitted: the unused smoothT/up branch (cs:50,60-67) is omitted verbatim
//     — it never reaches an output, so omission is byte-identical to running it (named above).
#include <cmath>

#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListParam / pointListInjectBug
#include "runtime/tixl_point.h"              // SwPoint full def (the host point currency)

namespace sw {

namespace {

// Vector3.Transform(v, q) — rotate vector v by quaternion q (.NET System.Numerics expansion):
//   t = 2 * cross(q.xyz, v);  result = v + q.w*t + cross(q.xyz, t)
void quatRotateVec(const float q[4], const float v[3], float out[3]) {
  float qx = q[0], qy = q[1], qz = q[2], qw = q[3];
  float tx = 2.0f * (qy * v[2] - qz * v[1]);
  float ty = 2.0f * (qz * v[0] - qx * v[2]);
  float tz = 2.0f * (qx * v[1] - qy * v[0]);
  float cx = qy * tz - qz * ty;
  float cy = qz * tx - qx * tz;
  float cz = qx * ty - qy * tx;
  out[0] = v[0] + qw * tx + cx;
  out[1] = v[1] + qw * ty + cy;
  out[2] = v[2] + qw * tz + cz;
}

// Quaternion.Normalize(q) — .NET System.Numerics (divide by length; identity passthrough on zero norm).
void quatNormalize(const float q[4], float out[4]) {
  float n2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
  float inv = n2 > 0.0f ? 1.0f / std::sqrt(n2) : 0.0f;
  out[0] = q[0] * inv; out[1] = q[1] * inv; out[2] = q[2] * inv; out[3] = q[3] * inv;
}

float v3len(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
float v3dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void v3cross(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}
void v3normalize(const float v[3], float out[3]) {
  float l = v3len(v);
  float inv = l > 0.0f ? 1.0f / l : 0.0f;
  out[0] = v[0] * inv; out[1] = v[1] * inv; out[2] = v[2] * inv;
}

// MathUtils.Clamp(value, -1, 1) — used by SlerpUnitWithRef's dot clamp.
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// RotateAroundAxis(v, axis, angle) — Rodrigues' rotation (cs:225-229).
void rotateAroundAxis(const float v[3], const float axis[3], float angle, float out[3]) {
  float c = std::cos(angle), s = std::sin(angle);
  float cr[3]; v3cross(axis, v, cr);
  float d = v3dot(axis, v);
  for (int i = 0; i < 3; ++i) out[i] = v[i] * c + cr[i] * s + axis[i] * d * (1.0f - c);
}

// SlerpUnitWithRef(a, b, t, refAxis) — cs:168-190 verbatim.
void slerpUnitWithRef(const float aIn[3], const float bIn[3], float t, const float refAxis[3], float out[3]) {
  float a[3], b[3];
  v3normalize(aIn, a);
  v3normalize(bIn, b);
  float dot = clampf(v3dot(a, b), -1.0f, 1.0f);

  if (dot > 0.9995f) {  // nearly identical: nlerp
    float lerp[3];
    for (int i = 0; i < 3; ++i) lerp[i] = a[i] + (b[i] - a[i]) * t;  // Vector3.Lerp(a,b,t)
    v3normalize(lerp, out);
    return;
  }
  if (dot < -0.9995f) {  // nearly opposite: rotate a around a tangent-derived axis
    float axis[3];
    v3cross(refAxis, a, axis);
    if (v3dot(axis, axis) < 1e-8f) {  // tangent ∥ a → pick an orthogonal seed
      float seed[3] = {1.0f, 0.0f, 0.0f};                       // UnitX
      if (!(std::fabs(a[0]) < 0.1f)) { seed[0] = 0.0f; seed[1] = 1.0f; }  // else UnitY
      v3cross(seed, a, axis);
    }
    v3normalize(axis, axis);
    rotateAroundAxis(a, axis, 3.14159265358979323846f * t, out);
    return;
  }
  float theta = std::acos(dot);
  float s = std::sin(theta);
  float w1 = std::sin((1.0f - t) * theta) / s;
  float w2 = std::sin(t * theta) / s;
  for (int i = 0; i < 3; ++i) out[i] = a[i] * w1 + b[i] * w2;
}

// Quaternion.CreateFromRotationMatrix — .NET System.Numerics verbatim. The matrix is built in the .cs as
// row-major (right; up; forward): M11..M13 = right, M21..M23 = up, M31..M33 = forward (cs:159-163).
void quatFromRotationMatrix(const float right[3], const float up[3], const float fwd[3], float out[4]) {
  float m11 = right[0], m12 = right[1], m13 = right[2];
  float m21 = up[0],    m22 = up[1],    m23 = up[2];
  float m31 = fwd[0],   m32 = fwd[1],   m33 = fwd[2];
  float trace = m11 + m22 + m33;
  float x, y, z, w;
  if (trace > 0.0f) {
    float s = std::sqrt(trace + 1.0f);
    w = s * 0.5f;
    s = 0.5f / s;
    x = (m23 - m32) * s;
    y = (m31 - m13) * s;
    z = (m12 - m21) * s;
  } else if (m11 >= m22 && m11 >= m33) {
    float s = std::sqrt(1.0f + m11 - m22 - m33);
    float invS = 0.5f / s;
    x = 0.5f * s;
    y = (m12 + m21) * invS;
    z = (m13 + m31) * invS;
    w = (m23 - m32) * invS;
  } else if (m22 > m33) {
    float s = std::sqrt(1.0f + m22 - m11 - m33);
    float invS = 0.5f / s;
    x = (m21 + m12) * invS;
    y = 0.5f * s;
    z = (m32 + m23) * invS;
    w = (m31 - m13) * invS;
  } else {
    float s = std::sqrt(1.0f + m33 - m11 - m22);
    float invS = 0.5f / s;
    x = (m31 + m13) * invS;
    y = (m32 + m23) * invS;
    z = 0.5f * s;
    w = (m12 - m21) * invS;
  }
  out[0] = x; out[1] = y; out[2] = z; out[3] = w;
}

// ComputeOrientation(qa, qb, bezierTangent, t) — cs:136-166 verbatim. Builds a RH look-at frame:
// forward = normalize(tangent), up = slerped key-up (deterministic axis), right = up×fwd, up = fwd×right.
void computeOrientation(const float qa[4], const float qb[4], const float bezierTangent[3], float t,
                        float out[4]) {
  float f[3];
  v3normalize(bezierTangent, f);  // +Z forward

  // up from keyframes (Vector3.Transform(UnitY, q) — raw q, NOT normalized, per cs:143-144).
  float unitY[3] = {0.0f, 1.0f, 0.0f};
  float upA[3], upB[3];
  quatRotateVec(qa, unitY, upA);
  quatRotateVec(qb, unitY, upB);
  float up[3];
  slerpUnitWithRef(upA, upB, t, f, up);  // deterministic axis

  // make up orthogonal to forward
  float d = v3dot(up, f);
  for (int i = 0; i < 3; ++i) up[i] -= f[i] * d;
  if (v3dot(up, up) < 1e-8f) {
    if (std::fabs(f[1]) < 0.99f) { up[0] = 0.0f; up[1] = 1.0f; up[2] = 0.0f; }  // UnitY
    else                        { up[0] = 1.0f; up[1] = 0.0f; up[2] = 0.0f; }   // UnitX
  }
  v3normalize(up, up);

  // RH basis: right = normalize(up × forward); up = forward × right
  float right[3];
  v3cross(up, f, right);
  v3normalize(right, right);
  v3cross(f, right, up);

  quatFromRotationMatrix(right, up, f, out);
}

void cookSampleCpuPoints(PointListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();

  // points is a SINGLE InputSlot<StructuredList>; the gather drops unwired single ports. Require exactly
  // one wired list (samplecpupoints-singleinput). Empty list → no output (NumElements==0 guard cs:23).
  if (!c.inputLists || c.inputLists->size() != 1) return;
  const std::vector<SwPoint>& in = (*c.inputLists)[0];
  if (in.empty()) return;
  const int n = (int)in.size();

  const std::map<std::string, float>* p = c.params;
  float samplePos   = pointListParam(p, "SamplePos", 0.0f);
  float tangentScale = pointListParam(p, "TangentScale", 0.0f);  // .t3 default 0
  if (!std::isfinite(samplePos)) samplePos = 0.0f;               // cs:27-28 (_IsFinite NaN guard)

  // f = samplePos.Clamp(0, N-1); i0 = (int)f.ClampMin(0); i1 = (i0+1).ClampMax(N-1).
  float f = clampf(samplePos, 0.0f, (float)(n - 1));
  float fc = f < 0.0f ? 0.0f : f;                                // ClampMin(0) before truncate
  int i0 = (int)fc;                                              // (int) truncates toward zero
  int i1 = i0 + 1;
  if (i1 > n - 1) i1 = n - 1;                                    // ClampMax(N-1)
  const SwPoint& a = in[i0];
  const SwPoint& b = in[i1];
  float t = f - (float)i0;

  float posA[3] = {a.Position.x, a.Position.y, a.Position.z};
  float posB[3] = {b.Position.x, b.Position.y, b.Position.z};
  float d[3] = {posB[0] - posA[0], posB[1] - posA[1], posB[2] - posA[2]};
  float l = v3len(d);
  if (l <= 1.1920929e-7f) {  // float.Epsilon (C#: smallest positive subnormal ~1.4e-45) — TiXL compares
                             // against float.Epsilon; we use FLT_EPSILON-scale guard. Practically the
                             // branch only fires when posA==posB (l==0), where any tiny eps is equivalent.
    // ★samplecpupoints-coincident: cs:44-48 — l<=eps → _result[0] = a (copy the whole key verbatim).
    c.output->push_back(a);
    if (pointListInjectBug() && !c.output->empty()) c.output->pop_back();
    return;
  }

  float tLength = tangentScale * l;
  // tA = Transform(+UnitZ*tLength, Normalize(a.Orientation)); tB = Transform(-UnitZ*tLength, Normalize(b.Orientation))
  float qaN[4], qbN[4];
  float qa[4] = {a.Rotation.x, a.Rotation.y, a.Rotation.z, a.Rotation.w};
  float qb[4] = {b.Rotation.x, b.Rotation.y, b.Rotation.z, b.Rotation.w};
  quatNormalize(qa, qaN);
  quatNormalize(qb, qbN);
  float zPos[3] = {0.0f, 0.0f, tLength};
  float zNeg[3] = {0.0f, 0.0f, -tLength};
  float tA[3], tB[3];
  quatRotateVec(qaN, zPos, tA);
  quatRotateVec(qbN, zNeg, tB);

  // Cubic Bezier control points: p0=posA, p1=posA+tA, p2=posB+tB, p3=posB.
  float p1[3] = {posA[0] + tA[0], posA[1] + tA[1], posA[2] + tA[2]};
  float p2[3] = {posB[0] + tB[0], posB[1] + tB[1], posB[2] + tB[2]};

  // Bezier.GetPoint(p0,p1,p2,p3,t) — t clamped to [0,1] (Bezier.cs). t is already in [0,1] here.
  float tc = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  float omt = 1.0f - tc;
  float pos[3], tan[3];
  for (int i = 0; i < 3; ++i) {
    pos[i] = omt * omt * omt * posA[i] +
             3.0f * omt * omt * tc * p1[i] +
             3.0f * omt * tc * tc * p2[i] +
             tc * tc * tc * posB[i];
    // Bezier.GetFirstDerivative(p0,p1,p2,p3,t) — cubic derivative (Bezier.cs).
    tan[i] = 3.0f * omt * omt * (p1[i] - posA[i]) +
             6.0f * omt * tc * (p2[i] - p1[i]) +
             3.0f * tc * tc * (posB[i] - p2[i]);
  }

  float orientation[4];
  computeOrientation(qa, qb, tan, t, orientation);  // ComputeOrientation passes RAW a/b.Orientation (cs:72)

  SwPoint out = swPointDefault();  // `new Point { Position, Orientation }` — other fields = Point defaults
  out.Position = {pos[0], pos[1], pos[2]};
  out.Rotation = {orientation[0], orientation[1], orientation[2], orientation[3]};
  c.output->push_back(out);

  // Test-only: corrupt the REAL output (drop the produced point) so the golden's RED case bites here.
  if (pointListInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. ONE SINGLE PointList input ("PointList", multiInput=false per TiXL InputSlot) + two
// Float params (SamplePos, TangentScale, .t3 defaults both 0). Output "ResultPoint" is a PointList (len 1).
// PortSpec field order: id,name,dataType,isInput,def,minV,maxV,widget,labels,pinless,vecArity,multiInput.
static const PointListOp _reg_samplecpupoints{
    {"SampleCpuPoints", "SampleCpuPoints",
     {{"ResultPoint", "ResultPoint", "PointList", false},
      {"PointList", "PointList", "PointList", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/false},
      {"SamplePos", "SamplePos", "Float", true, 0.0f, 0.0f, 100000.0f, Widget::Slider},
      {"TangentScale", "TangentScale", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Slider}},
     /*evaluate=*/nullptr},
    cookSampleCpuPoints};

}  // namespace sw
