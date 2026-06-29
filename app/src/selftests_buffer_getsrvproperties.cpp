// selftests_buffer_getsrvproperties — the GetSRVProperties passthrough GATE (--selftest-getsrvproperties).
//
// Validates GetSRVProperties (TiXL render/_dx11/api/GetSRVProperties.cs:18-37) end to end through the REAL
// cook driver: FloatsToBuffer (keystone) produces a 19-element buffer (one identity matrix = 16 floats +
// 3 scalars), GetSRVProperties forwards it, and we assert the forwarded ElementCount (.cs:28) == 19 off the
// passed-through SwBuffer view (debugCookedSwBuffer). The Buffer passthrough is the thin sibling of
// GetBufferComponents (getsrvproperties-srv-is-buffer + scalar-output-deferred).
//
// Under test (GetSRVProperties.cs:14-15,28): srv.Description.Buffer.ElementCount → SwBuffer.elementCount.
// FloatsToBuffer with identity matrix + Params=[1.5,2.5,3.5] → count 19 → forwarded count 19, stride 4.
//
// -bug (bufferInjectBug): FloatsToBuffer drops the LAST scalar float upstream → count 18 → the forwarded
// count is 18 ≠ 19 → FAIL on the REAL cook path (a real corruption propagated through the passthrough, not
// a flipped expected value). The tooth run_all --bite scans.
//
// SEPARATE file (self-registers its own --selftest row via REGISTER_SELFTESTS) so the Seam-1 fan-out teeth
// do not collide with the keystone selftests_buffer.cpp.
#include <cstdio>
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

// Row-major identity 4x4 in .X.Y.Z.W stored order (FloatsToBuffer.cs:43-47), host-fed via Node::params.
const float kIdentity16[16] = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1,
};

// Build FloatsToBuffer(id 1) <- one identity Vec4Param (host-fed) + Const(vals[i]) -> Params (port idx 2).
// Returns nothing; FloatsToBuffer node id is 1 (the GetSRVProperties input source).
void buildFloatsToBuffer(Graph& g, const std::vector<float>& vals) {
  Node f2b; f2b.id = 1; f2b.type = "FloatsToBuffer";
  for (int k = 0; k < 16; ++k) f2b.params["Vec4Params.0." + std::to_string(k)] = kIdentity16[k];
  g.nodes.push_back(f2b);
  const int paramsPin = pinId(1, /*portIndex=*/2);  // Params is port index 2 (Buffer=0, Vec4Params=1, Params=2)
  int connId = 100;
  for (size_t i = 0; i < vals.size(); ++i) {
    Node c; c.id = (int)(i + 2); c.type = "Const"; c.params["value"] = vals[i];
    g.nodes.push_back(c);
    g.connections.push_back({connId++, pinId(c.id, /*out port*/ 1), paramsPin});
  }
}

}  // namespace

int runGetSRVPropertiesSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  const std::vector<float> kParams = {1.5f, 2.5f, 3.5f};  // 16 (matrix) + 3 = 19 elements
  bool ok = true;

  Graph g;
  buildFloatsToBuffer(g, kParams);                       // FloatsToBuffer id 1
  Node gsp; gsp.id = 50; gsp.type = "GetSRVProperties";
  g.nodes.push_back(gsp);
  // FloatsToBuffer.Buffer (out port 0) -> GetSRVProperties.SRV (input port index 1).
  g.connections.push_back({900, pinId(1, /*out*/ 0), pinId(50, /*in*/ 1)});

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  bufferInjectBug() = injectBug;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/50);
  bufferInjectBug() = false;

  const SwBuffer* gb = pg.debugCookedSwBuffer(50);
  bool haveBuf = gb && gb->bytes;
  uint32_t count = haveBuf ? gb->elementCount : 0u;
  uint32_t stride = haveBuf ? gb->elementStride : 0u;
  bool countOK = haveBuf && count == 19u;   // GREEN contract: forwarded ElementCount==19; -bug → 18 → FAIL
  bool strideOK = haveBuf && stride == 4u;
  bool pass = countOK && strideOK;
  ok = ok && pass;
  std::printf("[selftest-getsrvproperties] ElementCount=%u(want 19; 18 under bug) Stride=%u -> %s\n",
              count, stride, pass ? "PASS" : "FAIL");

  q->release(); dev->release(); pool->release();
  return ok ? 0 : 1;
}

// Self-register the Seam-1 fan-out GetSRVProperties GATE row (orderBase 622, above intstobuffer's 621).
REGISTER_SELFTESTS(/*orderBase=*/622,
    {"getsrvproperties", runGetSRVPropertiesSelfTest});

}  // namespace sw
