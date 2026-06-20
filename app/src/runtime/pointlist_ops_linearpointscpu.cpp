// LinearPointsCpu pointlist op (pointlist seam leaf — params -> StructuredList<Point>, N points).
// TiXL authority: external/tixl/Operators/Lib/point/_cpu/LinearPointsCpu.cs (verbatim math below).
//
//   LinearPointsCpu.cs Update():
//     var countX = Count.GetValue(context).Clamp(1, 10000);
//     var count = countX;
//     if (_points.Length != count) { _points = new Point[count]; _pointList.SetLength(count); }
//     var startP = Start.GetValue(context);    // Vector3
//     var endP   = Offset.GetValue(context);   // Vector3  (NOTE: this is an OFFSET, not an end point)
//     var startW = StartW.GetValue(context);
//     var scaleW = OffsetW.GetValue(context);
//     var startPoint = startP;  var offset = endP;
//     for (var x = 0; x < countX; x++) {
//        var fX = x / (float)countX;                                 // NOTE: /countX, NOT /(countX-1)
//        _points[x].Position    = Vector3.Lerp(startPoint, startPoint + offset, fX);
//        _points[x].Orientation = Quaternion.Identity;
//        _points[x].F1          = Lerp(startW, startW + scaleW, fX);
//        _pointList[x] = _points[x];
//     }
//     Result.Value = _points; PointList.Value = _pointList;
//
//   Output: PointList = Slot<StructuredList> (= StructuredList<Point>), the CPU point-list currency.
//   TiXL exposes a SECOND output Result = Slot<Point[]> (the same data as a raw array) — sw carries only
//   the PointList currency (one ResultList output); the Point[] view is dropped (fork below).
//   .t3 defaults: Count=100, Start=(0,0,0), StartW=1, Offset=(1,0,0), OffsetW=0 (mirror LinearPointsCpu.t3
//   DefaultValue — fresh-drop = 100 points along a unit line, same family convention as RadialPointsCpu.t3
//   Count=100).
//
// EVAL-SIDE LAYOUT: PURE PRODUCER (no PointList input). The cook driver calls this with empty inputLists
// and the resolved Float params; the leaf clears + fills *output. Each point starts from swPointDefault()
// (TiXL `new Point[count]` zero-inits then the loop sets Position/Orientation/F1; Color/Scale/F2 stay at
// the C# struct default which Point's field initializers set to (1,1,1,1)/(1,1,1)/1 — swPointDefault()).
//
// FORK (named):
//   - cpupoint-reuses-swpoint: Point.Orientation→SwPoint.Rotation, Point.F1→SwPoint.FX1 (the SAME
//     rename the GPU 四流 adopted). The 64B stride + offsets are byte-identical to T3 Point.cs.
//   - linearpoints-pointarray-output-dropped: TiXL's second Result(Point[]) output is the SAME element
//     data as PointList — sw's pointlist currency IS that data; a separate Point[] slot would need a new
//     currency for no consumer. DEFERRED (named, not silent). The PointList output carries everything.
//   - linearpoints-fx-over-count: fX = x / countX (TiXL divides by COUNT, so the last point at x=countX-1
//     has fX = (countX-1)/countX < 1 — it never reaches start+offset; the LAST point is NOT the endpoint).
//     Reproduced exactly (do NOT "fix" to /(countX-1) — that would diverge from TiXL).
#include <cmath>

#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListInjectBug / swPointDefault
#include "runtime/tixl_point.h"              // SwPoint full def (the host point currency)

namespace sw {

namespace {

float lerpf(float a, float b, float t) { return a + (b - a) * t; }

void cookLinearPointsCpu(PointListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();

  const std::map<std::string, float>* p = c.params;
  int countX = (int)(pointListParam(p, "Count", 100.0f) + 0.5f);  // Count.Clamp(1,10000)
  if (countX < 1) countX = 1;
  if (countX > 10000) countX = 10000;

  const float zero3[3] = {0, 0, 0};
  const float offsetDefault[3] = {1, 0, 0};  // .t3 DefaultValue (mirror, hand-built ctx symmetry)
  float startPoint[3], offset[3];
  pointListVec3(p, "Start", zero3, startPoint);
  pointListVec3(p, "Offset", offsetDefault, offset);
  float startW = pointListParam(p, "StartW", 1.0f);
  float scaleW = pointListParam(p, "OffsetW", 0.0f);

  c.output->reserve((size_t)countX);
  for (int x = 0; x < countX; ++x) {
    float fX = (float)x / (float)countX;  // linearpoints-fx-over-count: /countX (NOT /(countX-1))
    SwPoint sp = swPointDefault();
    // Position = Lerp(start, start + offset, fX)
    sp.Position = {lerpf(startPoint[0], startPoint[0] + offset[0], fX),
                   lerpf(startPoint[1], startPoint[1] + offset[1], fX),
                   lerpf(startPoint[2], startPoint[2] + offset[2], fX)};
    sp.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};                     // Quaternion.Identity
    sp.FX1 = lerpf(startW, startW + scaleW, fX);                // Point.F1
    c.output->push_back(sp);
  }

  // Test-only: corrupt the REAL output → CLEAR the whole list (bites transport readback / GPU count 0 /
  // production pixel black). Off in production.
  if (pointListInjectBug()) c.output->clear();
}

}  // namespace

// Self-registration. PURE PRODUCER: Start/Offset Vec3 + StartW/OffsetW/Count params; single PointList
// output "ResultList". NodeSpec append (not insert) into pointListSpecSink via PointListOp.
static const PointListOp _reg_linearpointscpu{
    {"LinearPointsCpu", "LinearPointsCpu",
     {{"ResultList", "ResultList", "PointList", false},
      {"Count", "Count", "Float", true, 100.0f, 1.0f, 10000.0f},
      {"Start.x", "Start", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
      {"Start.y", "Start.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
      {"Start.z", "Start.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
      {"StartW", "StartW", "Float", true, 1.0f, 0.0f, 100.0f},
      {"Offset.x", "Offset", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
      {"Offset.y", "Offset.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
      {"Offset.z", "Offset.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
      {"OffsetW", "OffsetW", "Float", true, 0.0f, -100.0f, 100.0f}},
     /*evaluate=*/nullptr},
    cookLinearPointsCpu};

}  // namespace sw
