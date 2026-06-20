// RepeatAtPointsCpu pointlist op (pointlist seam leaf — TWO StructuredList<Point> in -> one out).
// TiXL authority: external/tixl/Operators/Lib/point/_cpu/RepeatAtPointsCpu.cs (verbatim math below).
//
//   RepeatAtPointsCpu.cs Update():
//     var sourcePoints      = SourcePoints.GetValue(context)      as StructuredList<Point>;
//     var destinationPoints = DestinationsPoints.GetValue(context) as StructuredList<Point>;
//     if (source==null || dest==null || source.NumElements==0 || dest.NumElements==0) {
//        _pointList.SetLength(0); ResultList.Value = _pointList; return;     // EITHER missing → empty
//     }
//     var count = source.NumElements * dest.NumElements;                    // full cartesian product
//     for (destinationIndex = 0; destinationIndex < dest.NumElements; destinationIndex++) {
//        var destination = dest.TypedElements[destinationIndex];
//        for (sourceIndex = 0; sourceIndex < source.NumElements; sourceIndex++) {
//           var source = sourcePoints.TypedElements[sourceIndex];
//           _pointList[destinationIndex*source.NumElements + sourceIndex] = new Point {
//              Position    = destination.Position + Vector3.Transform(source.Position, destination.Orientation),
//              F1          = source.F1,
//              Orientation = Quaternion.Multiply(destination.Orientation, source.Orientation),
//           };
//        }
//     }
//     ResultList.Value = _pointList;
//
//   Output: ResultList = Slot<StructuredList> (= StructuredList<Point>), length = source.N * dest.N.
//   Ordering: outer = destination, inner = source → block [d0:all sources][d1:all sources]... Each source
//   point is placed in EACH destination's local frame (rotated by dest.Orientation, offset by dest.Position).
//
// ★INPUT CARDINALITY (blueprint §8 R-4): SourcePoints and DestinationsPoints are TWO SINGLE InputSlots
//   (NOT a MultiInput). The cook driver gathers PointList inputs in SPEC PORT ORDER, one entry per WIRED
//   single-input port (unwired ports contribute NO entry). So inputLists is [Source, Destinations] ONLY
//   when BOTH are wired. We require inputLists.size() == 2 (both wired) → else empty output. This is
//   faithful: TiXL needs both non-null (either null → empty). It also resolves the port-order ambiguity:
//   if only one were wired, inputLists.size()==1 could not tell Source from Destinations (named fork).
//
// EVAL-SIDE LAYOUT: the cook driver hands the upstream PointList(s) as inputLists (spec port order). This
// op is a TWO-input consumer (port 1 = SourcePoints, port 2 = DestinationsPoints, both single). Each new
// point starts from swPointDefault() (TiXL `new Point()` seed) so unset Color/Scale/F2 carry TiXL defaults.
//
// FORK (named):
//   - cpupoint-reuses-swpoint: Point.Orientation→SwPoint.Rotation, Point.F1→SwPoint.FX1 (same GPU 四流 rename).
//   - repeatat-requires-both-wired: TiXL guards null/empty → empty list. Because the pointlist gather
//     drops unwired single ports (no entry), we require inputLists.size()==2 to BOTH disambiguate port
//     order AND mirror TiXL's "both must be present"; 0/1 wired → empty output (faithful + unambiguous).
//   - repeatat-empty-on-empty-input: either source OR dest empty → empty output (TiXL SetLength(0)).
#include <cmath>

#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListInjectBug / swPointDefault
#include "runtime/tixl_point.h"              // SwPoint full def (the host point currency)

namespace sw {

namespace {

// Hamilton product q1*q2, (x,y,z,w) convention (== .NET System.Numerics Quaternion.Multiply(q1,q2),
// SAME as qMul in pointlist_ops_transformcpupoint.cpp). Result = q1 * q2.
void qMul(const float a[4], const float b[4], float out[4]) {
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

// Vector3.Transform(v, q) — rotate vector v by quaternion q (.NET System.Numerics). Expanded form:
//   t = 2 * cross(q.xyz, v);  result = v + q.w*t + cross(q.xyz, t)
// (algebraically identical to q*(v,0)*q^-1 for a unit q; uses .NET's exact expansion, no normalization —
// TiXL's Orientation is a unit quaternion, so no normalization is needed here either).
void quatRotateVec(const float q[4], const float v[3], float out[3]) {
  float qx = q[0], qy = q[1], qz = q[2], qw = q[3];
  // t = 2 * cross(q.xyz, v)
  float tx = 2.0f * (qy * v[2] - qz * v[1]);
  float ty = 2.0f * (qz * v[0] - qx * v[2]);
  float tz = 2.0f * (qx * v[1] - qy * v[0]);
  // cross(q.xyz, t)
  float cx = qy * tz - qz * ty;
  float cy = qz * tx - qx * tz;
  float cz = qx * ty - qy * tx;
  out[0] = v[0] + qw * tx + cx;
  out[1] = v[1] + qw * ty + cy;
  out[2] = v[2] + qw * tz + cz;
}

void cookRepeatAtPointsCpu(PointListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();

  // repeatat-requires-both-wired: SourcePoints (port 1) + DestinationsPoints (port 2) are two SINGLE
  // input ports; the driver gathers wired ones in spec port order. Need BOTH → inputLists.size()==2.
  if (!c.inputLists || c.inputLists->size() != 2) return;
  const std::vector<SwPoint>& source = (*c.inputLists)[0];        // SourcePoints (spec port 1, first)
  const std::vector<SwPoint>& dest = (*c.inputLists)[1];          // DestinationsPoints (spec port 2)
  if (source.empty() || dest.empty()) return;                    // repeatat-empty-on-empty-input

  const size_t srcN = source.size();
  c.output->reserve(srcN * dest.size());
  for (size_t d = 0; d < dest.size(); ++d) {
    const SwPoint& destination = dest[d];
    float dq[4] = {destination.Rotation.x, destination.Rotation.y, destination.Rotation.z,
                   destination.Rotation.w};
    for (size_t s = 0; s < srcN; ++s) {
      const SwPoint& src = source[s];
      float srcPos[3] = {src.Position.x, src.Position.y, src.Position.z};
      float rotated[3];
      quatRotateVec(dq, srcPos, rotated);  // Vector3.Transform(source.Position, destination.Orientation)

      float sq[4] = {src.Rotation.x, src.Rotation.y, src.Rotation.z, src.Rotation.w};
      float outQ[4];
      qMul(dq, sq, outQ);  // Quaternion.Multiply(destination.Orientation, source.Orientation)

      SwPoint p = swPointDefault();
      p.Position = {destination.Position.x + rotated[0], destination.Position.y + rotated[1],
                    destination.Position.z + rotated[2]};
      p.FX1 = src.FX1;  // Point.F1 = source.F1
      p.Rotation = {outQ[0], outQ[1], outQ[2], outQ[3]};
      c.output->push_back(p);  // index = d*srcN + s (push order == TiXL's destinationIndex*source.N+sourceIndex)
    }
  }

  // Test-only: corrupt the REAL output → CLEAR the whole list (bites transport readback / GPU count 0 /
  // production pixel black). Off in production.
  if (pointListInjectBug()) c.output->clear();
}

}  // namespace

// Self-registration. TWO SINGLE PointList inputs (NOT MultiInput): "SourcePoints" (port 1) +
// "DestinationsPoints" (port 2), per the .cs (two InputSlot<StructuredList>). The cook driver gathers
// them in spec port order → inputLists[0]=Source, inputLists[1]=Destinations (both wired). One PointList
// output "ResultList" (length source.N * dest.N). NodeSpec append (not insert) via PointListOp.
static const PointListOp _reg_repeatatpointscpu{
    {"RepeatAtPointsCpu", "RepeatAtPointsCpu",
     {{"ResultList", "ResultList", "PointList", false},
      {"SourcePoints", "SourcePoints", "PointList", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/false},
      {"DestinationsPoints", "DestinationsPoints", "PointList", true, 0.0f, 0.0f, 0.0f, Widget::Slider,
       {}, false, 1, /*multiInput=*/false}},
     /*evaluate=*/nullptr},
    cookRepeatAtPointsCpu};

}  // namespace sw
