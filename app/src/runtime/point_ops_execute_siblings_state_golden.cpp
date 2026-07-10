// runtime/point_ops_execute_siblings_state_golden — the Execute-siblings render-state accumulation SEAM golden
// (--selftest-execute-siblings-state, EXECUTE_SIBLINGS_STATE_SEAM_SPEC.md §4.2).
//
// THE SEAM: TiXL wires Rasterizer/OutputMergerStage/InputAssemblerStage/Draw as FLAT SIBLINGS into Execute's
// MultiInput<Command> (not a nested Command→Command wrap — t3_import_renderstate.cpp ★STRUCTURAL FINDING). The
// collector (point_graph_command_cook.cpp + resident twin) now ACCUMULATES each flat render-state op's frozen
// delta in wire order (concatRenderSibling → mergeFrozenDelta) and STAMPS the running tuple onto each Draw
// sibling (DX11 implicit-device-context semantics: a state-setter affects every LATER Draw). The proof is
// COOK-LEVEL frozen-tuple comparison (closed-form oracle), NOT pixel — the Explicit item's actual render is a
// SEPARATE, deferred seam (bare-shader binding, §4.4). All four gates capture the stamped chain via the SAME
// renderStateCaptureForTest hook the both-leg golden uses.
//
// FOUR GATES (each GREEN in the faithful run, RED under the shared -bug executeSiblingsStateBugForTest):
//   1. TAKEOVER (PRIMARY): a GridPlane-modeled flat-sibling Execute [IA, Rasterizer(cull=None,CCW),
//      OutputMerger(SrcAlpha/InvSrcAlpha blend, depth-test/no-write), Draw] → the sole Explicit item's frozen
//      == the closed-form GridPlane oracle (gridPlaneExpected(), field-by-field). Proves the accumulation lands
//      the composed state on the Draw.
//   2. ORDER: wire order [Rasterizer(cull=Back), Draw1, OutputMerger(blend on), Draw2] → Draw1 (BEFORE the OM)
//      carries cull=Back + blend OFF; Draw2 (AFTER) carries cull=Back + blend ON. This is route (a)'s unique
//      fidelity witness — a nested-resynthesis (route (b)) could not express per-draw order (blueprint §3.2).
//   3. BOTH-LEG (★highest risk): the gate-1 graph driven through BOTH flat + resident legs → the stamped
//      Explicit tuples are byte-identical (a resident-only accumulator miss = silent wrong-render; S2c lesson).
//   4. MULTI-DRAW SHARING: [Rasterizer(cull=None), Draw1, Draw2] → both draws share the one accumulated tuple
//      (accum persists across draws).
//
// GridPlane.t3 ITSELF is not vendored in this tree (external/tixl @395c4c55 ships no render/gizmo .t3 assets),
// so gate 1 SYNTHESIZES the flat-sibling structure the blueprint §1.3 transcribed from it (post-fold: the
// importer folds RasterizerState/BlendState/DepthStencilState onto the Rasterizer/OM params — that fold is
// tested elsewhere; the COLLECTOR is what this golden exercises). The synthetic Graph is exactly what
// GridPlane.t3 imports to; building it in code (buildBothLegGraph precedent) is more controllable than parsing
// a 400-line asset full of unmapped shader nodes, and drives the identical production cook path.
//
// ZONE: runtime golden (binds the flat + resident cook legs + the closed-form GridPlane frozen oracle).
#include "runtime/point_ops.h"

#include "runtime/dx11_metal_state_map.h"  // Dx11Cull/Dx11Blend/Dx11BlendOp/Dx11Compare ordinals (oracle)
#include "runtime/point_graph.h"           // PointGraph::cook/cookResident, registerBuiltinPointOps
#include "runtime/render_command.h"        // FrozenRenderState / RenderDrawItem / DrawKind / renderStateCaptureForTest
#include "runtime/render_command_flow.h"   // executeSiblingsStateBugForTest (the -bug DRIVER flag)

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph / Node / Connection / pinId
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph → SymbolLibrary, paths == node-id strings)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

namespace {

// Full field-by-field frozen comparator (incl. topology — the existing renderstate_golden's frozenEqual omits
// it; this gate stamps an Explicit item whose topology IS load-bearing).
bool frozenEq(const FrozenRenderState& a, const FrozenRenderState& b) {
  return a.fillMode == b.fillMode && a.cullMode == b.cullMode && a.frontCCW == b.frontCCW &&
         a.depthBias == b.depthBias && a.slopeScaledDepthBias == b.slopeScaledDepthBias &&
         a.depthBiasClamp == b.depthBiasClamp && a.rt.enabled == b.rt.enabled && a.rt.srcRGB == b.rt.srcRGB &&
         a.rt.dstRGB == b.rt.dstRGB && a.rt.opRGB == b.rt.opRGB && a.rt.srcA == b.rt.srcA &&
         a.rt.dstA == b.rt.dstA && a.rt.opA == b.rt.opA && a.alphaToCoverage == b.alphaToCoverage &&
         a.depthCompare == b.depthCompare && a.depthWrite == b.depthWrite && a.topology == b.topology;
}

// The closed-form GridPlane frozen oracle — GridPlane.t3's DepthStencilState + BlendState/RTBD + RasterizerState
// children transcribed (mirror of point_ops_gridplane.cpp:81-94 gridPlaneFrozenState, which is anon-namespace
// and thus not linkable here). ONE documented divergence from that hand-crafted tuple: the IMPORTED OutputMerger
// sets rt.srcA=One (the census alpha-channel convenience constant — t3_import_renderstate.cpp:87-88 DROPS
// RTBD.SourceAlphaBlend), whereas the hand-crafted GridPlane op reads the raw BlendState's SourceAlphaBlend=
// SourceAlpha. srcA is the alpha-channel SOURCE factor (unread by the census's opaque-alpha RGBA-target render),
// so the flat-import path faithfully reproduces every OTHER field of the oracle; this gate anchors the
// collector's accumulated output to it.
FrozenRenderState gridPlaneExpected() {
  FrozenRenderState st;  // struct defaults = DX11 (fill Solid, depth Less/Write, blend off, topology TriangleList)
  st.cullMode = (uint32_t)Dx11Cull::None;         // RasterizerState CullMode=None
  st.frontCCW = true;                             // RasterizerState FrontCounterClockwise=true
  st.depthWrite = false;                          // DepthStencilState EnableZWrite=false (ZTest on → compare Less)
  st.rt.enabled = true;                           // RenderTargetBlendDescription BlendEnabled=true
  st.rt.srcRGB = (uint32_t)Dx11Blend::SrcAlpha;
  st.rt.dstRGB = (uint32_t)Dx11Blend::InvSrcAlpha;
  st.rt.opRGB = (uint32_t)Dx11BlendOp::Add;
  st.rt.srcA = (uint32_t)Dx11Blend::One;          // ← OM convenience (gridPlaneFrozenState uses SrcAlpha)
  st.rt.dstA = (uint32_t)Dx11Blend::InvSrcAlpha;  // OM enabled-const = InvSrcAlpha (== GridPlane's DestAlphaBlend)
  st.rt.opA = (uint32_t)Dx11BlendOp::Add;
  return st;
}

// One flat sibling of the synthetic Execute graph: an op type + its constant params.
struct Sib {
  const char* type;
  std::vector<std::pair<const char*, float>> params;
};

// Build a flat-sibling Execute graph: each sibling's Command OUT wires into Execute.Command (g.connections
// array order == the MultiInput wire order the collector iterates), Execute.out → RenderTarget.command. Returns
// the RenderTarget node id (the cook terminal). NONE of the render-state siblings gets a Command input wired →
// each cooks to an EMPTY chain → the collector accumulates its delta (the flat-sibling seam under test).
int buildFlatSiblingGraph(Graph& g, const std::vector<Sib>& sibs) {
  int nextId = 10;
  std::vector<std::pair<int, std::string>> sibOut;  // (id, type) — for wiring the OUT pins in order
  for (const Sib& s : sibs) {
    Node n;
    n.id = nextId++;
    n.type = s.type;
    for (const auto& kv : s.params) n.params[kv.first] = kv.second;
    g.nodes.push_back(n);
    sibOut.push_back({n.id, s.type});
  }
  const int execId = nextId++;
  Node ex;
  ex.id = execId;
  ex.type = "Execute";
  g.nodes.push_back(ex);
  const int rtId = nextId++;
  Node rt;
  rt.id = rtId;
  rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f;
  rt.params["CustomW"] = 64.0f;
  rt.params["CustomH"] = 64.0f;
  g.nodes.push_back(rt);
  int connId = 100;
  for (const auto& so : sibOut) {  // wire order preserved → the MultiInput/CollectedInputs order
    const int oPin = pinId(so.first, so.second == "Draw" ? 0 : 1);  // Draw = SOURCE(port 0); wrappers out=port 1
    g.connections.push_back({connId++, oPin, pinId(execId, 0)});    // sibling.out → Execute.Command
  }
  g.connections.push_back({connId++, pinId(execId, 1), pinId(rtId, 0)});  // Execute.out → RenderTarget.command
  return rtId;
}

// The GridPlane-modeled flat siblings (gates 1 & 3): IA(default topology) + Rasterizer(cull None + CCW) +
// OutputMerger(SrcAlpha/InvSrcAlpha blend, depth-test/no-write) + Draw. Wire order = §1.3 transcription with the
// unmapped VertexShaderStage/PixelShaderStage dropped (Draw last).
std::vector<Sib> gridPlaneSibs() {
  return {
      {"InputAssemblerStage", {}},  // PrimitiveTopology default (3 = TriangleList)
      {"Rasterizer", {{"CullMode", 0.0f}, {"FrontCounterClockwise", 1.0f}}},  // None + CCW
      {"OutputMerger",
       {{"BlendEnable", 1.0f},
        {"SourceBlend", 2.0f},        // SrcAlpha
        {"DestinationBlend", 3.0f},   // InvSrcAlpha
        {"BlendOp", 0.0f},            // Add
        {"DepthEnable", 1.0f},
        {"DepthWrite", 0.0f},
        {"DepthCompare", 1.0f}}},     // Less
      {"Draw", {{"VertexCount", 6.0f}}},
  };
}

// Cook a graph through the FLAT leg (PointGraph::cook → cookFlatCommand) and return the STAMPED chain the
// RenderTarget executor was handed (renderStateCaptureForTest copies it before drawing — the both-leg precedent).
RenderCommand captureFlat(const Graph& g, int rtId, MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q) {
  RenderCommand cap;
  renderStateCaptureForTest() = &cap;
  EvaluationContext ctx{};
  ctx.frameIndex = 0;
  ctx.time = 0.0f;
  ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 64, 64);
  pg.cook(g, ctx, nullptr, rtId);
  renderStateCaptureForTest() = nullptr;
  return cap;
}

// Cook a graph through the RESIDENT leg (PointGraph::cookResident → cookResidentCommand — PRODUCTION). Paths ==
// flat node-id strings (graph_bridge.h contract), so the RenderTarget terminal path is std::to_string(rtId).
RenderCommand captureResident(const Graph& g, int rtId, MTL::Device* dev, MTL::Library* lib,
                              MTL::CommandQueue* q) {
  RenderCommand cap;
  renderStateCaptureForTest() = &cap;
  EvaluationContext ctx{};
  ctx.frameIndex = 0;
  ctx.time = 0.0f;
  ctx.deltaTime = 1.0f / 60.0f;
  SymbolLibrary slib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
  PointGraph pg(dev, lib, q, 64, 64);
  pg.cookResident(rg, ctx, nullptr, std::to_string(rtId));
  renderStateCaptureForTest() = nullptr;
  return cap;
}

// ─────────────────────────── GATE 1: TAKEOVER (PRIMARY, cook-level frozen oracle) ───────────────────────────
bool gateTakeover(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q) {
  Graph g;
  const int rt = buildFlatSiblingGraph(g, gridPlaneSibs());
  const RenderCommand cap = captureFlat(g, rt, dev, lib, q);
  const FrozenRenderState want = gridPlaneExpected();
  const bool one = cap.items.size() == 1;
  const bool explicitStamped = one && cap.items[0].kind == DrawKind::Explicit && cap.items[0].hasRenderState;
  const bool frozenOk = one && frozenEq(cap.items[0].frozen, want);
  const bool ok = explicitStamped && frozenOk;
  std::printf("[selftest-execute-siblings-state] gate1-takeover items=%zu(want 1) kind=%d stamped=%d "
              "frozen==gridplane=%d -> %s\n",
              cap.items.size(), one ? (int)cap.items[0].kind : -1, one && cap.items[0].hasRenderState ? 1 : 0,
              frozenOk ? 1 : 0, ok ? "ok" : "tripped");
  return ok;
}

// ─────────────────────── GATE 2: ORDER (route (a)'s per-draw fidelity witness) ───────────────────────
bool gateOrder(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q) {
  Graph g;
  const int rt = buildFlatSiblingGraph(g, {
                                               {"Rasterizer", {{"CullMode", 2.0f}}},  // Back (before both draws)
                                               {"Draw", {{"VertexCount", 6.0f}}},      // Draw1: BEFORE the OM
                                               {"OutputMerger", {{"BlendEnable", 1.0f},
                                                                 {"SourceBlend", 2.0f},
                                                                 {"DestinationBlend", 3.0f}}},
                                               {"Draw", {{"VertexCount", 6.0f}}},      // Draw2: AFTER the OM
                                           });
  const RenderCommand cap = captureFlat(g, rt, dev, lib, q);
  const bool two = cap.items.size() == 2;
  // Draw1: stamped with Rasterizer's cull=Back, but blend OFF (the OM came AFTER it in wire order).
  const bool d1 = two && cap.items[0].hasRenderState &&
                  cap.items[0].frozen.cullMode == (uint32_t)Dx11Cull::Back && !cap.items[0].frozen.rt.enabled;
  // Draw2: stamped with cull=Back AND blend ON (accum carried the OM's blend forward to it).
  const bool d2 = two && cap.items[1].hasRenderState &&
                  cap.items[1].frozen.cullMode == (uint32_t)Dx11Cull::Back && cap.items[1].frozen.rt.enabled;
  const bool ok = d1 && d2;
  std::printf("[selftest-execute-siblings-state] gate2-order items=%zu(want 2) draw1{stamp=%d cull=%u blend=%d} "
              "draw2{stamp=%d cull=%u blend=%d} -> %s\n",
              cap.items.size(), two ? cap.items[0].hasRenderState : 0, two ? cap.items[0].frozen.cullMode : 99,
              two ? cap.items[0].frozen.rt.enabled : 0, two ? cap.items[1].hasRenderState : 0,
              two ? cap.items[1].frozen.cullMode : 99, two ? cap.items[1].frozen.rt.enabled : 0,
              ok ? "ok" : "tripped");
  return ok;
}

// ─────────────────────── GATE 3: BOTH-LEG (★highest risk: flat == resident byte-identical) ───────────────────────
bool gateBothLeg(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q) {
  Graph g;
  const int rt = buildFlatSiblingGraph(g, gridPlaneSibs());
  const RenderCommand fcap = captureFlat(g, rt, dev, lib, q);
  const RenderCommand rcap = captureResident(g, rt, dev, lib, q);
  const bool got = fcap.items.size() == 1 && rcap.items.size() == 1;
  const bool stamped = got && fcap.items[0].hasRenderState && rcap.items[0].hasRenderState;
  const bool nonDefault = stamped && rcap.items[0].frozen.cullMode == (uint32_t)Dx11Cull::None;
  const bool identical = got && frozenEq(fcap.items[0].frozen, rcap.items[0].frozen);
  const bool ok = got && stamped && nonDefault && identical;
  std::printf("[selftest-execute-siblings-state] gate3-bothleg flat.items=%zu res.items=%zu stamped=%d "
              "nonDefault=%d identical=%d -> %s\n",
              fcap.items.size(), rcap.items.size(), stamped ? 1 : 0, nonDefault ? 1 : 0, identical ? 1 : 0,
              ok ? "ok" : "tripped");
  return ok;
}

// ─────────────────────── GATE 4: MULTI-DRAW SHARING (accum persists across draws) ───────────────────────
bool gateMultiDraw(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q) {
  Graph g;
  const int rt = buildFlatSiblingGraph(g, {
                                               {"Rasterizer", {{"CullMode", 0.0f}}},  // None
                                               {"Draw", {{"VertexCount", 6.0f}}},      // Draw1
                                               {"Draw", {{"VertexCount", 6.0f}}},      // Draw2
                                           });
  const RenderCommand cap = captureFlat(g, rt, dev, lib, q);
  const bool two = cap.items.size() == 2;
  const bool both = two && cap.items[0].hasRenderState && cap.items[1].hasRenderState &&
                    cap.items[0].frozen.cullMode == (uint32_t)Dx11Cull::None &&
                    cap.items[1].frozen.cullMode == (uint32_t)Dx11Cull::None;
  const bool shared = two && frozenEq(cap.items[0].frozen, cap.items[1].frozen);  // both share the one accum
  const bool ok = both && shared;
  std::printf("[selftest-execute-siblings-state] gate4-multidraw items=%zu(want 2) bothCullNone=%d shared=%d "
              "-> %s\n",
              cap.items.size(), both ? 1 : 0, shared ? 1 : 0, ok ? "ok" : "tripped");
  return ok;
}

}  // namespace

int runExecuteSiblingsStateSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-execute-siblings-state] FAIL: no metallib\n");
    q->release();
    dev->release();
    pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // Execute + Rasterizer/OutputMerger/InputAssemblerStage/Draw + RenderTarget
  executeSiblingsStateBugForTest() = injectBug;  // ★-bug: skip the accumulation → every Draw stays unstamped

  const bool g1 = gateTakeover(dev, lib, q);
  const bool g2 = gateOrder(dev, lib, q);
  const bool g3 = gateBothLeg(dev, lib, q);
  const bool g4 = gateMultiDraw(dev, lib, q);

  executeSiblingsStateBugForTest() = false;  // reset the global (process hygiene)
  const bool faithful = g1 && g2 && g3 && g4;

  lib->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (faithful) {
      std::printf("[selftest-execute-siblings-state] FAIL: injectBug passed (the accumulation has no teeth — an "
                  "unstamped Draw was NOT caught by any of the four gates)\n");
      return 1;
    }
    std::printf("[selftest-execute-siblings-state] injectBug correctly RED (accumulation disabled → Draws "
                "unstamped → gates tripped)\n");
    return 1;
  }
  std::printf("[selftest-execute-siblings-state] gates{takeover=%d order=%d bothleg=%d multidraw=%d} -> %s\n", g1,
              g2, g3, g4, faithful ? "PASS" : "FAIL");
  return faithful ? 0 : 1;
}

}  // namespace sw
