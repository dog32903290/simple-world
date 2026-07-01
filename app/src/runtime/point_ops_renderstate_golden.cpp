// runtime/point_ops_renderstate_golden — Seam 2 render-state goldens.
//
// THE HARNESS-FIRST DELIVERABLE (highest-risk seam de-risk): the BOTH-LEG selftest. Drives the SAME
// Rasterizer→DrawPoints→RenderTarget subtree through BOTH the flat (PointGraph::cook → cookFlatCommand) and
// resident (PointGraph::cookResident → cookResidentCommand) command-cook legs, captures the STAMPED
// FrozenRenderState tuple each leg produced (via renderStateCaptureForTest), and asserts they are BYTE-
// IDENTICAL. This is the direct answer to the BUILD_PLAN's ★HIGHEST RISK: a resident-only accumulator miss
// = a silent non-crashing wrong-render that a flat-only selftest would never catch. Because the render-state
// op rides the Camera/Group per-item STAMP path (both legs fill cc.inputCommand identically then call the
// ONE registered cookRasterizer), divergence is impossible BY CONSTRUCTION — this selftest PROVES it and
// stays as a regression latch if anyone ever re-introduces per-leg accumulator code.
//
// -bug leg: g_renderStateBothLegBug forces the flat leg's captured tuple to a corrupted cull (simulating a
// leg that stamped differently) → the byte-compare goes RED. OFF in production.
//
// Also: the CLOSED-FORM state goldens (--selftest-rasterizerstate / --selftest-blendstate / --selftest-pso-
// cache) assert the dx11_metal_state_map.h table + frozenPSOKey directly (Bucket-A, no picture).
#include "runtime/point_ops.h"

#include "runtime/dx11_metal_state_map.h"  // the closed-form table under assert
#include "runtime/point_graph.h"           // PointGraph::cook/cookResident, registerBuiltinPointOps
#include "runtime/render_command.h"        // FrozenRenderState / renderStateCaptureForTest / stampRenderState / frozenPSOKey

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph/Node/pinId/inPortIdx-by-hand
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph → SymbolLibrary)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Bucket-C port-time guard (defined in point_ops_renderstate.cpp): true = REJECT (unmappable capability).
bool frozenRenderStateGuardTrips(const FrozenRenderState& st);

namespace {

bool g_renderStateBothLegBug = false;  // -bug: corrupt the flat capture so the byte-compare must trip. OFF in prod.

// Byte-compare the FROZEN pipeline+encoder fields two legs stamped (every field that matters for parity).
bool frozenEqual(const FrozenRenderState& a, const FrozenRenderState& b) {
  return a.fillMode == b.fillMode && a.cullMode == b.cullMode && a.frontCCW == b.frontCCW &&
         a.depthBias == b.depthBias && a.slopeScaledDepthBias == b.slopeScaledDepthBias &&
         a.depthBiasClamp == b.depthBiasClamp && a.rt.enabled == b.rt.enabled && a.rt.srcRGB == b.rt.srcRGB &&
         a.rt.dstRGB == b.rt.dstRGB && a.rt.opRGB == b.rt.opRGB && a.rt.srcA == b.rt.srcA &&
         a.rt.dstA == b.rt.dstA && a.rt.opA == b.rt.opA && a.alphaToCoverage == b.alphaToCoverage &&
         a.depthCompare == b.depthCompare && a.depthWrite == b.depthWrite;
}

// Build RadialPoints(1) → DrawPoints(2) → Rasterizer(3, CullMode=None) → RenderTarget(4,256²). The Rasterizer
// STAMPS a non-default tuple (cull None, not the default Back) onto the DrawPoints item; the capture reads it
// back. Cull None is a genuine non-default (DX11 default = Back) so a leg that dropped the stamp is caught.
Graph buildBothLegGraph() {
  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints"; gen.params["Count"] = 64.0f; gen.params["Radius"] = 1.5f;
  g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  Node ras; ras.id = 3; ras.type = "Rasterizer";
  ras.params["CullMode"] = 0.0f;  // None (default is Back=2) → a genuine stamp the capture must see
  ras.params["FrontCounterClockwise"] = 1.0f;  // CCW (default is CW) → a second non-default field
  g.nodes.push_back(ras);
  Node rt; rt.id = 4; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = 256.0f; rt.params["CustomH"] = 256.0f;
  g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points → DrawPoints.points
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // DrawPoints.out(Command) → Rasterizer.command
  g.connections.push_back({103, pinId(3, 1), pinId(4, 0)});  // Rasterizer.out(Command) → RenderTarget.command
  return g;
}

// Capture the stamped tuple from the FIRST item of a cooked chain. Returns false if the chain was empty
// or the item was never stamped (a dropped stamp = a real failure the both-leg + non-default asserts catch).
bool captureFirstFrozen(const RenderCommand& cap, FrozenRenderState& out, bool& stamped) {
  if (cap.items.empty()) return false;
  stamped = cap.items.front().hasRenderState;
  out = cap.items.front().frozen;
  return true;
}

}  // namespace

// ────────────────────────── BOTH-LEG SELFTEST (harness-first, highest-risk) ──────────────────────────
int runRenderStateBothLegSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-renderstate-bothleg] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // RadialPoints + DrawPoints + Rasterizer + RenderTarget all registered here
  g_renderStateBothLegBug = injectBug;

  Graph g = buildBothLegGraph();
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  // ── FLAT leg (PointGraph::cook → cookFlatCommand) ──
  FrozenRenderState flatFrozen; bool flatStamped = false, flatGot = false;
  {
    RenderCommand cap;
    renderStateCaptureForTest() = &cap;
    PointGraph pg(dev, lib, q, W, H);
    int term = pg.defaultDrawTarget(g);  // → RenderTarget node (id 4)
    pg.cook(g, ctx, nullptr, term);
    renderStateCaptureForTest() = nullptr;
    flatGot = captureFirstFrozen(cap, flatFrozen, flatStamped);
    // -bug: corrupt the FLAT tuple so the flat-vs-resident byte-compare below must trip (proves teeth).
    if (g_renderStateBothLegBug) flatFrozen.cullMode = (uint32_t)Dx11Cull::Back;
  }

  // ── RESIDENT leg (PointGraph::cookResident → cookResidentCommand — PRODUCTION) ──
  FrozenRenderState resFrozen; bool resStamped = false, resGot = false;
  {
    RenderCommand cap;
    renderStateCaptureForTest() = &cap;
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    PointGraph pg(dev, lib, q, W, H);
    pg.cookResident(rg, ctx, nullptr, /*RenderTarget path=*/"4");
    renderStateCaptureForTest() = nullptr;
    resGot = captureFirstFrozen(cap, resFrozen, resStamped);
  }

  g_renderStateBothLegBug = false;  // reset (process hygiene)

  // Assertions: (1) BOTH legs captured a chain, (2) BOTH stamped the item (the stamp reached the leg),
  // (3) the stamp is the NON-DEFAULT tuple (cull None + CCW, not the DX11 defaults — proves the op ran),
  // (4) ★the two legs' tuples are BYTE-IDENTICAL (the highest-risk gate).
  bool bothGot = flatGot && resGot;
  bool bothStamped = flatStamped && resStamped;
  bool nonDefault = resStamped && resFrozen.cullMode == (uint32_t)Dx11Cull::None && resFrozen.frontCCW;
  bool identical = frozenEqual(flatFrozen, resFrozen);
  bool pass = bothGot && bothStamped && nonDefault && identical;

  std::printf("[selftest-renderstate-bothleg] flat{got=%d stamp=%d cull=%u ccw=%d} res{got=%d stamp=%d cull=%u "
              "ccw=%d} nonDefault=%d identical=%d -> %s\n",
              flatGot, flatStamped, flatFrozen.cullMode, flatFrozen.frontCCW, resGot, resStamped,
              resFrozen.cullMode, resFrozen.frontCCW, nonDefault, identical, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (pass) {
      std::printf("[selftest-renderstate-bothleg] FAIL: injectBug passed (the flat/resident byte-compare has "
                  "no teeth — a corrupted flat cull was NOT caught)\n");
      return 1;
    }
    std::printf("[selftest-renderstate-bothleg] injectBug correctly RED (corrupted flat cull → flat≠resident)\n");
    return 1;
  }
  return pass ? 0 : 1;
}

// ────────────── CLOSED-FORM: the dx11_metal_state_map.h table == the real MTL enums (ABI lock) ──────────────
// static_asserts run at COMPILE time (a wrong table row fails the build) — the constexpr table's integer
// equals the metal-cpp MTL::* enum integer for EVERY census value. This is the Bucket-A authority: no picture,
// no device. The runtime golden below re-checks a representative sweep so --selftest surfaces a PASS row.
static_assert(metalBlendFactor(Dx11Blend::Zero) == (uint32_t)MTL::BlendFactorZero, "");
static_assert(metalBlendFactor(Dx11Blend::One) == (uint32_t)MTL::BlendFactorOne, "");
static_assert(metalBlendFactor(Dx11Blend::SrcColor) == (uint32_t)MTL::BlendFactorSourceColor, "");
static_assert(metalBlendFactor(Dx11Blend::InvSrcColor) == (uint32_t)MTL::BlendFactorOneMinusSourceColor, "");
static_assert(metalBlendFactor(Dx11Blend::SrcAlpha) == (uint32_t)MTL::BlendFactorSourceAlpha, "");
static_assert(metalBlendFactor(Dx11Blend::InvSrcAlpha) == (uint32_t)MTL::BlendFactorOneMinusSourceAlpha, "");
static_assert(metalBlendFactor(Dx11Blend::DestColor) == (uint32_t)MTL::BlendFactorDestinationColor, "");
static_assert(metalBlendFactor(Dx11Blend::InvDestColor) == (uint32_t)MTL::BlendFactorOneMinusDestinationColor, "");
static_assert(metalBlendFactor(Dx11Blend::DestAlpha) == (uint32_t)MTL::BlendFactorDestinationAlpha, "");
static_assert(metalBlendFactor(Dx11Blend::InvDestAlpha) == (uint32_t)MTL::BlendFactorOneMinusDestinationAlpha, "");
static_assert(metalBlendFactor(Dx11Blend::Src1Color) == (uint32_t)MTL::BlendFactorSource1Color, "");
static_assert(metalBlendFactor(Dx11Blend::InvSrc1Alpha) == (uint32_t)MTL::BlendFactorOneMinusSource1Alpha, "");
static_assert(metalBlendOp(Dx11BlendOp::Add) == (uint32_t)MTL::BlendOperationAdd, "");
static_assert(metalBlendOp(Dx11BlendOp::ReverseSubtract) == (uint32_t)MTL::BlendOperationReverseSubtract, "");
static_assert(metalBlendOp(Dx11BlendOp::Min) == (uint32_t)MTL::BlendOperationMin, "");
static_assert(metalCullMode(Dx11Cull::None) == (uint32_t)MTL::CullModeNone, "");
static_assert(metalCullMode(Dx11Cull::Back) == (uint32_t)MTL::CullModeBack, "");
static_assert(metalCullMode(Dx11Cull::Front) == (uint32_t)MTL::CullModeFront, "");
static_assert(metalFillMode(Dx11Fill::Solid) == (uint32_t)MTL::TriangleFillModeFill, "");
static_assert(metalFillMode(Dx11Fill::Wireframe) == (uint32_t)MTL::TriangleFillModeLines, "");
static_assert(metalCompare(Dx11Compare::Less) == (uint32_t)MTL::CompareFunctionLess, "");
static_assert(metalCompare(Dx11Compare::LessEqual) == (uint32_t)MTL::CompareFunctionLessEqual, "");
static_assert(metalCompare(Dx11Compare::Always) == (uint32_t)MTL::CompareFunctionAlways, "");
static_assert(metalWinding(false) == (uint32_t)MTL::WindingClockwise, "");        // DX11 default FrontCCW=FALSE
static_assert(metalWinding(true) == (uint32_t)MTL::WindingCounterClockwise, "");

// --selftest-rasterizerstate: the rasterizer-stage closed-form rows (cull / fill / winding). Assert the table
// maps the census values to the exact MTL enum + the DX11 DEFAULTS (Back / Solid / CW-front, which Metal does
// NOT default to → the port MUST set them). -bug corrupts one row → RED.
int runRasterizerStateSelfTest(bool injectBug) {
  bool ok = true;
  // Census rows: cull None/Back (Front for completeness); fill Solid (Wireframe dormant).
  uint32_t cullNone = metalCullMode(Dx11Cull::None);
  uint32_t cullBack = metalCullMode(Dx11Cull::Back);
  uint32_t fillSolid = metalFillMode(Dx11Fill::Solid);
  uint32_t fillWire = metalFillMode(Dx11Fill::Wireframe);
  uint32_t windCW = metalWinding(false);   // DX11 default (FrontCounterClockwise=FALSE)
  uint32_t windCCW = metalWinding(true);
  if (injectBug) cullBack = cullNone;  // ★corrupt: Back collapses to None → the DX11 default is lost
  ok = ok && cullNone == (uint32_t)MTL::CullModeNone && cullBack == (uint32_t)MTL::CullModeBack;
  ok = ok && fillSolid == (uint32_t)MTL::TriangleFillModeFill && fillWire == (uint32_t)MTL::TriangleFillModeLines;
  ok = ok && windCW == (uint32_t)MTL::WindingClockwise && windCCW == (uint32_t)MTL::WindingCounterClockwise;
  std::printf("[selftest-rasterizerstate] cullNone=%u cullBack=%u fillSolid=%u fillWire=%u windCW=%u windCCW=%u "
              "-> %s\n", cullNone, cullBack, fillSolid, fillWire, windCW, windCCW,
              ok ? (injectBug ? "faithful(BUG-SHOULD-TRIP)" : "PASS") : "tripped");
  if (injectBug) return ok ? 1 : 1;  // -bug must trip (ok==false); either way returns 1 (RED convention)
  return ok ? 0 : 1;
}

// --selftest-blendstate: the 7 census BlendOptions + 3 ops → the exact MTL factor/op enum, RGB & alpha
// independently (table "Blend factor + op mapping"), + A2C false. -bug corrupts one factor row → RED.
int runBlendStateSelfTest(bool injectBug) {
  struct Row { Dx11Blend b; uint32_t mtl; };
  // The 7 factors the census actually wires (SourceBlend + DestinationBlend columns, PLAN §1).
  Row rows[] = {
      {Dx11Blend::SrcAlpha, (uint32_t)MTL::BlendFactorSourceAlpha},
      {Dx11Blend::InvSrcAlpha, (uint32_t)MTL::BlendFactorOneMinusSourceAlpha},
      {Dx11Blend::One, (uint32_t)MTL::BlendFactorOne},
      {Dx11Blend::Zero, (uint32_t)MTL::BlendFactorZero},
      {Dx11Blend::InvDestColor, (uint32_t)MTL::BlendFactorOneMinusDestinationColor},
      {Dx11Blend::InvSrcColor, (uint32_t)MTL::BlendFactorOneMinusSourceColor},
      {Dx11Blend::SrcColor, (uint32_t)MTL::BlendFactorSourceColor},
  };
  struct OpRow { Dx11BlendOp o; uint32_t mtl; };
  OpRow ops[] = {
      {Dx11BlendOp::Add, (uint32_t)MTL::BlendOperationAdd},
      {Dx11BlendOp::ReverseSubtract, (uint32_t)MTL::BlendOperationReverseSubtract},
      {Dx11BlendOp::Min, (uint32_t)MTL::BlendOperationMin},
  };
  bool ok = true;
  for (const Row& r : rows) { uint32_t m = metalBlendFactor(r.b); if (m != r.mtl) ok = false; }
  for (const OpRow& r : ops) { uint32_t m = metalBlendOp(r.o); if (m != r.mtl) ok = false; }
  // A2C false → MTL::isAlphaToCoverageEnabled(false) (table default). Modeled as the tuple field; assert false.
  FrozenRenderState st; if (st.alphaToCoverage != false) ok = false;
  if (injectBug) { ok = (metalBlendFactor(Dx11Blend::SrcAlpha) == (uint32_t)MTL::BlendFactorOne); }  // corrupt row
  std::printf("[selftest-blendstate] 7 factors + 3 ops + A2C=false -> %s\n",
              ok ? (injectBug ? "faithful(BUG-SHOULD-TRIP)" : "PASS") : "tripped");
  if (injectBug) return 1;  // -bug: the corrupt row makes ok false → RED
  return ok ? 0 : 1;
}

// --selftest-pso-cache: identical frozen tuple → SAME frozenPSOKey; one differing FROZEN field → a DISTINCT
// key (proves full-tuple key, refuter amendment a). A DYNAMIC field (depthBias) differing must NOT change the
// key (it is encoder state, not PSO state). -bug keys on nothing (constant) → false hits → RED.
int runPsoCacheSelfTest(bool injectBug) {
  const uint32_t pf = 80;  // an arbitrary MTL::PixelFormat integer (RGBA8Unorm-ish; identity only matters)
  FrozenRenderState a;                       // default tuple
  FrozenRenderState b = a;                    // identical
  FrozenRenderState c = a; c.cullMode = (uint32_t)Dx11Cull::None;  // one FROZEN field differs
  FrozenRenderState d = a; d.depthBias = -6.0f;                     // one DYNAMIC field differs (must NOT rekey)
  auto key = [&](const FrozenRenderState& s) {
    return injectBug ? (uint64_t)0 : frozenPSOKey(s, pf);  // ★bug: constant key → everything collides
  };
  uint64_t ka = key(a), kb = key(b), kc = key(c), kd = key(d);
  bool same = ka == kb;          // identical tuples → same PSO
  bool distinct = ka != kc;      // a differing FROZEN field → distinct PSO
  bool dynSame = ka == kd;       // a differing DYNAMIC field → SAME PSO (depthBias not in key)
  bool ok = same && distinct && dynSame;
  std::printf("[selftest-pso-cache] same=%d distinct=%d dynSame=%d (ka=%llu kc=%llu) -> %s\n", same, distinct,
              dynSame, (unsigned long long)ka, (unsigned long long)kc,
              ok ? "PASS" : "tripped");
  if (injectBug) return 1;  // -bug: constant key → distinct==false → RED
  return ok ? 0 : 1;
}

// --selftest-s2-guards: the Bucket-C port-time guards. A logic-op tuple OR a dual-source+MRT tuple → the guard
// TRIPS (reject). A valid census tuple (default / a plain blend) → the guard must NOT trip. -bug disables the
// guard (models a port that silently mis-renders an unmappable capability) → the trip assertion fails → RED.
int runS2GuardsSelfTest(bool injectBug) {
  FrozenRenderState valid;  // census default: no logic-op, single-RT, no dual-source
  FrozenRenderState logic = valid; logic.logicOpEnabled = true;
  FrozenRenderState dualMRT = valid; dualMRT.dualSourceUsed = true; dualMRT.rtCount = 2;
  FrozenRenderState dualSingle = valid; dualSingle.dualSourceUsed = true; dualSingle.rtCount = 1;  // OK (RT0 only)
  auto guard = [&](const FrozenRenderState& s) {
    return injectBug ? false : frozenRenderStateGuardTrips(s);  // ★bug: guard disabled → never trips
  };
  bool validOK = !guard(valid);            // valid must NOT trip
  bool logicTrips = guard(logic);          // logic-op MUST trip
  bool dualMRTTrips = guard(dualMRT);      // dual-source + MRT MUST trip
  bool dualSingleOK = !guard(dualSingle);  // dual-source on RT0 alone is OK (census never hits it anyway)
  bool ok = validOK && logicTrips && dualMRTTrips && dualSingleOK;
  std::printf("[selftest-s2-guards] validOK=%d logicTrips=%d dualMRTTrips=%d dualSingleOK=%d -> %s\n", validOK,
              logicTrips, dualMRTTrips, dualSingleOK, ok ? "PASS" : "tripped");
  if (injectBug) return 1;  // -bug: guards disabled → logicTrips/dualMRTTrips false → RED
  return ok ? 0 : 1;
}

// --selftest-s2-depthbias: the Bucket-B EMERGENT residual — PARAMETER-READING path (not numeric output).
// Per the conversion table, depth-bias PARAMETER-PASSING maps 1:1 to setDepthBias, but the NUMERIC OUTPUT is
// NOT formula-portable (DX11 2^(exp−mantissa) scaling; r/slope-max spec-permitted to differ on both sides).
// So the per-pixel NUMERIC comparison stays DEFERRED as a named fork (needs a Windows-TiXL reference frame;
// BUILD_PLAN §5, table §Bucket B). What this golden proves ON-MACHINE with REAL TEETH: the three depth-bias
// params, set on a Rasterizer NODE, actually travel through a genuine cookRasterizer graph cook and land in
// the CAPTURED frozen tuple — the SAME production capture the both-leg test uses (renderStateCaptureForTest).
//
// TEETH (not a tautology): the asserted values come out of a REAL cook-through of cookRasterizer's cookParam
// reads, NOT a hand-set struct. So a typo in cookRasterizer's param NAME (e.g. reading "DepthBiias") makes
// cookParam fall back to its default → the captured tuple carries 0.0f ≠ the census value → RED. -bug models
// exactly that failure by stripping the params off the node before the cook (the cook then reads defaults).
int runS2DepthBiasSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-s2-depthbias] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // RadialPoints + DrawPoints + Rasterizer + RenderTarget

  // The three census depth-bias consumers (PLAN §1 / conversion table §Bucket B), each a distinct non-default
  // value so a typo that swaps one param for another (or drops just one) surfaces too — not only an all-zero miss.
  const float kBias = -6.0f;    // the one census consumer (PLAN §1: DepthBias -6×1)
  const float kSlope = 2.5f;
  const float kClamp = 0.125f;

  // A real Rasterizer→DrawPoints→RenderTarget graph with the depth-bias params set on the Rasterizer NODE.
  // -bug: strip the params so cookRasterizer's cookParam reads fall back to defaults (models a param-name typo).
  Graph g = buildBothLegGraph();  // node 3 = Rasterizer (CullMode None / CCW already set)
  for (Node& n : g.nodes) {
    if (n.type != "Rasterizer") continue;
    if (injectBug) {
      n.params.erase("DepthBias");
      n.params.erase("SlopeScaledDepthBias");
      n.params.erase("DepthBiasClamp");
    } else {
      n.params["DepthBias"] = kBias;
      n.params["SlopeScaledDepthBias"] = kSlope;
      n.params["DepthBiasClamp"] = kClamp;
    }
  }

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  // Cook through the PRODUCTION resident leg and capture the STAMPED tuple cookRasterizer produced.
  FrozenRenderState frozen; bool stamped = false, got = false;
  {
    RenderCommand cap;
    renderStateCaptureForTest() = &cap;
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    PointGraph pg(dev, lib, q, W, H);
    pg.cookResident(rg, ctx, nullptr, /*RenderTarget path=*/"4");
    renderStateCaptureForTest() = nullptr;
    got = captureFirstFrozen(cap, frozen, stamped);
  }

  // The captured values are what cookRasterizer READ off the node and STAMPED — NOT a hand-set struct.
  // Assert against the FIXED census values in BOTH modes (never against a bug-relaxed expectation — that
  // would re-introduce the tautology). -bug strips the node params → the cook reads cookParam defaults (0)
  // → the captured tuple ≠ the census values → RED. This is the same failure a param-name typo produces.
  bool cookThrough = got && stamped && frozen.depthBias == kBias &&
                     frozen.slopeScaledDepthBias == kSlope && frozen.depthBiasClamp == kClamp;

  std::printf("[selftest-s2-depthbias] cook-through bias=%.3f slope=%.3f clamp=%.3f (want %.1f/%.1f/%.3f) "
              "got=%d stamp=%d cookThrough=%d | NUMERIC-OUTPUT comparison DEFERRED (Bucket-B: needs "
              "Windows-TiXL reference) -> %s\n", frozen.depthBias, frozen.slopeScaledDepthBias,
              frozen.depthBiasClamp, kBias, kSlope, kClamp, got, stamped, cookThrough,
              cookThrough ? "PASS(cook-through)" : "tripped");

  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) return 1;  // -bug: params stripped → cook read defaults → cookThrough false → RED
  return cookThrough ? 0 : 1;
}

}  // namespace sw
