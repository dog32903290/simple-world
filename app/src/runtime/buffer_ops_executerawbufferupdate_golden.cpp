// buffer_ops_executerawbufferupdate_golden — --selftest-executerawbufferupdate.
//
// Proves ExecuteRawBufferUpdate FORWARDS the `Buffer` input (ExecuteRawBufferUpdate.cs:18) while
// UpdateCommands (.cs:17) runs for side-effect only — the load-bearing distinction from a plain
// passthrough. The tooth: wire DISTINCT buffers to the two ports and assert the output equals the
// `Buffer` port (NOT UpdateCommands). Both cook legs (flat + resident) — resident is production.
//
// Topology:
//   FloatsToBuffer_A(Params = 10,20,30) → ExecuteRawBufferUpdate.Buffer          [the FORWARDED input]
//   FloatsToBuffer_B(Params = 91,92)    → ExecuteRawBufferUpdate.UpdateCommands  [side-effect only]
//   ExecuteRawBufferUpdate = the TERMINAL.
// Closed-form: FloatsToBuffer packs its scalar Params as 16-byte-padded float slices (FloatsToBuffer.cs
// fill); we assert the FORWARDED buffer's element COUNT (3, from the A producer's 3 params — NOT 2 from
// B) and its first float (10). A regression forwarding UpdateCommands would yield count 2 / first 91.
//   ★injectBug: executeRawBufferForwardWrongPortBug() forwards the WRONG port (UpdateCommands) → count 2 /
//     first 91 → the tooth BITES (RED) on both legs. did-not-trip → the mismatch surfaces.
#include <cstdint>
#include <cstdio>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/buffer_ops_executerawbufferupdate.h"  // executeRawBufferForwardWrongPortBug
#include "runtime/graph.h"                // Graph / Node / pinId
#include "runtime/graph_bridge.h"         // libFromGraph
#include "runtime/point_graph.h"          // PointGraph / debugCookedSwBuffer / residentSwBufferFor
#include "runtime/resident_eval_graph.h"  // buildEvalGraph
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/sw_buffer.h"            // SwBuffer
#include "runtime/tixl_point.h"           // EvaluationContext

namespace sw {
namespace {

// A FloatsToBuffer producer (id `fid`) fed `n` Const params (starting at Const id `cid0`). Returns cid0+n.
int addFloatsToBuffer(Graph& g, int fid, int cid0, const float* vals, int n) {
  Node ftb; ftb.id = fid; ftb.type = "FloatsToBuffer"; g.nodes.push_back(ftb);
  int cid = cid0;
  for (int i = 0; i < n; ++i) {
    Node c; c.id = cid; c.type = "Const"; c.params["value"] = vals[i]; g.nodes.push_back(c);
    // Const.Result (port 1) → FloatsToBuffer.Params (port 2, the Float MultiInput).
    g.connections.push_back({1000 + cid, pinId(cid, 1), pinId(fid, 2)});
    ++cid;
  }
  return cid;
}

void buildGraph(Graph& g) {
  const float aVals[3] = {10.0f, 20.0f, 30.0f};  // the FORWARDED buffer (Buffer port) — 3 params
  const float bVals[2] = {91.0f, 92.0f};         // the side-effect buffer (UpdateCommands) — 2 params
  int cid = 10;
  cid = addFloatsToBuffer(g, /*fid=*/1, cid, aVals, 3);  // FloatsToBuffer_A → Buffer
  cid = addFloatsToBuffer(g, /*fid=*/2, cid, bVals, 2);  // FloatsToBuffer_B → UpdateCommands
  Node ex; ex.id = 40; ex.type = "ExecuteRawBufferUpdate"; g.nodes.push_back(ex);
  // FloatsToBuffer.Buffer output = port 0. ExecuteRawBufferUpdate ports: Output(0), UpdateCommands(1), Buffer(2).
  g.connections.push_back({201, pinId(1, 0), pinId(40, 2)});  // A → ExecuteRawBufferUpdate.Buffer
  g.connections.push_back({202, pinId(2, 0), pinId(40, 1)});  // B → ExecuteRawBufferUpdate.UpdateCommands
}

// Assert the forwarded SwBuffer is A's ([10,20,30] → count 3, first float 10), NOT B's (count 2, first 91).
bool checkForwarded(const SwBuffer* b, const char* label) {
  if (!b || !b->bytes) {
    std::printf("[selftest-executerawbufferupdate] %s NULL buffer -> FAIL\n", label);
    return false;
  }
  const float* got = static_cast<const float*>(b->bytes->contents());
  const float first = got ? got[0] : -1.0f;
  const bool ok = (b->elementCount == 3u) && got && (first == 10.0f);
  std::printf("[selftest-executerawbufferupdate] %s count=%u first=%.1f want{count=3 first=10 (the Buffer "
              "port, not UpdateCommands' count=2 first=91)} -> %s\n", label, b->elementCount, (double)first,
              ok ? "PASS" : "FAIL");
  return ok;
}

}  // namespace

int runExecuteRawBufferUpdateSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  bool ok = true;

  // LEG 1 — FLAT.
  {
    PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);
    Graph g; buildGraph(g);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    executeRawBufferForwardWrongPortBug() = injectBug;
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/40);
    executeRawBufferForwardWrongPortBug() = false;
    ok = checkForwarded(pg.debugCookedSwBuffer(40), injectBug ? "FLAT(bug)" : "FLAT") && ok;
  }

  // LEG 2 — ★PRODUCTION RESIDENT: the SAME graph through libFromGraph → buildEvalGraph → cookResident.
  {
    PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);
    Graph g; buildGraph(g);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    executeRawBufferForwardWrongPortBug() = injectBug;
    pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"40");
    executeRawBufferForwardWrongPortBug() = false;
    ok = checkForwarded(pg.residentSwBufferFor("40"), injectBug ? "RESIDENT(bug)" : "RESIDENT") && ok;
  }

  q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (ok) {
      std::printf("[selftest-executerawbufferupdate] FAIL: injectBug still passed (forwarded the Buffer port "
                  "despite the wrong-port bug — the seam is not actually selecting the Buffer input)\n");
      return 1;
    }
    std::printf("[selftest-executerawbufferupdate] injectBug correctly RED (forwarded UpdateCommands, count 2 "
                "first 91, instead of the Buffer port count 3 first 10)\n");
    return 1;
  }
  std::printf("[selftest-executerawbufferupdate] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/642, {"executerawbufferupdate", runExecuteRawBufferUpdateSelfTest});

}  // namespace sw
