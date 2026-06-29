// hexgrid_points_parity_golden — --selftest-hexgrid-parity. PARAM-COMPLETION fan-out (HexGridPoints):
// the last point-generator owed its full TiXL [Input] param set (the 11th input, Scale).
//
// WHAT THIS GATES: HexGridPoints.cs carries a Scale [Input] (default 1.0) that the sw port had
// SILENTLY dropped (sw=10 vs TiXL=11). Per HexGridPoints.t3:329-337 the Scale routes through a
// ScaleVector3 node that multiplies the Size Vector3 before it reaches the shader cbuffer — so the
// effective grid spacing is Size·Scale and every X/Y coordinate is PROPORTIONAL to Scale. The sw cook
// now applies that multiply host-side; this gate cooks HexGridPoints THROUGH the point-graph and
// asserts a closed-form X coordinate that only matches when Scale is honoured. The injectBug leg flips
// hexScaleBakedBugForceForTest() so the cook re-bakes the pre-gate behavior (Scale absent → multiplier
// 1); the asserted coordinates then halve and the teeth flip RED (the --bite-collectable proof the
// teeth aren't vacuous). All other HexGridPoints params (Count/Size/Center/Pivot/Orientation/SizeMode)
// were always wired and are covered by --selftest-hexgridpoints; this golden adds ONLY the Scale tooth.
//
// TiXL GROUND-TRUTH (anchor — the expected value is computed in-test from the .hlsl/.t3 formula, not
// from an sw readback):
//   * Scale default = 1.0, routed via ScaleVector3 to scale Size — HexGridPoints.t3:60-62 (Scale
//     DefaultValue 1.0) + HexGridPoints.t3:329-337 (Scale→ScaleVector3.Factor, Size→ScaleVector3.Vector).
//   * Cell-mode position (HexGridPoints.hlsl:88-114, Pattern=2 Hexa, SizeMode=Cell):
//       zeroAdjustedSize.x = (cx==1?0:Size.x*Scale)   [host bakes Size·Scale]
//       pos.x = zS.x*cell.x - zS.x*clampedCount.x*(Pivot.x+0.5)
//       pos.x += HexOffsetsAndAngles[cell.x%2 + ((cell.y+3)%6)*2].x * zS.x * 0.3333
//       pos.x *= HexScale*3   (HexScale = 0.578)
//     => pos.x is exactly LINEAR in Scale, so a wrong/dropped Scale halves it at Scale=2.
//
// ZONE: shell tier (app/src/ root, like grid_points_parity_golden.cpp). Pure verify scaffolding.
#include "runtime/point_graph.h"

#include <cmath>
#include <cstring>
#include <map>
#include <vector>

#include "parity_golden_harness.h"
#include "runtime/graph.h"        // Graph/Node/pinId
#include "runtime/point_ops.h"    // registerBuiltinPointOps / registerDrawOp / PointCookCtx
#include "runtime/tixl_point.h"   // SwPoint (64B)

namespace sw {
// Param-completion -bug latch, defined in point_ops_hexgridpoints.cpp. Forward-declared HERE (its sole
// caller) rather than in point_ops.h, which sits exactly on the line-count ratchet cap.
bool& hexScaleBakedBugForceForTest();

namespace {
std::vector<SwPoint>* g_hexParityCap = nullptr;
void captureHexParity(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_hexParityCap || !pts || c.count == 0) return;
  g_hexParityCap->assign(c.count, SwPoint{});
  std::memcpy(g_hexParityCap->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

// Closed-form HexGridPoints Cell-mode pos.x, computed from the HLSL constants (NOT an sw readback).
// cx=3 grid (cy=cz=1), Pivot=0, Center=0. Mirrors HexGridPoints.hlsl:88-114 for the asserted cell.
double hlslPosX(int cellx, double sizeX, double scale) {
  const double HexOffX[12] = {-1, 0, 0, -1, -1, 0, 0, -1, -1, 0, 0, -1};  // .x column of the table
  const int cx = 3;
  const double clamped = cx - 1;                  // cx!=1 -> cx-1
  const double zS = sizeX * scale;                // zeroAdjustedSize.x = Size.x*Scale (host-baked)
  double pos = zS * cellx - zS * clamped * 0.5;   // Pivot.x+0.5 = 0.5
  const int idx = (cellx % 2) + ((0 + 3) % 6) * 2;  // cell.y=0
  pos += HexOffX[idx] * zS * 0.3333;
  pos *= 0.578 * 3.0;                             // HexScale*3
  return pos;
}
}  // namespace

int runHexGridPointsParitySelfTest(bool injectBug) {
  ParityHarness h;
  ParityReport rep("selftest-hexgrid-parity");
  if (!h.ok()) {
    printf("[selftest-hexgrid-parity] FAIL: no metallib\n");
    return 1;
  }

  registerBuiltinPointOps();      // registers DrawPoints + the generators (incl. HexGridPoints cook)
  std::vector<SwPoint> captured;
  g_hexParityCap = &captured;
  registerDrawOp("DrawPoints", captureHexParity);

  // injectBug drives the cook to re-bake the pre-gate Scale behavior (multiplier 1) → the X teeth flip RED.
  hexScaleBakedBugForceForTest() = injectBug;

  // Fixed scene: a 3x1x1 hex row in Cell mode, Size.x=2, Scale=2. With Scale honoured the X span doubles
  // vs Scale=1; the -bug leg re-bakes Scale=1 so the same readback halves → RED.
  const double SIZE_X = 2.0, SCALE = 2.0;
  std::vector<SwPoint> v;
  {
    PointGraph pg(h.dev, h.lib, h.queue, 64, 64);
    Graph g;
    Node n;
    n.id = 1;
    n.type = "HexGridPoints";
    n.params["Count"]   = 3.0f;   // buffer capacity = CountX*CountY*CountZ
    n.params["CountX"]  = 3.0f;
    n.params["CountY"]  = 1.0f;
    n.params["CountZ"]  = 1.0f;
    n.params["SizeMode"]= 0.0f;   // Cell
    n.params["Size.x"]  = (float)SIZE_X;
    n.params["Size.y"]  = 1.0f;
    n.params["Size.z"]  = 1.0f;
    n.params["Scale"]   = (float)SCALE;
    g.nodes.push_back(n);
    Node d; d.id = 2; d.type = "DrawPoints"; g.nodes.push_back(d);
    g.connections.push_back({201, pinId(1, 0), pinId(2, 0)});
    EvaluationContext ctx{};
    ctx.deltaTime = 1.0f / 60.0f;
    g_hexParityCap = &v;
    pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));
  }

  rep.expectExact("count == CountX*CountY*CountZ == 3", (double)v.size(), 3.0);

  // TOOTH 1: point[0].Position.x == HLSL closed form at Scale=2 (cell.x=0). Baked: Scale→1 halves it → RED.
  {
    double exp0 = hlslPosX(0, SIZE_X, SCALE);   // = -6.936  (anchor: HexGridPoints.hlsl formula)
    double act0 = v.size() > 0 ? (double)v[0].Position.x : 0.0;
    rep.expect("Scale wired: point[0].x==hlsl(Scale2)", act0, exp0, 0.02);
  }

  // TOOTH 2: point[2].Position.x == HLSL closed form at Scale=2 (cell.x=2). Baked: Scale→1 halves it → RED.
  {
    double exp2 = hlslPosX(2, SIZE_X, SCALE);   // = +6.936
    double act2 = v.size() > 2 ? (double)v[2].Position.x : 0.0;
    rep.expect("Scale wired: point[2].x==hlsl(Scale2)", act2, exp2, 0.02);
  }

  g_hexParityCap = nullptr;
  hexScaleBakedBugForceForTest() = false;  // never leak the latch into other selftests
  return rep.finish();
}

}  // namespace sw
