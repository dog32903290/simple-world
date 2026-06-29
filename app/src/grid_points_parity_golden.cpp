// grid_points_parity_golden — --selftest-grid-parity. PARAM-COMPLETION fan-out #2 (GridPoints),
// the second node to ride the RadialPoints param-completion template.
//
// WHAT THIS GATES: GridPoints.hlsl writes per-point ATTRIBUTES — Color (Vector4), FX1/FX2 (the F1/F2
// Single inputs), Scale (PointScale), and a quaternion orientation qFromAngleAxis(OrientationAngle,
// OrientationAxis) — but the sw port BAKED them (white / 0 / identity / no Size·Scale multiplier).
// This gate cooks GridPoints THROUGH the point-graph and asserts each attribute now follows its
// TiXL input. The injectBug leg flips gridBakedBugForceForTest() so the cook re-bakes the pre-gate
// constants → every attribute tooth flips RED (the --bite-collectable proof the teeth aren't
// vacuous). Shape params (CountX/Y/Z/Size/Center/Pivot) were always wired and are covered by the
// existing --selftest-gridpoints; this golden adds ONLY the param-completion teeth.
//
// TiXL GROUND-TRUTH (anchor — every expected value cites a source):
//   * F1 default = 1.0, F2 default = 1.0          — GridPoints.t3 (Inputs F1/F2 DefaultValue 1.0).
//   * Color default = white                        — GridPoints.t3 (Color DefaultValue 1,1,1,1).
//   * OrientationAxis default = (1,0,0), Angle 0   — GridPoints.t3.
//   * Scale default = 0.1 (UNIFORM Size multiplier) — GridPoints.t3 (Scale DefaultValue 0.1) +
//     GridPoints.t3 ScaleVector3 node: shader Size = Size_input * Scale.
//   * Attribute writes (GridPoints.hlsl:74-83, Cartesian branch Tiling<0.5):
//       Color=Color; FX1=FX1; FX2=FX2; Scale=PointScale;
//       Rotation=qFromAngleAxis(OrientationAngle*PI/180, normalize(OrientationAxis)).
//
// ZONE: shell tier (app/src/ root, like radial_points_parity_golden.cpp). Pure verify scaffolding.
#include "runtime/point_graph.h"

#include <cmath>
#include <cstring>
#include <map>
#include <vector>

#include "parity_golden_harness.h"
#include "runtime/graph.h"        // Graph/Node/pinId
#include "runtime/point_ops.h"    // gridBakedBugForceForTest (param-completion -bug latch)
#include "runtime/tixl_point.h"   // SwPoint (64B)

namespace sw {
namespace {
std::vector<SwPoint>* g_gridParityCap = nullptr;
void captureGridParity(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_gridParityCap || !pts || c.count == 0) return;
  g_gridParityCap->assign(c.count, SwPoint{});
  std::memcpy(g_gridParityCap->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}
}  // namespace

int runGridPointsParitySelfTest(bool injectBug) {
  ParityHarness h;
  ParityReport rep("selftest-grid-parity");
  if (!h.ok()) {
    printf("[selftest-grid-parity] FAIL: no metallib\n");
    return 1;
  }

  registerBuiltinPointOps();  // GridPoints (cook) + DrawPoints
  std::vector<SwPoint> captured;
  g_gridParityCap = &captured;
  registerDrawOp("DrawPoints", captureGridParity);

  // injectBug drives the cook to re-bake the pre-gate attribute constants → every tooth flips RED.
  gridBakedBugForceForTest() = injectBug;

  // Shared cook helper: a small 4x1x1 grid (Count=4) so the bag is tiny and indices are stable.
  auto cookKnob = [&](const std::map<std::string, float>& extra, std::vector<SwPoint>& out) {
    PointGraph pg(h.dev, h.lib, h.queue, 64, 64);
    Graph g;
    Node n;
    n.id = 1;
    n.type = "GridPoints";
    n.params = extra;
    n.params["Count"] = 4.0f;  // buffer capacity
    n.params["CountX"] = 4.0f;
    n.params["CountY"] = 1.0f;
    n.params["CountZ"] = 1.0f;
    g.nodes.push_back(n);
    Node d;
    d.id = 2;
    d.type = "DrawPoints";
    g.nodes.push_back(d);
    g.connections.push_back({201, pinId(1, 0), pinId(2, 0)});
    EvaluationContext ctx{};
    ctx.deltaTime = 1.0f / 60.0f;
    g_gridParityCap = &out;
    pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));
  };

  // --- KNOB 1: F1 → SwPoint.FX1. .t3 DEFAULT is 1.0 (the old kernel baked 0). No override needed:
  // the DEFAULT itself was wrong. point[0].FX1 == 1.0. Baked: FX1=0 → RED. (GridPoints.hlsl:75)
  {
    std::vector<SwPoint> v;
    cookKnob({}, v);  // pure defaults
    double fx1 = v.empty() ? -1.0 : (double)v[0].FX1;
    rep.expect("F1 default wired: point[0].FX1==1", fx1, 1.0, 0.01);
  }

  // --- KNOB 2: F2 → SwPoint.FX2. F2=4.0 → point[0].FX2 == 4.0. Baked: FX2=0 → RED. (GridPoints.hlsl:76)
  {
    std::vector<SwPoint> v;
    cookKnob({{"F2", 4.0f}}, v);
    double fx2 = v.empty() ? -1.0 : (double)v[0].FX2;
    rep.expect("F2 wired: point[0].FX2==4", fx2, 4.0, 0.01);
  }

  // --- KNOB 3: Color (per-point). Color=(0.2,0.4,0.6,0.8) → point[0].Color == that.
  // Baked: white → RED. (GridPoints.hlsl:74)
  {
    std::vector<SwPoint> v;
    cookKnob({{"Color.x", 0.2f}, {"Color.y", 0.4f}, {"Color.z", 0.6f}, {"Color.w", 0.8f}}, v);
    double cr = v.empty() ? -1.0 : (double)v[0].Color.x;
    double cg = v.empty() ? -1.0 : (double)v[0].Color.y;
    rep.expect("Color wired: point[0].Color.x==0.2", cr, 0.2, 0.01);
    rep.expect("Color wired: point[0].Color.y==0.4", cg, 0.4, 0.01);
  }

  // --- KNOB 4: Orientation. qFromAngleAxis(angle*PI/180, normalize(axis)). With default axis (1,0,0)
  // and OrientationAngle=90 → Rotation = (sin(45°),0,0,cos(45°)) = (0.7071,0,0,0.7071).
  // Baked: identity (0,0,0,1) → x=0,w=1 → RED. (GridPoints.hlsl:83)
  {
    std::vector<SwPoint> v;
    cookKnob({{"OrientationAngle", 90.0f}}, v);
    double rx = v.empty() ? 0.0 : (double)v[0].Rotation.x;
    double rw = v.empty() ? 0.0 : (double)v[0].Rotation.w;
    rep.expect("Orientation wired: point[0].Rot.x==0.7071", rx, 0.70711, 0.01);
    rep.expect("Orientation wired: point[0].Rot.w==0.7071", rw, 0.70711, 0.01);
  }

  // --- KNOB 5: Scale (UNIFORM Size multiplier, .t3 ScaleVector3). Cell mode, CountX=4 → cell steps
  // 0..3, clampedCount=3, pos.x = (cell - 3*0.5) * (Size.x*Scale). With Size.x=2, Scale=1 the span
  // (max-min over x) = 3 * (2*1) = 6.0. Baked: Scale ignored (multiplier 1, but the latch keeps
  // Scale=1 so this leg matches) — to make a TOOTH that flips, drive Scale=2 and assert span doubles:
  //   span(Scale=2) = 3 * (2*2) = 12.0. Baked: Scale re-baked to 1 → span 6.0 → RED. (host Size·Scale)
  {
    std::vector<SwPoint> v;
    cookKnob({{"SizeMode", 0.0f}, {"Size.x", 2.0f}, {"Scale", 2.0f}}, v);
    double minX = 1e9, maxX = -1e9;
    for (const SwPoint& p : v) { minX = std::fmin(minX, (double)p.Position.x); maxX = std::fmax(maxX, (double)p.Position.x); }
    double span = v.empty() ? 0.0 : (maxX - minX);
    rep.expect("Scale wired: span(Size2,Scale2)==12", span, 12.0, 0.02);
  }

  // --- KNOB 6: PointScale → SwPoint.Scale. PointScale=3 → point[0].Scale == 3. (GridPoints.hlsl:77)
  // (Always-wired pre-gate, but pinned here so the attribute cluster is complete.) Not baked-RED:
  // PointScale was never baked, so this tooth stays GREEN under injectBug — a positive control.
  {
    std::vector<SwPoint> v;
    cookKnob({{"PointScale", 3.0f}}, v);
    double sc = v.empty() ? -1.0 : (double)v[0].Scale.x;
    rep.expect("PointScale wired: point[0].Scale==3", sc, 3.0, 0.01);
  }

  g_gridParityCap = nullptr;
  gridBakedBugForceForTest() = false;  // never leak the latch into other selftests
  return rep.finish();
}

}  // namespace sw
