// selftests_buffer_intstobuffer — the IntsToBuffer byte-parity GATE (--selftest-intstobuffer + -bug).
//
// Validates IntsToBuffer (TiXL numbers/int/process/IntsToBuffer.cs:19-49, const variant) end to end through
// the REAL cook driver (point_graph.cpp cookBufferNode → cookFlatBuffer): build Const scalar sources wired
// into the Params Float-MultiInput rail, cook, read the produced GPU buffer back (debugCookedSwBuffer →
// StorageModeShared contents()), and assert the EXACT int32 bytes against the TiXL 16-byte-pad formula
// computed in-test (NO pixel golden — a raw int buffer has no image; the byte layout IS the contract).
//
// Under test (IntsToBuffer.cs:24-25,33-35): Params=[7,8,9] → intCount=3 → arraySize=ceil(3/4)*4=4 (the
// 16-byte slice pad), so count=4, stride=4, bytes (as int32) = [7,8,9,0] (the 4th element is the zero pad).
//
// -bug (bufferInjectBug): IntsToBuffer drops the LAST int BEFORE padding → [7,8] still pads to 4, so the
// count stays 4 but bytes become [7,8,0,0] → byte[2] (9 vs 0) FAILs on the REAL cook path (a real
// corruption, not a flipped expected value). The tooth run_all --bite scans.
//
// This file self-registers its --selftest row via REGISTER_SELFTESTS (the same data-driven seam
// selftests_buffer.cpp uses) — a SEPARATE file so the two new Seam-1 fan-out teeth do not collide with the
// keystone selftests_buffer.cpp (each REGISTER_SELFTESTS block lands in selftestRegistry() independently).
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"               // Graph/Node/Connection/pinId
#include "runtime/eval_context.h"        // EvaluationContext (full def for the cook ctx)
#include "runtime/point_graph.h"         // PointGraph, debugCookedSwBuffer
#include "runtime/sw_buffer.h"           // SwBuffer
#include "runtime/buffer_op_registry.h"  // bufferInjectBug
#include "runtime/selftest_registry.h"   // REGISTER_SELFTESTS (this leaf self-registers its row)

namespace sw {
namespace {

// Build { IntsToBuffer(id 1) <- Const(vals[i]) -> Params (the Float MultiInput, port index 1) }.
// The int payload rides the Float rail (intstobuffer-int-via-floatrail): Const emits a float, the leaf
// casts float→int32. Wire order = vals order (cookFlatBuffer gathers in wire-declaration order).
void buildIntsToBuffer(Graph& g, const std::vector<float>& vals) {
  Node i2b; i2b.id = 1; i2b.type = "IntsToBuffer";
  g.nodes.push_back(i2b);
  // Params is port index 1 (Buffer out=0, Params=1).
  const int paramsPin = pinId(1, /*portIndex=*/1);
  int connId = 100;
  for (size_t i = 0; i < vals.size(); ++i) {
    Node c; c.id = (int)(i + 2); c.type = "Const"; c.params["value"] = vals[i];
    g.nodes.push_back(c);
    g.connections.push_back({connId++, pinId(c.id, /*out port*/ 1), paramsPin});  // wire order = vals order
  }
}

}  // namespace

int runIntsToBufferSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);  // no shaders needed (pure host→GPU memcpy)

  // [7,8,9] → 3 ints → padded up to a 4-multiple → count 4, bytes [7,8,9,0] (int32).
  const std::vector<float> kInts = {7.0f, 8.0f, 9.0f};
  const int32_t kWantBytes[4] = {7, 8, 9, 0};  // 4th element = the 16-byte-pad zero (.cs:24-25)
  bool ok = true;

  Graph g;
  buildIntsToBuffer(g, kInts);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  bufferInjectBug() = injectBug;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
  bufferInjectBug() = false;

  const SwBuffer* b = pg.debugCookedSwBuffer(1);
  const uint32_t wantCount = 4u;  // ceil(3/4)*4 = 4 (3 ints padded up to a 4-multiple)
  bool haveBuf = b && b->bytes;
  uint32_t count = haveBuf ? b->elementCount : 0u;
  uint32_t stride = haveBuf ? b->elementStride : 0u;
  bool countOK = haveBuf && count == wantCount;     // pad survives -bug, so count stays 4 either way
  bool strideOK = haveBuf && stride == 4u;

  bool bytesOK = haveBuf && countOK;
  if (bytesOK) {
    const int32_t* data = (const int32_t*)b->bytes->contents();
    // bytes (as int32) = [7,8,9,0]; -bug → [7,8,0,0] so data[2] (0 vs 9) FAILs.
    for (int k = 0; k < 4 && bytesOK; ++k)
      if (data[k] != kWantBytes[k]) bytesOK = false;
  }
  bool pass = haveBuf && countOK && strideOK && bytesOK;
  ok = ok && pass;
  std::printf("[selftest-intstobuffer] count=%u(want %u) stride=%u bytes=%d -> %s\n", count, wantCount,
              stride, bytesOK ? 1 : 0, pass ? "PASS" : "FAIL");

  q->release(); dev->release(); pool->release();
  return ok ? 0 : 1;
}

// Self-register the Seam-1 fan-out IntsToBuffer GATE row (orderBase 621, above the keystone's 620). The
// router builds --selftest-intstobuffer (+ -bug) from this; run_all_selftests.sh --bite scans the tooth.
REGISTER_SELFTESTS(/*orderBase=*/621,
    {"intstobuffer", runIntsToBufferSelfTest});

}  // namespace sw
