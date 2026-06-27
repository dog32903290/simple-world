// PointTrail — STATEFUL generate op (point/generate family): the 3-pass trail variant riding the built
// cross-frame state mechanism. Faithful port of TiXL's PointTrail (Clear/Collect/Copy passes).
//
// Reference:
//   external/tixl/Operators/Lib/point/generate/PointTrail.{cs,t3}
//   external/tixl/Operators/Lib/Assets/shaders/points/sim/PointTrail-{Clear,Collect,Copy}.hlsl
//
// CROSS-FRAME STATE MECHANISM (rides the built ensureState additively — NO driver threading change):
//   PointTrail differs from PointTrailFast: the persistent ring is NOT the output buffer — it is a
//   SEPARATE CyclePoints buffer kept in per-node STATE (the registered state factory, sized to the ring
//   length the driver passes). Each frame:
//     • Clear   the per-frame OUTPUT (output is PointGraph-owned, reused, but Copy overwrites every slot
//               so a clear+copy is enough; Clear NaNs first so an unwritten slot is a break).
//     • Collect the source bag INTO the persistent CyclePoints ring at the cross-frame head (CycleIndex).
//     • Copy    the ring NEWEST-FIRST into the output, fade + NaN separators.
//   CycleIndex (== FrameCount, +1/frame — same .t3 IntsToBuffer slot-0 wire as PointTrailFast) lives in
//   state across frames. The persistent CyclePoints ring is what makes a point's trail accumulate.
//
// COUNT-PRODUCT DRIVER SEAM (static-stash, proven RepeatAtPoints/PairPointsForLines pattern; identical to
//   PointTrailFast): ring length = srcN*(userTrailLength+1); the cook writes a file-static the
//   countTransform reads (single-threaded sequential cook → safe). Two-frame seeding is production-
//   faithful.
//
// ★.t3 DEFAULTS (verified from PointTrail.t3): TrailLength=100, WriteTrailOrderTo=F1(1), IsEnabled=true,
//   Reset=false, WriteLineSeparators=true. ringPerPoint = TrailLength + 1.
//
// SwPoint <-> TiXL Point: Position/Color/Scale 1:1; FX1/FX2/Scale carry the fade + NaN line separators.
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
#include "runtime/pointtrail_params.h"
#include "runtime/tixl_point.h"      // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

int pointTrailRingPerPoint(const PointCookCtx& c) {
  int tl = (int)(cookParam(c, "TrailLength", 100.0f) + 0.5f);
  if (tl < 1) tl = 1;
  return tl + 1;  // .t3: AddInts(TrailLength, +1) → ring stride per point
}

static uint32_t g_pointTrailResultCount = 0;
uint32_t pointTrailCountTransform(uint32_t /*naturalCount*/) { return g_pointTrailResultCount; }

MTL::ComputePipelineState* makePSO(MTL::Device* dev, MTL::Library* lib, const char* name) {
  if (!lib) return nullptr;
  MTL::Function* fn = lib->newFunction(NS::String::string(name, NS::UTF8StringEncoding));
  if (!fn) return nullptr;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &err);
  fn->release();
  return pso;
}

// Per-node state: the PERSISTENT CyclePoints ring (cross-frame accumulation) + 3 cached PSOs + head.
struct TrailState {
  MTL::Buffer* cyclePoints = nullptr;   // persistent ring, sized to the ring length
  uint32_t     ringCap = 0;
  MTL::ComputePipelineState* psoClear = nullptr;
  MTL::ComputePipelineState* psoCollect = nullptr;
  MTL::ComputePipelineState* psoCopy = nullptr;
  int   cycleIndex = 0;  // == FrameCount (+1 per enabled frame), kept across frames
  bool  seeded = false;
};

}  // namespace

void* pointTrailStateNew(MTL::Device* dev, MTL::Library* lib, uint32_t count) {
  TrailState* s = new TrailState();
  s->ringCap = count > 0 ? count : 1;
  s->cyclePoints = dev->newBuffer((NS::UInteger)s->ringCap * sizeof(SwPoint),
                                  MTL::ResourceStorageModeShared);
  s->psoClear = makePSO(dev, lib, "pointtrail_clear");
  s->psoCollect = makePSO(dev, lib, "pointtrail_collect");
  s->psoCopy = makePSO(dev, lib, "pointtrail_copy");
  return s;
}
void pointTrailStateFree(void* p) {
  TrailState* s = static_cast<TrailState*>(p);
  if (!s) return;
  if (s->cyclePoints) s->cyclePoints->release();
  if (s->psoClear) s->psoClear->release();
  if (s->psoCollect) s->psoCollect->release();
  if (s->psoCopy) s->psoCopy->release();
  delete s;
}

void cookPointTrail(PointCookCtx& c) {
  const MTL::Buffer* src = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  const uint32_t srcN = (c.inputCounts && c.inputCount > 0) ? c.inputCounts[0] : 0u;
  const int ringPerPoint = pointTrailRingPerPoint(c);
  const uint32_t bufferLength = srcN * (uint32_t)ringPerPoint;
  g_pointTrailResultCount = bufferLength;  // seed the NEXT cook's countTransform

  if (!c.output || srcN == 0 || !src || !c.state) return;
  TrailState* s = static_cast<TrailState*>(c.state);
  if (!s->psoClear || !s->psoCollect || !s->psoCopy || !s->cyclePoints) return;
  if (c.count < bufferLength || s->ringCap < bufferLength) return;  // frame-1 seeding; wait for resize

  const bool isEnabled = cookParam(c, "IsEnabled", 1.0f) > 0.5f;
  const bool reset = cookParam(c, "Reset", 0.0f) > 0.5f;
  const int writeOrderTo = (int)(cookParam(c, "WriteTrailOrderTo", 1.0f) + 0.5f);
  const bool writeSep = cookParam(c, "WriteLineSeparators", 1.0f) > 0.5f;

  // First cook OR Reset: clear the persistent ring to NaN-Scale (empty = break) + rewind the head.
  if (!s->seeded || reset) {
    SwPoint* ring = static_cast<SwPoint*>(s->cyclePoints->contents());
    for (uint32_t i = 0; i < bufferLength; ++i) { ring[i] = SwPoint{}; ring[i].Scale = {NAN, NAN, NAN}; }
    s->cycleIndex = 0;
    s->seeded = true;
  } else if (isEnabled) {
    s->cycleIndex = (s->cycleIndex + 1) % (int)bufferLength;  // == FrameCount advance
  }

  PointTrailParams P{};
  P.CycleIndex = s->cycleIndex;
  P.TrailLength = ringPerPoint;
  P.PointCount = (int)srcN;
  P.BufferLength = (int)bufferLength;
  P.WriteOrderTo = writeOrderTo;
  P.WriteLineSeperators = writeSep ? 1 : 0;

  const uint32_t tg = 64;
  // PASS 1 Clear: NaN the per-frame output (BufferLength threads).
  // PASS 2 Collect: write source into the persistent ring (PointCount threads).
  // PASS 3 Copy: emit newest-first into the output (BufferLength threads).
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  {
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(s->psoClear);
    enc->setBuffer(c.output, 0, POINTTRAIL_TrailPoints);
    enc->setBytes(&P, sizeof(P), POINTTRAIL_Params);
    enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(bufferLength, tg), 1, 1),
                              MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
  }
  {
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(s->psoCollect);
    enc->setBuffer(const_cast<MTL::Buffer*>(src), 0, POINTTRAIL_SourcePoints);
    enc->setBuffer(s->cyclePoints, 0, POINTTRAIL_CyclePoints);
    enc->setBytes(&P, sizeof(P), POINTTRAIL_Params);
    enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(srcN, tg), 1, 1),
                              MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
  }
  {
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(s->psoCopy);
    enc->setBuffer(s->cyclePoints, 0, POINTTRAIL_CyclePoints);
    enc->setBuffer(c.output, 0, POINTTRAIL_TrailPoints);
    enc->setBytes(&P, sizeof(P), POINTTRAIL_Params);
    enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(bufferLength, tg), 1, 1),
                              MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
  }
  cmd->commit();
  cmd->waitUntilCompleted();
}

void registerPointTrailOp() {
  registerPointOp("PointTrail", cookPointTrail,
                  pointTrailStateNew, pointTrailStateFree,
                  pointTrailCountTransform,
                  /*countFromFirstPointsInput=*/false);
}

// ===================== Golden: 2-frame 3-pass newest-first trail =============================
// ★Drives the 3 kernels DIRECTLY across TWO frames with a PERSISTENT CyclePoints ring (state). 1 source
// point, TrailLength=2 → ringPerPoint=3, BufferLength=3, WriteLineSeparators=true, WriteOrderTo=F1.
//   COLLECT writes into the ring at targetIndex=(CycleIndex + 0*3)%3 = CycleIndex:
//     f0 (CycleIndex=0): ring[0]=posA
//     f1 (CycleIndex=1): ring[1]=posB   (ring[0]=posA STILL there → cross-frame persistence)
//   COPY (frame 1, CycleIndex=1) emits newest-first into the output: for output i,
//     sourceIndex=(i+1)%3, targetIndex=2-i.
//     i=0: src=ring[1]=posB → out[2]=posB
//     i=1: src=ring[2]=(empty,NaN) → out[1]=empty
//     i=2: src=ring[0]=posA → out[0]=posA
//   So out[0]=posA, out[2]=posB. The newest sample (posB) is FARTHEST from index 0 (newest-first means
//   output index counts DOWN from BufferLength-1). The load-bearing facts: posA from f0 SURVIVED into
//   f1's output (cross-frame), AND both samples appear. We assert out[2].Position==posB && out[0].Position==posA.
// injectBug: wipe the ring before frame 1 (no persistence) → posA gone → out[0]!=posA → bites (RED).
int runPointTrailSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-pointtrail] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  MTL::ComputePipelineState* psoCollect = makePSO(dev, lib, "pointtrail_collect");
  MTL::ComputePipelineState* psoCopy = makePSO(dev, lib, "pointtrail_copy");
  MTL::ComputePipelineState* psoClear = makePSO(dev, lib, "pointtrail_clear");
  if (!psoCollect || !psoCopy || !psoClear) {
    printf("[selftest-pointtrail] FAIL: missing PSO\n");
    if (psoCollect) psoCollect->release(); if (psoCopy) psoCopy->release(); if (psoClear) psoClear->release();
    lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }

  const int srcN = 1, ringPerPoint = 3;
  const uint32_t bufferLength = (uint32_t)(srcN * ringPerPoint);
  const float posA = 2.0f, posB = 9.0f;

  MTL::Buffer* ring = dev->newBuffer(bufferLength * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* out = dev->newBuffer(bufferLength * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* srcBuf = dev->newBuffer(srcN * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  SwPoint* r = static_cast<SwPoint*>(ring->contents());
  for (uint32_t i = 0; i < bufferLength; ++i) { r[i] = SwPoint{}; r[i].Scale = {NAN, NAN, NAN}; }
  SwPoint* sp = static_cast<SwPoint*>(srcBuf->contents());

  PointTrailParams P{};
  P.TrailLength = ringPerPoint; P.PointCount = srcN; P.BufferLength = (int)bufferLength;
  P.WriteOrderTo = 1; P.WriteLineSeperators = 1;

  auto collect = [&](int cycleIndex, float px) {
    sp[0] = SwPoint{}; sp[0].Position = {px, 0, 0}; sp[0].Scale = {1, 1, 1};
    P.CycleIndex = cycleIndex;
    MTL::CommandBuffer* cmd = q->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(psoCollect);
    enc->setBuffer(srcBuf, 0, POINTTRAIL_SourcePoints);
    enc->setBuffer(ring, 0, POINTTRAIL_CyclePoints);
    enc->setBytes(&P, sizeof(P), POINTTRAIL_Params);
    enc->dispatchThreadgroups(MTL::Size::Make(1, 1, 1), MTL::Size::Make(64, 1, 1));
    enc->endEncoding(); cmd->commit(); cmd->waitUntilCompleted();
  };
  auto copy = [&](int cycleIndex) {
    P.CycleIndex = cycleIndex;
    MTL::CommandBuffer* cmd = q->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(psoCopy);
    enc->setBuffer(ring, 0, POINTTRAIL_CyclePoints);
    enc->setBuffer(out, 0, POINTTRAIL_TrailPoints);
    enc->setBytes(&P, sizeof(P), POINTTRAIL_Params);
    enc->dispatchThreadgroups(MTL::Size::Make(1, 1, 1), MTL::Size::Make(64, 1, 1));
    enc->endEncoding(); cmd->commit(); cmd->waitUntilCompleted();
  };

  collect(/*CycleIndex=*/0, posA);   // f0 → ring[0]=posA
  if (injectBug) {                    // bug: wipe the ring (no cross-frame persistence)
    for (uint32_t i = 0; i < bufferLength; ++i) { r[i] = SwPoint{}; r[i].Scale = {NAN, NAN, NAN}; }
  }
  collect(/*CycleIndex=*/1, posB);   // f1 → ring[1]=posB; ring[0] must STILL be posA
  copy(/*CycleIndex=*/1);             // emit newest-first into out

  SwPoint* o = static_cast<SwPoint*>(out->contents());
  bool out0OK = std::fabs(o[0].Position.x - posA) < 1e-4f;  // posA from f0 survived into f1's output
  bool out2OK = std::fabs(o[2].Position.x - posB) < 1e-4f;  // posB the new sample
  // Same load-bearing assertion in BOTH modes. injectBug wipes the ring → posA lost → out0OK false → the
  // test FAILS (returns non-zero) = the tooth BITES (--bite contract).
  bool pass = out0OK && out2OK;
  printf("[selftest-pointtrail] out[0].x=%.2f(want %.0f) out[2].x=%.2f(want %.0f) out0=%d out2=%d -> %s%s\n",
         o[0].Position.x, posA, o[2].Position.x, posB, out0OK ? 1 : 0, out2OK ? 1 : 0,
         pass ? "PASS" : "FAIL", injectBug ? " (bug: ring wiped → posA must be lost)" : "");

  psoCollect->release(); psoCopy->release(); psoClear->release();
  ring->release(); out->release(); srcBuf->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
