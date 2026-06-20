// MultiUpdatePoints — COMBINE op (point_combine family): a fan-in HELPER that forces multiple
// in-place point modifiers (each returning the SAME BufferWithViews) to evaluate, then passes the
// LAST connected buffer through unchanged. Faithful port of TiXL _internal/MultiUpdatePoints.
//
// Reference:
//   external/tixl/Operators/Lib/point/_internal/MultiUpdatePoints.cs   (Update logic)
//   external/tixl/Operators/Lib/point/_internal/MultiUpdatePoints.t3   (no children, no shader)
//   .t3ui Description: "A helper to combine multiple point modifiers like ApplyNoise or ApplyForce
//                       to the same Point buffer."
//
// TiXL .cs Update() (verbatim semantics):
//   connectedLists = PointBuffers.CollectedInputs.GetValue(...).Where(!= null)
//   if (connectedLists.Count == 0) -> Result = null
//   for index 1..N: if (connectedLists[index] != connectedLists[0]) Log.Warning("Inconsistent ...")
//   Result.Value = connectedLists[connectedLists.Count - 1]   // pass through the LAST buffer
//
// INPUT CARDINALITY (.cs): ONE MultiInputSlot<BufferWithViews> (PointBuffers, 7caa2f8c, DefaultValue
//   null). The cook DRIVER's buffer-input gather reads ONE connection per PORT (it does NOT expand a
//   MultiInput buffer port — only FloatList/PointList/Mesh/String gathers loop wires). So we model
//   TiXL's dynamic MultiInput as FIXED input0..input3 Points ports — the SAME convention CombineBuffers
//   uses ("v1 exposes a FIXED 4 Points input ports for TiXL's dynamic MultiInput"). Unwired = null.
//
// COUNT POLICY: TiXL returns connectedLists[last], so the output count = the LAST connected buffer's
//   count. The driver offers sum (default) or first-wired (countFromFirstPointsInput). In FAITHFUL
//   usage every connected buffer is the SAME BufferWithViews (TiXL warns on mismatch), so
//   last_count == first_count == any_count. We opt into countFromFirstPointsInput=true: the output is
//   sized to the first Points input's count, which equals the last in faithful (same-buffer) usage.
//   fork[count-first-equals-last]: under TiXL's warned "inconsistent buffers" case the counts could
//   differ; we size to the first input. This is the documented degenerate case TiXL itself flags, not
//   a faithful render path.
//
// NO SHADER, NO PARAMS: pure passthrough is a single GPU blit (copy the last wired bag into output).
//
// Self-contained leaf: own cook + register + golden. No driver/PointCookCtx change (layer-A: the
// point-buffer seam is already alive; this leaf only consumes it).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"        // Graph/Node/pinId
#include "runtime/point_graph.h"  // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tixl_point.h"   // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// MultiUpdatePoints: pass the LAST wired Points input through to output via a blit copy. The output
// is pre-sized to inputCounts[0] (first-wired count; == last in faithful same-buffer usage). We copy
// min(lastCount, c.count) points so we never write past the output bag. No wired input = no-op.
void cookMultiUpdatePoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.inputCounts) return;

  // Find the LAST non-null wired input (TiXL: connectedLists[connectedLists.Count - 1]).
  const MTL::Buffer* last = nullptr;
  uint32_t lastCount = 0;
  for (int i = 0; i < c.inputCount; ++i) {
    if (c.inputs[i] && c.inputCounts[i] > 0) {
      last = c.inputs[i];
      lastCount = c.inputCounts[i];
    }
  }
  if (!last) return;  // no connected buffers -> TiXL Result = null (no-op here)

  uint32_t n = (lastCount < c.count) ? lastCount : c.count;  // never overrun the output bag
  if (n == 0) return;

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
  blit->copyFromBuffer(const_cast<MTL::Buffer*>(last), 0, c.output, 0,
                       (NS::UInteger)n * sizeof(SwPoint));
  blit->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capMulti = nullptr;
void captureDrawMulti(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capMulti || !pts || c.count == 0) return;
  g_capMulti->assign(c.count, SwPoint{});
  std::memcpy(g_capMulti->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerMultiUpdatePointsOp() {
  // countFromFirstPointsInput=true: output count = first wired Points input's count (== the last
  // in faithful same-buffer usage). MultiUpdatePoints passes a buffer THROUGH; it never concatenates,
  // so the summed-count default would over-size the output (and dispatch garbage past the bag).
  registerPointOp("MultiUpdatePoints", cookMultiUpdatePoints,
                  /*stateNew=*/nullptr, /*stateFree=*/nullptr,
                  /*countTransform=*/nullptr,
                  /*countFromFirstPointsInput=*/true);
}

// Golden: TWO SpherePoints generators of the SAME count N (so the count contract holds whichever is
// "first") but DISTINCT radii — Rsmall=2 (input0) and Rbig=8 (input1). Both wired into
// MultiUpdatePoints; it passes the LAST wired input (Rbig) through -> DrawPoints captures it. Using
// equal-count, same-buffer-shape inputs keeps this on the FAITHFUL path (TiXL warns when the wired
// buffers differ; here they are interchangeable in count, only the radius distinguishes them so the
// last-passthrough is observable). TEETH:
//   (1) count == N (passthrough preserves count; the summed-count default would give 2N).
//   (2) every captured point lies on the BIG sphere: |Position| ~= Rbig (the LAST wired bag really
//       threaded through — GPU readback of the 64B SwPoint stride).
//   (3) byte-readback parity: the cooked GPU output buffer's first point == the captured first point
//       (proves the blit copied the 64B SwPoint stride verbatim).
// injectBug: DROP the input1 (Rbig) wire so the LAST wired input becomes the Rsmall sphere ->
//   captured points lie at r~=Rsmall, NOT Rbig -> assertion (2) FAILS while count stays N. This
//   proves the cook returns the LAST wired buffer (the passthrough contract), not a fixed/first slot.
//   (A real degeneracy of the passthrough, not a flipped assert.)
int runMultiUpdatePointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N     = 256;   // both spheres share this count (count contract is radius-agnostic)
  const float    Rsmall = 2.0f; // input0 sphere radius (bug's surviving last input)
  const float    Rbig   = 8.0f; // input1 sphere radius (faithful last-wired -> passthrough returns)

  MTL::Device*       dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue*   q = dev->newCommandQueue();
  NS::Error*         err = nullptr;
  MTL::Library*      lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-multiupdatepoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();        // SpherePoints (the input generators)
  registerMultiUpdatePointsOp();    // this op (explicit -> self-contained)
  std::vector<SwPoint> captured;
  g_capMulti = &captured;
  registerDrawOp("DrawPoints", captureDrawMulti);

  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node sphSmall; sphSmall.id = 1; sphSmall.type = "SpherePoints";
  sphSmall.params["Count"] = (float)N; sphSmall.params["Radius"] = Rsmall;
  g.nodes.push_back(sphSmall);
  Node sphBig; sphBig.id = 2; sphBig.type = "SpherePoints";
  sphBig.params["Count"] = (float)N; sphBig.params["Radius"] = Rbig;
  g.nodes.push_back(sphBig);
  Node mu; mu.id = 3; mu.type = "MultiUpdatePoints"; g.nodes.push_back(mu);
  Node drw; drw.id = 4; drw.type = "DrawPoints"; g.nodes.push_back(drw);

  // input0 (port 0) <- small sphere (the bug's surviving last input; also drives the count contract)
  g.connections.push_back({101, pinId(1, 0), pinId(3, 0)});
  // input1 (port 1) <- big sphere (the LAST wired input -> what passthrough must return).
  // injectBug DROPS this wire so the last wired input becomes the small sphere.
  if (!injectBug)
    g.connections.push_back({102, pinId(2, 0), pinId(3, 1)});
  // MultiUpdatePoints.out (port 4) -> DrawPoints.points
  g.connections.push_back({103, pinId(3, 4), pinId(4, 0)});

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  // GPU-readback golden: also pull the cooked output buffer directly (64B-stride proof).
  const MTL::Buffer* outBuf = pg.debugCookedBuffer(3);
  uint32_t cookedCount = pg.debugCookedCount(3);

  const float Rexpect = Rbig;  // GREEN: last wired = big sphere; BITE drops it -> small sphere fails
  bool countOK = (captured.size() == N);   // teeth: passthrough preserves N (not 2N)
  bool onSphere = !captured.empty();
  for (const SwPoint& p : captured) {
    float r = std::sqrt(p.Position.x * p.Position.x + p.Position.y * p.Position.y +
                        p.Position.z * p.Position.z);
    if (std::fabs(r - Rexpect) > 0.05f) { onSphere = false; break; }  // bug -> r~=Rsmall, fails
  }

  // byte-readback parity: the cooked GPU buffer's first point matches the captured first point
  // (proves debugCookedBuffer + the blit copied the 64B SwPoint stride verbatim).
  bool readbackOK = (outBuf != nullptr) && (cookedCount == captured.size());
  if (readbackOK && cookedCount > 0) {
    const SwPoint* gpu = reinterpret_cast<const SwPoint*>(
        const_cast<MTL::Buffer*>(outBuf)->contents());
    if (std::fabs(gpu[0].Position.x - captured[0].Position.x) > 1e-5f ||
        std::fabs(gpu[0].Position.y - captured[0].Position.y) > 1e-5f ||
        std::fabs(gpu[0].Position.z - captured[0].Position.z) > 1e-5f)
      readbackOK = false;
  }

  bool pass = countOK && onSphere && readbackOK;
  printf("[selftest-multiupdatepoints] n=%zu(want %u) onSphere(r~=%.1f)=%d readback=%d(cooked=%u) -> %s\n",
         captured.size(), N, Rexpect, onSphere ? 1 : 0, readbackOK ? 1 : 0, cookedCount,
         pass ? "PASS" : "FAIL");

  g_capMulti = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
