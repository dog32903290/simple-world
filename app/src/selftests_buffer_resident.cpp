// selftests_buffer_resident — the WO-E RESIDENT-leg Buffer cook GATE (--selftest-buffer-resident + -bug).
//
// THE DE-RISK (SEAM1_FANOUT_BUILD_PLAN §WO-E / §最高風險): today production runs the RESIDENT leg, and a
// Buffer op there fell through to a graceful zero-no-op (cookResident terminal → cookNode → dataType!="Points"
// → clearTarget), so buffer ops produced NOTHING in the live app. This golden is the flat==resident byte-parity
// gate that captured that gap (RED on the UNMODIFIED resident leg) and then proves the fix (GREEN once
// cookResidentBuffer lands). The flat leg is the INDEPENDENT TiXL-verified reference (selftests_buffer.cpp
// pins FloatsToBuffer's bytes against the TiXL fill formula), so resident==flat ⟹ resident==TiXL (SOUND,
// non-circular): we never compare resident to a hand-rolled expectation, only to the already-trusted flat bytes.
//
// ★THE PARAM-PATH DIVERGENCE (the subtlest risk this golden is built to NOT hide): the flat cookFlatBuffer
// reads FloatsToBuffer's matrices from Node::params["Vec4Params.<m>.<k>"] (a FLAT-TEST-ONLY stand-in — no
// Vector4[] producer op exists). Resident has NO Node::params, and resolveResidentFloatInputs only projects
// DECLARED Float input ports, so a synthetic "Vec4Params.0.k" key NEVER surfaces on the resident leg. If this
// golden host-fed matrices via Node::params, the flat leg would gather them and the resident leg would not →
// the two legs would diverge for a reason that has nothing to do with the cook walker. So this golden drives
// the payload ENTIRELY through REAL WIRED CONNECTIONS (Const float nodes → FloatsToBuffer.Params, the Float
// MultiInput): both legs gather the SAME wired scalars (flat via evalFloat over g.connections, resident via
// the ResidentInput::Connection drivers), ZERO Vec4Params matrices. That is the PRODUCTION path (real wired
// float connections), and it makes the two gathers byte-identical BY CONSTRUCTION. The TransformsConstBuffer
// camera divergence (WO-D, Vec4Params-via-camera-bridge) is a separate op not exercised here.
//
// LEG 1 — FloatsToBuffer flat==resident: Const(1.5/2.5/3.5) → FloatsToBuffer.Params (3 wired floats, no
//         matrices) → count=3, stride=4, bytes=[1.5,2.5,3.5]. Cook the SAME graph on BOTH legs; assert the
//         resident SwBuffer (bytes/stride/count) is byte-identical to the flat SwBuffer.
// LEG 2 — GetBufferComponents flat==resident: FloatsToBuffer → GetBufferComponents passthrough; assert the
//         resident forwarded metadata (elementCount=3, elementStride=4 + the forwarded bytes) == the flat one.
//         Proves the resident Buffer→Buffer recursion (a consumer gathering its upstream Buffer input).
//
// -bug (bufferInjectBug, the SAME real-corruption toggle the flat golden uses): FloatsToBuffer drops the LAST
// scalar float on BOTH legs → both produce count 2. The legs would still AGREE (both corrupt identically), so
// the parity assert alone would not bite. The bite is the HARD count assert: each leg must produce the FULL
// count=3; under -bug both are 2 → FAIL. The tooth fires on the real cook path on BOTH legs, not a flipped
// expected value. (run_all_selftests.sh --bite scans this.)
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/buffer_op_registry.h"   // bufferInjectBug
#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/graph.h"                // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph -> SymbolLibrary, paths == ids)
#include "runtime/point_graph.h"          // PointGraph + debugCookedSwBuffer / residentSwBufferFor
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production path)
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/sw_buffer.h"            // SwBuffer

namespace sw {
namespace {

// Build { FloatsToBuffer(id 1) <- Const(vals[i]) -> Params (Float MultiInput, port index 2), in wire order }.
// NO Vec4Params matrices (the param-path divergence is avoided by construction — see file header). Returns via
// the passed graph; the FloatsToBuffer node id is 1. ★Real wired connections only → both legs gather identically.
void buildFloatsToBuffer(Graph& g, const std::vector<float>& vals) {
  Node f2b; f2b.id = 1; f2b.type = "FloatsToBuffer";
  g.nodes.push_back(f2b);
  const int paramsPin = pinId(1, /*portIndex=*/2);  // Buffer out=0, Vec4Params=1, Params=2
  int connId = 100;
  for (size_t i = 0; i < vals.size(); ++i) {
    Node c; c.id = (int)(i + 2); c.type = "Const"; c.params["value"] = vals[i];
    g.nodes.push_back(c);
    g.connections.push_back({connId++, pinId(c.id, /*out port*/ 1), paramsPin});  // wire order = vals order
  }
}

// Read back the SAME graph cooked on BOTH legs, keyed by node id (flat) and resident path (= id as string,
// libFromGraph paths == ids). Asserts the resident SwBuffer is byte-identical to the flat one (bytes + the
// stride/count metadata) AND that BOTH legs hit the expected non-degenerate count (the -bug tooth).
bool legParity(MTL::Device* dev, MTL::CommandQueue* q, const Graph& g, int terminalId,
               uint32_t wantCount, bool injectBug, const char* label) {
  const std::string termPath = std::to_string(terminalId);

  // FLAT leg (the TiXL-verified reference).
  PointGraph pgFlat(dev, /*lib=*/nullptr, q, 64, 64);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  bufferInjectBug() = injectBug;
  pgFlat.cook(g, ctx, nullptr, terminalId);
  bufferInjectBug() = false;
  const SwBuffer* flat = pgFlat.debugCookedSwBuffer(terminalId);

  // RESIDENT leg (production path): flat Graph -> SymbolLibrary (paths == ids) -> resident graph -> cookResident.
  PointGraph pgRes(dev, /*lib=*/nullptr, q, 64, 64);
  SymbolLibrary slib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
  bufferInjectBug() = injectBug;
  pgRes.cookResident(rg, ctx, nullptr, termPath);
  bufferInjectBug() = false;
  const SwBuffer* res = pgRes.residentSwBufferFor(termPath);

  // The flat leg must have produced a real buffer (the trusted reference). Its count must be the expected
  // non-degenerate value (the HARD per-leg tooth: -bug drops it → FAIL even though the legs still AGREE).
  bool flatOK = flat && flat->bytes && flat->elementCount == wantCount && flat->elementStride == 4u;

  // Resident must be byte-identical to flat: same count, same stride, same bytes. On the UNMODIFIED resident
  // leg `res` is null (zero-no-op) → resOK false → RED. After cookResidentBuffer lands → byte-identical → GREEN.
  bool resOK = res && res->bytes && flat && flat->bytes &&
               res->elementCount == flat->elementCount && res->elementStride == flat->elementStride;
  if (resOK) {
    const uint32_t bytes = res->elementCount * res->elementStride;
    resOK = (std::memcmp(res->bytes->contents(), flat->bytes->contents(), bytes) == 0);
  }

  bool pass = flatOK && resOK;
  std::printf("[selftest-buffer-resident] %s flat{count=%u stride=%u} resident{count=%u stride=%u} "
              "flatOK=%d resOK=%d (want count=%u) -> %s\n",
              label, flat ? flat->elementCount : 0, flat ? flat->elementStride : 0,
              res ? res->elementCount : 0, res ? res->elementStride : 0, flatOK ? 1 : 0, resOK ? 1 : 0,
              wantCount, pass ? "PASS" : "FAIL");
  return pass;
}

}  // namespace

int runBufferResidentSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();

  const std::vector<float> kParams = {1.5f, 2.5f, 3.5f};
  const uint32_t kWantCount = (uint32_t)kParams.size();  // 3 wired floats, no matrices
  bool ok = true;

  // LEG 1 — FloatsToBuffer flat==resident (the de-risk core).
  {
    Graph g;
    buildFloatsToBuffer(g, kParams);
    ok = legParity(dev, q, g, /*terminal=*/1, kWantCount, injectBug, "LEG1 FloatsToBuffer") && ok;
  }

  // LEG 2 — GetBufferComponents flat==resident (the Buffer->Buffer resident recursion).
  {
    Graph g;
    buildFloatsToBuffer(g, kParams);                      // FloatsToBuffer id 1
    Node gbc; gbc.id = 50; gbc.type = "GetBufferComponents";
    g.nodes.push_back(gbc);
    // FloatsToBuffer.Buffer (out port 0) -> GetBufferComponents.BufferWithViews (input port index 1).
    g.connections.push_back({900, pinId(1, /*out*/ 0), pinId(50, /*in*/ 1)});
    ok = legParity(dev, q, g, /*terminal=*/50, kWantCount, injectBug, "LEG2 GetBufferComponents") && ok;
  }

  q->release(); dev->release(); pool->release();
  std::printf("[selftest-buffer-resident] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// Self-register the WO-E GATE row (orderBase 622, just above the Seam-1 flat gate at 620).
REGISTER_SELFTESTS(/*orderBase=*/622,
    {"buffer-resident", runBufferResidentSelfTest});

}  // namespace sw
