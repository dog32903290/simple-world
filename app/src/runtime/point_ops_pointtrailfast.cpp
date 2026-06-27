// PointTrailFast — STATEFUL generate op (point/generate family): a FIXED-size trail ring riding the
// built cross-frame state mechanism (ensureState/countTransform, the ParticleSystem precedent).
// Faithful port of TiXL's single-kernel PointTrailFast.
//
// Reference:
//   external/tixl/Operators/Lib/point/generate/PointTrailFast.cs        (5 inputs + slots)
//   external/tixl/Operators/Lib/point/generate/PointTrailFast.t3        (buffer-count graph, .t3 defaults)
//   external/tixl/Operators/Lib/Assets/shaders/points/sim/PointTrailFast.hlsl   (the per-point ring math)
//
// CROSS-FRAME STATE MECHANISM (rides the built ensureState additively — NO driver threading change):
//   • The OUTPUT buffer IS the ring (PointGraph-owned, ensureOut reuses it across frames WITHOUT
//     clearing — point_graph_internal.h:186-196). So old trail samples persist frame-to-frame in the
//     same buffer; only the write head (CycleIndex) advances. This is exactly TiXL's persistent ring.
//   • Per-node STATE (registered state factory) holds: the cached PSO + the monotonic CycleIndex +
//     a `seeded` flag (mirror of cookParticleSim's SimState seeded/frame). The state factory is the
//     same registerPointOp(stateNew, stateFree) hook ParticleSystem uses — additive, existing ops
//     unaffected.
//
// COUNT-PRODUCT DRIVER SEAM (static-stash, the proven RepeatAtPoints/PairPointsForLines pattern):
//   countTransform is `uint32_t(*)(uint32_t)` — it can NOT read the TrailLength param directly (the
//   task's "reads the pool param" premise does not hold; the hook only sees the natural count). The
//   PROVEN cure (point_ops_repeatatpoints.cpp:78-82) is a FILE-STATIC the cook fn writes and
//   countTransform reads. Cook fns run SINGLE-THREADED & SEQUENTIALLY → safe. The ring size =
//   count * ringPerPoint where ringPerPoint = userTrailLength + 1 (the +1 is the .t3 AddInts(+1)
//   separator slot baked into BOTH the buffer allocation and Params2.TrailLength). Two-frame seeding
//   (frame 1 the static defaults to count*1 → cook sets it → frame 2 reallocs to the real ring) is
//   production-faithful, identical to RepeatAtPoints.
//
// ★.t3 DEFAULTS (verified from PointTrailFast.t3): TrailLength=100, AddSeperatorThreshold=0.0,
//   Reset=false, IsEnabled=true. ringPerPoint = TrailLength + 1.
//
// SwPoint <-> TiXL Point: Position/Color/Scale 1:1; Scale carries the NaN line-separator flag.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"        // calcDispatchCount
#include "runtime/graph.h"           // Graph/Node/pinId/findSpec
#include "runtime/point_graph.h"     // PointCookCtx, registerPointOp
#include "runtime/pointtrailfast_params.h"
#include "runtime/tixl_point.h"      // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// Resolve the ring slots per source point = userTrailLength + 1 (the .t3 AddInts(+1) separator slot).
int trailRingPerPoint(const PointCookCtx& c) {
  int tl = (int)(cookParam(c, "TrailLength", 100.0f) + 0.5f);
  if (tl < 1) tl = 1;
  return tl + 1;  // .t3: AddInts(TrailLength, +1) feeds both buffer size and Params2.TrailLength
}

// static-stash for the countTransform hook (single-threaded sequential cook → safe; proven pattern).
static uint32_t g_pointTrailFastResultCount = 0;
uint32_t pointTrailFastCountTransform(uint32_t /*naturalCount*/) { return g_pointTrailFastResultCount; }

// Per-node persistent state: cached PSO + cross-frame ring write head + seeded flag.
// ★CycleIndex == FrameCount (.t3 trace): the Params2 IntsToBuffer wires FrameCount → CycleIndex (slot 0),
//   AddInts(TrailLength,+1) → TrailLength, srcN → PointCount, _omit(1) → WriteOrderTo. So CycleIndex
//   increments by ONE per enabled frame (NOT by TrailLength — the .cs comment is misleading). The ring
//   stride per point is TrailLength(+1); the head walks one slot/frame so a point's last (TrailLength+1)
//   frames fill its slots before wrapping. cycleIndex is kept here across frames (the cross-frame state).
struct TrailFastState {
  MTL::ComputePipelineState* pso = nullptr;
  int   cycleIndex = 0;   // == FrameCount: ring write head, +1 each enabled frame (cross-frame)
  bool  seeded = false;   // false until the first cook clears the ring + lays the first samples
};

MTL::ComputePipelineState* makePSO(MTL::Device* dev, MTL::Library* lib, const char* name) {
  if (!lib) return nullptr;
  MTL::Function* fn = lib->newFunction(NS::String::string(name, NS::UTF8StringEncoding));
  if (!fn) return nullptr;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &err);
  fn->release();
  return pso;
}

}  // namespace

void* pointTrailFastStateNew(MTL::Device* dev, MTL::Library* lib, uint32_t /*count*/) {
  TrailFastState* s = new TrailFastState();
  s->pso = makePSO(dev, lib, "pointtrailfast");
  return s;
}
void pointTrailFastStateFree(void* p) {
  TrailFastState* s = static_cast<TrailFastState*>(p);
  if (!s) return;
  if (s->pso) s->pso->release();
  delete s;
}

// PointTrailFast cook: srcN points (input[0]) -> persistent ring (output). Each enabled frame writes
// the source bag one ring-stride forward (cross-frame trail). The ring is the PointGraph-owned output
// buffer (uncleared across frames). On the first cook (or Reset), clear the ring to NaN so empty slots
// read as line breaks (TiXL allocs a fresh zero buffer; we clear to NaN-Scale = the renderer's break).
void cookPointTrailFast(PointCookCtx& c) {
  const MTL::Buffer* src = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  const uint32_t srcN = (c.inputCounts && c.inputCount > 0) ? c.inputCounts[0] : 0u;
  const int ringPerPoint = trailRingPerPoint(c);
  // Seed the static so the NEXT cook's countTransform sizes the ring to srcN * ringPerPoint.
  g_pointTrailFastResultCount = srcN * (uint32_t)ringPerPoint;

  if (!c.output || srcN == 0 || !src || !c.state) return;
  TrailFastState* s = static_cast<TrailFastState*>(c.state);
  if (!s->pso) return;

  const uint32_t bufferLength = srcN * (uint32_t)ringPerPoint;
  if (c.count < bufferLength) return;  // output not yet sized to the ring (frame-1 seeding); wait

  const bool isEnabled = cookParam(c, "IsEnabled", 1.0f) > 0.5f;
  const bool reset = cookParam(c, "Reset", 0.0f) > 0.5f;
  const float addSepThreshold = cookParam(c, "AddSeperatorThreshold", 0.0f);

  // First cook OR Reset: clear the whole ring to NaN-Scale (every slot a line break) and rewind the head.
  if (!s->seeded || reset) {
    SwPoint* dst = static_cast<SwPoint*>(c.output->contents());
    for (uint32_t i = 0; i < bufferLength; ++i) {
      dst[i] = SwPoint{};
      dst[i].Scale = {NAN, NAN, NAN};  // empty slot = separator
    }
    s->cycleIndex = 0;
    s->seeded = true;
  } else if (isEnabled) {
    // Advance CycleIndex by ONE (== FrameCount, the .t3 IntsToBuffer slot-0 wire). The kernel's modulo
    // keeps it in [0,bufferLength); kept across frames = the persistent trail head.
    s->cycleIndex = (s->cycleIndex + 1) % (int)bufferLength;
  }
  // When disabled (and already seeded), the head holds — the kernel re-stamps the current slot (no-op-ish).

  PointTrailFastParams P{};
  P.AddSeparatorThreshold = addSepThreshold;
  P.CycleIndex = s->cycleIndex;
  P.TrailLength = ringPerPoint;     // SHADER ring stride == userTrailLength + 1 (.t3 AddInts(+1))
  P.PointCount = (int)srcN;
  P.WriteOrderTo = 0;

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(s->pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(src), 0, POINTTRAILFAST_SourcePoints);
  enc->setBuffer(c.output, 0, POINTTRAILFAST_TrailPoints);
  enc->setBytes(&P, sizeof(P), POINTTRAILFAST_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(srcN, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

void registerPointTrailFastOp() {
  registerPointOp("PointTrailFast", cookPointTrailFast,
                  pointTrailFastStateNew, pointTrailFastStateFree,
                  pointTrailFastCountTransform,
                  /*countFromFirstPointsInput=*/false);
}

// ===================== Golden: 2-frame cross-frame ring accumulation =========================
// ★Drives the GPU kernel DIRECTLY across TWO frames with a PERSISTENT ring buffer (the cross-frame
// state mechanism the op rides). 1 source point, TrailLength=2 → ringPerPoint=3, bufferLength=3,
// AddSeparatorThreshold=0 (no break-on-distance). CycleIndex == FrameCount (+1 per frame):
//   f0 (CycleIndex=0): targetIndex=(0 + 0*3)%3 = 0 → ring[0]=posA; ring[1].Scale=NaN
//   f1 (CycleIndex=1): targetIndex=(1 + 0*3)%3 = 1 → ring[1]=posB; ring[2].Scale=NaN
// After the frame boundary the ring holds BOTH samples: ring[0]==posA (the f0 sample SURVIVED into f1)
// AND ring[1]==posB (the new f1 sample). That posA survival IS the cross-frame trail — a per-frame-fresh
// buffer would have lost it. We assert ring[0].Position==posA && ring[1].Position==posB.
// injectBug: re-zero the ring before frame 1 (== disabling state persistence) → posA is wiped → ring[0]
// is no longer posA → the cross-frame tooth bites (RED).
int runPointTrailFastSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-pointtrailfast] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  MTL::ComputePipelineState* pso = makePSO(dev, lib, "pointtrailfast");
  if (!pso) {
    printf("[selftest-pointtrailfast] FAIL: no PSO 'pointtrailfast'\n");
    lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }

  const int srcN = 1, ringPerPoint = 3;       // TrailLength=2 → +1 = 3
  const uint32_t bufferLength = (uint32_t)(srcN * ringPerPoint);
  const float posA = 1.0f, posB = 7.0f;       // distinct x positions for the two frames

  // Persistent ring (the cross-frame state) — allocated ONCE, reused across both frames.
  MTL::Buffer* ring = dev->newBuffer(bufferLength * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  SwPoint* r = static_cast<SwPoint*>(ring->contents());
  for (uint32_t i = 0; i < bufferLength; ++i) { r[i] = SwPoint{}; r[i].Scale = {NAN, NAN, NAN}; }

  MTL::Buffer* srcBuf = dev->newBuffer(srcN * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  SwPoint* sp = static_cast<SwPoint*>(srcBuf->contents());

  auto runFrame = [&](int cycleIndex, float px) {
    sp[0] = SwPoint{}; sp[0].Position = {px, 0.0f, 0.0f}; sp[0].Scale = {1, 1, 1};
    PointTrailFastParams P{};
    P.AddSeparatorThreshold = 0.0f;
    P.CycleIndex = cycleIndex; P.TrailLength = ringPerPoint; P.PointCount = srcN; P.WriteOrderTo = 0;
    MTL::CommandBuffer* cmd = q->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(srcBuf, 0, POINTTRAILFAST_SourcePoints);
    enc->setBuffer(ring, 0, POINTTRAILFAST_TrailPoints);
    enc->setBytes(&P, sizeof(P), POINTTRAILFAST_Params);
    enc->dispatchThreadgroups(MTL::Size::Make(1, 1, 1), MTL::Size::Make(64, 1, 1));
    enc->endEncoding(); cmd->commit(); cmd->waitUntilCompleted();
  };

  runFrame(/*CycleIndex=*/0, posA);  // f0 → ring[0]=posA
  if (injectBug) {                    // bug: wipe the ring (= no cross-frame persistence)
    for (uint32_t i = 0; i < bufferLength; ++i) { r[i] = SwPoint{}; r[i].Scale = {NAN, NAN, NAN}; }
  }
  runFrame(/*CycleIndex=*/1, posB);  // f1 → ring[1]=posB; ring[0] must STILL be posA (persisted)

  bool slot0OK = std::fabs(r[0].Position.x - posA) < 1e-4f;  // f0 sample survived the frame boundary
  bool slot1OK = std::fabs(r[1].Position.x - posB) < 1e-4f;  // f1 sample landed at the advanced head
  // Same load-bearing assertion in BOTH modes (cross-frame persistence). injectBug wipes the ring → posA
  // is lost → slot0OK is false → the test FAILS (returns non-zero) = the tooth BITES (--bite contract).
  bool pass = slot0OK && slot1OK;
  printf("[selftest-pointtrailfast] ring[0].x=%.2f(want %.0f) ring[1].x=%.2f(want %.0f) "
         "slot0=%d slot1=%d -> %s%s\n",
         r[0].Position.x, posA, r[1].Position.x, posB, slot0OK ? 1 : 0, slot1OK ? 1 : 0,
         pass ? "PASS" : "FAIL", injectBug ? " (bug: ring wiped → posA must be lost)" : "");

  pso->release(); ring->release(); srcBuf->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
