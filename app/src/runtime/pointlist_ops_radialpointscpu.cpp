// RadialPointsCpu pointlist op (pointlist self-registration seam leaf — params -> StructuredList<Point>).
// TiXL authority: external/tixl/Operators/Lib/point/_cpu/RadialPointsCpu.cs (verbatim math below).
//
//   RadialPointsCpu.cs Update():
//     var closeCircle = CloseCircle.GetValue(context);
//     var circleOffset = closeCircle ? 1 : 0;
//     var corners   = Count.GetValue(context).Clamp(1, 10000);
//     var pointCount = corners + circleOffset;
//     var listCount  = corners + 2 * circleOffset;            // +1 for the closing dup, +1 separator
//     ... _pointList.SetLength(listCount) ...
//     var angelInRads = StartAngle * ToRad + PI/2;
//     var deltaAngle  = -Cycles * 2PI / (pointCount - circleOffset);   // == -Cycles*2PI / corners
//     for (index = 0; index < pointCount; index++) {
//        var f = corners == 1 ? 1 : (float)index / pointCount;
//        var length = Lerp(radius, radius + radiusOffset, f);
//        var v   = Vector3.UnitX * length;                              // (length, 0, 0)
//        var rot = Quaternion.CreateFromAxisAngle(axis, angelInRads);
//        var vInAxis = Vector3.Transform(v, rot) + Vector3.Lerp(center, center + offset, f);
//        p = new Point { Position = vInAxis, F1 = Lerp(W, W+WOffset, f), Orientation = rot };
//        _pointList[index] = p;
//        angelInRads += deltaAngle;
//     }
//     if (closeCircle) _pointList[listCount - 1] = Point.Separator();   // Scale = NaN sentinel
//
//   Output: ResultList = Slot<StructuredList> (= StructuredList<Point>) — the CPU point-list currency.
//   .t3 defaults: Count=100, Radius=1, RadiusOffset=0, Center=(0,0,0), Offset=(0,0,0), StartAngle=0,
//                 Cycles=1, Axis=(0,0,1), W=1, WOffset=0, CloseCircle=false.
//
// EVAL-SIDE LAYOUT: this is a PURE PRODUCER — no PointList input to gather. The cook driver
// (point_graph.cpp cookPointListNode / resident cookResidentPointList) calls this with empty
// inputLists and the resolved Float params; the leaf clears + fills *output. Each created point starts
// from swPointDefault() (TiXL `new Point()` seed) so unset fields (Color/Scale/F2) carry the TiXL
// defaults, then Position/F1(=FX1)/Orientation(=Rotation) are overwritten per the .cs.
//
// FORK (named):
//   - cpupoint-reuses-swpoint: Point.Orientation→SwPoint.Rotation, Point.F1→SwPoint.FX1 (the SAME
//     rename the GPU 四流 adopted). The 64B stride + offsets are byte-identical to T3 Point.cs.
//   - fork-axis-normalize: Quaternion.CreateFromAxisAngle normalizes its axis internally (C#).
//     We normalize too; a zero-length axis → identity rotation (degenerate but safe — matches the
//     RotateVector3 leaf's convention, value_eval_ops.cpp). Default axis (0,0,1) is already unit.
//   - separator-nan-scale: Point.Separator() sets Scale=(NaN,NaN,NaN) (Point.cs:45). SwPoint.Scale is
//     packed_float3 (sw_packed3) → set all three to NaN. DrawPoints' vertex shader pushes a NaN-Scale
//     point offscreen (draw_points.metal:21), so the closing separator draws nothing (= TiXL: the
//     separator marks a polyline break, never a rendered point).
#include <cmath>

#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListInjectBug / swPointDefault
#include "runtime/tixl_point.h"              // SwPoint full def (the host point currency)

namespace sw {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kToRad = kPi / 180.0f;

// Rotate vector v by the quaternion rot = CreateFromAxisAngle(axis, angle) — Vector3.Transform(v, q).
// Implemented via Rodrigues (the SAME formula RotateVector3 uses, value_eval_ops.cpp:339-350), which
// is mathematically identical to building the quaternion and applying it. axis is normalized here
// (fork-axis-normalize); a zero axis → identity (v unchanged).
void rotateAxisAngle(const float v[3], const float axis[3], float angle, float out[3]) {
  float ax = axis[0], ay = axis[1], az = axis[2];
  float len = std::sqrt(ax * ax + ay * ay + az * az);
  if (len < 1e-8f) {  // zero axis → identity rotation
    out[0] = v[0]; out[1] = v[1]; out[2] = v[2];
    return;
  }
  float nx = ax / len, ny = ay / len, nz = az / len;
  float c = std::cos(angle), s = std::sin(angle);
  float ndotv = nx * v[0] + ny * v[1] + nz * v[2];
  float crossX = ny * v[2] - nz * v[1];
  float crossY = nz * v[0] - nx * v[2];
  float crossZ = nx * v[1] - ny * v[0];
  out[0] = v[0] * c + crossX * s + nx * ndotv * (1.0f - c);
  out[1] = v[1] * c + crossY * s + ny * ndotv * (1.0f - c);
  out[2] = v[2] * c + crossZ * s + nz * ndotv * (1.0f - c);
}

// Quaternion CreateFromAxisAngle(axis, angle) — the rot stored on Point.Orientation (=SwPoint.Rotation).
// (x,y,z) = axis_normalized * sin(angle/2), w = cos(angle/2). Zero axis → identity (0,0,0,1).
void quatFromAxisAngle(const float axis[3], float angle, float out[4]) {
  float ax = axis[0], ay = axis[1], az = axis[2];
  float len = std::sqrt(ax * ax + ay * ay + az * az);
  if (len < 1e-8f) { out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 1; return; }
  float nx = ax / len, ny = ay / len, nz = az / len;
  float h = angle * 0.5f;
  float s = std::sin(h);
  out[0] = nx * s; out[1] = ny * s; out[2] = nz * s; out[3] = std::cos(h);
}

float lerpf(float a, float b, float t) { return a + (b - a) * t; }

void cookRadialPointsCpu(PointListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();  // RadialPointsCpu produces a fresh list every cook (it SetLength()s each frame)

  const std::map<std::string, float>* p = c.params;
  bool closeCircle = pointListParam(p, "CloseCircle", 0.0f) > 0.5f;
  int circleOffset = closeCircle ? 1 : 0;
  // Count.Clamp(1, 10000). The Float "Count" port resolves through the value spine.
  int corners = (int)(pointListParam(p, "Count", 100.0f) + 0.5f);
  if (corners < 1) corners = 1;
  if (corners > 10000) corners = 10000;
  int pointCount = corners + circleOffset;
  int listCount = corners + 2 * circleOffset;

  float radius = pointListParam(p, "Radius", 1.0f);
  float radiusOffset = pointListParam(p, "RadiusOffset", 0.0f);
  float thickness = pointListParam(p, "W", 1.0f);
  float thicknessOffset = pointListParam(p, "WOffset", 0.0f);
  const float zero3[3] = {0, 0, 0};
  const float axisDefault[3] = {0, 0, 1};
  float center[3], offset[3], axis[3];
  pointListVec3(p, "Center", zero3, center);
  pointListVec3(p, "Offset", zero3, offset);
  pointListVec3(p, "Axis", axisDefault, axis);

  float angelInRads = pointListParam(p, "StartAngle", 0.0f) * kToRad + kPi * 0.5f;
  float cycles = pointListParam(p, "Cycles", 1.0f);
  float deltaAngle = -cycles * (2.0f * kPi) / (float)(pointCount - circleOffset);  // == /corners

  c.output->reserve((size_t)listCount);
  for (int index = 0; index < pointCount; ++index) {
    float f = (corners == 1) ? 1.0f : (float)index / (float)pointCount;
    float length = lerpf(radius, radius + radiusOffset, f);
    float v[3] = {length, 0.0f, 0.0f};  // Vector3.UnitX * length

    float rotV[3];
    rotateAxisAngle(v, axis, angelInRads, rotV);  // Vector3.Transform(v, rot)
    float quat[4];
    quatFromAxisAngle(axis, angelInRads, quat);   // Point.Orientation = rot

    SwPoint sp = swPointDefault();
    // vInAxis = rotV + Lerp(center, center+offset, f)
    sp.Position = {rotV[0] + lerpf(center[0], center[0] + offset[0], f),
                   rotV[1] + lerpf(center[1], center[1] + offset[1], f),
                   rotV[2] + lerpf(center[2], center[2] + offset[2], f)};
    sp.FX1 = lerpf(thickness, thickness + thicknessOffset, f);  // Point.F1
    sp.Rotation = {quat[0], quat[1], quat[2], quat[3]};         // Point.Orientation
    c.output->push_back(sp);

    angelInRads += deltaAngle;
  }

  if (closeCircle) {
    // _pointList SetLength(listCount) leaves index pointCount..listCount-2 default-constructed (TiXL
    // SetLength fills new slots with default Point) and writes Point.Separator() at listCount-1.
    // pointCount = corners+1, listCount = corners+2 → one default Point at index corners+1, then the
    // separator at corners+2-1 = listCount-1. Mirror: push a default Point for each gap, then separator.
    while ((int)c.output->size() < listCount - 1) c.output->push_back(swPointDefault());
    SwPoint sep = swPointDefault();          // Point.Separator(): default EXCEPT Scale = NaN
    sep.Scale = {std::nanf(""), std::nanf(""), std::nanf("")};  // separator-nan-scale (Point.cs:45)
    c.output->push_back(sep);
  }

  // Test-only: corrupt the REAL output on the actual cook path so the golden's RED case bites here, NOT
  // by flipping the expected value. CLEAR the whole list (not just drop one): this decisively bites
  // every leg — the transport readback (empty != expected coords), the GPU-buffer readback (count 0),
  // AND the production pixel leg (an empty bag → DrawPoints draws nothing → black screen). Off in prod.
  if (pointListInjectBug()) c.output->clear();
}

}  // namespace

// Self-registration. File-scope static PointListOp — independent leaf .cpp (no shared edit point).
// Feeds pointListSpecSink() + pointListCookFns() during pre-main dynamic init. Ports mirror the .t3
// (vector params as component ports drawn as one Vec widget, read via pointListVec3). The single output
// "ResultList" is the new PointList currency.
static const PointListOp _reg_radialpointscpu{
    {"RadialPointsCpu", "RadialPointsCpu",
     {{"ResultList", "ResultList", "PointList", false},
      {"Count", "Count", "Float", true, 100.0f, 1.0f, 10000.0f},
      {"Radius", "Radius", "Float", true, 1.0f, 0.0f, 100.0f},
      {"RadiusOffset", "RadiusOffset", "Float", true, 0.0f, -100.0f, 100.0f},
      {"Center.x", "Center", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
      {"Center.y", "Center.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
      {"Center.z", "Center.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
      {"Offset.x", "Offset", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
      {"Offset.y", "Offset.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
      {"Offset.z", "Offset.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
      {"StartAngle", "StartAngle", "Float", true, 0.0f, 0.0f, 360.0f},
      {"Cycles", "Cycles", "Float", true, 1.0f, 0.0f, 100.0f},
      {"Axis.x", "Axis", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"Axis.y", "Axis.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Axis.z", "Axis.z", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"W", "W", "Float", true, 1.0f, 0.0f, 100.0f},
      {"WOffset", "WOffset", "Float", true, 0.0f, -100.0f, 100.0f},
      {"CloseCircle", "CloseCircle", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
     /*evaluate=*/nullptr},  // PointList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookRadialPointsCpu};

}  // namespace sw
