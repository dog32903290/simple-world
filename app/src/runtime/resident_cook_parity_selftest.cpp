// Headless RED->GREEN proof of slice-2b parity: cookResident must drive REAL-shaped op
// machinery exactly like cook(). One pipeline exercises every 2b mechanism at once, with the
// expected value HAND-COMPUTED so a silent flat+resident twin failure can't fake a pass:
//
//   Const(5) ──Radius──> Gen(Count=6) ──emit──> Sim ──> Draw(cmd) ──> RT(tex, Custom 32x32)
//                                    Turb(Amount=7) ──forces──┘
//
//   gen:  x = Radius                      (param threading: a WIRE into a Float param)
//   sim:  x = in.x + 10*counter + Amount  (counter lives in c.state — persists across cooks;
//                                          Amount read via cookInputParam on the forces wire)
//   draw: emits the 1-item RenderCommand + captures the bag
//   RT:   stub tex executor records chain size + output texture size (Resolution pin) — and
//         target() must BE the 32x32 resolution-sized texture (displayTex parity)
//
// frame 1: x = 5+10+7 = 22; frame 2: x = 5+20+7 = 32 (state persisted). Then a PREVIEW cook
// (terminal = the sim node itself) synthesizes a 1-item chain through the "RenderTarget"
// executor: x = 5+30+7 = 42, executor output = the WINDOW-sized texture. Flat and resident
// run the identical sequence; every probe asserts flat == resident == hand-computed.
//
// injectBug: the sim stub ignores c.state (counter never persists -> frame 2 reads 22 not 32)
// -> the across-cooks assertion FAILS — teeth proving the test really checks persistence.
#include "runtime/point_graph.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"       // SymbolLibrary / Symbol / SymbolChild / SymbolConnection
#include "runtime/graph.h"                // Graph / Node / pinId
#include "runtime/resident_eval_graph.h"  // buildEvalGraph
#include "runtime/tixl_point.h"           // SwPoint + EvaluationContext

namespace sw {
namespace {

std::vector<SwPoint>* g_parCap = nullptr;  // capture target (draw cmd op fills it)
int g_texCalls = 0;                        // tex executor invocations
size_t g_chainItems = 0;                   // items in the chain the executor received
uint32_t g_texOutW = 0, g_texOutH = 0;     // the executor's output texture size
bool g_bugIgnoreState = false;             // injectBug: sim stub drops its persistent counter

// Generator: x = Radius (the resolved-param probe; a wire into Radius must reach here).
void parGen(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  const float radius = cookParam(c, "Radius", 2.0f);
  SwPoint* dst = (SwPoint*)c.output->contents();
  for (uint32_t i = 0; i < c.count; ++i) { dst[i] = SwPoint{}; dst[i].Position.x = radius; }
}

// Stateful sim: counter in c.state persists across cooks; Amount comes off the forces WIRE.
void* parSimStateNew(MTL::Device*, MTL::Library*, uint32_t) { return new int(0); }
void parSimStateFree(void* p) { delete static_cast<int*>(p); }
void parSim(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  int local = 0;
  int* counter = (g_bugIgnoreState || !c.state) ? &local : static_cast<int*>(c.state);
  *counter += 1;
  const float amount = cookInputParam(c, 1, "Amount", 0.0f);  // forces = buffer input 1
  const MTL::Buffer* in0 = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  const SwPoint* src = in0 ? (const SwPoint*)const_cast<MTL::Buffer*>(in0)->contents() : nullptr;
  SwPoint* dst = (SwPoint*)c.output->contents();
  for (uint32_t i = 0; i < c.count; ++i) {
    dst[i] = src ? src[i] : SwPoint{};
    dst[i].Position.x = (src ? src[i].Position.x : 0.0f) + 10.0f * (float)*counter + amount;
  }
}

// Draw cmd op: emit the 1-item chain AND capture the bag for value assertions.
RenderCommand parDraw(CmdCookCtx& c) {
  RenderCommand rc;
  if (c.points && c.count > 0) {
    rc.items.push_back(RenderDrawItem{c.points, c.count, 3.5f});
    if (g_parCap) {
      g_parCap->assign(c.count, SwPoint{});
      std::memcpy(g_parCap->data(), const_cast<MTL::Buffer*>(c.points)->contents(),
                  (size_t)c.count * sizeof(SwPoint));
    }
  }
  return rc;
}

// Tex executor stub: record what the driver handed it (no GPU work -> runs with lib=nullptr).
void parTex(TexCookCtx& c) {
  ++g_texCalls;
  g_chainItems = c.command ? c.command->items.size() : 0;
  g_texOutW = c.output ? (uint32_t)c.output->width() : 0;
  g_texOutH = c.output ? (uint32_t)c.output->height() : 0;
  if (g_parCap && c.command && !c.command->items.empty()) {
    const RenderDrawItem& it = c.command->items[0];
    if (it.points && it.count > 0) {
      g_parCap->assign(it.count, SwPoint{});
      std::memcpy(g_parCap->data(), const_cast<MTL::Buffer*>(it.points)->contents(),
                  (size_t)it.count * sizeof(SwPoint));
    }
  }
}

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

struct Probe {
  float x = -1.0f;        // captured bag Position.x (uniform across points)
  size_t n = 0;           // captured bag size
  size_t chain = 0;       // chain items the executor saw
  uint32_t w = 0, h = 0;  // executor output texture size
  uint32_t targetW = 0;   // pg.target() width after the cook (displayTex parity)
};

bool probeEq(const Probe& a, const Probe& b, float expectX, size_t expectN) {
  return a.x == b.x && a.x == expectX && a.n == b.n && a.n == expectN && a.chain == b.chain &&
         a.w == b.w && a.h == b.h && a.targetW == b.targetW;
}

}  // namespace

int runResidentCookParitySelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  g_bugIgnoreState = injectBug;

  registerPointOp("RadialPoints", parGen);
  registerPointOp("ParticleSystem", parSim, parSimStateNew, parSimStateFree);
  registerCmdOp("DrawPoints", parDraw);
  registerTexOp("RenderTarget", parTex);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();

  const uint32_t N = 6;
  const float kRadius = 5.0f, kAmount = 7.0f;

  // --- FLAT: Const(5)->gen.Radius; gen(Count=6)->sim.emit; turb(Amount=7)->sim.forces;
  //     sim->draw; draw.out->rt.command; rt = Custom 32x32 ---
  Graph fg;
  Node cn; cn.id = 5; cn.type = "Const"; cn.params["value"] = kRadius; fg.nodes.push_back(cn);
  Node gn; gn.id = 1; gn.type = "RadialPoints"; gn.params["Count"] = (float)N; fg.nodes.push_back(gn);
  Node sn; sn.id = 2; sn.type = "ParticleSystem"; fg.nodes.push_back(sn);
  Node tn; tn.id = 6; tn.type = "TurbulenceForce"; tn.params["Amount"] = kAmount; fg.nodes.push_back(tn);
  Node dn; dn.id = 3; dn.type = "DrawPoints"; fg.nodes.push_back(dn);
  Node rn; rn.id = 4; rn.type = "RenderTarget";
  rn.params["Resolution"] = 4.0f; rn.params["CustomW"] = 32.0f; rn.params["CustomH"] = 32.0f;
  fg.nodes.push_back(rn);
  fg.connections.push_back({101, pinId(5, 1), pinId(1, 2)});  // Const.out -> gen.Radius
  fg.connections.push_back({102, pinId(1, 0), pinId(2, 0)});  // gen.points -> sim.emit
  fg.connections.push_back({103, pinId(6, 0), pinId(2, 1)});  // turb.force -> sim.forces
  fg.connections.push_back({104, pinId(2, 2), pinId(3, 0)});  // sim.result -> draw.points
  fg.connections.push_back({105, pinId(3, 1), pinId(4, 0)});  // draw.out -> rt.command

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  auto runFlat = [&](PointGraph& pg, int target) -> Probe {
    std::vector<SwPoint> bag; g_parCap = &bag;
    g_texCalls = 0; g_chainItems = 0; g_texOutW = 0; g_texOutH = 0;
    pg.cook(fg, ctx, nullptr, target);
    Probe p;
    p.x = bag.empty() ? -1.0f : bag[0].Position.x;
    p.n = bag.size(); p.chain = g_chainItems; p.w = g_texOutW; p.h = g_texOutH;
    p.targetW = pg.target() ? (uint32_t)pg.target()->width() : 0;
    g_parCap = nullptr;
    return p;
  };

  PointGraph fpg(dev, /*lib=*/nullptr, q, 64, 64);
  Probe f1 = runFlat(fpg, 4);  // frame 1: tex terminal
  Probe f2 = runFlat(fpg, 4);  // frame 2: state persisted -> 32
  Probe f3 = runFlat(fpg, 2);  // preview: terminal = sim node -> synthesized 1-item chain

  // --- RESIDENT: the equivalent SymbolLibrary ---
  SymbolLibrary lib;
  lib.symbols["Const"] = atomicOp("Const", {{"value", "value", "Float", 0.0f}},
                                  {{"out", "out", "Float", 0.0f}});
  lib.symbols["RadialPoints"] =
      atomicOp("RadialPoints", {{"Count", "Count", "Float", 2048.0f}, {"Radius", "Radius", "Float", 2.0f}},
               {{"points", "points", "Points", 0.0f}});
  lib.symbols["ParticleSystem"] = atomicOp(
      "ParticleSystem", {{"emit", "emit", "Points", 0.0f}, {"forces", "forces", "ParticleForce", 0.0f}},
      {{"result", "result", "Points", 0.0f}});
  lib.symbols["TurbulenceForce"] = atomicOp("TurbulenceForce", {{"Amount", "Amount", "Float", 15.0f}},
                                            {{"force", "force", "ParticleForce", 0.0f}});
  lib.symbols["DrawPoints"] = atomicOp("DrawPoints", {{"points", "points", "Points", 0.0f}},
                                       {{"out", "out", "Command", 0.0f}});
  lib.symbols["RenderTarget"] = atomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c5; c5.id = 5; c5.symbolId = "Const"; c5.overrides["value"] = kRadius;
  SymbolChild c1; c1.id = 1; c1.symbolId = "RadialPoints"; c1.overrides["Count"] = (float)N;
  SymbolChild c2; c2.id = 2; c2.symbolId = "ParticleSystem";
  SymbolChild c6; c6.id = 6; c6.symbolId = "TurbulenceForce"; c6.overrides["Amount"] = kAmount;
  SymbolChild c3; c3.id = 3; c3.symbolId = "DrawPoints";
  SymbolChild c4; c4.id = 4; c4.symbolId = "RenderTarget";
  c4.overrides["Resolution"] = 4.0f; c4.overrides["CustomW"] = 32.0f; c4.overrides["CustomH"] = 32.0f;
  root.children = {c5, c1, c2, c6, c3, c4};
  root.connections = {
      {5, "out", 1, "Radius"},   {1, "points", 2, "emit"},  {6, "force", 2, "forces"},
      {2, "result", 3, "points"}, {3, "out", 4, "command"}, {4, "out", kSymbolBoundary, "out"},
  };
  lib.symbols["Root"] = root; lib.rootId = "Root";
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");

  auto runRes = [&](PointGraph& pg, const std::string& target) -> Probe {
    std::vector<SwPoint> bag; g_parCap = &bag;
    g_texCalls = 0; g_chainItems = 0; g_texOutW = 0; g_texOutH = 0;
    pg.cookResident(rg, ctx, nullptr, target);
    Probe p;
    p.x = bag.empty() ? -1.0f : bag[0].Position.x;
    p.n = bag.size(); p.chain = g_chainItems; p.w = g_texOutW; p.h = g_texOutH;
    p.targetW = pg.target() ? (uint32_t)pg.target()->width() : 0;
    g_parCap = nullptr;
    return p;
  };

  PointGraph rpg(dev, /*lib=*/nullptr, q, 64, 64);
  Probe r1 = runRes(rpg, "4");
  Probe r2 = runRes(rpg, "4");
  Probe r3 = runRes(rpg, "2");

  // frame 1: 5 + 10*1 + 7 = 22; frame 2: 32 (state persisted); preview cook 3: 42.
  bool tex1 = probeEq(f1, r1, 22.0f, N) && f1.chain == 1 && f1.w == 32 && f1.targetW == 32;
  bool tex2 = probeEq(f2, r2, 32.0f, N) && f2.targetW == 32;
  bool prev = probeEq(f3, r3, 42.0f, N) && f3.chain == 1 && f3.w == 64 && f3.targetW == 64;
  bool pass = tex1 && tex2 && prev;
  printf("[selftest-residentparity] f1/r1 x=%.0f/%.0f(want 22) f2/r2 x=%.0f/%.0f(want 32) "
         "prev x=%.0f/%.0f(want 42) chain=%zu/%zu rt=%ux%u(target %u, want 32) "
         "prevOut=%ux%u(target %u, want 64) -> %s\n",
         f1.x, r1.x, f2.x, r2.x, f3.x, r3.x, f1.chain, r1.chain, f1.w, f1.h, f1.targetW,
         f3.w, f3.h, f3.targetW, pass ? "PASS" : "FAIL");

  g_bugIgnoreState = false;
  q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
