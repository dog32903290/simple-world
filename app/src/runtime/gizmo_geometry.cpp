// gizmo_geometry impl — CPU line-segment generators for the gizmo draw family (camera3d C3 Tranche 0).
// TiXL authority: external/tixl/Operators/Lib/render/gizmo/ConeGizmo.cs (verbatim math, file:line cited).
//
// Tranche 0 ships ONLY emitConeLines (+ the shared separator emitter). The rest of the wireframe family
// (grid/box/sphere/locator) lands its own emit fn here in Tranche 1 — not stubbed now (YAGNI).
#include "runtime/gizmo_geometry.h"

#include <cmath>

#include "runtime/pointlist_op_registry.h"  // swPointDefault (TiXL `new Point()` seed)
#include "runtime/tixl_point.h"             // SwPoint full def (64B host point currency)

namespace sw {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}  // namespace

void gizmoEmitSeparator(std::vector<SwPoint>& out) {
  // Point.Separator() (Point.cs:45): a default Point EXCEPT Scale = (NaN,NaN,NaN). The Lines shader pushes
  // a NaN-Scale point offscreen → the break draws nothing (cpupoint-reuses-swpoint fork; same as
  // LinePointsCpu). All other fields keep the TiXL `new Point()` defaults (swPointDefault()).
  SwPoint sep = swPointDefault();
  sep.Scale = {std::nanf(""), std::nanf(""), std::nanf("")};
  out.push_back(sep);
}

void emitConeLines(std::vector<SwPoint>& out, float angleDegrees, float length, int segments, int rayCount) {
  out.clear();  // the cook rewrites its whole list each frame (ConeGizmo.cs rebuilds _pointList)

  // ConeGizmo.cs:27-28 — clamp inputs.
  if (segments < 3) segments = 3;
  if (rayCount < 0) rayCount = 0;

  // ConeGizmo.cs:31-36 — BASS half-angle geometry. Full angle is the edge-to-edge spread, so the radius
  // uses HALF the angle. The cone extends -Z (forward), so the base circle sits at z = -length.
  float halfAngleRadians = angleDegrees * 0.5f * kPi / 180.0f;
  float radius = std::tan(halfAngleRadians) * length;
  float baseZ = -length;

  // ConeGizmo.cs:57-82 — base circle: one line segment per `segments`, each = 2 points + 1 separator.
  for (int i = 0; i < segments; ++i) {
    float angle1 = ((float)i / (float)segments) * kPi * 2.0f;
    float angle2 = ((float)(i + 1) / (float)segments) * kPi * 2.0f;

    float x1 = std::cos(angle1) * radius;
    float y1 = std::sin(angle1) * radius;
    float x2 = std::cos(angle2) * radius;
    float y2 = std::sin(angle2) * radius;

    SwPoint p1 = swPointDefault();  // ConeGizmo.cs sets F1=1, Color=Vector4.One — swPointDefault() supplies
    p1.Position = {x1, y1, baseZ};  // exactly those (F1/FX1=1, Color=(1,1,1,1)); set explicitly for parity.
    p1.FX1 = 1.0f;
    p1.Color = {1.0f, 1.0f, 1.0f, 1.0f};
    out.push_back(p1);

    SwPoint p2 = swPointDefault();
    p2.Position = {x2, y2, baseZ};
    p2.FX1 = 1.0f;
    p2.Color = {1.0f, 1.0f, 1.0f, 1.0f};
    out.push_back(p2);

    gizmoEmitSeparator(out);  // ConeGizmo.cs:81 — break so segments don't connect across.
  }

  // ConeGizmo.cs:85-107 — apex rays: one line per `rayCount`, apex (origin) → base-circle point.
  for (int i = 0; i < rayCount; ++i) {
    float angle = ((float)i / (float)rayCount) * kPi * 2.0f;
    float x = std::cos(angle) * radius;
    float y = std::sin(angle) * radius;

    SwPoint apex = swPointDefault();  // ConeGizmo.cs:92-97 — Position = Vector3.Zero (origin / apex).
    apex.Position = {0.0f, 0.0f, 0.0f};
    apex.FX1 = 1.0f;
    apex.Color = {1.0f, 1.0f, 1.0f, 1.0f};
    out.push_back(apex);

    SwPoint base = swPointDefault();  // ConeGizmo.cs:99-104 — base point on the circle.
    base.Position = {x, y, baseZ};
    base.FX1 = 1.0f;
    base.Color = {1.0f, 1.0f, 1.0f, 1.0f};
    out.push_back(base);

    gizmoEmitSeparator(out);  // ConeGizmo.cs:106 — break between rays.
  }
}

}  // namespace sw
