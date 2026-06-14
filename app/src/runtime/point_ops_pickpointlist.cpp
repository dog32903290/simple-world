// PickPointList — batch 24 COMBINE op (point_combine family): selects one input buffer from a
// multi-input list by Index, passing it directly through as the output (zero-copy passthrough).
// Faithful port of TiXL PickPointList (pure C# logic, no HLSL shader).
//
// Reference:
//   external/tixl/Operators/Lib/point/combine/PickPointList.cs (lines 17-38)
//
// TiXL logic (PickPointList.cs Update):
//   var connections = Input.GetCollectedTypedInputs();
//   var index = Index.GetValue(context).Mod(connections.Count);
//   Selected.Value = connections[index].GetValue(context);
//
// TiXL ports ported (from .cs InputSlots):
//   Input  (MultiInputSlot<BufferWithViews>)  -> c.inputs[0..inputCount-1]  (multi-input list)
//   Index  (int, default 0)                   -> param "Index" (float, cast to int)
//
// Count policy:
//   Output count = selected input count = inputCounts[selectedIdx].
//   Since PointGraph's driver sums all Points inputs (concat contract), we register with
//   countFromFirstPointsInput=false and use a countTransform that returns the chosen count.
//   The chosen count is communicated via the file-static g_pickSelected* pattern (same approach
//   as PairPointsForLines). The cook fn reads Index and inputCounts, writes
//   g_pickListSelectedCount, so countTransform returns the right buffer size.
//
//   Implementation: cook fn does a GPU blit of the selected input into the output buffer
//   (passthrough copy). This is correct because PointGraph allocates the output buffer for
//   us and we cannot simply redirect the output pointer (it's owned by PointGraph).
//   A blit of selectedCount points copies the exact data TiXL would return.
//
// NAMED FORKS:
//   fork[blit-vs-passthrough]: TiXL's C# directly returns the selected buffer handle
//     (zero-copy). Our output is a freshly allocated buffer owned by PointGraph; we do a
//     GPU blit of the selected input into it. Semantically equivalent.
//
// Self-contained leaf: own capture + registerDrawOp.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/point_graph.h"
#include "runtime/tixl_point.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// g_pickListSelectedCount: updated by cookPickPointList each cook so countTransform
// can return the output buffer size = selected input count (not the sum of all inputs).
static uint32_t g_pickListSelectedCount = 0;

uint32_t pickListCountTransform(uint32_t /*naturalCount*/) {
  return g_pickListSelectedCount;
}

void cookPickPointList(PointCookCtx& c) {
  if (!c.output) return;
  if (c.inputCount == 0) {
    g_pickListSelectedCount = 0;
    return;
  }

  // PickPointList.cs line 19: Index.Mod(connections.Count)
  int idx  = (int)cookParam(c, "Index", 0.0f);
  // Modulo (positive result for any idx including negative)
  int mod  = c.inputCount;
  int sel  = ((idx % mod) + mod) % mod;

  const MTL::Buffer* src  = (sel < c.inputCount) ? c.inputs[sel] : nullptr;
  uint32_t           selN = (c.inputCounts && sel < c.inputCount) ? c.inputCounts[sel] : 0u;

  // Update static for countTransform (next frame driver will resize if needed).
  g_pickListSelectedCount = selN;

  if (!src || selN == 0) return;

  // Blit selected input buffer into our output.
  MTL::CommandBuffer*      cmd  = c.queue->commandBuffer();
  MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
  blit->copyFromBuffer(const_cast<MTL::Buffer*>(src), 0,
                       c.output, 0,
                       (NS::UInteger)selN * sizeof(SwPoint));
  blit->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

// --- golden plumbing (self-contained) ---
std::vector<SwPoint>* g_capPickList = nullptr;
void captureDrawPickList(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capPickList || !pts || c.count == 0) return;
  g_capPickList->assign(c.count, SwPoint{});
  std::memcpy(g_capPickList->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerPickPointListOp() {
  registerPointOp("PickPointList", cookPickPointList,
                  /*stateNew=*/nullptr, /*stateFree=*/nullptr,
                  pickListCountTransform,
                  /*countFromFirstPointsInput=*/false);
}

// Golden: Three inputs — N0=8 (x=0..), N1=4 (x=100..), N2=6 (x=200..).
// With Index=1 -> selected = input[1] -> N1=4 points, x starts at 100.
//
// Assertions:
//   (1) captured count == N1 (4), not the sum 8+4+6=18 or N0=8
//   (2) all output points have Position.x in [100..103] (from input[1])
//
// injectBug: assert we're selecting input[0] (x in [0..7]) instead of input[1] (x in [100..103]).
//   Real cook selects index=1 -> x=100+i -> assertion "x in [0..7]" FAILS -> RED.
//
// Implementation note: we test the GPU cook via PointGraph (builds 3 RadialPoints-like hand
// buffers, connects them to PickPointList, cooks, verifies captured output).
// For simplicity we skip the full graph cook and hand-craft the PointCookCtx directly,
// matching the pattern used by other leaf selftests (SnapToPoints, CombineBuffers).
int runPickPointListSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N0 = 8, N1 = 4, N2 = 6;
  const float base0 = 0.0f, base1 = 100.0f, base2 = 200.0f;

  MTL::Device*       dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue*   q = dev->newCommandQueue();
  NS::Error*         err = nullptr;
  MTL::Library*      lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-pickpointlist] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Build three input buffers.
  MTL::Buffer* buf0 = dev->newBuffer(N0 * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* buf1 = dev->newBuffer(N1 * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* buf2 = dev->newBuffer(N2 * sizeof(SwPoint), MTL::ResourceStorageModeShared);

  auto fill = [](MTL::Buffer* buf, uint32_t N, float base) {
    auto* p = reinterpret_cast<SwPoint*>(buf->contents());
    for (uint32_t i = 0; i < N; ++i) {
      p[i] = SwPoint{};
      p[i].Position.x = base + (float)i;
    }
  };
  fill(buf0, N0, base0);
  fill(buf1, N1, base1);
  fill(buf2, N2, base2);

  // Allocate output buffer sized to max possible (N0, the largest).
  uint32_t outN = N0;  // worst case alloc
  MTL::Buffer* outBuf = dev->newBuffer(outN * sizeof(SwPoint), MTL::ResourceStorageModeShared);

  // Build PointCookCtx manually (mirrors SnapToPoints selftest approach).
  // Index=1 -> select buf1 (N1=4, x=100..).
  const MTL::Buffer* inputs[3] = {buf0, buf1, buf2};
  uint32_t           counts[3] = {N0,   N1,   N2};
  std::map<std::string, float> params;
  params["Index"] = 1.0f;

  g_pickListSelectedCount = N1;  // prime the static for this direct-invoke selftest

  PointCookCtx ctx{};
  ctx.dev        = dev;
  ctx.lib        = lib;
  ctx.queue      = q;
  ctx.inputs     = inputs;
  ctx.inputCounts = counts;
  ctx.inputCount  = 3;
  ctx.output      = outBuf;
  ctx.count       = N1;   // will be overwritten by cook but set for safety
  ctx.params      = &params;

  cookPickPointList(ctx);

  auto* out = reinterpret_cast<SwPoint*>(outBuf->contents());

  // Assert: output has N1 points with x in [base1..base1+N1-1].
  // injectBug: assert x in [base0..base0+N0-1] instead (wrong input) -> FAIL.
  bool rangeOK = true;
  uint32_t checkN = N1;  // we copied N1 points
  float expectBase = injectBug ? base0 : base1;
  uint32_t expectN = injectBug ? N0 : N1;

  for (uint32_t i = 0; i < checkN; ++i) {
    float x = out[i].Position.x;
    // Check x is in [expectBase .. expectBase + expectN - 1]
    if (x < expectBase - 0.1f || x > expectBase + (float)expectN - 0.9f) {
      rangeOK = false;
    }
  }

  // Also verify count via g_pickListSelectedCount (set by cook).
  bool countOK = (g_pickListSelectedCount == N1);

  bool pass = rangeOK && countOK;
  printf("[selftest-pickpointlist] selectedN=%u(want %u) rangeOK=%d countOK=%d -> %s\n",
         g_pickListSelectedCount, N1, rangeOK ? 1 : 0, countOK ? 1 : 0,
         pass ? "PASS" : "FAIL");

  outBuf->release();
  buf0->release(); buf1->release(); buf2->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
