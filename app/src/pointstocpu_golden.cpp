// pointstocpu_golden — golden for PointsToCPU on the PointList currency (the GPU→host point readback):
//   --selftest-pointstocpu   (PointsToCPU: read a GPU Points bag → host List<Point>, flat)
//
// PointsToCPU is the DOWNLOAD mirror of ListToBuffer's upload bridge: it reads whole SwPoints (all 64
// bytes) out of a StorageModeShared Points bag's contents(), honoring the TiXL StartIndex/MaxCount clamp
// (PointsToCPU.cs:106-132). Its production readback path is the flat PointGraph::cook, which actually
// DISPATCHES the GPU point pipeline and reads the cooked bag back; the pure-CPU resident pass has no
// resident Points gather (the GPU bag is unreachable from the resident pointlist cook), so PointsToCPU is
// flat-only here — a hand-built-bag DIRECT-COOK tooth for precise clamp math (BYTE-IDENTICAL per SwPoint)
// + a GRAPH SMOKE (LinePoints(16) → PointsToCPU → terminal) for end-to-end wiring through the cook driver.
//
// injectBug routes through pointListInjectBug() so the RED case corrupts the REAL cook output (drops the
// last point) on the actual op path — teeth on the cook, not the expected value (mirror colorlist_fanout).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <simd/simd.h>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                  // Graph/Node/Connection/pinId
#include "runtime/eval_context.h"           // EvaluationContext
#include "runtime/point_graph.h"            // PointGraph::cook + debugCookedPointList
#include "runtime/pointlist_op_registry.h"  // PointListCookCtx / pointListInjectBug / findPointListOp
#include "runtime/tixl_point.h"             // SwPoint (64B stride)

namespace sw {

const PointListCookFn* findPointListOp(const std::string&);  // dispatch a cook fn directly (PointsToCPU)

namespace {

// Byte-identical SwPoint compare (all 64 bytes — Position/FX1/Rotation/Color/Scale/FX2). The readback is
// a raw copy out of the bag, so the assertion is bytewise, not float-near.
bool ptEq(const SwPoint& a, const SwPoint& b) { return std::memcmp(&a, &b, sizeof(SwPoint)) == 0; }
bool listEq(const std::vector<SwPoint>& a, const std::vector<SwPoint>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (!ptEq(a[i], b[i])) return false;
  return true;
}

// Direct-cook one PointsToCPU over a hand-built bag. Returns the read-back host point list.
std::vector<SwPoint> runReadDirect(MTL::Device* dev, const std::vector<SwPoint>& rows,
                                   int startIndex, int maxCount) {
  const size_t inBytes = rows.size() * sizeof(SwPoint);
  MTL::Buffer* bag = dev->newBuffer(rows.empty() ? sizeof(SwPoint) : inBytes,
                                    MTL::ResourceStorageModeShared);
  if (!rows.empty()) std::memcpy(bag->contents(), rows.data(), inBytes);

  std::map<std::string, float> params;
  params["StartIndex"] = (float)startIndex;
  params["MaxCount"]   = (float)maxCount;
  std::vector<SwPoint> out;

  PointListCookCtx pc;
  pc.inputPointsBag = bag;
  pc.inputPointsCount = (uint32_t)rows.size();
  pc.output = &out;
  pc.params = &params;
  const PointListCookFn* fn = findPointListOp("PointsToCPU");
  if (fn && *fn) (*fn)(pc);
  bag->release();
  return out;
}

// Hand-build a distinct SwPoint: Position=(i,0,0) so per-point identity + ordering is unambiguous, plus a
// distinct FX1/Color/Scale/FX2 per index so the BYTE compare catches a mis-offset (not just Position).
SwPoint mkPt(int i) {
  float f = (float)i;
  SwPoint p{};
  p.Position = {f, 0.0f, 0.0f};
  p.FX1      = 1.0f + f;
  p.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  p.Color    = simd::make_float4(f, 1.0f - f, 0.5f, 1.0f);
  p.Scale    = {2.0f + f, 1.0f, 1.0f};
  p.FX2      = 7.0f + f;
  return p;
}

}  // namespace

int runPointsToCpuSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(
      NS::String::string("shaders.metallib", NS::UTF8StringEncoding), &err);
  bool ok = true;

  // 16 distinct points (= LinePoints(16), the smoke's count) so order + indexing is unambiguous.
  std::vector<SwPoint> rows;
  for (int i = 0; i < 16; ++i) rows.push_back(mkPt(i));

  pointListInjectBug() = injectBug;

  // CASE A — StartIndex=0, MaxCount=50, count=16 → all 16 points, byte-identical, in order. The EXPECTED
  // value is the FULL list (NOT bug-adjusted): injectBug drops the last point in the REAL cook → got ≠ want
  // → RED (teeth on the actual op path, not the expected value).
  {
    std::vector<SwPoint> got = runReadDirect(dev, rows, 0, 50);
    bool pass = listEq(got, rows);
    ok = ok && pass;
    std::printf("[selftest-pointstocpu] A(all) size=%zu -> %s\n", got.size(), pass ? "PASS" : "FAIL");
  }
  // CASE B — StartIndex=1 → drops point[0], list = points[1..15] (15 points). cs:106/124.
  {
    std::vector<SwPoint> got = runReadDirect(dev, rows, 1, 50);
    std::vector<SwPoint> want(rows.begin() + 1, rows.end());
    bool pass = listEq(got, want);
    ok = ok && pass;
    std::printf("[selftest-pointstocpu] B(start=1) size=%zu -> %s\n", got.size(), pass ? "PASS" : "FAIL");
  }
  // CASE C — MaxCount=2 → [P0,P1] (clamp). cs:119-122.
  {
    std::vector<SwPoint> got = runReadDirect(dev, rows, 0, 2);
    std::vector<SwPoint> want(rows.begin(), rows.begin() + 2);
    bool pass = listEq(got, want);
    ok = ok && pass;
    std::printf("[selftest-pointstocpu] C(max=2) size=%zu -> %s\n", got.size(), pass ? "PASS" : "FAIL");
  }
  // CASE D — StartIndex >= count → empty (cs:112-115). injectBug cannot drop from empty; stays [].
  {
    std::vector<SwPoint> got = runReadDirect(dev, rows, 16, 50);
    bool pass = got.empty();
    ok = ok && pass;
    std::printf("[selftest-pointstocpu] D(start>=count) size=%zu -> %s\n",
                got.size(), pass ? "PASS" : "FAIL");
  }
  pointListInjectBug() = false;

  // TOOTH 2 — graph smoke: LinePoints(16) → PointsToCPU → terminal, cook through the real driver. Proves
  // the Points-input gather is wired into cookPointListNode (count == the line's 16 points). The bug-mode
  // drop is exercised on the direct teeth above; here we only assert non-bug wiring (so the graph leg does
  // not flip pass on bug-mode — the direct teeth own the RED). We assert count == 16 and the first point's
  // Position is finite (the bag actually crossed GPU→host through the driver, not a stale/garbage read).
  if (lib) {
    Graph g;
    Node gen; gen.id = 2; gen.type = "LinePoints";
    gen.params["Count"] = 16.0f; gen.params["Length"] = 4.0f;
    g.nodes.push_back(gen);
    Node rd; rd.id = 1; rd.type = "PointsToCPU"; g.nodes.push_back(rd);
    g.connections.push_back({101, pinId(2, 0), pinId(1, /*PointBuffer*/ 0)});

    PointGraph pg(dev, lib, q, 256, 256);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, /*terminal*/ 1);
    const std::vector<SwPoint>* got = pg.debugCookedPointList(1);
    size_t n = got ? got->size() : 0;
    bool pass = (n == 16) && got && std::isfinite((*got)[0].Position.x);
    ok = ok && pass;
    std::printf("[selftest-pointstocpu] graph LinePoints(16)->PointsToCPU count=%zu -> %s\n",
                n, pass ? "PASS" : "FAIL");
  } else {
    std::printf("[selftest-pointstocpu] graph smoke SKIPPED (no metallib)\n");
  }

  q->release(); if (lib) lib->release(); dev->release(); pool->release();
  std::printf("[selftest-pointstocpu] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
