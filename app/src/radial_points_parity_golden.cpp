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
#include "runtime/point_ops.h"    // radialBakedBugForceForTest (param-completion -bug latch)
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

  // ===========================================================================================
  // PARAM-COMPLETION GATE teeth — the 10 knobs that were baked are now wired. Each tooth cooks a
  // SMALL scene (Count=4) with ONE knob driven off its default and asserts the .hlsl-computed
  // effect. injectBug flips radialBakedBugForceForTest() so the cook re-bakes the pre-gate
  // constants → every value tooth here flips RED. (Orientation's baked-identity is proven RED by
  // the git-stash red-first run; the latch can't un-wire a quaternion without a shader test-seam.)
  //
  // Shared scene math (Count=4, Axis=+Z default → up=(0,1,0), dir=normalize(cross(+Z,up))=(-1,0,0);
  // GainAndBias=(0.5,0.5) identity → f=index/4; Radius=1, Cycles=1, StartAngle=0):
  //   point[i] angle = 2π·(i/4); pos = rotateAroundAxis((-1,0,0), +Z, angle).
  radialBakedBugForceForTest() = injectBug;

  auto cookKnob = [&](const std::map<std::string, float>& params, std::vector<SwPoint>& out) {
    PointGraph p2(h.dev, h.lib, h.queue, 64, 64);
    Graph gg;
    Node n;
    n.id = 1;
    n.type = "RadialPoints";
    n.params = params;
    n.params["Count"] = 4.0f;  // small ring; all knob teeth share it
    gg.nodes.push_back(n);
    Node d;
    d.id = 2;
    d.type = "DrawPoints";
    gg.nodes.push_back(d);
    gg.connections.push_back({201, pinId(1, 0), pinId(2, 0)});
    EvaluationContext c2{};
    c2.deltaTime = 1.0f / 60.0f;
    g_radialCap = &out;
    p2.cook(gg, c2, nullptr, p2.defaultDrawTarget(gg));
  };

  // --- KNOB 1: Scale (PointScaleRange). Scale=(1,4) → Scale_i = 1 + 4·(i/4); point[2]=3.0.
  // Baked: Scale=(1,0) → always 1.0 → RED. (RadialPoints.hlsl:96)
  {
    std::vector<SwPoint> v;
    cookKnob({{"Scale.x", 1.0f}, {"Scale.y", 4.0f}}, v);
    double s2 = v.size() > 2 ? (double)v[2].Scale.x : -1.0;
    rep.expect("Scale.y wired: point[2].Scale==3", s2, 3.0, 0.01);
  }

  // --- KNOB 2: F1 → FX1 = F1.x + F1.y·f. Default F1=(1,0) → FX1==1 for all (★TiXL default is 1,
  // the old kernel baked 0). No override needed: the DEFAULT itself was wrong. point[1].FX1==1.0.
  // Baked: FX1=0 → RED. (RadialPoints.hlsl:126 + .t3:18)
  {
    std::vector<SwPoint> v;
    cookKnob({}, v);  // pure defaults
    double fx1 = v.size() > 1 ? (double)v[1].FX1 : -1.0;
    rep.expect("F1 default wired: point[1].FX1==1", fx1, 1.0, 0.01);
  }

  // --- KNOB 3: F2 → FX2 = F2.x + F2.y·f. F2=(2,4) → point[2].FX2 = 2 + 4·0.5 = 4.0.
  // Baked: FX2=0 → RED. (RadialPoints.hlsl:127)
  {
    std::vector<SwPoint> v;
    cookKnob({{"F2.x", 2.0f}, {"F2.y", 4.0f}}, v);
    double fx2 = v.size() > 2 ? (double)v[2].FX2 : -1.0;
    rep.expect("F2 wired: point[2].FX2==4", fx2, 4.0, 0.01);
  }

  // --- KNOB 4: Color (per-point). Color=(0.2,0.4,0.6,0.8) → point[0].Color == that.
  // Baked: white → RED. (RadialPoints.hlsl:124)
  {
    std::vector<SwPoint> v;
    cookKnob({{"Color.x", 0.2f}, {"Color.y", 0.4f}, {"Color.z", 0.6f}, {"Color.w", 0.8f}}, v);
    double cr = v.empty() ? -1.0 : (double)v[0].Color.x;
    double cg = v.empty() ? -1.0 : (double)v[0].Color.y;
    rep.expect("Color wired: point[0].Color.x==0.2", cr, 0.2, 0.01);
    rep.expect("Color wired: point[0].Color.y==0.4", cg, 0.4, 0.01);
  }

  // --- KNOB 5: GainAndBias remaps f. GB=(0.5,0.25): gain neutral, bias 0.25 → ApplyGainAndBias(0.5)
  // = GetBias(0.25,0.5) = 0.25 (bias-functions.hlsl:6,26). So point[2] uses f=0.25, not 0.5:
  // angle=π/2, pos=rotateAroundAxis((-1,0,0),+Z,π/2)=(0,-1,0). Baked: identity f=0.5 → (1,0,0) → RED.
  {
    std::vector<SwPoint> v;
    cookKnob({{"GainAndBias.x", 0.5f}, {"GainAndBias.y", 0.25f}}, v);
    double px = v.size() > 2 ? (double)v[2].Position.x : 99.0;
    double py = v.size() > 2 ? (double)v[2].Position.y : 99.0;
    rep.expect("GainAndBias wired: point[2].x==0", px, 0.0, 0.01);
    rep.expect("GainAndBias wired: point[2].y==-1", py, -1.0, 0.01);
  }

  // --- KNOB 6: CloseCircleLine. true → angleStepCount=count-2; last point (i=3) Scale = NaN
  // (RadialPoints.hlsl:77,94). Baked: closeCircle ignored → Scale finite (1.0) → RED.
  {
    std::vector<SwPoint> v;
    cookKnob({{"CloseCircleLine", 1.0f}}, v);
    bool isNaN = v.size() > 3 && std::isnan((double)v[3].Scale.x);
    rep.expectTrue("CloseCircle wired: point[3].Scale is NaN", isNaN,
                   v.size() > 3 ? (double)v[3].Scale.x : 0.0);
  }

  // --- KNOB 7: Orientation (Classic, all defaults). TiXL writes a NON-identity quaternion:
  // point[0].Rotation = qMul(normalize(qMul(qFromAngleAxis(0,+Z), qLookAt(+Z,(0,1,0)))), identity)
  // = (0,0,1,0) (qLookAt(+Z,+Y) computed: hand-verified). Baked: identity (0,0,0,1) → z=0,w=1 → RED.
  // (RadialPoints.hlsl:98-106 + quat-functions.hlsl qLookAt)
  {
    std::vector<SwPoint> v;
    cookKnob({}, v);  // pure defaults; orientation is already non-identity
    double rz = v.empty() ? 0.0 : (double)v[0].Rotation.z;
    double rw = v.empty() ? 0.0 : (double)v[0].Rotation.w;
    rep.expect("Orientation wired: point[0].Rot.z==1", rz, 1.0, 0.01);
    rep.expect("Orientation wired: point[0].Rot.w==0", rw, 0.0, 0.01);
  }

  // --- KNOB 8/9/10: Axis / OffsetCenter / OrientationAxis|Angle. Axis drives the whole ring plane;
  // a clean single-number tooth: Axis=+Y → up=(0,0,1) (Axis.y>0.7), dir=normalize(cross(+Y,+Z))=
  // (1,0,0); point[0] (f=0,angle=0) pos = (1,0,0). Baked: Axis=+Z → point[0]=(-1,0,0) → RED.
  // (RadialPoints.hlsl:83,86)
  {
    std::vector<SwPoint> v;
    cookKnob({{"Axis.x", 0.0f}, {"Axis.y", 1.0f}, {"Axis.z", 0.0f}}, v);
    double px = v.empty() ? 99.0 : (double)v[0].Position.x;
    rep.expect("Axis wired: +Y → point[0].x==+1", px, 1.0, 0.01);
  }
  // OffsetCenter=(0,0,5): Center += OffsetCenter·f. point[2] f=0.5 → z = 5·0.5 = 2.5.
  // Baked: OffsetCenter ignored → z=0 → RED. (RadialPoints.hlsl:90)
  {
    std::vector<SwPoint> v;
    cookKnob({{"OffsetCenter.x", 0.0f}, {"OffsetCenter.y", 0.0f}, {"OffsetCenter.z", 5.0f}}, v);
    double pz = v.size() > 2 ? (double)v[2].Position.z : 99.0;
    rep.expect("OffsetCenter wired: point[2].z==2.5", pz, 2.5, 0.01);
  }

  g_radialCap = nullptr;
  radialBakedBugForceForTest() = false;  // never leak the latch into other selftests
  return rep.finish();
}

}  // namespace sw
