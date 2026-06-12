// runtime/bypass_selftest_shared — definitions for the shared bypass-selftest stub kit (see .h).
#include "runtime/bypass_selftest_shared.h"

#include <cstring>
#include <map>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"         // NodeSpec / setDynamicSpecs
#include "runtime/graph_bridge.h"  // specFromSymbol (canonical Symbol -> NodeSpec)
#include "runtime/point_graph.h"   // registerPointOp / registerCmdOp / registerTexOp + cook ctxs

namespace sw::bypass_st {

std::vector<SwPoint>* g_bag = nullptr;
const MTL::Buffer* g_drawSeenBuf = nullptr;
RenderCommand g_chain;
MTL::Texture* g_rtTex = nullptr;
MTL::Texture* g_filterTex = nullptr;
int g_rtRuns = 0, g_filterRuns = 0, g_jamRuns = 0, g_modRuns = 0;

namespace {

// Generator stub: per-point DISTINCT positions so "passes through point-for-point" is a real
// claim (an all-equal fill would pass under reordering/recount bugs).
void bpGen(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  SwPoint* dst = (SwPoint*)c.output->contents();
  for (uint32_t i = 0; i < c.count; ++i) {
    dst[i] = SwPoint{};
    dst[i].Position = {genX((int)i), genY((int)i), genZ((int)i)};
  }
}
// Modifier stub: x *= 2 — the mutation a working bypass must SKIP.
void bpMul(PointCookCtx& c) {
  ++g_modRuns;
  if (!c.output || c.count == 0) return;
  SwPoint* dst = (SwPoint*)c.output->contents();
  const MTL::Buffer* in0 = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  const SwPoint* src = in0 ? (const SwPoint*)const_cast<MTL::Buffer*>(in0)->contents() : nullptr;
  for (uint32_t i = 0; i < c.count; ++i) {
    if (!src) { dst[i] = SwPoint{}; continue; }
    dst[i] = src[i];
    dst[i].Position.x = src[i].Position.x * 2.0f;
  }
}
// DrawPoints stub: capture the upstream bag + emit a recognizable 1-item chain.
RenderCommand bpDraw(CmdCookCtx& c) {
  g_drawSeenBuf = c.points;
  if (g_bag && c.points && c.count > 0) {
    g_bag->assign(c.count, SwPoint{});
    std::memcpy(g_bag->data(), const_cast<MTL::Buffer*>(c.points)->contents(),
                (size_t)c.count * sizeof(SwPoint));
  }
  RenderCommand rc;
  rc.items.push_back(RenderDrawItem{c.points, c.count, 7.5f});
  return rc;
}
// CmdJam stub (Command -> Command): a cmd op whose OWN output is unmistakable garbage. A working
// Command bypass means this never runs and the upstream chain arrives instead.
RenderCommand bpJam(CmdCookCtx&) {
  ++g_jamRuns;
  RenderCommand rc;
  rc.items.push_back(RenderDrawItem{nullptr, 12345u, 1.0f});
  return rc;
}
// RenderTarget stub (tex executor): record the chain + the texture it was asked to fill.
void bpRenderTarget(TexCookCtx& c) {
  ++g_rtRuns;
  if (c.command) g_chain = *c.command;
  g_rtTex = c.output;
}
// TexFilter stub (Texture2D -> Texture2D): a tex op a working Texture2D bypass must SKIP.
void bpTexFilter(TexCookCtx& c) {
  ++g_filterRuns;
  g_filterTex = c.output;
}

}  // namespace

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

Symbol symGen() {
  return atomicOp("RadialPoints", {{"Count", "Count", "Float", 6.0f}},
                  {{"points", "points", "Points", 0.0f}});
}
Symbol symMod() {
  return atomicOp("ParticleSystem",
                  {{"emit", "emit", "Points", 0.0f}, {"forces", "forces", "ParticleForce", 0.0f}},
                  {{"result", "result", "Points", 0.0f}});
}
Symbol symDraw() {
  return atomicOp("DrawPoints", {{"points", "points", "Points", 0.0f}},
                  {{"out", "out", "Command", 0.0f}});
}
Symbol symJam() {
  return atomicOp("CmdJam", {{"command", "command", "Command", 0.0f}},
                  {{"out", "out", "Command", 0.0f}});
}
Symbol symRT() {
  return atomicOp("RenderTarget", {{"command", "command", "Command", 0.0f}},
                  {{"out", "out", "Texture2D", 0.0f}});
}
Symbol symFilter() {
  return atomicOp("TexFilter", {{"tex", "tex", "Texture2D", 0.0f}},
                  {{"out", "out", "Texture2D", 0.0f}});
}

void installStubs() {
  registerPointOp("RadialPoints", bpGen);
  registerPointOp("ParticleSystem", bpMul);
  registerCmdOp("DrawPoints", bpDraw);
  registerCmdOp("CmdJam", bpJam);
  registerTexOp("RenderTarget", bpRenderTarget);
  registerTexOp("TexFilter", bpTexFilter);
  std::map<std::string, NodeSpec> dyn;
  dyn["CmdJam"] = specFromSymbol(symJam());
  dyn["TexFilter"] = specFromSymbol(symFilter());
  setDynamicSpecs(std::move(dyn));
}

void removeStubSpecs() { setDynamicSpecs({}); }

}  // namespace sw::bypass_st
