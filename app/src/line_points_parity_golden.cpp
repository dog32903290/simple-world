// line_points_parity_golden — --selftest-line-parity. PARAM-COMPLETION fan-out (LinePoints),
// the third CORE generator to ride the RadialPoints/GridPoints param-completion template.
//
// WHAT THIS GATES: LinePoints.hlsl writes per-point ATTRIBUTES — Color = lerp(ColorA, ColorB, f1),
// FX1/FX2 = Fn.x + Fn.y·f1, and a quaternion orientation (Simple mode: qFromAngleAxis(OrientationAngle
// + Twist·f, OrientationAxis); UsingUpVector mode: qMul(prefix, qLookAt(Direction, up))) — plus an
// AddSeparator that hands the LAST point a NaN scale (line terminator). The sw port BAKED them (white /
// 0 / identity / no separator). This gate cooks LinePoints THROUGH the point-graph and asserts each
// attribute now follows its TiXL input. The injectBug leg flips lineBakedBugForceForTest() so the cook
// re-bakes the pre-gate constants → every attribute tooth flips RED (the --bite-collectable proof the
// teeth aren't vacuous). Shape params (Count/Length/Pivot/Center/Direction/Scale) were always wired and
// are covered by --selftest-linepoints; this golden adds ONLY the param-completion teeth.
//
// TiXL GROUND-TRUTH (anchor — every expected value cites a source):
//   * F1 default (1,0), F2 default (1,0)            — LinePoints.t3 (Inputs F1/F2 DefaultValue X=1,Y=0).
//   * ColorA / ColorB default white                 — LinePoints.t3 (DefaultValue 1,1,1,1 both).
//   * OrientationAxis default (0,0,1), Angle 0      — LinePoints.t3.
//   * Orientation default = 1 (Simple)              — LinePoints.t3 (Orientation DefaultValue 1).
//   * Attribute writes (LinePoints.hlsl main):
//       steps = pointCount-1-(AddSeparator?1:0);  t = i/steps;  f1 = ApplyGainAndBias(t, GainAndBias);
//       f = f1 - Pivot;
//       Color = lerp(ColorA, ColorB, f1);  FX1 = FX1.x + FX1.y*f1;  FX2 = FX2.x + FX2.y*f1;
//       Simple-mode Rotation = normalize(qFromAngleAxis((OrientationAngle + Twist*f)/180*PI, axis));
//       Scale = (AddSeparator && i==last) ? sqrt(-1) : PointSize.x + PointSize.y*f1.
//   NOTE the .hlsl uses the literal 3.141578 (not a precise PI); the kernel mirrors it. The teeth
//   below assert to 0.01 tolerance so the 3.141578-vs-π drift (~1e-5) is well inside the band.
//
// ZONE: shell tier (app/src/ root, like grid_points_parity_golden.cpp). Pure verify scaffolding.
#include "runtime/point_graph.h"

#include <cmath>
#include <cstring>
#include <map>
#include <vector>

#include "parity_golden_harness.h"
#include "runtime/graph.h"        // Graph/Node/pinId
#include "runtime/point_ops.h"    // lineBakedBugForceForTest (param-completion -bug latch)
#include "runtime/tixl_point.h"   // SwPoint (64B)

namespace sw {
namespace {
std::vector<SwPoint>* g_lineParityCap = nullptr;
void captureLineParity(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_lineParityCap || !pts || c.count == 0) return;
  g_lineParityCap->assign(c.count, SwPoint{});
  std::memcpy(g_lineParityCap->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}
}  // namespace

int runLinePointsParitySelfTest(bool injectBug) {
  ParityHarness h;
  ParityReport rep("selftest-line-parity");
  if (!h.ok()) {
    printf("[selftest-line-parity] FAIL: no metallib\n");
    return 1;
  }

  registerBuiltinPointOps();  // LinePoints (cook) + DrawPoints
  std::vector<SwPoint> captured;
  g_lineParityCap = &captured;
  registerDrawOp("DrawPoints", captureLineParity);

  // injectBug drives the cook to re-bake the pre-gate attribute constants → every tooth flips RED.
  lineBakedBugForceForTest() = injectBug;

  // Shared cook helper: a tiny Count=4 line so indices are stable. GainAndBias identity (default
  // 0.5,0.5), Pivot default 0.5. With Count=4, steps=3 (separator off): f1 = t = i/3 → point[0] f1=0,
  // point[3] f1=1. Direction (0,1,0) default; the orientation teeth use Simple mode (default) so the
  // quaternion is a clean axis-angle the test can pin in closed form.
  auto cookKnob = [&](const std::map<std::string, float>& extra, std::vector<SwPoint>& out) {
    PointGraph pg(h.dev, h.lib, h.queue, 64, 64);
    Graph g;
    Node n;
    n.id = 1;
    n.type = "LinePoints";
    n.params = extra;
    n.params["Count"] = 4.0f;
    g.nodes.push_back(n);
    Node d;
    d.id = 2;
    d.type = "DrawPoints";
    g.nodes.push_back(d);
    g.connections.push_back({201, pinId(1, 0), pinId(2, 0)});
    EvaluationContext ctx{};
    ctx.deltaTime = 1.0f / 60.0f;
    g_lineParityCap = &out;
    pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));
  };

  // --- KNOB 1: F1 → SwPoint.FX1 = F1.x + F1.y*f1. F1=(2,0) → FX1==2 at every point. Baked: FX1=0 → RED.
  {
    std::vector<SwPoint> v;
    cookKnob({{"F1.x", 2.0f}, {"F1.y", 0.0f}}, v);
    double fx1 = v.empty() ? -1.0 : (double)v[0].FX1;
    rep.expect("F1 wired: point[0].FX1==2", fx1, 2.0, 0.01);
  }

  // --- KNOB 2: F2 → SwPoint.FX2 = F2.x + F2.y*f1. F2=(3,0) → FX2==3. Baked: FX2=0 → RED.
  {
    std::vector<SwPoint> v;
    cookKnob({{"F2.x", 3.0f}, {"F2.y", 0.0f}}, v);
    double fx2 = v.empty() ? -1.0 : (double)v[0].FX2;
    rep.expect("F2 wired: point[0].FX2==3", fx2, 3.0, 0.01);
  }

  // --- KNOB 3: ColorA → point[0] (f1=0 → Color=ColorA). ColorA=(0.2,0.4,0.6,0.8). Baked: white → RED.
  {
    std::vector<SwPoint> v;
    cookKnob({{"ColorA.x", 0.2f}, {"ColorA.y", 0.4f}, {"ColorA.z", 0.6f}, {"ColorA.w", 0.8f}}, v);
    double cr = v.empty() ? -1.0 : (double)v[0].Color.x;
    double cg = v.empty() ? -1.0 : (double)v[0].Color.y;
    rep.expect("ColorA wired: point[0].Color.x==0.2", cr, 0.2, 0.01);
    rep.expect("ColorA wired: point[0].Color.y==0.4", cg, 0.4, 0.01);
  }

  // --- KNOB 4: ColorB → point[3] (f1=1 → Color=ColorB). This is the DUAL-color tooth: lerp must reach
  // ColorB at the far end. ColorB=(0.1,0.2,0.3,0.4). Baked: white → RED. (LinePoints.hlsl Color lerp)
  {
    std::vector<SwPoint> v;
    cookKnob({{"ColorB.x", 0.1f}, {"ColorB.y", 0.2f}, {"ColorB.z", 0.3f}, {"ColorB.w", 0.4f}}, v);
    double cr = v.size() < 4 ? -1.0 : (double)v[3].Color.x;
    double cg = v.size() < 4 ? -1.0 : (double)v[3].Color.y;
    rep.expect("ColorB wired: point[3].Color.x==0.1", cr, 0.1, 0.01);
    rep.expect("ColorB wired: point[3].Color.y==0.2", cg, 0.2, 0.01);
  }

  // --- KNOB 5: Orientation (Simple mode, default). qFromAngleAxis((Angle+Twist*f)/180*PI, axis), axis
  // default (0,0,1). OrientationAngle=90, Twist=0 → angle = 90/180*PI = PI/2 → quat = (0,0,sin(PI/4),
  // cos(PI/4)) = (0,0,0.7071,0.7071). Baked: identity (0,0,0,1) → z=0,w=1 → RED. (LinePoints.hlsl Simple)
  {
    std::vector<SwPoint> v;
    cookKnob({{"OrientationAngle", 90.0f}}, v);
    double rz = v.empty() ? 0.0 : (double)v[0].Rotation.z;
    double rw = v.empty() ? 0.0 : (double)v[0].Rotation.w;
    rep.expect("Orientation wired: point[0].Rot.z==0.7071", rz, 0.70711, 0.01);
    rep.expect("Orientation wired: point[0].Rot.w==0.7071", rw, 0.70711, 0.01);
  }

  // --- KNOB 6: Twist ramps the angle by f = f1 - Pivot. Angle=0, Twist=180, axis (0,0,1), Pivot=0.5.
  // point[3]: f1=1 → f=0.5 → angle = (180*0.5)/180*PI = PI/2 → Rot.z = sin(PI/4) = 0.7071. Baked: Twist
  // re-baked to 0 → identity → Rot.z=0 → RED. Proves Twist actually varies rotation along the line.
  {
    std::vector<SwPoint> v;
    cookKnob({{"Twist", 180.0f}}, v);
    double rz = v.size() < 4 ? 0.0 : (double)v[3].Rotation.z;
    rep.expect("Twist wired: point[3].Rot.z==0.7071", rz, 0.70711, 0.01);
  }

  // --- KNOB 7: AddSeparator → the LAST point gets a NaN scale (line terminator). AddSeparator=1 →
  // point[3].Scale is NaN. Baked: separator off → finite scale → isnan==0 → RED. (LinePoints.hlsl Scale)
  {
    std::vector<SwPoint> v;
    cookKnob({{"AddSeparator", 1.0f}}, v);
    double isNan = (v.size() < 4) ? 0.0 : (std::isnan((double)v[3].Scale.x) ? 1.0 : 0.0);
    rep.expect("AddSeparator wired: point[3].Scale is NaN", isNan, 1.0, 0.5);
  }

  g_lineParityCap = nullptr;
  lineBakedBugForceForTest() = false;  // never leak the latch into other selftests
  return rep.finish();
}

}  // namespace sw
