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
#include "runtime/render_command.h"       // RenderCommand / RenderDrawItem / FrozenRenderState
#include "runtime/render_command_state.h" // applyFrozenBlend / makeFrozenDepthStencilState (impl here)

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

// EXECUTOR SIDE (called from makeDrawPSO when an item is STAMPED): apply the frozen BLEND state onto a PSO
// colorAttachment via the CLOSED-FORM table (metalBlendFactor/metalBlendOp). BlendEnable=false → blending off
// (opaque src*One+dst*Zero, the DX11 default) → byte-identical to a non-blended legacy PSO. ColorWriteMask is
// left at MTL default (WriteAll — TiXL hardcodes All, no port). This is the OutputMerger materialization: the
// PSO's blend equation now comes from the stamped tuple, not the hardcoded BlendMode switch.
void applyFrozenBlend(MTL::RenderPipelineColorAttachmentDescriptor* att, const FrozenRenderState& st) {
  if (!st.rt.enabled) { att->setBlendingEnabled(false); return; }
  att->setBlendingEnabled(true);
  att->setRgbBlendOperation((MTL::BlendOperation)metalBlendOp((Dx11BlendOp)st.rt.opRGB));
  att->setAlphaBlendOperation((MTL::BlendOperation)metalBlendOp((Dx11BlendOp)st.rt.opA));
  att->setSourceRGBBlendFactor((MTL::BlendFactor)metalBlendFactor((Dx11Blend)st.rt.srcRGB));
  att->setDestinationRGBBlendFactor((MTL::BlendFactor)metalBlendFactor((Dx11Blend)st.rt.dstRGB));
  att->setSourceAlphaBlendFactor((MTL::BlendFactor)metalBlendFactor((Dx11Blend)st.rt.srcA));
  att->setDestinationAlphaBlendFactor((MTL::BlendFactor)metalBlendFactor((Dx11Blend)st.rt.dstA));
}

// EXECUTOR SIDE (called from cookRenderTarget's item loop for a STAMPED item): build the frozen DEPTH-STENCIL
// state (compare + write) via the closed-form compare table. A2C/stencil are dormant (census never wires) so
// only compare+write are set — the same two knobs the pre-Seam-2 dsMesh/dsDisabled pair carried. Caller owns
// the release. Unstamped items keep the executor's legacy dsMesh/dsDisabled (this is stamped-only).
MTL::DepthStencilState* makeFrozenDepthStencilState(MTL::Device* dev, const FrozenRenderState& st) {
  MTL::DepthStencilDescriptor* dsd = MTL::DepthStencilDescriptor::alloc()->init();
  dsd->setDepthCompareFunction((MTL::CompareFunction)metalCompare((Dx11Compare)st.depthCompare));
  dsd->setDepthWriteEnabled(st.depthWrite);
  MTL::DepthStencilState* ds = dev->newDepthStencilState(dsd);
  dsd->release();
  return ds;
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

// ─────────────────────────── OUTPUT-MERGER OP (Command → Command) ───────────────────────────
// TiXL OutputMergerStage.cs folds a BlendState (RenderTargetBlendDescription[] + A2C + IndependentBlend)
// + a DepthStencilState around a Draw, terminated by one deviceContext.Draw. SW is retained-mode: this op
// folds the census-real blend + depth knobs DIRECTLY onto the op (no separate BlendState currency — same
// posture as cookRasterizer / the "no RasterizerState currency" fold) and STAMPS the accumulated blend/depth
// half of FrozenRenderState onto the subtree via stampRenderState. The RenderTarget executor reads the stamp
// to materialize the PSO's colorAttachment blend + the encoder's MTLDepthStencilState.
//
// CENSUS (PLAN §1): BlendEnable true×22/false×11; SourceBlend {SrcAlpha,One,Zero,InvDestColor};
// DestinationBlend {InvSrcAlpha,One,InvSrcColor,Zero,SrcColor,SrcAlpha}; BlendOp {Add,ReverseSubtract,Min}.
// ColorWriteMask hardcoded All (no port). A2C/IndependentBlend always false (dormant). Depth: compare
// Less(default)/LessEqual, write on. The op folds the RGB blend triple + the SAME-channel alpha triple the
// census wires (Src=One/SrcAlpha/InvSrcAlpha · Dst=InvSrcAlpha/Zero/One/DstAlpha), driven off the RGB enum by
// default (blend[Enum] indices index the shared Dx11Blend/Dx11BlendOp ordinals via the closed-form table).
namespace {
// The census-wired BlendOptions the OutputMerger enum exposes (index → Dx11Blend ordinal). Order chosen so
// the .t3 default (blend disabled = src One / dst Zero) sits at 0/1 and the common alpha-over combo is present.
Dx11Blend blendFactorFromIndex(int i) {
  switch (i) {
    case 1:  return Dx11Blend::One;
    case 2:  return Dx11Blend::SrcAlpha;
    case 3:  return Dx11Blend::InvSrcAlpha;
    case 4:  return Dx11Blend::SrcColor;
    case 5:  return Dx11Blend::InvSrcColor;
    case 6:  return Dx11Blend::DestColor;
    case 7:  return Dx11Blend::InvDestColor;
    case 8:  return Dx11Blend::DestAlpha;
    case 9:  return Dx11Blend::InvDestAlpha;
    default: return Dx11Blend::Zero;  // 0
  }
}
Dx11BlendOp blendOpFromIndex(int i) {
  switch (i) {
    case 1:  return Dx11BlendOp::Subtract;
    case 2:  return Dx11BlendOp::ReverseSubtract;
    case 3:  return Dx11BlendOp::Min;
    case 4:  return Dx11BlendOp::Max;
    default: return Dx11BlendOp::Add;  // 0
  }
}
Dx11Compare compareFromIndex(int i) {
  switch (i) {
    case 0:  return Dx11Compare::Never;
    case 1:  return Dx11Compare::Less;      // DX11 default
    case 2:  return Dx11Compare::Equal;
    case 3:  return Dx11Compare::LessEqual;
    case 4:  return Dx11Compare::Greater;
    case 5:  return Dx11Compare::NotEqual;
    case 6:  return Dx11Compare::GreaterEqual;
    case 7:  return Dx11Compare::Always;
    default: return Dx11Compare::Less;
  }
}
}  // namespace

// OutputMerger: Command subtree in → Command out. Reads its blend + depth params, builds the blend/depth half
// of a FrozenRenderState (leaving the rasterizer stage at DX11 defaults — this op owns the OM stage only),
// stamps it onto every unstamped subtree item. Unwired Command → empty chain (TiXL evals an empty subtree).
// BlendEnable=false → the DX11 default opaque src*One + dst*Zero (blending off); the executor then draws the
// item with blend disabled (byte-identical to the pre-Seam-2 opaque path for a default tuple).
RenderCommand cookOutputMerger(CmdCookCtx& c) {
  if (!c.inputCommand) return RenderCommand{};  // no subtree wired → empty
  FrozenRenderState st;  // defaults = DX11 defaults (rasterizer Back/Solid/CW untouched by this stage)
  st.rt.enabled = cookParam(c, "BlendEnable", 0.0f) > 0.5f;   // DX11 default FALSE (opaque)
  int srcI = (int)std::lround(cookParam(c, "SourceBlend", 1.0f));       // default One
  int dstI = (int)std::lround(cookParam(c, "DestinationBlend", 0.0f));  // default Zero
  int opI  = (int)std::lround(cookParam(c, "BlendOp", 0.0f));           // default Add
  st.rt.srcRGB = (uint32_t)blendFactorFromIndex(srcI);
  st.rt.dstRGB = (uint32_t)blendFactorFromIndex(dstI);
  st.rt.opRGB  = (uint32_t)blendOpFromIndex(opI);
  // Alpha channel (census "Src/Dst/AlphaOp" column, PLAN §1): AlphaOp is ALWAYS Add (the census never wires a
  // non-Add alpha op — even when the RGB op is ReverseSubtract/Min); SrcAlpha=One; DstAlpha is a CONSTANT
  // InverseSourceAlpha — NOT derived from the RGB dst. Ground truth: TiXL DefaultRenderingStates.cs sets
  // DestinationAlphaBlend = BlendOption.InverseSourceAlpha for BOTH DefaultBlendState (Normal, :68) AND
  // AdditiveBlendState (:112). Deriving dstA from dstRGB was WRONG for Additive (dstRGB=One → dstA would become
  // One instead of InvSrcAlpha). This is a closed-form constant, latched by --selftest-outputmerger-cookthrough's
  // Additive case (frozen.rt.dstA == InvSrcAlpha). Blend OFF → opaque One/Zero (dstA=Zero, blend disabled).
  st.rt.srcA = (uint32_t)Dx11Blend::One;                                          // census: alpha src One
  st.rt.dstA = st.rt.enabled ? (uint32_t)Dx11Blend::InvSrcAlpha                   // TiXL const: DstAlphaBlend=InvSrcAlpha
                             : (uint32_t)Dx11Blend::Zero;                         // blend OFF → opaque
  st.rt.opA  = (uint32_t)Dx11BlendOp::Add;                                        // census: alpha op ALWAYS Add
  // Depth-stencil half (DX11 default DepthEnable=TRUE / WriteAll / LESS). DepthEnable=false → compare Always +
  // write off (the executor's depth-disabled state — every 2D kind's effective default → byte-identical).
  bool depthEnable = cookParam(c, "DepthEnable", 0.0f) > 0.5f;  // sw 2D default: depth inert (Always/no-write)
  st.depthCompare = depthEnable
      ? (uint32_t)compareFromIndex((int)std::lround(cookParam(c, "DepthCompare", 1.0f)))  // default Less
      : (uint32_t)Dx11Compare::Always;
  st.depthWrite = depthEnable && (cookParam(c, "DepthWrite", 1.0f) > 0.5f);
  st.alphaToCoverage = false;  // census: never enabled (dormant fork)
  return stampRenderState(*c.inputCommand, st);
}

void registerOutputMergerOp() { registerCmdOp("OutputMerger", cookOutputMerger); }

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
