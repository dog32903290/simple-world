// VisibleGizmos command op + golden — see point_ops_visiblegizmos.h.
// TiXL authority: external/tixl/Operators/Lib/render/gizmo/VisibleGizmos.cs (GUID d61d7192).
//
// BACKWARD-TRACE (VisibleGizmos.cs:16-63 Update):
//   var visibility = Visibility.GetValue(context);
//   if (visibility == GizmoVisibility.Inherit) visibility = context.ShowGizmos;   // :19-20
//   [dead prewarm :23-31 — _updatedOnce initialized true at :65, block never runs]
//   var showIfSelected = ...MouseInput.SelectedChildId parent walk...;            // :34-48 (editor-only)
//   if (visibility != GizmoVisibility.On && !showIfSelected) return;              // :50-51 GATE
//   if (commands.Count == 0) return;                                              // :54-57
//   foreach (var t in commands) t.GetValue(context);                              // :59-62 eval all wires
// GizmoVisibility: Inherit=-1, Off=0, On=1, IfSelected=2 (Core/Operator/EvaluationContext.cs:16-22).
//
// SW: the driver's MultiInput Command collector (the S2a Execute keystone) has ALREADY gathered every
// wired Commands chain into cc.inputCommand in wire order, so the op is the GATE only: hidden → return
// an empty chain (items dropped); visible → pass the concatenated chain through (== Execute). Forks
// (inherit-off / ifselected-never / gather-side-effects) named in the header.
#include "runtime/point_ops_visiblegizmos.h"

#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp/registerTexOp, cookParam, cookResident
#include "runtime/render_command.h"  // RenderCommand / RenderDrawItem

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph/Node/NodeSpec/PortSpec/pinId/setDynamicSpecs/findSpec
#include "runtime/graph_bridge.h"         // libFromGraph
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// cookVisibleGizmos: MultiInput Commands in → Command out, gated by Visibility (see backward-trace).
RenderCommand cookVisibleGizmos(CmdCookCtx& c) {
  RenderCommand rc;
  int vis = (int)cookParam(c, "Visibility", -1.0f);  // C# (int) trunc; .t3 default "Inherit" = -1
  if (vis == -1) vis = 0;              // fork-visiblegizmos-inherit-off (fresh-context ShowGizmos = Off)
  const bool showIfSelected = false;   // fork-visiblegizmos-ifselected-never (no editor selection here)
  if (vis != 1 && !showIfSelected) return rc;  // VisibleGizmos.cs:50-51 — hidden: drop the chain
  if (!c.inputCommand) return rc;              // :54-57 — no wired Commands
  rc.items = c.inputCommand->items;            // :59-62 — pass every collected wire through
  return rc;
}

void registerVisibleGizmosOp() { registerCmdOp("VisibleGizmos", cookVisibleGizmos); }

// ───────────────────────────────── GOLDEN ─────────────────────────────────
namespace {
// ★injectBug: DROP THE GATE in the real cook — force pass-through regardless of Visibility (the exact
// corruption a lost :50-51 early-return would cause). CPU op-wrapper flag, OFF in production.
bool g_gizmosDropGate = false;

RenderCommand cookVisibleGizmosForTest(CmdCookCtx& c) {
  if (g_gizmosDropGate) {
    RenderCommand rc;
    if (c.inputCommand) rc.items = c.inputCommand->items;  // gate lost → always visible
    return rc;
  }
  return cookVisibleGizmos(c);
}

void cookSolidImageVg(TexCookCtx& c) {
  if (!c.output) return;
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(1.0, 0.0, 0.0, 1.0));  // solid RED
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  cmd->renderCommandEncoder(pass)->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

NodeSpec atomicSpecVg(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}
int outPortIdxVg(const char* type) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (!s->ports[i].isInput) return (int)i;
  return -1;
}
int inPortIdxVg(const char* type, const char* dataType) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (s->ports[i].isInput && s->ports[i].dataType == dataType) return (int)i;
  return -1;
}

// SolidImage + Layer2d test specs (no production NodeSpec — the ortho/shift golden kit);
// VisibleGizmos/RenderTarget resolve to their PRODUCTION rows (built-ins win on clash).
void installVgSpecs() {
  std::map<std::string, NodeSpec> dyn;
  dyn["SolidImage"] = atomicSpecVg("SolidImage", {{"out", "out", "Texture2D", false}});
  dyn["Layer2d"] = atomicSpecVg("Layer2d",
      {{"Image", "Image", "Texture2D", true},
       {"out", "out", "Command", false},
       {"Position.x", "Position", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 2},
       {"Position.y", "Position.y", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
       {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
       {"ScaleMode", "ScaleMode", "Float", true, 0.0f, 0.0f, 5.0f, Widget::Enum, {}, true},
       {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {}, true}});
  setDynamicSpecs(std::move(dyn));
}

// SolidRed(1) → Layer2d(2, center, half 0.6)  ┐
//               Layer2d(3, at x=0.8, half 0.12) ┴→ VisibleGizmos(4).Commands (TWO wires) → RenderTarget(5).
Graph buildVgGraph(float visibility, uint32_t W, uint32_t H) {
  Graph g;
  Node sa; sa.id = 1; sa.type = "SolidImage"; g.nodes.push_back(sa);
  Node l1; l1.id = 2; l1.type = "Layer2d";
  l1.params["Scale"] = 0.6f; l1.params["ScaleMode"] = 4.0f; l1.params["BlendMode"] = 0.0f;  // Stretch/Normal
  g.nodes.push_back(l1);
  Node l2; l2.id = 3; l2.type = "Layer2d";
  l2.params["Scale"] = 0.12f; l2.params["ScaleMode"] = 4.0f; l2.params["BlendMode"] = 0.0f;
  l2.params["Position.x"] = 0.8f;  // world x 0.8 → NDC 0.8 at the default camera (d·tan(fov/2)=1)
  g.nodes.push_back(l2);
  Node vg; vg.id = 4; vg.type = "VisibleGizmos";
  vg.params["Visibility"] = visibility;
  g.nodes.push_back(vg);
  Node rt; rt.id = 5; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
  g.nodes.push_back(rt);
  const int vgCmdIn = inPortIdxVg("VisibleGizmos", "Command");
  g.connections.push_back({101, pinId(1, outPortIdxVg("SolidImage")), pinId(2, inPortIdxVg("Layer2d", "Texture2D"))});
  g.connections.push_back({102, pinId(1, outPortIdxVg("SolidImage")), pinId(3, inPortIdxVg("Layer2d", "Texture2D"))});
  g.connections.push_back({103, pinId(2, outPortIdxVg("Layer2d")), pinId(4, vgCmdIn)});  // wire 0
  g.connections.push_back({104, pinId(3, outPortIdxVg("Layer2d")), pinId(4, vgCmdIn)});  // wire 1 (MultiInput)
  g.connections.push_back({105, pinId(4, outPortIdxVg("VisibleGizmos")), pinId(5, inPortIdxVg("RenderTarget", "Command"))});
  return g;
}

int readTargetRVg(PointGraph& pg, uint32_t W, uint32_t H, float ndcX, float ndcY) {
  MTL::Texture* tex = pg.target();
  if (!tex || (uint32_t)tex->width() != W || (uint32_t)tex->height() != H) return -1;
  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  int x = (int)((ndcX * 0.5f + 0.5f) * (float)(W - 1) + 0.5f);
  int y = (int)((1.0f - (ndcY * 0.5f + 0.5f)) * (float)(H - 1) + 0.5f);
  x = x < 0 ? 0 : (x >= (int)W ? (int)W - 1 : x);
  y = y < 0 ? 0 : (y >= (int)H ? (int)H - 1 : y);
  return px[((size_t)y * W + x) * 4 + 0];
}
}  // namespace

// --selftest-visiblegizmos: the gate truth-table through the PRODUCTION resident terminal.
// Probes: center (0,0) = quad-1 plateau; (0.8,0) = quad-2 plateau ([0.68,0.92], gap 0.08 from quad-1's
// 0.6 edge). Quad-2 rides the SECOND MultiInput wire, so its probe also proves the multi-wire gather
// passes THROUGH the gate (a first-wire-only collapse would blank it on the On leg).
int runVisibleGizmosSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-visiblegizmos] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();
  registerCmdOp("VisibleGizmos", cookVisibleGizmosForTest);  // OVERRIDE with the gate-drop wrapper
  registerTexOp("SolidImage", cookSolidImageVg);             // test source
  installVgSpecs();

  g_gizmosDropGate = injectBug;  // ★bug: gate lost → every leg renders → the hidden legs trip

  struct Leg { const char* name; float vis; bool expectVisible; };
  // GizmoVisibility ints per EvaluationContext.cs:16-22; expectations per VisibleGizmos.cs:50-51 +
  // fork-visiblegizmos-inherit-off (Inherit → fresh-context ShowGizmos default Off).
  const Leg legs[3] = {{"On", 1.0f, true}, {"Off", 0.0f, false}, {"Inherit", -1.0f, false}};
  bool allFaithful = true;
  for (const Leg& leg : legs) {
    Graph g = buildVgGraph(leg.vis, W, H);
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    PointGraph pg(dev, lib, q, W, H);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cookResident(rg, ctx, nullptr, /*RenderTarget path=*/"5");
    int p1 = readTargetRVg(pg, W, H, 0.0f, 0.0f);   // quad-1 (wire 0) plateau
    int p2 = readTargetRVg(pg, W, H, 0.8f, 0.0f);   // quad-2 (wire 1) plateau
    bool ok = leg.expectVisible ? (p1 > 200 && p2 > 200) : (p1 < 40 && p2 < 40);
    allFaithful = allFaithful && ok;
    std::printf("[selftest-visiblegizmos] %s(vis=%d): p1=%d p2=%d expect=%s -> %s\n", leg.name,
                (int)leg.vis, p1, p2, leg.expectVisible ? "visible" : "hidden",
                ok ? "faithful-ok" : "tripped");
  }

  g_gizmosDropGate = false;  // reset (process hygiene)
  setDynamicSpecs({});
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-visiblegizmos] FAIL: injectBug tripped no tooth\n");
      return 0;  // did-not-trip → NO-BITE latch catches the dead tooth (GOLDEN_STANDARD polarity)
    }
    std::printf("[selftest-visiblegizmos] injectBug correctly RED (gate dropped → the Off/Inherit legs "
                "render the quads)\n");
    return 1;
  }
  std::printf("[selftest-visiblegizmos] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

}  // namespace sw
