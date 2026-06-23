// conegizmo_golden — --selftest-conegizmo. The C3 gizmo Tranche-0 golden: ConeGizmo (the first gizmo
// consumer) cooked through the pointlist seam, output asserted element-for-element vs a CLOSED-FORM
// reference re-derived INDEPENDENTLY from ConeGizmo.cs (NOT a copy of the leaf's code path) — so the
// golden checks the leaf against TiXL, not against itself.
//
// ConeGizmo is a generator (Output = StructuredList<Point>), so the gate is structural, no pixels: cook
// ConeGizmo as a terminal pointlist node → debugCookedPointList → assert positions + separators match the
// hand-derived cone (base circle on radius tan(angle/2)·length at z=-length, apex rays from origin).
//
// Params: Angle=90, Length=2, Segments=6, RayCount=2. Angle=90 → halfAngle=45° → tan(45°)=1 →
// radius = 1·2 = 2 (clean), baseZ = -2. Expected layout (ConeGizmo.cs:40-46 count formula):
//   base circle = 6 segments × 3 (2 pts + separator) = 18
//   apex rays   = 2 rays   × 3 (apex + base + separator) = 6
//   total = 24 points.
//
// injectBug routes through pointListInjectBug(): the leaf CLEARS its real output → readback empty (≠ the
// 24-point cone) → the count + every element assertion FAILS. Teeth on the actual cook path.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"            // EvaluationContext
#include "runtime/graph.h"                   // Graph/Node
#include "runtime/point_graph.h"             // PointGraph::cook + debugCookedPointList + registerBuiltinPointOps
#include "runtime/pointlist_op_registry.h"   // pointListInjectBug / swPointDefault
#include "runtime/tixl_point.h"              // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

constexpr float kPi = 3.14159265358979323846f;
bool nearf(float a, float b, float t = 1e-4f) { return std::fabs(a - b) < t; }
bool isNanf(float a) { return a != a; }

// CLOSED-FORM ConeGizmo reference (ConeGizmo.cs:30-107, re-derived — NOT a copy of the leaf). Returns the
// expected StructuredList<Point>: base-circle segments then apex rays, separators between. A separator is a
// point with Scale = NaN (Point.Separator(), Point.cs:45) and otherwise the `new Point()` defaults.
std::vector<SwPoint> coneRef(float angleDeg, float length, int segments, int rayCount) {
  std::vector<SwPoint> out;
  if (segments < 3) segments = 3;
  if (rayCount < 0) rayCount = 0;
  float halfAngle = angleDeg * 0.5f * kPi / 180.0f;
  float radius = std::tan(halfAngle) * length;
  float baseZ = -length;

  auto pt = [&](float x, float y, float z) {
    SwPoint p = swPointDefault();
    p.Position = {x, y, z};
    p.FX1 = 1.0f;
    p.Color = {1.0f, 1.0f, 1.0f, 1.0f};
    return p;
  };
  auto sep = []() {
    SwPoint s = swPointDefault();
    s.Scale = {std::nanf(""), std::nanf(""), std::nanf("")};
    return s;
  };

  for (int i = 0; i < segments; ++i) {
    float a1 = ((float)i / (float)segments) * kPi * 2.0f;
    float a2 = ((float)(i + 1) / (float)segments) * kPi * 2.0f;
    out.push_back(pt(std::cos(a1) * radius, std::sin(a1) * radius, baseZ));
    out.push_back(pt(std::cos(a2) * radius, std::sin(a2) * radius, baseZ));
    out.push_back(sep());
  }
  for (int i = 0; i < rayCount; ++i) {
    float a = ((float)i / (float)rayCount) * kPi * 2.0f;
    out.push_back(pt(0.0f, 0.0f, 0.0f));
    out.push_back(pt(std::cos(a) * radius, std::sin(a) * radius, baseZ));
    out.push_back(sep());
  }
  return out;
}

// Element equality: positions + F1 + Color, AND separator parity (Scale NaN vs finite must agree).
bool coneEq(const SwPoint& g, const SwPoint& w) {
  bool gSep = isNanf(g.Scale.x), wSep = isNanf(w.Scale.x);
  if (gSep != wSep) return false;
  if (!nearf(g.Position.x, w.Position.x) || !nearf(g.Position.y, w.Position.y) ||
      !nearf(g.Position.z, w.Position.z))
    return false;
  if (gSep) return true;  // separator: position is default(0,0,0) both sides; Scale=NaN already matched.
  return nearf(g.FX1, w.FX1) && nearf(g.Color.x, w.Color.x) && nearf(g.Color.y, w.Color.y) &&
         nearf(g.Color.z, w.Color.z) && nearf(g.Color.w, w.Color.w);
}

}  // namespace

int runConeGizmoSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-conegizmo] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // pulls the pointlist cook driver path

  const float kAngle = 90.0f, kLength = 2.0f;
  const int kSeg = 6, kRay = 2;

  PointGraph pg(dev, lib, q, 64, 64);
  Graph g;
  Node r; r.id = 1; r.type = "ConeGizmo";
  r.params["Angle"] = kAngle; r.params["Length"] = kLength;
  r.params["Segments"] = (float)kSeg; r.params["RayCount"] = (float)kRay;
  g.nodes.push_back(r);

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pointListInjectBug() = injectBug;
  pg.cook(g, ctx, nullptr, /*terminal=*/1);
  pointListInjectBug() = false;

  const std::vector<SwPoint>* got = pg.debugCookedPointList(1);
  std::vector<SwPoint> want = coneRef(kAngle, kLength, kSeg, kRay);  // 24 points

  // HARD tooth: always assert the FULL non-degenerate reference. injectBug clears the output → empty →
  // size mismatch → FAIL here (the RED rides the real cook path, not a flipped expected value).
  bool pass = got && got->size() == want.size();
  if (pass)
    for (size_t i = 0; i < want.size(); ++i)
      if (!coneEq((*got)[i], want[i])) { pass = false; break; }

  // Discriminating structural probes (independent of the per-element loop, so a wrong segment-count or
  // radius formula bites even if some elements coincidentally line up):
  //   - radius: every base-circle point (indices 0,1 of each segment triple) must lie at radius=2 in
  //     the z=-2 plane (Angle=90 → tan(45°)·2 = 2).
  //   - separators: every 3rd element (index%3==2) must be a Scale=NaN break.
  //   - apex: the first point of each ray triple (after the 18 base elements) is the origin.
  bool radiusOk = pass, sepOk = pass, apexOk = pass;
  if (pass) {
    const float r2 = std::tan(kAngle * 0.5f * kPi / 180.0f) * kLength;  // = 2
    int baseElems = kSeg * 3;  // 18
    for (int i = 0; i < (int)got->size(); ++i) {
      const SwPoint& p = (*got)[i];
      bool isSep = (i % 3 == 2);
      if (isSep) { if (!isNanf(p.Scale.x)) sepOk = false; continue; }
      if (i < baseElems) {
        float rr = std::sqrt(p.Position.x * p.Position.x + p.Position.y * p.Position.y);
        if (!nearf(rr, r2) || !nearf(p.Position.z, -kLength)) radiusOk = false;
      } else if ((i - baseElems) % 3 == 0) {  // apex point of a ray
        if (!nearf(p.Position.x, 0.0f) || !nearf(p.Position.y, 0.0f) || !nearf(p.Position.z, 0.0f))
          apexOk = false;
      }
    }
  } else {
    radiusOk = sepOk = apexOk = false;
  }
  bool finalPass = pass && radiusOk && sepOk && apexOk;

  std::printf("[selftest-conegizmo] n=%zu want=%zu(6seg+2ray) radius=%d(@2,z=-2) sep=%d apex@origin=%d -> %s\n",
              got ? got->size() : 0, want.size(), radiusOk ? 1 : 0, sepOk ? 1 : 0, apexOk ? 1 : 0,
              finalPass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return finalPass ? 0 : 1;
}

}  // namespace sw
