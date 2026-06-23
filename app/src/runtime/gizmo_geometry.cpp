// gizmo_geometry impl — CPU line-segment generators for the gizmo draw family (camera3d C3 Tranche 0).
// TiXL authority: external/tixl/Operators/Lib/render/gizmo/ConeGizmo.cs (verbatim math, file:line cited).
//
// Tranche 0 ships ONLY emitConeLines (+ the shared separator emitter). The rest of the wireframe family
// (grid/box/sphere/locator) lands its own emit fn here in Tranche 1 — not stubbed now (YAGNI).
#include "runtime/gizmo_geometry.h"

#include <algorithm>
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

namespace {
// One white wireframe line segment: 2 points (TiXL CommonPointSets F1=1, Color=Vector4.One) + 1 separator
// (Point.cs:45). The shared shape of every gizmo edge (CommonPointSets/LinePoints all emit "2 pts + break").
void emitSeg(std::vector<SwPoint>& out, float ax, float ay, float az, float bx, float by, float bz) {
  SwPoint a = swPointDefault();  // F1=1, Color=(1,1,1,1) already (swPointDefault), set explicitly for parity.
  a.Position = {ax, ay, az}; a.FX1 = 1.0f; a.Color = {1.0f, 1.0f, 1.0f, 1.0f};
  out.push_back(a);
  SwPoint b = swPointDefault();
  b.Position = {bx, by, bz}; b.FX1 = 1.0f; b.Color = {1.0f, 1.0f, 1.0f, 1.0f};
  out.push_back(b);
  gizmoEmitSeparator(out);  // CommonPointSets / LinePoints separate every segment with Point.Separator().
}

// Place a planar (u,v) point into world XYZ for the chosen grid plane (orientation: 0=XY, 1=XZ, 2=YZ).
void planeToXyz(int orientation, float u, float v, float* x, float* y, float* z) {
  switch (orientation) {
    case 1:  *x = u; *y = 0.0f; *z = v; break;  // XZ (y=0)
    case 2:  *x = 0.0f; *y = u; *z = v; break;  // YZ (x=0)
    default: *x = u; *y = v; *z = 0.0f; break;  // XY (z=0)
  }
}
}  // namespace

void emitGridLines(std::vector<SwPoint>& out, int segsX, int segsY, int orientation) {
  out.clear();
  if (segsX < 1) segsX = 1;  // a grid needs at least one cell (Math.Max(1) convention).
  if (segsY < 1) segsY = 1;

  const float kHalf = 0.5f;  // CommonPointSets' S — the unit cell spans [-0.5,0.5] (the cook scales it).
  // (segsX+1) lines running along the V axis (constant U), spaced across U in [-0.5,0.5].
  for (int i = 0; i <= segsX; ++i) {
    float u = -kHalf + (float)i / (float)segsX;  // i/segsX in [0,1] → u in [-0.5,0.5]
    float ax, ay, az, bx, by, bz;
    planeToXyz(orientation, u, -kHalf, &ax, &ay, &az);
    planeToXyz(orientation, u, +kHalf, &bx, &by, &bz);
    emitSeg(out, ax, ay, az, bx, by, bz);
  }
  // (segsY+1) lines running along the U axis (constant V), spaced across V in [-0.5,0.5].
  for (int j = 0; j <= segsY; ++j) {
    float v = -kHalf + (float)j / (float)segsY;
    float ax, ay, az, bx, by, bz;
    planeToXyz(orientation, -kHalf, v, &ax, &ay, &az);
    planeToXyz(orientation, +kHalf, v, &bx, &by, &bz);
    emitSeg(out, ax, ay, az, bx, by, bz);
  }
}

void emitBoxEdges(std::vector<SwPoint>& out) {
  out.clear();
  // VERBATIM CommonPointSets.CubePoints (CommonPointSets.cs:104-167), S=0.5. The 12 edges in TiXL order:
  // 4 edges parallel to X (top/bottom of the two z faces), 4 parallel to Y, 4 parallel to Z.
  const float S = 0.5f;
  // --- 4 edges along X ---
  emitSeg(out, -S, -S,  S,   S, -S,  S);  // CubePoints[0..1]
  emitSeg(out, -S,  S,  S,   S,  S,  S);  // [3..4]
  emitSeg(out, -S, -S, -S,   S, -S, -S);  // [6..7]
  emitSeg(out, -S,  S, -S,   S,  S, -S);  // [9..10]
  // --- 4 edges along Y ---
  emitSeg(out, -S, -S,  S,  -S,  S,  S);  // [12..13]
  emitSeg(out,  S, -S,  S,   S,  S,  S);  // [15..16]
  emitSeg(out, -S, -S, -S,  -S,  S, -S);  // [18..19]
  emitSeg(out,  S, -S, -S,   S,  S, -S);  // [21..22]
  // --- 4 edges along Z ---
  emitSeg(out, -S, -S, -S,  -S, -S,  S);  // [24..25]
  emitSeg(out,  S, -S, -S,   S, -S,  S);  // [27..28]
  emitSeg(out, -S,  S, -S,  -S,  S,  S);  // [30..31]
  emitSeg(out,  S,  S, -S,   S,  S,  S);  // [33..34]
}

void emitSphereRings(std::vector<SwPoint>& out, int rings, int segments) {
  out.clear();
  if (rings < 1) rings = 1;
  if (segments < 3) segments = 3;  // a circle needs >=3 segments (ConeGizmo.cs:27 convention).
  const float kPi2 = kPi * 2.0f;

  // LATITUDE rings: `rings` horizontal circles at evenly spaced y in (-1,1), radius = sqrt(1-y^2) (unit
  // sphere). Each ring = `segments` segments around the circle (closed loop via the wrap to seg 0).
  for (int r = 0; r < rings; ++r) {
    float t = (float)(r + 1) / (float)(rings + 1);  // (0,1) exclusive → skip the degenerate poles
    float y = -1.0f + 2.0f * t;                      // y in (-1,1)
    float rr = std::sqrt(std::max(0.0f, 1.0f - y * y));
    for (int s = 0; s < segments; ++s) {
      float a1 = ((float)s / (float)segments) * kPi2;
      float a2 = ((float)(s + 1) / (float)segments) * kPi2;
      emitSeg(out, std::cos(a1) * rr, y, std::sin(a1) * rr,
                   std::cos(a2) * rr, y, std::sin(a2) * rr);
    }
  }
  // LONGITUDE rings: `rings` great circles through the poles, each rotated by phi around the y axis. A
  // longitude circle is the set (sin(theta)cos(phi), cos(theta), sin(theta)sin(phi)) for theta in [0,2pi).
  for (int r = 0; r < rings; ++r) {
    float phi = ((float)r / (float)rings) * kPi;  // [0,pi) — a great circle + its mirror covers the sphere
    float cphi = std::cos(phi), sphi = std::sin(phi);
    for (int s = 0; s < segments; ++s) {
      float t1 = ((float)s / (float)segments) * kPi2;
      float t2 = ((float)(s + 1) / (float)segments) * kPi2;
      float r1 = std::sin(t1), r2 = std::sin(t2);
      emitSeg(out, r1 * cphi, std::cos(t1), r1 * sphi,
                   r2 * cphi, std::cos(t2), r2 * sphi);
    }
  }
}

void emitAxisCross(std::vector<SwPoint>& out) {
  out.clear();
  // VERBATIM CommonPointSets.CrossPoints (CommonPointSets.cs:73-83), S=0.5: one line on each axis through
  // the origin. TiXL order: Y axis, then X axis, then Z axis (the order the Cross buffer lists them).
  const float S = 0.5f;
  emitSeg(out, 0.0f, -S, 0.0f,  0.0f,  S, 0.0f);  // Y axis (CrossPoints[0..1])
  emitSeg(out,  -S, 0.0f, 0.0f,   S, 0.0f, 0.0f);  // X axis (CrossPoints[3..4])
  emitSeg(out, 0.0f, 0.0f,  -S,  0.0f, 0.0f,  S);  // Z axis (CrossPoints[6..7])
}

}  // namespace sw
