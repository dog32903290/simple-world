// LinePointsCpu pointlist op (pointlist seam leaf — params -> StructuredList<Point>, 2 or 3 points).
// TiXL authority: external/tixl/Operators/Lib/point/_cpu/LinePointsCpu.cs (verbatim math below).
//
//   LinePointsCpu.cs ctor:  _pointListWithSeparator.TypedElements[2] = Point.Separator();  // 3-len array
//                           _pointList = new(2);                                            // 2-len array
//   LinePointsCpu.cs Update():
//     var from = From.GetValue(context);     // Vector3
//     var to   = To.GetValue(context);       // Vector3
//     var w    = W.GetValue(context);
//     var wOffset = WOffset.GetValue(context);
//     var addSeparator = AddSeparator.GetValue(context);
//     var rot = Quaternion.CreateFromAxisAngle((0,1,0), Math.Atan2(from.X - to.X, from.Y - to.Y));
//     var array = addSeparator ? _pointListWithSeparator : _pointList;   // 3-len (w/ separator) or 2-len
//     array[0].Position = from; array[0].F1 = w;          array[0].Orientation = rot;
//     array[1].Position = to;   array[1].F1 = w + wOffset; array[1].Orientation = rot;
//     ResultList.Value = array;
//
//   Output: ResultList = Slot<StructuredList> (= StructuredList<Point>). 2 points (from→to), OR 3 with a
//   trailing Separator when AddSeparator (the separator marks a polyline break, never a rendered point).
//   .t3 defaults: From=(-1,0,0), To=(1,0,0), W=1, WOffset=0, AddSeparator=true (mirror LinePointsCpu.t3
//   DefaultValue — fresh-drop = unit horizontal line WITH separator, same family convention as
//   RadialPointsCpu.t3 Count=100). (When From==To the atan2(0,0)=0 → identity quaternion — degenerate.)
//
// EVAL-SIDE LAYOUT: PURE PRODUCER (no PointList input). The cook driver calls this with empty inputLists
// and the resolved Float params; the leaf clears + fills *output. Each point starts from swPointDefault()
// (TiXL `new Point()` seed via the StructuredList ctor's default elements) so unset fields
// (Color/Scale/F2) carry the TiXL defaults, then Position/F1(=FX1)/Orientation(=Rotation) overwrite.
//
// FORK (named):
//   - cpupoint-reuses-swpoint: Point.Orientation→SwPoint.Rotation, Point.F1→SwPoint.FX1 (the SAME
//     rename the GPU 四流 adopted). The 64B stride + offsets are byte-identical to T3 Point.cs.
//   - separator-nan-scale: Point.Separator() sets Scale=(NaN,NaN,NaN) (Point.cs:45). SwPoint.Scale is
//     packed_float3 → set all three to NaN. DrawPoints' vertex shader pushes a NaN-Scale point offscreen
//     (draw_points.metal), so the trailing separator draws nothing (= TiXL polyline break).
//   - linepoints-atan2-degenerate: System.Math.Atan2(0,0) returns 0 (not NaN) — when From.XY == To.XY
//     the rotation is CreateFromAxisAngle((0,1,0), 0) = identity. We mirror std::atan2(0,0)==0 (C/C++
//     guarantees this), so both points keep the identity quaternion. Named, faithful.
#include <cmath>

#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListInjectBug / swPointDefault
#include "runtime/tixl_point.h"              // SwPoint full def (the host point currency)

namespace sw {

namespace {

// Quaternion CreateFromAxisAngle((0,1,0), angle): axis = Y, already unit → (0, sin(angle/2), 0, cos).
void quatAroundY(float angle, float out[4]) {
  float h = angle * 0.5f;
  out[0] = 0.0f; out[1] = std::sin(h); out[2] = 0.0f; out[3] = std::cos(h);
}

void cookLinePointsCpu(PointListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();  // LinePointsCpu rewrites its fixed-length array every cook

  const std::map<std::string, float>* p = c.params;
  const float fromDefault[3] = {-1, 0, 0};  // .t3 DefaultValue (mirror, hand-built ctx symmetry)
  const float toDefault[3] = {1, 0, 0};
  float from[3], to[3];
  pointListVec3(p, "From", fromDefault, from);
  pointListVec3(p, "To", toDefault, to);
  float w = pointListParam(p, "W", 1.0f);
  float wOffset = pointListParam(p, "WOffset", 0.0f);
  bool addSeparator = pointListParam(p, "AddSeparator", 1.0f) > 0.5f;

  // rot = CreateFromAxisAngle((0,1,0), atan2(from.X - to.X, from.Y - to.Y)).
  float angle = std::atan2(from[0] - to[0], from[1] - to[1]);
  float rot[4];
  quatAroundY(angle, rot);

  SwPoint p0 = swPointDefault();
  p0.Position = {from[0], from[1], from[2]};
  p0.FX1 = w;
  p0.Rotation = {rot[0], rot[1], rot[2], rot[3]};
  c.output->push_back(p0);

  SwPoint p1 = swPointDefault();
  p1.Position = {to[0], to[1], to[2]};
  p1.FX1 = w + wOffset;
  p1.Rotation = {rot[0], rot[1], rot[2], rot[3]};
  c.output->push_back(p1);

  if (addSeparator) {
    // _pointListWithSeparator[2] = Point.Separator() (set once in TiXL's ctor, untouched per-frame).
    SwPoint sep = swPointDefault();          // default EXCEPT Scale = NaN
    sep.Scale = {std::nanf(""), std::nanf(""), std::nanf("")};  // separator-nan-scale (Point.cs:45)
    c.output->push_back(sep);
  }

  // Test-only: corrupt the REAL output on the actual cook path so the golden's RED bites here (NOT by
  // flipping the expected value). CLEAR the whole list → empty bag bites every leg (transport readback ≠
  // expected, GPU-buffer count 0, production pixel black). Off in production.
  if (pointListInjectBug()) c.output->clear();
}

}  // namespace

// Self-registration. PURE PRODUCER: ports are the From/To Vec3 + W/WOffset/AddSeparator params and the
// single PointList output "ResultList". Vector params are component ports (read via pointListVec3),
// drawn as one Vec widget. NodeSpec append (not insert) into the pointListSpecSink via PointListOp.
static const PointListOp _reg_linepointscpu{
    {"LinePointsCpu", "LinePointsCpu",
     {{"ResultList", "ResultList", "PointList", false},
      {"From.x", "From", "Float", true, -1.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
      {"From.y", "From.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
      {"From.z", "From.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
      {"To.x", "To", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
      {"To.y", "To.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
      {"To.z", "To.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
      {"W", "W", "Float", true, 1.0f, 0.0f, 100.0f},
      {"WOffset", "WOffset", "Float", true, 0.0f, -100.0f, 100.0f},
      {"AddSeparator", "AddSeparator", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool}},
     /*evaluate=*/nullptr},  // PointList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookLinePointsCpu};

}  // namespace sw
