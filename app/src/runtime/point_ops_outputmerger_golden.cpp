// runtime/point_ops_outputmerger_golden — Seam 2 OUTPUT-MERGER goldens (blend + depth STAGE).
//
// THE HARNESS-FIRST DELIVERABLE: the OutputMerger BOTH-LEG selftest. Drives the SAME
// RadialPoints→DrawPoints→OutputMerger→RenderTarget subtree through BOTH the flat (PointGraph::cook →
// cookFlatCommand) and resident (PointGraph::cookResident → cookResidentCommand) command-cook legs, captures
// the STAMPED FrozenRenderState tuple each leg produced (via renderStateCaptureForTest), and asserts the
// BLEND + DEPTH half is BYTE-IDENTICAL. This mirrors the Rasterizer both-leg (point_ops_renderstate_golden.cpp)
// but exercises the OutputMerger op's blend/depth fold instead of the rasterizer fold — a resident-only
// accumulator miss on blend = a silent wrong-composite. Because cookOutputMerger rides the SAME per-item STAMP
// path (both legs fill cc.inputCommand identically then call the ONE registered cookOutputMerger), divergence
// is impossible BY CONSTRUCTION — this PROVES it and latches against any future per-leg accumulator code.
//
// Plus the COOK-THROUGH golden: a real OutputMerger NODE with BlendEnable + SourceBlend/DestinationBlend/BlendOp
// set, cooked THROUGH the production resident leg; the captured stamped tuple's blend fields must equal the
// census values cookOutputMerger READ off the node — NOT a hand-set struct (a param-name typo → cookParam
// default → RED; the blood-lesson rule against繞-cook struct-stuffing). The closed-form blend-factor table
// itself is already asserted by --selftest-blendstate; this proves the OP wires it end-to-end.
#include "runtime/point_ops.h"

#include "runtime/dx11_metal_state_map.h"  // Dx11Blend/Dx11BlendOp/Dx11Compare ordinals under assert
#include "runtime/point_graph.h"           // PointGraph::cook/cookResident, registerBuiltinPointOps
#include "runtime/render_command.h"        // FrozenRenderState / renderStateCaptureForTest

#include <cstdint>
#include <cstdio>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph/Node/pinId
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph → SymbolLibrary)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// Byte-compare the BLEND + DEPTH fields two legs stamped (the OutputMerger stage's half of the tuple).
bool blendDepthEqual(const FrozenRenderState& a, const FrozenRenderState& b) {
  return a.rt.enabled == b.rt.enabled && a.rt.srcRGB == b.rt.srcRGB && a.rt.dstRGB == b.rt.dstRGB &&
         a.rt.opRGB == b.rt.opRGB && a.rt.srcA == b.rt.srcA && a.rt.dstA == b.rt.dstA &&
         a.rt.opA == b.rt.opA && a.alphaToCoverage == b.alphaToCoverage &&
         a.depthCompare == b.depthCompare && a.depthWrite == b.depthWrite;
}

bool g_omBothLegBug = false;  // -bug: corrupt the flat capture so the byte-compare must trip. OFF in prod.

// Build RadialPoints(1) → DrawPoints(2) → OutputMerger(3) → RenderTarget(4,256²). The OutputMerger STAMPS a
// NON-DEFAULT blend tuple (BlendEnable ON, Src=SrcAlpha, Dst=InvSrcAlpha, Op=ReverseSubtract) + depth compare
// GreaterEqual onto the DrawPoints item; the capture reads it back. These are all genuine non-defaults (the
// DX11 default is blend OFF / One-Zero-Add / depth Less) so a leg that dropped the stamp is caught.
// buildOMGraph mode: kNonDefault = the census non-default combo (SrcAlpha/InvSrcAlpha/ReverseSubtract);
// kStrip = no OM params (cook reads cookParam defaults, models a param-name typo → RED); kAdditive = the TiXL
// AdditiveBlendState combo (Src=SrcAlpha, Dst=One, Op=Add) whose RGB dst=One ≠ its alpha dst — the case that
// PROVES dstA is a CONSTANT InvSrcAlpha, not derived from dstRGB.
enum class OMMode { kNonDefault, kStrip, kAdditive };
Graph buildOMGraph(OMMode mode) {
  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints"; gen.params["Count"] = 64.0f; gen.params["Radius"] = 1.5f;
  g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  Node om; om.id = 3; om.type = "OutputMerger";
  if (mode == OMMode::kNonDefault) {
    om.params["BlendEnable"] = 1.0f;        // ON (default OFF)
    om.params["SourceBlend"] = 2.0f;        // SrcAlpha (default One=1)
    om.params["DestinationBlend"] = 3.0f;   // InvSrcAlpha (default Zero=0)
    om.params["BlendOp"] = 2.0f;            // ReverseSubtract (default Add=0)
    om.params["DepthEnable"] = 1.0f;        // ON → honor DepthCompare/DepthWrite
    om.params["DepthCompare"] = 6.0f;       // GreaterEqual (default Less=1)
    om.params["DepthWrite"] = 0.0f;         // write OFF
  } else if (mode == OMMode::kAdditive) {
    om.params["BlendEnable"] = 1.0f;        // ON
    om.params["SourceBlend"] = 2.0f;        // SrcAlpha (TiXL AdditiveBlendState)
    om.params["DestinationBlend"] = 1.0f;   // One  ← RGB dst=One; alpha dst MUST stay InvSrcAlpha (TiXL const)
    om.params["BlendOp"] = 0.0f;            // Add
  }
  g.nodes.push_back(om);
  Node rt; rt.id = 4; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = 256.0f; rt.params["CustomH"] = 256.0f;
  g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points → DrawPoints.points
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // DrawPoints.out(Command) → OutputMerger.command
  g.connections.push_back({103, pinId(3, 1), pinId(4, 0)});  // OutputMerger.out(Command) → RenderTarget.command
  return g;
}

// Cook `g` through ONE leg (flat if resident=false, else the production resident leg) and capture the first
// item's stamped tuple. Returns got/stamped via the out-params.
void cookAndCapture(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, const Graph& g, bool resident,
                    FrozenRenderState& out, bool& stamped, bool& got) {
  const uint32_t W = 256, H = 256;
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  RenderCommand cap;
  renderStateCaptureForTest() = &cap;
  if (resident) {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    PointGraph pg(dev, lib, q, W, H);
    pg.cookResident(rg, ctx, nullptr, /*RenderTarget path=*/"4");
  } else {
    PointGraph pg(dev, lib, q, W, H);
    int term = pg.defaultDrawTarget(g);  // → RenderTarget node (id 4)
    pg.cook(g, ctx, nullptr, term);
  }
  renderStateCaptureForTest() = nullptr;
  got = !cap.items.empty();
  stamped = got && cap.items.front().hasRenderState;
  if (got) out = cap.items.front().frozen;
}

}  // namespace

// ────────────────────── OUTPUT-MERGER BOTH-LEG SELFTEST (harness-first, highest-risk) ──────────────────────
int runOutputMergerBothLegSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-outputmerger-bothleg] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // RadialPoints + DrawPoints + OutputMerger + RenderTarget
  g_omBothLegBug = injectBug;

  Graph g = buildOMGraph(OMMode::kNonDefault);

  FrozenRenderState flatF, resF;
  bool flatStamp = false, flatGot = false, resStamp = false, resGot = false;
  cookAndCapture(dev, lib, q, g, /*resident=*/false, flatF, flatStamp, flatGot);
  // -bug: corrupt the FLAT tuple so the flat-vs-resident byte-compare below must trip (proves teeth).
  if (g_omBothLegBug) flatF.rt.srcRGB = (uint32_t)Dx11Blend::One;
  cookAndCapture(dev, lib, q, g, /*resident=*/true, resF, resStamp, resGot);
  g_omBothLegBug = false;

  // Assertions: (1) BOTH legs captured + stamped, (2) the stamp is the NON-DEFAULT blend tuple (proves the op
  // ran: BlendEnable + SrcAlpha/InvSrcAlpha/ReverseSubtract, not the DX11 One/Zero/Add default), (3) ★the two
  // legs' blend+depth tuples are BYTE-IDENTICAL (the highest-risk gate).
  bool bothGot = flatGot && resGot, bothStamped = flatStamp && resStamp;
  bool nonDefault = resStamp && resF.rt.enabled &&
                    resF.rt.srcRGB == (uint32_t)Dx11Blend::SrcAlpha &&
                    resF.rt.dstRGB == (uint32_t)Dx11Blend::InvSrcAlpha &&
                    resF.rt.opRGB == (uint32_t)Dx11BlendOp::ReverseSubtract &&
                    resF.depthCompare == (uint32_t)Dx11Compare::GreaterEqual && !resF.depthWrite;
  bool identical = blendDepthEqual(flatF, resF);
  bool pass = bothGot && bothStamped && nonDefault && identical;

  std::printf("[selftest-outputmerger-bothleg] flat{en=%d src=%u dst=%u op=%u cmp=%u} res{en=%d src=%u dst=%u "
              "op=%u cmp=%u} nonDefault=%d identical=%d -> %s\n", flatF.rt.enabled, flatF.rt.srcRGB,
              flatF.rt.dstRGB, flatF.rt.opRGB, flatF.depthCompare, resF.rt.enabled, resF.rt.srcRGB,
              resF.rt.dstRGB, resF.rt.opRGB, resF.depthCompare, nonDefault, identical, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  if (injectBug) {
    if (pass) {
      std::printf("[selftest-outputmerger-bothleg] FAIL: injectBug passed (corrupted flat srcRGB NOT caught)\n");
      return 1;
    }
    std::printf("[selftest-outputmerger-bothleg] injectBug correctly RED (corrupted flat blend → flat≠resident)\n");
    return 1;
  }
  return pass ? 0 : 1;
}

// ─────── OUTPUT-MERGER COOK-THROUGH GOLDEN (blood-lesson: no繞-cook struct-stuffing; real NodeSpec) ───────
// A real OutputMerger NODE with the census blend/depth params set, cooked THROUGH the PRODUCTION resident leg;
// the captured stamped tuple's blend fields must equal the census values cookOutputMerger READ off the node
// (via cookParam). TEETH: a typo in cookOutputMerger's param NAME (e.g. "SourceBlnd") makes cookParam fall
// back to its default → the captured srcRGB ≠ SrcAlpha → RED. -bug models exactly that by STRIPPING the params
// off the node before the cook (the cook then reads cookParam defaults: blend OFF, One/Zero/Add).
int runOutputMergerCookThroughSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-outputmerger-cookthrough] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();

  // -bug strips the OutputMerger params → cookOutputMerger reads cookParam defaults (blend OFF → One/Zero/Add,
  // the DX11 opaque default). No-bug sets the census non-default combo.
  Graph g = buildOMGraph(injectBug ? OMMode::kStrip : OMMode::kNonDefault);

  FrozenRenderState frozen; bool stamped = false, got = false;
  cookAndCapture(dev, lib, q, g, /*resident=*/true, frozen, stamped, got);

  // The captured values are what cookOutputMerger READ off the node and STAMPED — NOT a hand-set struct.
  // Assert against the FIXED census combo in BOTH modes (never a bug-relaxed expectation = no tautology).
  // The non-default combo happens to set DestinationBlend=InvSrcAlpha, so dstRGB==dstA here — this case alone
  // could NOT catch a dstA=dstRGB derivation bug. The Additive case below closes that hole.
  bool cookThrough = got && stamped && frozen.rt.enabled &&
                     frozen.rt.srcRGB == (uint32_t)Dx11Blend::SrcAlpha &&
                     frozen.rt.dstRGB == (uint32_t)Dx11Blend::InvSrcAlpha &&
                     frozen.rt.opRGB == (uint32_t)Dx11BlendOp::ReverseSubtract &&
                     frozen.depthCompare == (uint32_t)Dx11Compare::GreaterEqual && !frozen.depthWrite;

  std::printf("[selftest-outputmerger-cookthrough] en=%d src=%u(want %u) dst=%u(want %u) op=%u(want %u) "
              "cmp=%u(want %u) write=%d got=%d stamp=%d cookThrough=%d -> %s\n", frozen.rt.enabled,
              frozen.rt.srcRGB, (uint32_t)Dx11Blend::SrcAlpha, frozen.rt.dstRGB, (uint32_t)Dx11Blend::InvSrcAlpha,
              frozen.rt.opRGB, (uint32_t)Dx11BlendOp::ReverseSubtract, frozen.depthCompare,
              (uint32_t)Dx11Compare::GreaterEqual, frozen.depthWrite, got, stamped, cookThrough,
              cookThrough ? "PASS(cook-through)" : "tripped");

  // ── CLOSED-FORM ALPHA GOLDEN (blood-lesson: dstA is a CONSTANT, not derived from dstRGB) ──
  // TiXL DefaultRenderingStates.cs: BOTH DefaultBlendState (:68) and AdditiveBlendState (:112) set
  // DestinationAlphaBlend = BlendOption.InverseSourceAlpha. The Additive combo (DestinationBlend=One → dstRGB=One)
  // is the ONLY census case where dstRGB ≠ dstA, so it is the sole prover that dstA is a constant. RED-FIRST:
  // with the old dstA = st.rt.enabled ? st.rt.dstRGB : Zero, this Additive cook yields dstA=One → alphaOK false
  // → RED. With the TiXL constant InvSrcAlpha → GREEN. This golden IS the "alpha needs no Windows frame" proof.
  Graph ga = buildOMGraph(injectBug ? OMMode::kStrip : OMMode::kAdditive);
  FrozenRenderState af; bool aStamped = false, aGot = false;
  cookAndCapture(dev, lib, q, ga, /*resident=*/true, af, aStamped, aGot);
  bool alphaOK = aGot && aStamped && af.rt.enabled &&
                 af.rt.dstRGB == (uint32_t)Dx11Blend::One &&               // Additive RGB dst = One
                 af.rt.srcA == (uint32_t)Dx11Blend::One &&                 // census: alpha src One
                 af.rt.dstA == (uint32_t)Dx11Blend::InvSrcAlpha &&         // ★TiXL const: alpha dst InvSrcAlpha (NOT One)
                 af.rt.opA == (uint32_t)Dx11BlendOp::Add;                  // census: alpha op Add
  std::printf("[selftest-outputmerger-cookthrough-alpha] additive dstRGB=%u srcA=%u dstA=%u(want %u) opA=%u "
              "alphaOK=%d -> %s\n", af.rt.dstRGB, af.rt.srcA, af.rt.dstA, (uint32_t)Dx11Blend::InvSrcAlpha,
              af.rt.opA, alphaOK, alphaOK ? "PASS(alpha-const)" : "tripped");

  lib->release(); q->release(); dev->release(); pool->release();
  if (injectBug) return 1;  // -bug: params stripped → cook read defaults → cookThrough/alphaOK false → RED
  return (cookThrough && alphaOK) ? 0 : 1;
}

}  // namespace sw
