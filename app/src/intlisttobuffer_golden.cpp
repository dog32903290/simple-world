// intlisttobuffer_golden — --selftest-intlisttobuffer (+ -bug). PARITY golden for IntListToBuffer (the
// list-currency-fed Buffer PRODUCER, IntListToBuffer.cs). Proves the LIST-CURRENCY BRIDGE: a FloatList
// producer (FloatsToList carrying integer-valued floats = sw's List<int>, Cut32) feeds IntListToBuffer's
// single List<int> wire, and the produced GPU buffer is TIGHT int32 (count = list size, stride 4, NO
// 16-byte pad) with byte-exact contents, on BOTH the flat cook AND the PRODUCTION resident cook
// (cook_ctx.h both-legs byte-identical rule).
//
// WHY THIS SHAPE (chain-through, not a hand-built ctx): the bridge is the SEAM under test — a golden that
// hand-fed BufferCookCtx::inputFloatList would prove the leaf's int32 pack but NOT the gather (cookFlatBuffer's
// new FloatList branch / cookResidentFloatList). So this builds a REAL graph (FloatsToList → IntListToBuffer)
// and cooks it through the driver, so the tooth rides the actual gather seam. Readback = IntListToBuffer's own
// SwBuffer (bufferMeta), via debugCookedSwBuffer (flat id) / residentSwBufferFor (resident path).
//
// ★THE IntsToBuffer DISTINCTION (the reason this needs the NEW seam) — asserted head-on: IntsToBuffer pads
// the count UP to a multiple of 4 (16-byte const-buffer slices) with a 0-tail; IntListToBuffer is TIGHT
// (arraySize = intList.Count exactly, IntListToBuffer.cs:26, no pad). The probe uses 3 ints → this golden
// asserts count==3 (NOT 4) and bytes==[7,8,9] (NOT [7,8,9,0]). A regression that routed IntListToBuffer
// through the padded path would produce count 4 → RED.
//
// PROBE (non-trivial): the list [7, 8, 9] (3 integer-valued floats). Distinct values in wire order prove the
// gather order + the float→int32 cast; the count (3) proves the TIGHT no-pad packing. Expected bytes (int32,
// little-endian) = 07 00 00 00 | 08 00 00 00 | 09 00 00 00, count 3, stride 4 — CITED from IntListToBuffer.cs
// (:26 arraySize=Count, :38 totalSizeInBytes=arraySize*4, :49 stride 4), not an sw snapshot.
//
// injectBug routes bufferInjectBug(): IntListToBuffer's cook drops the LAST int on the REAL cook path (both
// legs) → count 2, bytes [7,8]. The HARD count assert (want 3) FAILs. Teeth on the actual op path, not a
// flipped expected value. (run_all_selftests.sh --bite scans this.)
#include <cstdint>
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
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/sw_buffer.h"           // SwBuffer

namespace sw {
namespace {

// Build { FloatsToList(id 1) <- 3 ints } + { IntListToBuffer(id 40) <- FloatsToList }. FloatsToList ports:
// [0]=Input (Float MultiInput), [1]=out (FloatList). IntListToBuffer ports: [0]=Buffer (out), [1]=IntList
// (FloatList input). Returns via `g`; IntListToBuffer node id = 40.
void buildGraph(Graph& g) {
  Node ftl; ftl.id = 1; ftl.type = "FloatsToList"; g.nodes.push_back(ftl);
  const float ints[3] = {7.0f, 8.0f, 9.0f};  // integer-valued floats = sw's List<int>
  const int inputPin = pinId(1, /*Input*/ 0);
  int nextNode = 2, connId = 100;
  for (int i = 0; i < 3; ++i) {
    Node c; c.id = nextNode++; c.type = "Const"; c.params["value"] = ints[i];
    g.nodes.push_back(c);
    g.connections.push_back({connId++, pinId(c.id, /*out*/ 1), inputPin});  // wire order = ints order
  }

  Node ilb; ilb.id = 40; ilb.type = "IntListToBuffer"; g.nodes.push_back(ilb);
  g.connections.push_back({connId++, pinId(1, /*FloatsToList out*/ 1), pinId(40, /*IntList*/ 1)});
}

// Assert a cooked SwBuffer is TIGHT int32 [7,8,9]: count==3 (NOT 4 — the IntsToBuffer no-pad distinction),
// stride==4, bytes byte-exact. Returns pass.
bool checkBuffer(const SwBuffer* b, const char* label) {
  if (!b || !b->bytes) { std::printf("[selftest-intlisttobuffer] %s NULL buffer -> FAIL\n", label); return false; }
  bool ok = (b->elementCount == 3u) && (b->elementStride == 4u);  // TIGHT: count = list size, NO pad
  const int32_t want[3] = {7, 8, 9};
  if (ok) {
    const int32_t* got = static_cast<const int32_t*>(b->bytes->contents());
    ok = (std::memcmp(got, want, sizeof(want)) == 0);
  }
  std::printf("[selftest-intlisttobuffer] %s count=%u stride=%u", label, b->elementCount, b->elementStride);
  if (b->bytes) {
    const int32_t* got = static_cast<const int32_t*>(b->bytes->contents());
    std::printf(" bytes=[");
    for (uint32_t i = 0; i < b->elementCount; ++i) std::printf("%s%d", i ? "," : "", got[i]);
    std::printf("]");
  }
  std::printf(" want{count=3 stride=4 bytes=[7,8,9]} -> %s\n", ok ? "PASS" : "FAIL");
  return ok;
}

}  // namespace

int runIntListToBufferSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();

  bool ok = true;

  // LEG 1 — FLAT: cook IntListToBuffer (id 40) as terminal; its FloatList gather cooks FloatsToList (1) via
  // cookFloatListNode → the new FloatList branch fills inputFloatList → the leaf packs int32. Read the SwBuffer.
  {
    PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);
    Graph g; buildGraph(g);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    bufferInjectBug() = injectBug;
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/40);
    bufferInjectBug() = false;
    ok = checkBuffer(pg.debugCookedSwBuffer(40), injectBug ? "FLAT(bug)" : "FLAT") && ok;
  }

  // LEG 2 — ★PRODUCTION RESIDENT: the SAME graph through libFromGraph → buildEvalGraph → cookResident with
  // the IntListToBuffer terminal (path "40"); its resident FloatList gather (cookResidentFloatList) fills
  // inputFloatList → the leaf packs int32. Read residentSwBufferFor("40"). Proves the bridge is LIVE on the
  // production leg, byte-identical to flat.
  {
    PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);
    Graph g; buildGraph(g);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    bufferInjectBug() = injectBug;
    pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"40");
    bufferInjectBug() = false;
    ok = checkBuffer(pg.residentSwBufferFor("40"), injectBug ? "RESIDENT(bug)" : "RESIDENT") && ok;
  }

  q->release(); dev->release(); pool->release();
  std::printf("[selftest-intlisttobuffer] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// Self-register (orderBase 641, next to buildgradient's 640 block).
REGISTER_SELFTESTS(/*orderBase=*/641, {"intlisttobuffer", runIntListToBufferSelfTest});

}  // namespace sw
