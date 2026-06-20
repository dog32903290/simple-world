// TransformCpuPoint pointlist op (pointlist seam leaf — StructuredList<Point> -> StructuredList<Point>).
// TiXL authority: external/tixl/Operators/Lib/point/_cpu/TransformCpuPoint.cs (verbatim math below).
//
//   TransformCpuPoint.cs Update():
//     var connectedLists = Lists2.CollectedInputs...Where(c => c != null).ToList();
//     if (connectedLists.Count != 1) return;                          // EXACTLY one input list
//     var translation = Translation.GetValue(context);
//     var rotation    = Rotation.GetValue(context);
//     var useIncremental = Incremental.GetValue(context);
//     var space = Space.GetEnumValue<Spaces>(context);                 // 0=WorldSpace 1=PointSpace
//     if (connectedLists[0] as StructuredList<Point> is var pointList) {
//        if (pointList != null && pointList.NumElements > 0) {
//           var p = _pointList.TypedElements[0];                       // own persistent state
//           if (!useIncremental) p = pointList.TypedElements[0];       // else copy input[0]
//           if (space == PointSpace) {                                 // rotate translation into p-space
//              var q = p.Orientation;
//              var resultQ = q * (new Quaternion(translation,0)) / q;  // q * t * q^-1 (pure quat)
//              translation = resultQ.XYZ;
//           }
//           p.Orientation = p.Orientation * CreateFromYawPitchRoll(rotation.X, rotation.Y, rotation.Z);
//           p.Position += translation;
//           _pointList[0] = p;
//           Position.Value = p.Position;
//        }
//        ResultPoint.Value = _pointList;                               // a 1-ELEMENT list
//     }
//
//   Output: ResultPoint = Slot<StructuredList> (= StructuredList<Point>, length 1). TransformCpuPoint
//   outputs EXACTLY ONE point (the transformed first point of its input) — NOT the whole list mapped.
//   .t3 defaults: Translation=(0,0,0), Rotation=(0,0,0), Space=0 (WorldSpace), Incremental=false.
//
// EVAL-SIDE LAYOUT: the cook driver hands the upstream PointList(s) as inputLists. TiXL requires
// EXACTLY one connected list (Count != 1 → produce nothing). We mirror: 0 or >1 wired → empty output
// (faithful — TiXL's ResultPoint keeps its prior value, but a fresh cook with no valid input yields
// nothing meaningful; an empty list is the honest "no point produced"). The kept point starts from the
// input's element 0 (non-incremental default), gets its orientation post-multiplied by the YawPitchRoll
// rotation, and its position offset by translation (world-space).
//
// FORK (named):
//   - cpupoint-reuses-swpoint: Point.Orientation→SwPoint.Rotation (same rename the GPU 四流 adopted).
//   - transformcpupoint-incremental-deferred: Incremental=true accumulates on TiXL's own persistent
//     _pointList[0] ACROSS FRAMES (stateful). The pointlist cook flow has no per-node host state yet
//     (the GPU flow's `state` is for Metal buffers). We faithfully implement the DEFAULT non-incremental
//     path (Incremental=false, the .t3 default); when Incremental is requested we fall back to the
//     non-incremental copy (input[0]) and NAME this gap. A stateful host-list seam (parallel to ensureState)
//     unlocks it later — out of THIS block's scope (matches the blueprint's "first batch, no readback").
//   - transformcpupoint-yawpitchroll: CreateFromYawPitchRoll(yaw=Rotation.X, pitch=Rotation.Y,
//     roll=Rotation.Z) per .NET System.Numerics (Rotation port order X/Y/Z = yaw/pitch/roll), exact
//     .NET formula reproduced below. NOTE: TiXL passes Rotation RAW (no */180*PI in the .cs) → the
//     Rotation port is in RADIANS, NOT degrees — we pass it straight through (no conversion).
#include <cmath>

#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListInjectBug
#include "runtime/tixl_point.h"              // SwPoint full def (the host point currency)

namespace sw {

namespace {

// Hamilton product q1*q2, (x,y,z,w) convention (== qMul in shaders/shared/quat.metal.h == .NET
// System.Numerics Quaternion.operator*). Result = q1 * q2.
void qMul(const float a[4], const float b[4], float out[4]) {
  // (a.w*b.xyz + b.w*a.xyz + a.xyz × b.xyz, a.w*b.w - a.xyz·b.xyz)  [System.Numerics form]
  float ax = a[0], ay = a[1], az = a[2], aw = a[3];
  float bx = b[0], by = b[1], bz = b[2], bw = b[3];
  float cx = ay * bz - az * by;  // cross(a,b)
  float cy = az * bx - ax * bz;
  float cz = ax * by - ay * bx;
  float dot = ax * bx + ay * by + az * bz;
  out[0] = ax * bw + bx * aw + cx;
  out[1] = ay * bw + by * aw + cy;
  out[2] = az * bw + bz * aw + cz;
  out[3] = aw * bw - dot;
}

// Quaternion.CreateFromYawPitchRoll(yaw, pitch, roll) — .NET System.Numerics formula verbatim.
void quatFromYawPitchRoll(float yaw, float pitch, float roll, float out[4]) {
  float hr = roll * 0.5f, sr = std::sin(hr), cr = std::cos(hr);
  float hp = pitch * 0.5f, sp = std::sin(hp), cp = std::cos(hp);
  float hy = yaw * 0.5f, sy = std::sin(hy), cy = std::cos(hy);
  out[0] = cy * sp * cr + sy * cp * sr;  // x
  out[1] = sy * cp * cr - cy * sp * sr;  // y
  out[2] = cy * cp * sr - sy * sp * cr;  // z
  out[3] = cy * cp * cr + sy * sp * sr;  // w
}

void cookTransformCpuPoint(PointListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();

  // TiXL: connectedLists.Count != 1 → return (no point). inputLists holds one entry per WIRED PointList
  // source (MultiInput-expanded). Empty source lists count as connected-but-empty in TiXL only if a
  // non-null StructuredList; an unwired port contributes no entry here (mirror of the FloatList gather).
  if (!c.inputLists || c.inputLists->size() != 1) return;
  const std::vector<SwPoint>& in = (*c.inputLists)[0];
  if (in.empty()) return;  // pointList.NumElements > 0 guard

  const std::map<std::string, float>* p = c.params;
  const float zero3[3] = {0, 0, 0};
  float translation[3], rotation[3];
  pointListVec3(p, "Translation", zero3, translation);
  pointListVec3(p, "Rotation", zero3, rotation);
  bool pointSpace = pointListParam(p, "Space", 0.0f) > 0.5f;  // 0=WorldSpace 1=PointSpace
  // Incremental handled as non-incremental (transformcpupoint-incremental-deferred); both read input[0].

  SwPoint pt = in[0];  // p = pointList.TypedElements[0] (non-incremental default)

  if (pointSpace) {
    // resultQ = q * Quaternion(translation, 0) * q^-1 (rotate the translation into the point's frame).
    float q[4] = {pt.Rotation.x, pt.Rotation.y, pt.Rotation.z, pt.Rotation.w};
    float qInv[4];
    float n2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    float inv = n2 > 1e-12f ? 1.0f / n2 : 0.0f;
    qInv[0] = -q[0] * inv; qInv[1] = -q[1] * inv; qInv[2] = -q[2] * inv; qInv[3] = q[3] * inv;
    float tq[4] = {translation[0], translation[1], translation[2], 0.0f};  // Quaternion(translation, 0)
    float tmp[4], res[4];
    qMul(q, tq, tmp);       // q * t
    qMul(tmp, qInv, res);   // (q * t) * q^-1
    translation[0] = res[0]; translation[1] = res[1]; translation[2] = res[2];
  }

  // p.Orientation = p.Orientation * CreateFromYawPitchRoll(rotation.X, rotation.Y, rotation.Z)
  float ypr[4];
  quatFromYawPitchRoll(rotation[0], rotation[1], rotation[2], ypr);
  float q0[4] = {pt.Rotation.x, pt.Rotation.y, pt.Rotation.z, pt.Rotation.w};
  float qOut[4];
  qMul(q0, ypr, qOut);
  pt.Rotation = {qOut[0], qOut[1], qOut[2], qOut[3]};

  // p.Position += translation
  pt.Position = {pt.Position.x + translation[0], pt.Position.y + translation[1],
                 pt.Position.z + translation[2]};

  c.output->push_back(pt);  // _pointList[0] = p; ResultPoint = the 1-element list

  // Test-only: corrupt the REAL output (drop the produced point) so the golden's RED case bites here.
  if (pointListInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. Ports: ONE PointList input ("Lists2", MultiInput per TiXL MultiInputSlot) + the
// transform params; output "ResultPoint" is a PointList (length 1). The cook driver gathers Lists2 into
// inputLists (MultiInput-expanded), so a 0/2+ wire count → inputLists size != 1 → no output (TiXL parity).
static const PointListOp _reg_transformcpupoint{
    {"TransformCpuPoint", "TransformCpuPoint",
     {{"ResultPoint", "ResultPoint", "PointList", false},
      {"Lists2", "Lists2", "PointList", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"Translation.x", "Translation", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
      {"Translation.y", "Translation.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
      {"Translation.z", "Translation.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
      {"Rotation.x", "Rotation", "Float", true, 0.0f, -6.2831855f, 6.2831855f, Widget::Vec, {}, true, 3},
      {"Rotation.y", "Rotation.y", "Float", true, 0.0f, -6.2831855f, 6.2831855f, Widget::Vec, {}, true, 1},
      {"Rotation.z", "Rotation.z", "Float", true, 0.0f, -6.2831855f, 6.2831855f, Widget::Vec, {}, true, 1},
      {"Space", "Space", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"WorldSpace", "PointSpace"}},
      {"Incremental", "Incremental", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
     /*evaluate=*/nullptr},
    cookTransformCpuPoint};

}  // namespace sw
