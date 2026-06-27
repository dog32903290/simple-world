// GridPoints generator op (lane A) — dispatch the gridpoints kernel into the node's output
// bag. Faithful to external/tixl .../points/generate/GridPoints.hlsl (Cartesian tiling).
//
// Self-contained leaf: cookGridPoints + registerGridPointsOp() + runGridPointsSelfTest().
// Mirrors point_ops.cpp's cookRadialPoints (paramOr scalars, readVecN vectors) and
// point_ops_selftest.cpp's golden template (own capture vector + capture-only DrawOp).
//
// NOTE on count: PointGraph::nodeCount sizes a generator's output bag from a single Float
// port literally named "Count". GridPoints' real shape is CountX*CountY*CountZ, so the
// NodeSpec carries a "Count" port = the buffer CAPACITY (total grid points) alongside the
// per-axis CountX/Y/Z (the actual grid dims). The cook recomputes total = cx*cy*cz and only
// writes points that fit in c.count; the host should set Count = cx*cy*cz. Flagged in parityNotes.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"         // calcDispatchCount
#include "runtime/graph.h"            // Graph/Node/findSpec/pinId/readVecN
#include "runtime/gridpoints_params.h" // GridParams, GridBinding
#include "runtime/point_graph.h"      // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tex_op_cache.h"     // cachedComputePSO
#include "runtime/tixl_point.h"       // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// GridPoints generator: writes a 3D grid of SwPoints. Counts per axis (scalar Float),
// Size/Center/Pivot via cookVecN (vector params), SizeMode (Cell|Bounds) scalar.
void cookGridPoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "gridpoints");
  if (!pso) return;

  GridParams P{};
  P.CountX = (uint32_t)std::lround(std::fmax(1.0f, cookParam(c, "CountX", 10.0f)));
  P.CountY = (uint32_t)std::lround(std::fmax(1.0f, cookParam(c, "CountY", 10.0f)));
  P.CountZ = (uint32_t)std::lround(std::fmax(1.0f, cookParam(c, "CountZ", 1.0f)));
  P.SizeMode = (uint32_t)(cookParam(c, "SizeMode", 0.0f) > 0.5f ? 1u : 0u);
  P.PointScale = cookParam(c, "PointScale", 1.0f);

  float size[3] = {1.0f, 1.0f, 1.0f};
  float center[3] = {0.0f, 0.0f, 0.0f};
  float pivot[3] = {0.0f, 0.0f, 0.0f};
  cookVecN(c, "Size", size, 3, size);
  cookVecN(c, "Center", center, 3, center);
  cookVecN(c, "Pivot", pivot, 3, pivot);
  P.SizeX = size[0]; P.SizeY = size[1]; P.SizeZ = size[2];
  P.CenterX = center[0]; P.CenterY = center[1]; P.CenterZ = center[2];
  P.PivotX = pivot[0]; P.PivotY = pivot[1]; P.PivotZ = pivot[2];

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(c.output, 0, GRID_Points);
  enc->setBytes(&P, sizeof(P), GRID_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

// --- golden (self-contained: own capture vector + capture-only DrawOp) ---
std::vector<SwPoint>* g_gridCap = nullptr;
void captureGridDraw(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_gridCap || !pts || c.count == 0) return;
  g_gridCap->assign(c.count, SwPoint{});
  std::memcpy(g_gridCap->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerGridPointsOp() { registerPointOp("GridPoints", cookGridPoints); }

// TiXL-formula golden: a Cell-mode 4x3x1 grid centered at Center. Asserts:
//   (1) count == nx*ny*nz   (the product law)
//   (2) all points colinear in z (single layer)  + the bbox span matches
//       (nx-1)*Size.x by (ny-1)*Size.y   (even spacing over the bounds)
//   (3) the grid is centered: mean position == Center (Pivot=0 -> centered on Center)
// injectBug: collapse Size to 0 -> every point lands on Center -> the bbox-span assertion
// FAILs (a real degeneracy: the grid degenerates to a single point).
int runGridPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t NX = 4, NY = 3, NZ = 1;
  const uint32_t TOTAL = NX * NY * NZ;  // 12
  const float SX = 2.0f, SY = 3.0f;     // Cell-mode spacing per axis
  const float CX = 5.0f, CY = -1.0f;    // Center

  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-gridpoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerGridPointsOp();
  std::vector<SwPoint> captured;
  g_gridCap = &captured;
  registerDrawOp("DrawPoints", captureGridDraw);

  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "GridPoints";
  gen.params["Count"] = (float)TOTAL;  // buffer capacity = product (host responsibility)
  gen.params["CountX"] = (float)NX;
  gen.params["CountY"] = (float)NY;
  gen.params["CountZ"] = (float)NZ;
  gen.params["SizeMode"] = 0.0f;  // Cell
  gen.params["Size.x"] = injectBug ? 0.0f : SX;  // bug: zero spacing -> grid collapses
  gen.params["Size.y"] = injectBug ? 0.0f : SY;
  gen.params["Size.z"] = 1.0f;
  gen.params["Center.x"] = CX;
  gen.params["Center.y"] = CY;
  g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // GridPoints.points -> DrawPoints.points

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  bool countOK = captured.size() == TOTAL;  // (1) product law
  float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f, minZ = 1e9f, maxZ = -1e9f;
  float meanX = 0.0f, meanY = 0.0f;
  for (const SwPoint& p : captured) {
    minX = std::fmin(minX, p.Position.x); maxX = std::fmax(maxX, p.Position.x);
    minY = std::fmin(minY, p.Position.y); maxY = std::fmax(maxY, p.Position.y);
    minZ = std::fmin(minZ, p.Position.z); maxZ = std::fmax(maxZ, p.Position.z);
    meanX += p.Position.x; meanY += p.Position.y;
  }
  if (!captured.empty()) { meanX /= (float)captured.size(); meanY /= (float)captured.size(); }

  float spanX = maxX - minX, spanY = maxY - minY, spanZ = maxZ - minZ;
  float wantSpanX = (float)(NX - 1) * SX;  // (2) even spacing over bounds
  float wantSpanY = (float)(NY - 1) * SY;
  bool spanOK = std::fabs(spanX - wantSpanX) < 0.01f &&
                std::fabs(spanY - wantSpanY) < 0.01f &&
                std::fabs(spanZ - 0.0f) < 0.01f;  // single z-layer -> flat
  bool centeredOK = std::fabs(meanX - CX) < 0.01f &&
                    std::fabs(meanY - CY) < 0.01f;  // (3) Pivot=0 centers on Center

  bool pass = countOK && spanOK && centeredOK;
  printf("[selftest-gridpoints] n=%zu(need %u) span=(%.2f,%.2f,%.2f) want=(%.2f,%.2f,0) "
         "mean=(%.2f,%.2f) want=(%.1f,%.1f) -> %s\n",
         captured.size(), TOTAL, spanX, spanY, spanZ, wantSpanX, wantSpanY,
         meanX, meanY, CX, CY, pass ? "PASS" : "FAIL");

  g_gridCap = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
