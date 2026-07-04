// grid_position_golden — --selftest-gridposition. Value-output-rail golden for GridPosition, a per-cell
// grid layout op whose value depends on BOTH its resolved Float inputs (Index, RasterSize) AND the cook-
// context aspect (ctx.requestedWidth/Height). Cooked once per frame by cookValueOutputNodes (the
// no-evaluate value-output rail, sibling of RequestedResolution); evalResidentFloat then returns
// extOut[outputPortIndex]. Three legs (mirror of the RequestedResolution golden):
//   (1) build a 1-node GridPosition resident graph with KNOWN Index/RasterSize + KNOWN ctx resolution,
//       cook through cookValueOutputNodes → read extOut[0..3] (Position.x/y, Size.x/y);
//   (2) cross-check evalResidentFloat(<each output port>) returns the SAME extOut (no-evaluate readback);
//   (3) assert against the HAND-DERIVED TiXL formula (GridPosition.cs:23-34, independent of the impl).
//
// HAND-DERIVED (GridPosition.cs:19-37) — probe坐發散中段 (NOT index=0 / square grid / identity):
//   Index=5, RasterSize=(4,2), RequestedResolution=1920×1080 (aspect=16:9):
//     columns = Clamp(4,1,10000) = 4 ; rows = Clamp(2,1,10000) = 2
//     row     = 5 / 4 = 1 (INT division)         ; column = 5 - 1*4 = 1
//     aspect  = 1920 / 1080 = 1.7777778
//     sizeX   = 1/4 = 0.25    ; sizeY = 1/2 = 0.5
//     x = (1/4 - 0.5)*aspect*2 + 0.25*aspect = (-0.25)*1.7777778*2 + 0.25*1.7777778
//       = -0.8888889 + 0.4444444 = -0.4444444
//     y = ((2-1-1)/2 - 0.5)*2 + 0.5 = (0/2 - 0.5)*2 + 0.5 = -1.0 + 0.5 = -0.5
//   → Position = (-0.4444444, -0.5) ; Size = (0.25, 0.5).
//   ★ WHY THIS PROBE BITES THE BODY: every term is exercised off its identity — non-square grid (columns≠rows
//     → the 1/columns vs 1/rows split matters), non-zero row+column (index/columns int-division matters:
//     if it were float division row would be 1.25 and column would differ), non-unit aspect (the *aspect
//     factor on x but NOT y matters). Swap any term (drop *aspect, use float div, swap columns/rows, drop
//     the +sizeX*aspect half-cell offset) → the assert RED. NOT an identity/degenerate point.
//
//   A SECOND probe (Index=0, RasterSize=(1,1), aspect square 100×100) pins the corner/degenerate case:
//     columns=rows=1, row=0, column=0, aspect=1, sizeX=sizeY=1
//     x = (0/1 - 0.5)*1*2 + 1*1 = -1 + 1 = 0 ; y = ((1-0-1)/1 - 0.5)*2 + 1 = -1 + 1 = 0 → Position=(0,0).
//
// injectBug: feed a DEGENERATE ctx resolution (0×0 → aspect NaN via 0/0) — the aspect term collapses so
// Position.x diverges from the -0.4444444 want → RED on the REAL cook path (mirror of the RequestedResolution
// golden's injectBug = wrong ctx resolution; a TEST-INPUT tooth corrupting the actual cook, NOT a want-flip).
#include <cmath>
#include <cstdio>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // findSpec / Graph
#include "runtime/graph_bridge.h"         // libFromGraph
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / cookValueOutputNodes / evalResidentFloat / ResidentEvalCtx
#include "runtime/value_op_registry.h"    // valueOpSelfTests() (the golden registrar, zero shared-file edit)

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// Build a 1-node GridPosition resident graph with the given inputs, cook it through the value-output rail
// at the given ctx resolution, return the 4 outputs (Position.x/y, Size.x/y) into out[4]. Also verifies
// evalResidentFloat agrees with extOut for each output port (leg 2). `evalAgrees` is set false on mismatch.
void cookGrid(float index, float rasterX, float rasterY, uint32_t reqW, uint32_t reqH, float out[4],
              bool& evalAgrees) {
  Graph g;
  Node gp; gp.id = 1; gp.type = "GridPosition";
  gp.params["Index"] = index;
  gp.params["RasterSize.x"] = rasterX;
  gp.params["RasterSize.y"] = rasterY;
  g.nodes.push_back(gp);

  SymbolLibrary lib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
  ResidentEvalCtx ctx;
  ctx.requestedWidth = reqW;
  ctx.requestedHeight = reqH;
  ctx.lib = &lib;
  cookValueOutputNodes(rg, ctx);

  const ResidentNode* n = rg.node("1");
  out[0] = n ? n->extOut[0] : -999.0f;  // Position.x
  out[1] = n ? n->extOut[1] : -999.0f;  // Position.y
  out[2] = n ? n->extOut[2] : -999.0f;  // Size.x
  out[3] = n ? n->extOut[3] : -999.0f;  // Size.y

  // LEG 2 — evalResidentFloat must return the SAME extOut for each output port (no-evaluate readback).
  const float ePx = evalResidentFloat(rg, "1", "Position.x", ctx);
  const float ePy = evalResidentFloat(rg, "1", "Position.y", ctx);
  const float eSx = evalResidentFloat(rg, "1", "Size.x", ctx);
  const float eSy = evalResidentFloat(rg, "1", "Size.y", ctx);
  evalAgrees = nearf(ePx, out[0]) && nearf(ePy, out[1]) && nearf(eSx, out[2]) && nearf(eSy, out[3]);
}

}  // namespace

int runGridPositionSelfTest(bool injectBug) {
  bool ok = true;

  // ★ DIVERGENT PROBE: Index=5, RasterSize=(4,2), 1920×1080. injectBug feeds 0×0 → aspect 0/0 = NaN →
  //   Position.x is NaN (≠ -0.4444444) → RED on the real cook path.
  {
    float out[4]; bool evalAgrees = false;
    const uint32_t w = injectBug ? 0u : 1920u;
    const uint32_t h = injectBug ? 0u : 1080u;
    cookGrid(5.0f, 4.0f, 2.0f, w, h, out, evalAgrees);
    const float exPx = -0.4444444f, exPy = -0.5f, exSx = 0.25f, exSy = 0.5f;
    bool pass = nearf(out[0], exPx) && nearf(out[1], exPy) && nearf(out[2], exSx) && nearf(out[3], exSy);
    ok = ok && pass && evalAgrees;
    std::printf("[selftest-gridposition] idx=5 raster=(4,2) 1920x1080 → Pos=(%.5f,%.5f) Size=(%.5f,%.5f) "
                "want=(-0.44444,-0.5)(0.25,0.5) evalAgrees=%d%s -> %s\n",
                out[0], out[1], out[2], out[3], evalAgrees ? 1 : 0,
                injectBug ? " (injectBug→ctx 0x0)" : "", pass && evalAgrees ? "PASS" : "FAIL");
  }

  // DEGENERATE/CORNER PROBE (skip under injectBug — the first leg already carries the RED): Index=0,
  //   RasterSize=(1,1), square 100×100 → Position=(0,0), Size=(1,1).
  if (!injectBug) {
    float out[4]; bool evalAgrees = false;
    cookGrid(0.0f, 1.0f, 1.0f, 100u, 100u, out, evalAgrees);
    bool pass = nearf(out[0], 0.0f) && nearf(out[1], 0.0f) && nearf(out[2], 1.0f) && nearf(out[3], 1.0f);
    ok = ok && pass && evalAgrees;
    std::printf("[selftest-gridposition] idx=0 raster=(1,1) 100x100 → Pos=(%.5f,%.5f) Size=(%.5f,%.5f) "
                "want=(0,0)(1,1) evalAgrees=%d -> %s\n",
                out[0], out[1], out[2], out[3], evalAgrees ? 1 : 0, pass && evalAgrees ? "PASS" : "FAIL");

    // A THIRD probe proving the RasterSize clamp (0 → 1): RasterSize=(0,0) must clamp to (1,1) not divide
    //   by zero. Index=0, 200×100 (aspect 2) → columns=rows=1 → same as (1,1): Position=(0,0), Size=(1,1)…
    //   but with aspect=2: x=(0-0.5)*2*2 + 1*2 = -2 + 2 = 0 ; y=((1-0-1)-0.5)*2+1 = -1+1 = 0. Position=(0,0).
    float out2[4]; bool ea2 = false;
    cookGrid(0.0f, 0.0f, 0.0f, 200u, 100u, out2, ea2);
    bool pass2 = nearf(out2[0], 0.0f) && nearf(out2[1], 0.0f) && nearf(out2[2], 1.0f) && nearf(out2[3], 1.0f);
    ok = ok && pass2 && ea2;
    std::printf("[selftest-gridposition] idx=0 raster=(0,0)→clamp(1,1) 200x100 → Pos=(%.5f,%.5f) Size=(%.5f,%.5f) "
                "want=(0,0)(1,1) (clamp guards /0) -> %s\n",
                out2[0], out2[1], out2[2], out2[3], pass2 && ea2 ? "PASS" : "FAIL");
  }

  std::printf("[selftest-gridposition] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// Golden registrar: DIRECT push into the live-consumed value-op selftest sink (no shared-file edit —
// selftests.cpp iterates valueOpSelfTests() for --selftest-<name> / -bug). Mirror of
// resident_value_output_cook.cpp's RequestedResolutionGoldenRegistrar. --selftest-gridposition / -bug.
namespace {
struct GridPositionGoldenRegistrar {
  GridPositionGoldenRegistrar() { valueOpSelfTests().push_back({"gridposition", runGridPositionSelfTest}); }
};
static const GridPositionGoldenRegistrar _reg_gridposition_golden;
}  // namespace

}  // namespace sw
