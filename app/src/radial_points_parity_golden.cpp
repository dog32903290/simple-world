// radial_points_parity_golden — --selftest-radial-parity. PARITY golden for the stateful/heavy
// GENERATOR RadialPoints (PARITY_GATE_PLAN.md Stage-1 pilot, "可手算類 / 真 parity").
//
// SMOKE vs PARITY: the existing --selftest-radialop asserts "points sit on a ring of radius R" where
// R is whatever the test itself passes (R=2.0). That is SELF-CONSISTENT, not parity: it would pass
// for ANY radius. This golden instead cooks RadialPoints with its PRODUCTION DEFAULTS (no Count/Radius
// override) and asserts against TiXL's .t3 CONSTANTS. The two diverge today, so it is RED — that RED is
// the proof the template has teeth (rule 1).
//
// FIXED SCENE: a single RadialPoints node, NO params set → PointGraph reads the NodeSpec defaults
// (node_registry_generators.cpp Count=100; cook Radius default 1.0, point_ops.cpp — both TiXL, post
// Stage-3) → CPU readback the cooked bag via the capture DrawPoints sink (same as point_ops_selftest.cpp).
//
// TiXL GROUND-TRUTH (anchor, rule 2 — every expected value cites a source line):
//   * Count    default = 100   — RadialPoints.t3:69 ("b654ffe2...Count" DefaultValue 100).
//   * Radius   default = 1.0   — RadialPoints.t3:65 ("acce4779...Radius" DefaultValue 1.0).
//   * Position math (RadialPoints.hlsl:79-91) with TiXL defaults Radius=1, Cycles(=Rotations)=1,
//     Center=0, Axis=+Z, StartAngle=0, CloseCircle=false, GainAndBias=(0.5,0.5):
//       - GainAndBias=(0.5,0.5) is IDENTITY: ApplyGainAndBias(x,(0.5,0.5)) == x (bias-functions.hlsl:26,
//         GetBias(0.5,·)=id and GetSchlickBias(0.5,·)=id), so f = index/Count exactly — matches sw's
//         baked f=index/Count (radial_points.metal:33). No divergence in the index mapping.
//       - up = Axis.y>0.7 ? (0,0,1) : (0,1,0) = (0,1,0); dir = normalize(cross(+Z,(0,1,0))) = (-1,0,0);
//         l = Radius + 0*f = Radius; v = rotateAroundAxis(dir*l, +Z, angle) + 0.
//         => EVERY point lies at radius |v| = Radius from the center. So the RING RADIUS == Radius == 1.0.
//       - point i=0: angle = 0 → v = dir*Radius = (-Radius, 0, 0) = (-1, 0, 0) at TiXL defaults.
//
// CHECKS (all anchored to the constants above):
//   1. count       == 100   (TiXL .t3) — production now cooks 100 → GREEN (injectBug 2048 → RED).
//   2. ringRadius  == 1.0   (TiXL .t3) — production radius now 1.0 → GREEN (injectBug 2.0 → RED).
//   3. point[0].x  == -1.0  (TiXL hlsl closed-form) — production now -1.0 → GREEN (injectBug -2.0 → RED).
// Stage-3 corrected the PRODUCTION defaults to TiXL (Count=100, Radius=1) in node_registry_generators.cpp
// + point_ops.cpp, so the no-override leg now cooks parity values → all three GREEN.
//
// injectBug LEG (standard sw tooth): re-cook with the PRE-FIX deviation (Count=2048, Radius=2.0). That
// reproduces the exact divergence Stage-3 removed → count/ringRadius/point0.x all flip RED. So the gate
// reads no-bug GREEN ↔ injectBug RED — the --bite-collectable tooth, proving the checks are not
// vacuously satisfiable and that the production defaults are pinned to the TiXL constants.
//
// ZONE: shell tier (app/src/ root, like movepointstosdf_golden.cpp) — crosses runtime (PointGraph cook +
// registerBuiltinPointOps). Pure verify scaffolding; touches NO production node code.
#include "runtime/point_graph.h"

#include <cmath>
#include <cstring>
#include <map>
#include <vector>

#include "parity_golden_harness.h"
#include "runtime/graph.h"        // Graph/Node/pinId
#include "runtime/tixl_point.h"   // SwPoint (64B)

namespace sw {
namespace {
std::vector<SwPoint>* g_radialCap = nullptr;
void captureRadial(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_radialCap || !pts || c.count == 0) return;
  g_radialCap->assign(c.count, SwPoint{});
  std::memcpy(g_radialCap->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}
}  // namespace

int runRadialPointsParitySelfTest(bool injectBug) {
  ParityHarness h;
  ParityReport rep("selftest-radial-parity");
  if (!h.ok()) {
    printf("[selftest-radial-parity] FAIL: no metallib\n");
    return 1;
  }

  registerBuiltinPointOps();  // RadialPoints (cook) + DrawPoints
  std::vector<SwPoint> captured;
  g_radialCap = &captured;
  registerDrawOp("DrawPoints", captureRadial);

  // The cooked bag size is the NodeSpec Count default (PointGraph::nodeCount reads port.def when the
  // Count param is absent). 2048 entries fit a 64x64 readback target trivially; the bag is its own buffer.
  PointGraph pg(h.dev, h.lib, h.queue, 64, 64);

  Graph g;
  Node gen;
  gen.id = 1;
  gen.type = "RadialPoints";
  // STANDARD TOOTH (post Stage-3 fix): no-bug leg sets NOTHING → production defaults, now corrected
  // to TiXL (.t3 Count=100, Radius=1.0) → all three checks GREEN. injectBug leg re-introduces the
  // pre-fix deviation (Count=2048, Radius=2.0) → count/ringRadius/point0.x all RED. This is the
  // --bite-collectable form: no-bug GREEN ↔ injectBug RED.
  if (injectBug) {
    gen.params["Count"] = 2048.0f;  // pre-fix bug (TiXL .t3:69 = 100)
    gen.params["Radius"] = 2.0f;    // pre-fix bug (TiXL .t3:65 = 1.0)
  }
  g.nodes.push_back(gen);
  Node drw;
  drw.id = 2;
  drw.type = "DrawPoints";
  g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points -> DrawPoints.points

  EvaluationContext ctx{};
  ctx.frameIndex = 0;
  ctx.time = 0.0f;
  ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  // ---- CHECK 1: point count == TiXL .t3 default (100). Production default = 2048 → RED.
  rep.expectExact("count==TiXL.t3(100)", (double)captured.size(), 100.0);

  // ---- CHECK 2: ring radius == TiXL Radius default (1.0). Every point sits at |xy| = Radius
  // (RadialPoints.hlsl: l = Radius + 0*f, rotation preserves length). We assert the MAX radius over
  // the bag equals 1.0 (a single number that pins the whole ring; with RadiusOffset=0 min==max).
  double maxR = 0.0;
  for (const SwPoint& p : captured) {
    double r = std::sqrt((double)p.Position.x * p.Position.x + (double)p.Position.y * p.Position.y);
    if (r > maxR) maxR = r;
  }
  rep.expect("ringRadius==TiXL.t3(1.0)", maxR, 1.0, 0.01);

  // ---- CHECK 3: closed-form point[0] = (-Radius,0,0) at TiXL defaults → x == -1.0. Production → -2.0.
  double p0x = captured.empty() ? 0.0 : (double)captured[0].Position.x;
  double p0y = captured.empty() ? 999.0 : (double)captured[0].Position.y;
  rep.expect("point0.x==TiXL(-1.0)", p0x, -1.0, 0.01);
  rep.expect("point0.y==TiXL(0.0)", p0y, 0.0, 0.01);

  g_radialCap = nullptr;
  return rep.finish();
}

}  // namespace sw
