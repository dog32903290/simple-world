// floatlist_golden — --selftest-floatlist. TRANSPORT golden for the 5th cook flow (host List<float>):
// build Const scalar sources wired into FloatsToList's scalar Float MultiInput, run PointGraph::cook
// (the cookFloatListNode branch gathers the wires into a host list + runs the leaf), read the host
// list back via debugCookedFloatList(), and assert it equals the wired values IN ORDER. NO GPU /
// texture — the FloatList currency is a CPU std::vector<float> the whole way (Slice B turns it into a
// texture; not here).
//
// What this proves (the architecture-defining part of the seam):
//   (1) the FloatList host list flows node→node: FloatsToList's output is readable downstream via the
//       driver-owned floatListBuf (the transport mechanism);
//   (2) the scalar Float MultiInput GATHER ORDER is the WIRE-DECLARATION order (the subtle spot) —
//       a [3,1,2]-wired graph reads back [3,1,2], not sorted / not the first wire only.
//
// injectBug routes through floatListInjectBug() so the RED case corrupts the REAL cook output (drops
// the last element), not the expected value — teeth on the actual op path (mirror of mesh_golden.cpp).
#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"            // EvaluationContext
#include "runtime/floatlist_op_registry.h"  // floatListInjectBug
#include "runtime/graph.h"                   // Graph/Node/Connection/pinId
#include "runtime/point_graph.h"             // PointGraph::cook + debugCookedFloatList

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-5f; }

// Build { Const(vals[i]) -> FloatsToList.Input (multiInput), in vals order } and cook FloatsToList as
// the terminal. Returns the cooked host list (empty on failure). The wire order = vals order, so the
// readback list must equal vals exactly IF the gather honors wire-declaration order.
std::vector<float> cookFloatsToList(PointGraph& pg, const std::vector<float>& vals) {
  Graph g;
  // The FloatsToList node (id 1). Its "Input" is port index 0 (scalar Float MultiInput).
  Node ftl; ftl.id = 1; ftl.type = "FloatsToList";
  g.nodes.push_back(ftl);
  const int ftlInputPin = pinId(1, /*portIndex=*/0);  // "Input" is the first port

  // One Const per value, each wired into FloatsToList.Input. Const port 0 = "value" (input),
  // port 1 = "out" (Float output). Connections are pushed in vals order → wire-declaration order.
  int connId = 100;
  for (size_t i = 0; i < vals.size(); ++i) {
    Node c; c.id = (int)(i + 2); c.type = "Const"; c.params["value"] = vals[i];
    g.nodes.push_back(c);
    g.connections.push_back({connId++, pinId(c.id, /*out port*/ 1), ftlInputPin});
  }

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);

  const std::vector<float>* out = pg.debugCookedFloatList(1);
  return out ? *out : std::vector<float>{};
}

bool listEq(const std::vector<float>& a, const std::vector<float>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (!nearf(a[i], b[i])) return false;
  return true;
}

void printList(const char* tag, const std::vector<float>& v) {
  std::printf("%s [", tag);
  for (size_t i = 0; i < v.size(); ++i) std::printf("%s%.2f", i ? "," : "", v[i]);
  std::printf("]");
}

}  // namespace

int runFloatListSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  bool ok = true;

  // LEG 1 — basic transport: FloatsToList([1,2,3]) → host list [1,2,3]. Proves the producer's output
  // is readable node→node via the driver-owned floatListBuf (the transport mechanism). injectBug drops
  // the last element in the REAL cook → [1,2] ≠ [1,2,3] → RED.
  {
    floatListInjectBug() = injectBug;
    std::vector<float> got = cookFloatsToList(pg, {1.0f, 2.0f, 3.0f});
    floatListInjectBug() = false;
    std::vector<float> want = {1.0f, 2.0f, 3.0f};
    bool pass = listEq(got, want);
    ok = ok && pass;
    std::printf("[selftest-floatlist] transport FloatsToList([1,2,3])=");
    printList("", got);
    std::printf(" want=[1,2,3] -> %s\n", pass ? "PASS" : "FAIL");
  }

  // LEG 2 — MultiInput GATHER ORDER (the subtle spot): wires declared [3,1,2] must read back [3,1,2]
  // (wire-declaration order), NOT sorted [1,2,3] and NOT the first wire only [3]. This is the load-
  // bearing assertion for the gather. injectBug (drop last) → [3,1] ≠ [3,1,2] → RED.
  {
    floatListInjectBug() = injectBug;
    std::vector<float> got = cookFloatsToList(pg, {3.0f, 1.0f, 2.0f});
    floatListInjectBug() = false;
    std::vector<float> want = {3.0f, 1.0f, 2.0f};
    bool pass = listEq(got, want);
    ok = ok && pass;
    std::printf("[selftest-floatlist] gather-order wires[3,1,2]=");
    printList("", got);
    std::printf(" want=[3,1,2] (wire-decl order) -> %s\n", pass ? "PASS" : "FAIL");
  }

  q->release();
  dev->release();
  pool->release();

  // Harness convention (run_all_selftests.sh --bite): the -bug variant must exit NON-zero. injectBug
  // drops the last element in the REAL cook -> ok is false -> return 1 (the tooth bites). No inversion.
  std::printf("[selftest-floatlist] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
