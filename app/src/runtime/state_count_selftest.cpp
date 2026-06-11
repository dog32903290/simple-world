// Golden for the ensureState grow rule (refuter-2b finding 1, promoted repro).
//
// The hazard: a stateful op's stateNew sizes internal buffers (the sim's particle buffer)
// to the count it FIRST saw; if the runtime hands the SAME state back after the node's
// Count grew (柏為 drags Count up / rewires a Const), the op dispatches newCount threads
// over an oldCount buffer = GPU out-of-bounds write. The fix (point_graph_internal.h
// ensureState): state is re-created when count grows past the capacity it was born with —
// mirror of ensureOut's grow rule; growth resets sim continuity, correctness over
// continuity.
//
// Green: cook at Count=10, edit to Count=40, cook again — on BOTH the flat path and the
// resident path (shared Impl, separate key spaces) the op must see a state whose capacity
// covers the new count (no overrun, recreation observed). injectBug: stateNew caps its
// allocation at 10 regardless of count (an op violating the "size to count" contract) —
// the overrun detector must FIRE -> FAIL (teeth).
#include "runtime/point_graph.h"

#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"               // Graph / Node / pinId
#include "runtime/graph_bridge.h"        // libFromGraph (resident leg)
#include "runtime/resident_eval_graph.h" // buildEvalGraph
#include "runtime/tixl_point.h"          // SwPoint + EvaluationContext

namespace sw {
namespace {

struct CapState {
  uint32_t capacity = 0;
  std::vector<int> buf;
};
bool g_capLie = false;  // injectBug: under-allocate regardless of count
void* capStateNew(MTL::Device*, MTL::Library*, uint32_t count) {
  CapState* s = new CapState();
  s->capacity = g_capLie ? (count < 10 ? count : 10) : count;
  s->buf.assign(s->capacity ? s->capacity : 1, 0);
  return s;
}
void capStateFree(void* p) { delete static_cast<CapState*>(p); }

bool g_overrun = false;       // op asked to write past its persistent state capacity
uint32_t g_lastSeenCap = 0;   // capacity of the state handed to the op on the last cook

// Stateful generator: writes `count` entries into its persistent state (mirror of the sim
// dispatching `count` threads over s->particles).
void capGen(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  CapState* s = static_cast<CapState*>(c.state);
  if (s) {
    g_lastSeenCap = s->capacity;
    if (c.count > s->capacity) g_overrun = true;  // would be a GPU OOB write in production
    for (uint32_t i = 0; i < c.count && i < s->capacity; ++i) s->buf[i] = (int)i;
  }
  SwPoint* dst = (SwPoint*)c.output->contents();
  for (uint32_t i = 0; i < c.count; ++i) { dst[i] = SwPoint{}; dst[i].Position.x = (float)c.count; }
}

RenderCommand capDraw(CmdCookCtx& c) {
  RenderCommand rc;
  if (c.points && c.count > 0) rc.items.push_back(RenderDrawItem{c.points, c.count, 3.5f});
  return rc;
}
void capTex(TexCookCtx&) {}  // no-op executor (lib == nullptr)

Graph makeGraph(float count) {
  Graph g;
  Node gn; gn.id = 1; gn.type = "RadialPoints"; gn.params["Count"] = count; g.nodes.push_back(gn);
  Node dn; dn.id = 3; dn.type = "DrawPoints"; g.nodes.push_back(dn);
  Node rn; rn.id = 4; rn.type = "RenderTarget"; rn.params["Resolution"] = 0.0f; g.nodes.push_back(rn);
  g.connections.push_back({101, pinId(1, 0), pinId(3, 0)});
  g.connections.push_back({102, pinId(3, 1), pinId(4, 0)});
  return g;
}

}  // namespace

int runStateCountSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  g_capLie = injectBug;

  registerPointOp("RadialPoints", capGen, capStateNew, capStateFree);
  registerCmdOp("DrawPoints", capDraw);
  registerTexOp("RenderTarget", capTex);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  Graph g1 = makeGraph(10.0f);
  Graph g2 = makeGraph(40.0f);  // the EDIT between cooks: Count grows

  // Flat leg.
  g_overrun = false; g_lastSeenCap = 0;
  PointGraph fpg(dev, /*lib=*/nullptr, q, 64, 64);
  fpg.cook(g1, ctx, nullptr, 4);
  fpg.cook(g2, ctx, nullptr, 4);
  bool flatOk = !g_overrun && g_lastSeenCap >= 40;

  // Resident leg (same Impl rule, path keys): mirror both graphs through the bridge.
  g_overrun = false; g_lastSeenCap = 0;
  SymbolLibrary lib1 = libFromGraph(g1);
  SymbolLibrary lib2 = libFromGraph(g2);
  ResidentEvalGraph rg1 = buildEvalGraph(lib1, "Root");
  ResidentEvalGraph rg2 = buildEvalGraph(lib2, "Root");
  PointGraph rpg(dev, /*lib=*/nullptr, q, 64, 64);
  rpg.cookResident(rg1, ctx, nullptr, "4");
  rpg.cookResident(rg2, ctx, nullptr, "4");
  bool resOk = !g_overrun && g_lastSeenCap >= 40;

  bool pass = flatOk && resOk;
  printf("[selftest-statecount] flat(noOverrun,cap>=40)=%d resident=%d -> %s%s\n",
         flatOk ? 1 : 0, resOk ? 1 : 0, pass ? "PASS" : "FAIL",
         injectBug ? " (bug: stateNew under-allocates -> overrun must fire)" : "");

  g_capLie = false;
  q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
