// runtime/point_ops_sliceviewport_golden — SliceViewPort cell-viewport golden.
//
// EXPECTED VALUES (independent-of-impl, hand-derived from TiXL SliceViewPort.cs:40-73 — NOT from sw helpers):
//   cells=(4,2), CellIndex=5, Stretch=(1,1), ambient RequestedResolution normalized 1×1:
//     cellCount=8; mod=5%8=5; column=5%4=1; row=5/4=1; cellSize=(1/4,1/2).
//     viewport = ((1+0)·1/4, (1+0)·1/2, 1/4·1, 1/2·1) = (0.25, 0.50, 0.25, 0.50)      (cs:49-52)
//     RepeatView clip: M11 = (4/2)/1 = 2.0;  M22 = 1/1 = 1.0                            (cs:69-70)
//   Cell RequestedResolution push (cs:103): ambient 800×600, cells (4,2) → cell (200,300).
//
// PART 1 (closed-form): assert computeSliceViewPortCell + resolveSliceViewPortResolution match the formulas.
//   -bug is INVISIBLE to this part (it corrupts the executor stamp, not the pure math) — Part 2 carries the tooth.
// PART 2 (real render, pixel-in-cell): render a FULL-SCREEN white quad through SliceViewPort into a 256×256
//   RenderTarget. With the cell viewport stamped, white lands ONLY inside the cell rect (x∈[64,128), y∈[128,256)
//   at 256²) — the region OUTSIDE the cell stays black. injectBug skips the viewport stamp → the quad fills the
//   WHOLE target → an outside-cell probe pixel turns white → RED.
#include "runtime/point_ops_sliceviewport.h"

#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                  // Graph/Node/pinId
#include "runtime/point_graph.h"            // PointGraph::cook, registerBuiltinPointOps, RenderResolution
#include "runtime/tixl_point.h"             // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

bool near1(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// Sample a pixel's brightness (max of RGB) at (px,py) in a W×H RGBA8 readback.
int bright(const std::vector<uint8_t>& px, uint32_t W, uint32_t x, uint32_t y) {
  const size_t i = ((size_t)y * W + x) * 4;
  return std::max({(int)px[i], (int)px[i + 1], (int)px[i + 2]});
}

}  // namespace

int runSliceViewPortSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  bool ok = true;

  // ── PART 1: closed-form (SliceViewPort.cs:40-73). Not affected by injectBug (pure math). ──
  {
    std::map<std::string, float> p;
    p["CellCounts.x"] = 4.0f; p["CellCounts.y"] = 2.0f; p["CellIndex"] = 5.0f;
    p["Stretch.x"] = 1.0f; p["Stretch.y"] = 1.0f;
    float vp[4], clip[2];
    computeSliceViewPortCell(p, 1u, 1u, vp, clip);
    const bool vpOK = near1(vp[0], 0.25f) && near1(vp[1], 0.50f) && near1(vp[2], 0.25f) && near1(vp[3], 0.50f);
    const bool clipOK = near1(clip[0], 2.0f) && near1(clip[1], 1.0f);
    RenderResolution cell = resolveSliceViewPortResolution(p, RenderResolution{800, 600});
    const bool resOK = cell.w == 200 && cell.h == 300;
    ok = ok && vpOK && clipOK && resOK;
    std::printf("[selftest-sliceviewport] closed-form vp=(%.3f,%.3f,%.3f,%.3f want 0.25,0.5,0.25,0.5) "
                "clip=(%.2f,%.2f want 2,1) cell=%ux%u(want 200x300) -> %s\n", vp[0], vp[1], vp[2], vp[3],
                clip[0], clip[1], cell.w, cell.h, (vpOK && clipOK && resOK) ? "PASS" : "FAIL");
  }

  // ── PART 2: real render, pixel-in-cell (the executor viewport stamp). ──
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-sliceviewport] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();

  // Graph: RadialPoints(large ring, fills the view) → DrawPoints → SliceViewPort(cells 4×2, cell 5) → RenderTarget.
  // Cell 5 = column 1, row 1 → viewport x∈[0.25,0.50), y∈[0.50,1.0) of the 256² target = x∈[64,128), y∈[128,256).
  const uint32_t W = 256, H = 256;
  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = 512.0f; gen.params["Radius"] = 3.2f;  // ring near the view edge → fills the cell area
  g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; drw.params["PointSize"] = 0.4f; g.nodes.push_back(drw);
  Node sv; sv.id = 3; sv.type = "SliceViewPort";
  sv.params["CellCounts.x"] = 4.0f; sv.params["CellCounts.y"] = 2.0f; sv.params["CellIndex"] = 5.0f;
  g.nodes.push_back(sv);
  Node rt; rt.id = 4; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
  g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points → DrawPoints.points
  g.connections.push_back({102, pinId(2, 1), pinId(3, 1)});  // DrawPoints.out → SliceViewPort.SubGraph (input port 1)
  g.connections.push_back({103, pinId(3, 0), pinId(4, 0)});  // SliceViewPort.Output → RenderTarget.command

  sliceViewPortDisableStampForTest() = injectBug;  // -bug: skip the viewport stamp → content fills full target

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, W, H);
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));
  MTL::Texture* tex = pg.debugCookedTexture(4);

  sliceViewPortDisableStampForTest() = false;  // reset (never leak)

  int inCell = 0, outside = 0;
  if (tex && (uint32_t)tex->width() == W && (uint32_t)tex->height() == H) {
    std::vector<uint8_t> px((size_t)W * H * 4, 0);
    tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
    // Count lit pixels INSIDE the cell (x∈[64,128), y∈[128,256)) vs OUTSIDE it. Faithful: content only in
    // the cell → inCell>0 && outside==0. -bug (no viewport): the quad fills the whole target → outside>0.
    for (uint32_t y = 0; y < H; ++y)
      for (uint32_t x = 0; x < W; ++x) {
        if (bright(px, W, x, y) <= 30) continue;
        const bool cell = (x >= 64 && x < 128 && y >= 128 && y < 256);
        if (cell) ++inCell; else ++outside;
      }
  }
  const bool renderOK = inCell > 50 && outside == 0;
  ok = ok && renderOK;
  std::printf("[selftest-sliceviewport] render inCell=%d(need>50) outside=%d(need 0)%s -> %s\n", inCell,
              outside, injectBug ? " (injectBug→no viewport stamp)" : "", renderOK ? "PASS" : "tripped");

  lib->release(); q->release(); dev->release(); pool->release();
  return ok ? 0 : 1;
}

}  // namespace sw
