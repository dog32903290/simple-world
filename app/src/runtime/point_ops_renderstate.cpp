// runtime/point_ops_renderstate — Seam 2: DX11 render-state → Metal PSO collapse.
//
// TiXL splits render-state into immediate-mode DX11 context mutators (Rasterizer.cs / OutputMergerStage.cs
// / BlendState.cs) with save/set/restore, terminated by one Draw. SW is retained-mode: these ops are
// Command→Command wrappers that ACCUMULATE their mutations into a FrozenRenderState and STAMP it onto every
// RenderDrawItem their subtree produced — exactly the Camera-stamp / Group-SRT push/pop mechanism
// (render_command.h). The Draw-leaf executor (cookRenderTarget) reads the stamp to materialize ONE cached
// MTL::RenderPipelineState + set the encoder dynamic state.
//
// WHY the stamp (not a CmdCookCtx accumulator field + driver save/set/restore): the Camera/Group precedent
// already rides the Command→Command gather (cc.inputCommand) that BOTH cook legs (cookFlatCommand /
// cookResidentCommand) fill IDENTICALLY, then call the ONE registered op fn. So the accumulator lives in the
// op fn (this file), the stamp is the ONLY mutation, and flat/resident CANNOT diverge by construction — the
// highest-risk seam (a resident-only accumulator miss = silent wrong-render) is dissolved: there is no
// per-leg accumulator code to get wrong. The both-leg selftest still PROVES it (drives the same subtree
// through both legs, asserts byte-identical frozen tuples). This reconciles the BUILD_PLAN's "renderStateAccum
// CmdCookCtx field + driver save/restore in both legs" with the simpler already-proven stamp posture.
//
// PARITY AUTHORITY: docs/agent/census/DX11_METAL_CONVERSION_TABLE.md (the closed-form enum table lives in
// dx11_metal_state_map.h) + SEAM2_RENDERSTATE_BUILD_PLAN.md.
//
// ZONE: runtime leaf. Contains: the two SHARED helpers (stampRenderState / frozenPSOKey) + the render-state
// ops (Rasterizer/OutputMerger) + the closed-form goldens. Split note: if this grows past ~400 lines, the
// goldens move to point_ops_renderstate_golden.cpp (the drawmeshunlit/camera golden-split precedent).
#include "runtime/render_command.h"       // RenderCommand / RenderDrawItem / FrozenRenderState (+ helper decls)

#include "runtime/dx11_metal_state_map.h"  // Dx11Cull/Dx11Fill/Dx11Compare/Dx11Blend* (closed-form ordinals)
#include "runtime/point_graph.h"           // CmdCookCtx, registerCmdOp, cookParam
#include "runtime/graph.h"                 // Graph/Node

#include <cmath>
#include <cstdint>

#include <Metal/Metal.hpp>  // the executor applies the frozen rasterizer state onto the encoder

namespace sw {

// EXECUTOR SIDE (called from cookRenderTarget's item loop): apply the ENCODER dynamic rasterizer state
// (cull / winding / depth-bias — table "Cull/winding routing" = live encoder state, NOT PSO state) for one
// draw item. Stamped item → the frozen tuple's cull/winding/bias (mapped through the closed-form table).
// Unstamped item → the pre-Seam-2 effective state (cull None, no bias) so every existing chain is byte-
// identical AND a stamped item never leaves stale cull/bias for the next unstamped item. Winding is left at
// the encoder default for unstamped items (2D quads are winding-agnostic; the pre-Seam-2 path never set it).
// DepthBias is Bucket-B EMERGENT (setDepthBias param-passing maps 1:1; numeric output not formula-portable —
// the deferred golden owns the value); this WIRES the pass-through so a graph that sets -6 reaches the encoder.
void applyFrozenRasterEncoderState(MTL::RenderCommandEncoder* enc, const RenderDrawItem& it) {
  if (it.hasRenderState) {
    enc->setCullMode((MTL::CullMode)metalCullMode((Dx11Cull)it.frozen.cullMode));
    enc->setFrontFacingWinding((MTL::Winding)metalWinding(it.frozen.frontCCW));
    enc->setDepthBias(it.frozen.depthBias, it.frozen.slopeScaledDepthBias, it.frozen.depthBiasClamp);
  } else {
    enc->setCullMode(MTL::CullModeNone);   // pre-Seam-2 effective default (encoder starts CullModeNone)
    enc->setDepthBias(0.0f, 0.0f, 0.0f);   // no bias (pre-Seam-2 path never set a bias)
  }
}

// Seam 2 both-leg CAPTURE hook (see render_command.h). null in production; the both-leg selftest sets it so
// cookRenderTarget copies the stamped chain into it to compare flat-vs-resident frozen tuples.
RenderCommand*& renderStateCaptureForTest() {
  static RenderCommand* p = nullptr;
  return p;
}

// ─────────────────────── SHARED HELPER 1: the per-item stamp (both legs ride this) ───────────────────────
RenderCommand stampRenderState(const RenderCommand& src, const FrozenRenderState& st) {
  RenderCommand rc;
  rc.items = src.items;  // COPY the subtree (we re-emit it, possibly stamped)
  for (RenderDrawItem& it : rc.items) {
    if (it.hasRenderState) continue;  // a NESTED render-state op already stamped this (innermost wins = pop)
    it.hasRenderState = true;
    it.frozen = st;
  }
  return rc;
}

// ─────────────────────── SHARED HELPER 2: the PSO cache key (FROZEN fields only) ───────────────────────
// A stable FNV-1a-style mix over the pipeline-descriptor fields. The DYNAMIC encoder fields (depthBias/
// slope/clamp) are EXCLUDED (they are setDepthBias live state, not PSO identity — keying on them explodes
// the cache; the --selftest-pso-cache -bug leg proves that inclusion would false-miss). colorPixelFormat is
// IN the key (a different attachment format = a different PSO). rtCount/guard flags are NOT in the key (they
// gate at port time, not at PSO build).
uint64_t frozenPSOKey(const FrozenRenderState& st, uint32_t colorPixelFormat) {
  uint64_t h = 1469598103934665603ull;  // FNV offset basis
  auto mix = [&h](uint64_t v) {
    h ^= v;
    h *= 1099511628211ull;  // FNV prime
  };
  mix(st.fillMode);
  mix(st.cullMode);
  mix(st.frontCCW ? 1u : 0u);
  mix(st.rt.enabled ? 1u : 0u);
  mix(st.rt.srcRGB);
  mix(st.rt.dstRGB);
  mix(st.rt.opRGB);
  mix(st.rt.srcA);
  mix(st.rt.dstA);
  mix(st.rt.opA);
  mix(st.alphaToCoverage ? 1u : 0u);
  mix(st.depthCompare);
  mix(st.depthWrite ? 1u : 0u);
  mix(colorPixelFormat);
  return h;
}

// ─────────────────────────── SPIKE OP: Rasterizer (Command → Command) ───────────────────────────
// TiXL Rasterizer.cs folds a RasterizerState.cs (CullMode/FillMode/FrontCounterClockwise/DepthBias/
// SlopeScaledDepthBias/DepthBiasClamp) around a Draw. SW folds those params directly onto the op (no
// separate RasterizerState currency) and STAMPS the accumulated FrozenRenderState onto the subtree via
// stampRenderState (the shared push). Enum inputs carry the .t3 CullMode/FillMode INT; the op maps them
// to Dx11* ordinals here (the closed-form table then maps THOSE to MTL in the executor).
//
// CullMode enum options (sw index → Dx11Cull): 0=None, 1=Front, 2=Back (census: only None/Back wired;
// .t3 default matches the DX11 Back default). FillMode: 0=Solid, 1=Wireframe (Wireframe dormant fork).
namespace {
Dx11Cull cullFromIndex(int i) {
  switch (i) {
    case 1:  return Dx11Cull::Front;
    case 2:  return Dx11Cull::Back;
    default: return Dx11Cull::None;  // 0
  }
}
Dx11Fill fillFromIndex(int i) {
  return i == 1 ? Dx11Fill::Wireframe : Dx11Fill::Solid;  // 0 = Solid (default)
}
}  // namespace

// Rasterizer: Command subtree in → Command out. Reads its rasterizer params, builds a FrozenRenderState
// (leaving blend/depth at the DX11 defaults — this op only owns the rasterizer stage), stamps it onto
// every unstamped subtree item. Unwired Command → empty chain (TiXL would eval an empty subtree).
RenderCommand cookRasterizer(CmdCookCtx& c) {
  if (!c.inputCommand) return RenderCommand{};  // no subtree wired → empty
  FrozenRenderState st;  // defaults = DX11 defaults (blend off, depth Less/write) — only rasterizer set below
  st.cullMode = (uint32_t)cullFromIndex((int)std::lround(cookParam(c, "CullMode", 2.0f)));  // .t3 default Back
  st.fillMode = (uint32_t)fillFromIndex((int)std::lround(cookParam(c, "FillMode", 0.0f)));  // .t3 default Solid
  st.frontCCW = cookParam(c, "FrontCounterClockwise", 0.0f) > 0.5f;  // DX11 default FALSE (CW front)
  st.depthBias = cookParam(c, "DepthBias", 0.0f);                    // Bucket-B EMERGENT (deferred golden)
  st.slopeScaledDepthBias = cookParam(c, "SlopeScaledDepthBias", 0.0f);
  st.depthBiasClamp = cookParam(c, "DepthBiasClamp", 0.0f);
  return stampRenderState(*c.inputCommand, st);
}

void registerRasterizerOp() { registerCmdOp("Rasterizer", cookRasterizer); }

// ─────────────────────────── BUCKET-C GUARDS (NO-METAL-EQUIVALENT, port-time) ───────────────────────────
// Two DX11 render-state capabilities have no standard public-Metal path (conversion table §Bucket C): logic-op
// blending (only via MoltenVK's private API) and dual-source blend combined with >1 render target (mutually
// exclusive on Metal). Census: NEITHER is ever wired in TiXL's node set → these guards are provable no-ops.
// The guard is a port-time VALIDATION: it returns true (= REJECT, don't materialize a PSO) when a frozen tuple
// requests an unmappable capability, false (= OK) otherwise. A valid census tuple must NEVER trip it. Returning
// a bool (not throwing/aborting) keeps the leaf pure-runtime + lets the executor + the golden both check it.
bool frozenRenderStateGuardTrips(const FrozenRenderState& st) {
  if (st.logicOpEnabled) return true;                        // logic-op: no public Metal path
  if (st.dualSourceUsed && st.rtCount > 1) return true;      // dual-source + MRT: mutually exclusive on Metal
  return false;                                              // valid: mappable closed-form
}

}  // namespace sw
