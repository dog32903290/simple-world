// BoundingBoxPoints — batch 38 GENERATOR op (reads a Points input, emits ONE point).
// CPU-READBACK NAMED FORK of external/tixl .../point/generate/BoundingBoxPoints
// (.cs ports; .hlsl math, quoted below). TiXL computes the axis-aligned bounding box (AABB)
// of every source point on the GPU (atomic ordered-int min/max into an aux Bounds buffer over
// two extra kernels), then writes exactly ONE output point:
//
//   external/tixl .../shaders/points/generate/BoundingBoxPoints.hlsl:118-127
//     float3 centerPos = (minPos + maxPos) * 0.5;
//     ResultPoints[0].Position = centerPos;
//     ResultPoints[0].Stretch  = maxPos - minPos;
//     ResultPoints[0].Selected = 1;
//     ResultPoints[0].Color    = 1;
//     ResultPoints[0].W        = 1;
//     ResultPoints[0].Rotation = float4(0, 0, 0, 1);
//
// Valid-point filter (BoundingBoxPoints.hlsl:61): a source whose Position has ANY NaN component is
// excluded from the AABB — so separator points (NaN Scale is the marker, but a NaN *Position*
// also reads here) never pollute min/max. We mirror the EXACT predicate: skip a source point if
// any of position.{x,y,z} is NaN.
//
// ───────────────── NAMED FORKS (who / why / authority) ─────────────────
// FORK 1 — CPU-readback instead of GPU (orchestrator, batch 38). TiXL's GPU path needs a
//   multi-kernel dance: clear2 zeroes an aux MinMax(uint3) Bounds buffer, main() does ordered-int
//   InterlockedMin/Max (FloatToOInt/OIntToFloat, .hlsl:28-43) into Bounds[0], a memory barrier,
//   then thread 0 writes ResultPoints[0]. That machinery exists ONLY because GPU has no float
//   atomics. On CPU we read the source bag straight from its shared buffer (every SwPoint bag is
//   MTL::StorageModeShared — point_graph.cpp ensureOut + all leaf allocs) and do a plain min/max
//   loop. The RESULT is bit-identical (same float min/max, same center/size formula); we drop only
//   the atomic-ordered-int encoding, which is a pure GPU implementation detail, not a value. This
//   is the same posture as CommonPointSets (CPU-fill fork of a GPU-mirrored StructuredList).
//
// FORK 2 — field mapping Stretch→Scale, Selected→FX2, W→FX1. This is NOT a guess: TiXL's
//   LegacyPoint (the struct the .hlsl writes) and our SwPoint (= TiXL `Point`) share the SAME
//   64-byte stride field-for-field (external/tixl .../shaders/shared/point.hlsl:9-27):
//       LegacyPoint{ Position@0, W@12,        Rotation@16, Color@32, Stretch@48,  Selected@60 }
//       Point/SwPoint{ Position@0, FX1@12,    Rotation@16, Color@32, Scale@48,    FX2@60 }
//   So `ResultPoints[0].Stretch` lands in the bytes our SwPoint calls `Scale` (@48); `.Selected`
//   lands in `FX2` (@60); `.W` lands in `FX1` (@12). The mapping is the SAME bytes, authoritative
//   from the shared header's own comment ("W -> Radius, Stretch -> Velocity, Selected -> BirthTime"
//   — the particle aliasing, which proves these three fields are the reinterpretable trio). So box
//   SIZE = SwPoint.Scale, Selected flag = SwPoint.FX2, W = SwPoint.FX1. No invented field.
//
// COUNT POLICY: output count = 1, ALWAYS. No Count port. bbCountTransform ignores the natural
//   (input-bag) count and returns 1, so the cook driver sizes the output bag to exactly one point.
//   countFromFirstPointsInput=false (the natural count is the summed input bag count; we override
//   it to 1 regardless). The source bag count is still read via c.inputCounts[0] for the loop.
//
// Self-contained leaf: own capture vector + draw op (mirrors point_ops_resamplelinepoints.cpp).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"        // Graph/Node/pinId
#include "runtime/point_graph.h"  // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tixl_point.h"   // SwPoint (64B) + field offsets proven by static_assert

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// Output count is fixed at 1 (one AABB point), regardless of the input bag size.
uint32_t bbCountTransform(uint32_t /*natural*/) { return 1u; }

// CPU-readback cook: read the source bag from its shared buffer, accumulate min/max over valid
// (non-NaN-position) points, write ONE output point per BoundingBoxPoints.hlsl:118-127.
void cookBoundingBoxPoints(PointCookCtx& c) {
  if (!c.output) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  uint32_t sourceCount = (c.inputCounts && c.inputCount > 0) ? c.inputCounts[0] : 0u;

  SwPoint* out = (SwPoint*)c.output->contents();
  SwPoint& r = out[0];

  // Defaults for the unwired / empty / all-NaN case: TiXL's ordered-int min/max start as
  // (0xffffffff -> +largest, 0 -> -largest); with no valid point min>max and the (min+max)*0.5
  // center is meaningless but finite. We instead emit a clean degenerate box at the origin
  // (center=0, size=0) so an unwired node draws a single visible point rather than garbage.
  if (!srcBag || sourceCount == 0) {
    r.Position = {0.0f, 0.0f, 0.0f};
    r.FX1      = 1.0f;                       // W = 1
    r.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};   // identity
    r.Color    = {1.0f, 1.0f, 1.0f, 1.0f};   // Color = 1
    r.Scale    = {0.0f, 0.0f, 0.0f};         // Stretch = 0 (degenerate box)
    r.FX2      = 1.0f;                        // Selected = 1
    return;
  }

  const SwPoint* src = (const SwPoint*)const_cast<MTL::Buffer*>(srcBag)->contents();

  float minX = 0.0f, minY = 0.0f, minZ = 0.0f;
  float maxX = 0.0f, maxY = 0.0f, maxZ = 0.0f;
  bool seen = false;
  for (uint32_t i = 0; i < sourceCount; ++i) {
    float px = src[i].Position.x, py = src[i].Position.y, pz = src[i].Position.z;
    // BoundingBoxPoints.hlsl:61 — isValid = !(isnan(x)||isnan(y)||isnan(z)). NaN-position points
    // (separators read here) are dropped from the AABB.
    if (std::isnan(px) || std::isnan(py) || std::isnan(pz)) continue;
    if (!seen) {
      minX = maxX = px; minY = maxY = py; minZ = maxZ = pz;
      seen = true;
    } else {
      if (px < minX) minX = px; if (px > maxX) maxX = px;
      if (py < minY) minY = py; if (py > maxY) maxY = py;
      if (pz < minZ) minZ = pz; if (pz > maxZ) maxZ = pz;
    }
  }
  if (!seen) { minX = minY = minZ = maxX = maxY = maxZ = 0.0f; }  // all-NaN -> origin box

  // BoundingBoxPoints.hlsl:118 — centerPos = (minPos + maxPos) * 0.5
  r.Position = {(minX + maxX) * 0.5f, (minY + maxY) * 0.5f, (minZ + maxZ) * 0.5f};
  r.FX1      = 1.0f;                                          // .W = 1 (FORK 2: W@12 == FX1@12)
  r.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};                      // .Rotation = float4(0,0,0,1)
  r.Color    = {1.0f, 1.0f, 1.0f, 1.0f};                      // .Color = 1
  r.Scale    = {maxX - minX, maxY - minY, maxZ - minZ};       // .Stretch (FORK 2: Stretch@48 == Scale@48)
  r.FX2      = 1.0f;                                          // .Selected = 1 (FORK 2: Selected@60 == FX2@60)
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capBBox = nullptr;
void captureDrawBBox(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capBBox || !pts || c.count == 0) return;
  g_capBBox->assign(c.count, SwPoint{});
  std::memcpy(g_capBBox->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

// Direct cook runner for precise teeth (no graph plumbing): build a shared source buffer from
// `rows`, hand-construct a PointCookCtx feeding it as inputs[0], call the real cookBoundingBoxPoints,
// and read back the single output point. Mirrors ResampleLinePoints' runResampleKernelDirect.
SwPoint runBBoxCookDirect(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q,
                          const std::vector<SwPoint>& rows, const EvaluationContext& ctx) {
  const size_t inBytes = rows.size() * sizeof(SwPoint);
  MTL::Buffer* src = dev->newBuffer(rows.empty() ? sizeof(SwPoint) : inBytes,
                                    MTL::ResourceStorageModeShared);
  if (!rows.empty()) std::memcpy(src->contents(), rows.data(), inBytes);
  MTL::Buffer* out = dev->newBuffer(sizeof(SwPoint), MTL::ResourceStorageModeShared);

  const MTL::Buffer* ins[1] = {src};
  uint32_t inCounts[1] = {(uint32_t)rows.size()};
  PointCookCtx cc{};
  cc.dev = dev; cc.lib = lib; cc.queue = q; cc.ctx = &ctx;
  cc.count = 1;  // BoundingBoxPoints always emits 1 (bbCountTransform)
  cc.inputs = ins; cc.inputCounts = inCounts; cc.inputCount = 1;
  cc.output = out;
  cookBoundingBoxPoints(cc);

  SwPoint result{};
  std::memcpy(&result, out->contents(), sizeof(SwPoint));
  src->release(); out->release();
  return result;
}

SwPoint mkBBoxPt(float x, float y, float z) {
  SwPoint p{};
  p.Position = {x, y, z};
  p.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  p.Color    = {1.0f, 1.0f, 1.0f, 1.0f};
  p.Scale    = {1.0f, 1.0f, 1.0f};
  p.FX1 = 0.0f; p.FX2 = 0.0f;
  return p;
}

}  // namespace

void registerBoundingBoxPointsOp() {
  registerPointOp("BoundingBoxPoints", cookBoundingBoxPoints,
                  /*stateNew=*/nullptr, /*stateFree=*/nullptr,
                  bbCountTransform,
                  /*countFromFirstPointsInput=*/false);
}

// =============================================================================
// Golden — BoundingBoxPoints over hand-built point bags. The three coordinate teeth call the real
// cookBoundingBoxPoints DIRECTLY (runBBoxCookDirect, a hand-built PointCookCtx feeding the source
// as inputs[0]) so the min/max + center + field-mapping math is asserted precisely. A final graph
// smoke (LinePoints -> BoundingBoxPoints -> DrawPoints capture) proves the op is wired into the cook
// driver end-to-end (input-bag gather + bbCountTransform sizing the output bag to 1).
//
//   CASE A (unit cube, hand-computable): source = the 8 corners of the ±1 cube plus a scatter
//     point at (0.5,-0.5,0.5) inside it. AABB = [-1,+1]^3 -> Position==(0,0,0),
//     Scale(box size)==(2,2,2), FX2(Selected)==1, FX1(W)==1, Color==1, Rotation==identity.
//
//   CASE B (asymmetric box -> nonzero center): source = {(1,2,3),(5,8,11)} -> AABB
//     min=(1,2,3) max=(5,8,11) -> center=(3,5,7), size=(4,6,8). Proves center is the MIDPOINT
//     (min+max)/2, not the mean or the origin, and size = max-min per axis.
//
//   CASE C (NaN-position exclusion): source = {(−1,−1,−1), SEPARATOR(NaN position), (+1,+1,+1)}.
//     The separator's NaN position must be DROPPED from the AABB -> center==(0,0,0),
//     size==(2,2,2) — IDENTICAL to a clean 2-point ±1 box. If NaN leaked into min/max the box
//     would be NaN/inf. Proves the isValid filter (.hlsl:61).
//
//   injectBug: CASE A asserts the WRONG center law (center == max, i.e. (1,1,1)) — a real formula
//     flip of (min+max)*0.5. The faithful cook yields (0,0,0) so the assertion FAILS (rc=1).
//     A genuine center error, not an inverted assert.
// =============================================================================
int runBoundingBoxPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-boundingboxpoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  auto near = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
  bool pass = true;

  // ===== CASE A: unit ±1 cube + interior scatter =====
  {
    std::vector<SwPoint> rows;
    for (int sx = -1; sx <= 1; sx += 2)
      for (int sy = -1; sy <= 1; sy += 2)
        for (int sz = -1; sz <= 1; sz += 2)
          rows.push_back(mkBBoxPt((float)sx, (float)sy, (float)sz));
    rows.push_back(mkBBoxPt(0.5f, -0.5f, 0.5f));  // interior scatter (does not move the AABB)

    SwPoint p = runBBoxCookDirect(dev, lib, q, rows, ctx);
    // injectBug asserts center == max == (1,1,1) (formula flip); faithful center == (0,0,0).
    float exC = injectBug ? 1.0f : 0.0f;
    bool ok = near(p.Position.x, exC) && near(p.Position.y, exC) && near(p.Position.z, exC) &&
              near(p.Scale.x, 2.0f) && near(p.Scale.y, 2.0f) && near(p.Scale.z, 2.0f) &&
              near(p.FX2, 1.0f) && near(p.FX1, 1.0f) &&
              near(p.Color.x, 1.0f) && near(p.Rotation.w, 1.0f);
    printf("[selftest-boundingboxpoints] A(unit cube) center=(%.3f,%.3f,%.3f) "
           "size=(%.3f,%.3f,%.3f) sel=%.1f w=%.1f -> %s\n",
           p.Position.x, p.Position.y, p.Position.z, p.Scale.x, p.Scale.y, p.Scale.z,
           p.FX2, p.FX1, ok ? "ok" : "NO");
    pass = pass && ok;
  }

  // ===== CASE B: asymmetric box -> nonzero center (midpoint, not mean) =====
  {
    std::vector<SwPoint> rows;
    rows.push_back(mkBBoxPt(1.0f, 2.0f, 3.0f));
    rows.push_back(mkBBoxPt(5.0f, 8.0f, 11.0f));
    SwPoint p = runBBoxCookDirect(dev, lib, q, rows, ctx);
    bool ok = near(p.Position.x, 3.0f) && near(p.Position.y, 5.0f) && near(p.Position.z, 7.0f) &&
              near(p.Scale.x, 4.0f) && near(p.Scale.y, 6.0f) && near(p.Scale.z, 8.0f);
    printf("[selftest-boundingboxpoints] B(asym) center=(%.3f,%.3f,%.3f) size=(%.3f,%.3f,%.3f) -> %s\n",
           p.Position.x, p.Position.y, p.Position.z, p.Scale.x, p.Scale.y, p.Scale.z,
           ok ? "ok" : "NO");
    pass = pass && ok;
  }

  // ===== CASE C: NaN-position exclusion (separator must not pollute the AABB) =====
  {
    std::vector<SwPoint> rows;
    rows.push_back(mkBBoxPt(-1.0f, -1.0f, -1.0f));
    SwPoint sep = mkBBoxPt(0.0f, 0.0f, 0.0f);
    sep.Position = {NAN, NAN, NAN};       // NaN POSITION -> excluded by isValid (.hlsl:61)
    sep.Scale    = {NAN, NAN, NAN};       // separator marker
    rows.push_back(sep);
    rows.push_back(mkBBoxPt(1.0f, 1.0f, 1.0f));
    SwPoint p = runBBoxCookDirect(dev, lib, q, rows, ctx);
    // If NaN leaked in, center/size would be NaN. Faithful result == clean ±1 box.
    bool ok = std::isfinite(p.Position.x) && std::isfinite(p.Scale.x) &&
              near(p.Position.x, 0.0f) && near(p.Position.y, 0.0f) && near(p.Position.z, 0.0f) &&
              near(p.Scale.x, 2.0f) && near(p.Scale.y, 2.0f) && near(p.Scale.z, 2.0f);
    printf("[selftest-boundingboxpoints] C(NaN-skip) center=(%.3f,%.3f,%.3f) size=(%.3f,%.3f,%.3f) -> %s\n",
           p.Position.x, p.Position.y, p.Position.z, p.Scale.x, p.Scale.y, p.Scale.z,
           ok ? "ok" : "NO");
    pass = pass && ok;
  }

  printf("[selftest-boundingboxpoints] -> %s%s\n", pass ? "PASS" : "FAIL",
         injectBug ? " (bug-mode: expect FAIL)" : "");

  // --- graph-path smoke: LinePoints -> BoundingBoxPoints -> DrawPoints capture ---
  // Proves the op is wired into the real cook driver (input-bag gather + bbCountTransform sizing
  // the output bag to exactly 1, regardless of the LinePoints source size).
  {
    registerBuiltinPointOps();
    registerBoundingBoxPointsOp();
    std::vector<SwPoint> captured;
    g_capBBox = &captured;
    registerDrawOp("DrawPoints", captureDrawBBox);

    PointGraph pg(dev, lib, q, 256, 256);
    Graph g;
    Node gen; gen.id = 1; gen.type = "LinePoints";
    gen.params["Count"]  = 16.0f;
    gen.params["Length"] = 4.0f;
    g.nodes.push_back(gen);
    Node bb; bb.id = 2; bb.type = "BoundingBoxPoints"; g.nodes.push_back(bb);
    Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
    g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // line -> bbox.input
    g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // bbox.out -> draw

    pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

    bool graphCountOk = (captured.size() == 1);  // 16-point line -> ONE AABB point
    // The LinePoints default Direction is (0,1,0) so the line runs along Y: the box has extent
    // along Y (Scale.y > 0) and ~0 along X/Z. Assert a finite, non-degenerate box (Y extent) +
    // Selected=1 — proves the source bag was actually read through the driver, not the empty path.
    float boxY = graphCountOk ? captured[0].Scale.y : 0.0f;
    bool graphBoxOk = graphCountOk && std::isfinite(boxY) && boxY > 0.0f &&
                      near(captured[0].FX2, 1.0f);
    printf("[selftest-boundingboxpoints] graph LinePoints(16)->BBox captured=%zu boxY=%.3f -> %s\n",
           captured.size(), boxY, (graphCountOk && graphBoxOk) ? "ok" : "NO");
    pass = pass && graphCountOk && graphBoxOk;
    g_capBBox = nullptr;
  }

  printf("[selftest-boundingboxpoints] -> %s\n", pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
