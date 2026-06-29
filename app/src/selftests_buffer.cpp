// selftests_buffer — the Seam-1 byte-parity GATE (--selftest-floatstobuffer + -bug).
//
// Validates the "Buffer" port currency (TiXL BufferWithViews → one MTL::Buffer) end to end through the
// REAL cook driver (point_graph.cpp cookBufferNode): build Const scalar sources + a host-fed identity
// matrix into FloatsToBuffer, cook, read the produced GPU buffer back (debugCookedSwBuffer →
// StorageModeShared contents()), and assert the EXACT bytes against the TiXL fill formula computed
// in-test (NO pixel golden — a raw byte buffer has no image; the byte layout IS the contract).
//
// Fill order under test (FloatsToBuffer.cs:38-54): ALL matrix floats first (16/matrix, .X.Y.Z.W), then
// the scalar floats. With one identity matrix + Params=[1.5,2.5,3.5]: count = 3 + 16 = 19, stride = 4,
// bytes[0..15] = the identity in row-major .X.Y.Z.W, bytes[16..18] = 1.5,2.5,3.5.
//
// -bug (bufferInjectBug): FloatsToBuffer drops the LAST scalar float → count = 18, byte[18] missing →
// the count assert + the [16..18] payload assert FAIL on the REAL cook path (a real corruption, not a
// flipped expected value). The tooth run_all --bite scans.
//
// LEG 2 (the GetBufferComponents seam): FloatsToBuffer → GetBufferComponents passthrough; assert the
// forwarded metadata Length(=elementCount)==19, Stride(=elementStride)==4 (GetBufferComponents.cs:60-61).
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"         // Graph/Node/Connection/pinId
#include "runtime/eval_context.h"  // EvaluationContext (full def for the cook ctx)
#include "runtime/point_graph.h"   // PointGraph, debugCookedSwBuffer
#include "runtime/sw_buffer.h"     // SwBuffer
#include "runtime/buffer_op_registry.h"  // bufferInjectBug
#include "runtime/selftest_registry.h"   // REGISTER_SELFTESTS (this leaf self-registers its row)

namespace sw {
namespace {

// Row-major identity 4x4 in .X.Y.Z.W stored order (FloatsToBuffer.cs:43-47 reads each vec4's .X.Y.Z.W).
const float kIdentity16[16] = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1,
};

// Build { FloatsToBuffer(id 1) <- one identity Vec4Param (host-fed via Node::params "Vec4Params.0.k")
//         + Const(vals[i]) -> Params (the scalar Float MultiInput, port index 2) }.
// Returns the FloatsToBuffer node id (1); caller cooks + reads debugCookedSwBuffer.
void buildFloatsToBuffer(Graph& g, const std::vector<float>& vals) {
  Node f2b; f2b.id = 1; f2b.type = "FloatsToBuffer";
  // Host-fed identity matrix (fork floatstobuffer-vec4-from-nodeparams): matrix 0, components 0..15.
  for (int k = 0; k < 16; ++k) f2b.params["Vec4Params.0." + std::to_string(k)] = kIdentity16[k];
  g.nodes.push_back(f2b);
  // Params is port index 2 (Buffer out=0, Vec4Params=1, Params=2).
  const int paramsPin = pinId(1, /*portIndex=*/2);
  int connId = 100;
  for (size_t i = 0; i < vals.size(); ++i) {
    Node c; c.id = (int)(i + 2); c.type = "Const"; c.params["value"] = vals[i];
    g.nodes.push_back(c);
    g.connections.push_back({connId++, pinId(c.id, /*out port*/ 1), paramsPin});  // wire order = vals order
  }
}

}  // namespace

int runFloatsToBufferSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);  // no shaders needed (pure host→GPU memcpy)

  const std::vector<float> kParams = {1.5f, 2.5f, 3.5f};
  bool ok = true;

  // LEG 1 — byte-parity of FloatsToBuffer through the real cook driver.
  {
    Graph g;
    buildFloatsToBuffer(g, kParams);
    EvaluationContext ctx{};
    ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    bufferInjectBug() = injectBug;
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
    bufferInjectBug() = false;

    const SwBuffer* b = pg.debugCookedSwBuffer(1);
    const uint32_t wantCount = (uint32_t)(16 + kParams.size());  // 16 (matrix) + 3 (scalars) = 19
    bool haveBuf = b && b->bytes;
    uint32_t count = haveBuf ? b->elementCount : 0u;
    uint32_t stride = haveBuf ? b->elementStride : 0u;
    bool countOK = haveBuf && count == wantCount;        // -bug → 18 ≠ 19 → FAIL
    bool strideOK = haveBuf && stride == 4u;

    bool bytesOK = haveBuf && countOK;
    if (bytesOK) {
      const float* data = (const float*)b->bytes->contents();
      // bytes[0..15] = identity (.X.Y.Z.W); bytes[16..18] = 1.5,2.5,3.5.
      for (int k = 0; k < 16 && bytesOK; ++k)
        if (data[k] != kIdentity16[k]) bytesOK = false;
      for (size_t i = 0; i < kParams.size() && bytesOK; ++i)
        if (data[16 + i] != kParams[i]) bytesOK = false;
    }
    bool pass = haveBuf && countOK && strideOK && bytesOK;
    ok = ok && pass;
    std::printf("[selftest-floatstobuffer] count=%u(want %u) stride=%u bytes=%d -> %s\n", count, wantCount,
                stride, bytesOK ? 1 : 0, pass ? "PASS" : "FAIL");
  }

  // LEG 2 — GetBufferComponents passthrough metadata (Length==19, Stride==4).
  {
    Graph g;
    buildFloatsToBuffer(g, kParams);                       // FloatsToBuffer id 1
    Node gbc; gbc.id = 50; gbc.type = "GetBufferComponents";
    g.nodes.push_back(gbc);
    // FloatsToBuffer.Buffer (out port 0) -> GetBufferComponents.BufferWithViews (input port index 1).
    g.connections.push_back({900, pinId(1, /*out*/ 0), pinId(50, /*in*/ 1)});

    EvaluationContext ctx{};
    ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    bufferInjectBug() = injectBug;
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/50);
    bufferInjectBug() = false;

    const SwBuffer* gb = pg.debugCookedSwBuffer(50);
    const uint32_t wantLen = injectBug ? 18u : 19u;  // -bug drops one float upstream → 18
    bool haveBuf = gb && gb->bytes;
    uint32_t length = haveBuf ? gb->elementCount : 0u;
    uint32_t stride = haveBuf ? gb->elementStride : 0u;
    bool lenOK = haveBuf && length == 19u;   // the GREEN contract: forwarded Length==19
    bool strideOK = haveBuf && stride == 4u;
    bool pass = lenOK && strideOK;
    ok = ok && pass;
    std::printf("[selftest-floatstobuffer] GetBufferComponents Length=%u(want %u green/18 bug) Stride=%u -> %s\n",
                length, wantLen, stride, pass ? "PASS" : "FAIL");
  }

  q->release(); dev->release(); pool->release();
  return ok ? 0 : 1;
}

// Self-register the Seam-1 GATE row (fresh orderBase 620, above the current max 600). The router builds
// --selftest-floatstobuffer (+ -bug) from this; run_all_selftests.sh --bite scans the tooth.
REGISTER_SELFTESTS(/*orderBase=*/620,
    {"floatstobuffer", runFloatsToBufferSelfTest});

}  // namespace sw
