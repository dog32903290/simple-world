// RenderTarget texture op (lane A render-target pivot, batch 1) — the THIRD cook flow.
// Executes an upstream RenderCommand (Command stream) into a sized texture: TiXL's
// RenderTarget (external/tixl .../image/generate/basic/RenderTarget.cs). This is the
// RESOLUTION PIN point — Resolution param decides the output texture size; WindowFollow
// tracks the output window (dynamic, no squash), fixed modes pin 16:9 / HD / 4K.
//
// Self-contained leaf: cookRenderTarget + resolveRenderResolution + registerRenderTargetOp()
// + runRenderTargetSelfTest(). Batch 1 lands the op + texture-stream machinery and proves
// it in isolation; the cook() terminal dispatch wires it in batch 2/3 (until then texReg is
// empty in production — zero behavior change, exactly like batch 0's cmd stream).
//
// The draw is faithful to cookDrawPoints (same draw_points pipeline + DRAW_* bindings),
// but loops the RenderCommand's items into ONE render pass: clear once, draw each item.
// That single-pass-N-draws is the payoff of RenderCommand being a data record, not a
// closure (compositing = the executor walks the chain; layers don't clear each other).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/cmd_view_background.h"  // commandViewBackground() — Output-window view bg (terminal Command clear)
#include "runtime/draw_params.h"      // DrawLineParams/DrawBillboardParams/DrawScreenQuadParams + bindings
#include "runtime/field_camera.h"     // defaultLayerCameraForward / objectToClipSpace (Layer2d seam, F1)
#include "runtime/graph.h"            // Graph/Node
#include "runtime/mesh_draw_params.h" // MeshDrawParams + MESH_* bindings (DrawKind::Mesh)
#include "runtime/particle_params.h"  // DRAW_Points, DRAW_ViewExtent
#include "runtime/point_graph.h"      // TexCookCtx, RenderResolution, registerTexOp
#include "runtime/render_command.h"   // RenderCommand / RenderDrawItem / DrawKind
#include "runtime/render_command_state.h" // makeFrozenDepthStencilState (executor depth-stencil)
#include "runtime/tixl_point.h"       // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Seam 2: apply the frozen rasterizer state (cull/winding/depthBias) onto the encoder per item — defined
// in point_ops_renderstate.cpp (the render-state leaf owns the closed-form MTL mapping).
void applyFrozenRasterEncoderState(MTL::RenderCommandEncoder* enc, const RenderDrawItem& it);

// Test-only depth-disable hook (Tooth B). File-scope flag, off in production; the depth-occlusion golden
// flips it to prove the LessEqual+ZWrite state is load-bearing (see render_command.h).
bool& meshDepthDisableForTest() {
  static bool v = false;
  return v;
}

namespace {

float paramOr(const std::map<std::string, float>& params, const char* id, float def) {
  auto it = params.find(id);
  return it != params.end() ? it->second : def;
}

// Build a render PSO for a vs/fs function pair into `pixelFormat`. `blend` turns on standard src-alpha
// blending (lines/billboards composite over prior draws); DrawPoints stays opaque (blend=false). When `mode`
// is non-null it overrides `blend` and selects the EXACT TiXL BlendMode factor table (Normal/Add, from
// Core/Rendering/DefaultRenderingStates.cs) — used by DrawScreenQuad. nullptr if either function is missing.
// ★Seam 2 OutputMerger: when `frozen` is non-null (a STAMPED item), the PSO's blend equation comes from that
// tuple via applyFrozenBlend (the closed-form metalBlend* table) — OVERRIDING both `mode` and `blend`. Unstamped
// items pass frozen=null → the exact legacy hardcoded blend path (byte-identical, press-pass).
MTL::RenderPipelineState* makeDrawPSO(MTL::Device* dev, MTL::Library* lib, const char* vsName,
                                      const char* fsName, MTL::PixelFormat pf, bool blend,
                                      const BlendMode* mode = nullptr,
                                      const FrozenRenderState* frozen = nullptr) {
  MTL::Function* vs = lib->newFunction(NS::String::string(vsName, NS::UTF8StringEncoding));
  MTL::Function* fs = lib->newFunction(NS::String::string(fsName, NS::UTF8StringEncoding));
  MTL::RenderPipelineState* rps = nullptr;
  if (vs && fs) {
    MTL::RenderPipelineDescriptor* rpd = MTL::RenderPipelineDescriptor::alloc()->init();
    rpd->setVertexFunction(vs);
    rpd->setFragmentFunction(fs);
    // ★ALL-PSOs-FORMAT GOTCHA (depth seam): the pass attaches a Depth32Float texture (so the mesh can depth-
    // test), and once a pass has a depth attachment EVERY PSO drawing into it MUST declare the same
    // depthAttachmentPixelFormat — even the depth-DISABLED 2D composites. A mismatch = hard PSO validation
    // failure / no draw. Declared UNCONDITIONALLY; the per-draw MTLDepthStencilState (encoder) decides whether
    // a kind tests/writes depth (2D kinds = Always + write-off → byte-identical 2D, the depth buffer inert).
    rpd->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
    auto* att = rpd->colorAttachments()->object(0);
    att->setPixelFormat(pf);
    if (frozen) {
      applyFrozenBlend(att, *frozen);  // Seam 2: PSO blend from the stamped OutputMerger tuple (closed-form)
    } else if (mode) {
      // TiXL BlendMode factor table (DefaultRenderingStates.cs / PickBlendMode.t3). RGB factors
      // differ per mode; the ALPHA channel is the SAME for both (SrcA=One, DstA=1-SrcA, Add) —
      // verbatim from DefaultBlendState/AdditiveBlendState. RGB op is Add for both.
      att->setBlendingEnabled(true);
      att->setRgbBlendOperation(MTL::BlendOperationAdd);
      att->setAlphaBlendOperation(MTL::BlendOperationAdd);
      att->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);  // both modes: src*SrcA
      att->setDestinationRGBBlendFactor(*mode == BlendMode::Additive
                                            ? MTL::BlendFactorOne                 // Additive: + dst*1
                                            : MTL::BlendFactorOneMinusSourceAlpha);  // Normal: + dst*(1-SrcA)
      att->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
      att->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    } else if (blend) {
      att->setBlendingEnabled(true);
      att->setRgbBlendOperation(MTL::BlendOperationAdd);
      att->setAlphaBlendOperation(MTL::BlendOperationAdd);
      att->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
      att->setSourceAlphaBlendFactor(MTL::BlendFactorSourceAlpha);
      att->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
      att->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    }
    NS::Error* err = nullptr;
    rps = dev->newRenderPipelineState(rpd, &err);
    rpd->release();
  }
  if (vs) vs->release();
  if (fs) fs->release();
  return rps;
}

}  // namespace

// RenderTarget draw: open one render pass on `output`, clear it once, then draw every item in the
// command chain in order (later items composite on top). The chain can MIX draw kinds (DrawPoints /
// DrawLines / DrawBillboards): each item names its DrawKind, the executor selects the matching PSO
// + primitive type. PSOs are built lazily per kind per call (only the kinds actually present) — the live
// loop's per-frame caching is a follow-up. NOT file-local (out of the anon namespace) so the draw-op leaf
// selftests can drive a chain straight through it (point_ops_drawlines.cpp / point_ops_drawbillboards.cpp).
void cookRenderTarget(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  if (RenderCommand* cap = renderStateCaptureForTest(); cap && c.command) *cap = *c.command;  // S2 both-leg cap
  MTL::PixelFormat pf = c.output->pixelFormat();
  MTL::RenderPipelineState* psoPoints = nullptr;
  MTL::RenderPipelineState* psoPoints2 = nullptr;     // DrawPoints2 (DrawKind::Points2)
  MTL::RenderPipelineState* psoLines = nullptr;
  MTL::RenderPipelineState* psoLinesBuildup = nullptr;  // DrawLinesBuildup (DrawKind::LinesBuildup)
  MTL::RenderPipelineState* psoBb = nullptr;
  // ScreenQuad PSO variants, lazily built per blend mode (FORK#3, scoped to this batch's 2 modes:
  // Normal/Additive). Same per-call lazy posture as the point/line PSOs above — the executor's
  // per-frame PSO caching is a deferred follow-up (note at the top of this fn); folding ScreenQuad
  // into the future cache is a one-line key extension when that lands.
  MTL::RenderPipelineState* psoSQ[2] = {nullptr, nullptr};  // [Normal, Additive]
  MTL::SamplerState* sqSampler = nullptr;
  // Layer2d (DrawKind::Layer2d): same lazy-per-blend-mode posture as ScreenQuad. F2 — a SEPARATE
  // PSO (draw_quad_xf_vs + the shared draw_screenquad_fs), the clip-space ScreenQuad leaf untouched.
  MTL::RenderPipelineState* psoL2[2] = {nullptr, nullptr};  // [Normal, Additive]
  // F1 — function-local transform context (NOT a runtime global): the default camera FORWARD pair for
  // THIS output's aspect (the resolution-pin point). When no Camera op is present (Cut 1: always),
  // ObjectToClipSpace = ObjectToWorld · defaultWorldToCamera · defaultCameraToClipSpace. Built once
  // here, reused per Layer2d item; Cut 2's Camera op will push/pop this context.
  const float aspectF =
      (c.output->height() > 0) ? (float)c.output->width() / (float)c.output->height() : 1.0f;
  const LayerCameraForward camFwd = defaultLayerCameraForward(aspectF);

  // ── Seam 2 per-item BLEND-PSO cache (OutputMerger materialization) ──────────────────────────────
  // A STAMPED item (it.hasRenderState) draws with a PSO whose blend equation comes from it.frozen (via
  // applyFrozenBlend in makeDrawPSO), NOT the hardcoded per-kind blend. Materialized ONCE and cached by
  // frozenPSOKey(frozen, pf) MIXED with the vs/fs pair (a different shader = a different PSO even for the
  // same blend tuple). Two items with the same frozen tuple + same kind → the SAME cached PSO. UNstamped
  // items NEVER touch this — they keep the legacy lazy per-kind PSOs above (byte-identical, press-pass).
  std::unordered_map<uint64_t, MTL::RenderPipelineState*> frozenCache;
  // pickPSO: the SINGLE PSO selector every draw case rides. STAMPED item → a frozen-blend PSO from the cache
  // (keyed by frozenPSOKey(frozen,pf) folded with the vs/fs pair, materialized once via applyFrozenBlend).
  // UNstamped item → the legacy lazy per-kind PSO in *slot (built once with the hardcoded blend/mode →
  // byte-identical to the pre-Seam-2 path). Returns nullptr if the shader pair is missing (case breaks).
  auto pickPSO = [&](const RenderDrawItem& it, MTL::RenderPipelineState** slot, const char* vs, const char* fs,
                     bool blend, const BlendMode* mode) -> MTL::RenderPipelineState* {
    if (it.hasRenderState) {
      uint64_t key = frozenPSOKey(it.frozen, (uint32_t)pf);
      for (const char* p = vs; *p; ++p) key = (key ^ (uint8_t)*p) * 1099511628211ull;  // fold vs name
      for (const char* p = fs; *p; ++p) key = (key ^ (uint8_t)*p) * 1099511628211ull;  // fold fs name
      auto found = frozenCache.find(key);
      if (found != frozenCache.end()) return found->second;
      MTL::RenderPipelineState* pso = makeDrawPSO(c.dev, c.lib, vs, fs, pf, false, nullptr, &it.frozen);
      frozenCache.emplace(key, pso);
      return pso;
    }
    if (!*slot) *slot = makeDrawPSO(c.dev, c.lib, vs, fs, pf, blend, mode);  // legacy lazy per-kind
    return *slot;
  };
  // A stamped item's DEPTH-STENCIL (compare+write from it.frozen) is built per stamped-item and released at
  // pass end (small count; the frozen depth-stencils accumulate in this vector). Unstamped → dsDisabled/dsMesh.
  std::vector<MTL::DepthStencilState*> frozenDSPool;

  // ── Depth seam (TiXL DrawMeshUnlit DepthStencilState) ──────────────────────────────────────────
  // Alloc a Depth32Float depth texture (same W×H, private, render-target) + attach (clear 1.0=far, DontCare
  // store — consumed within the pass). clip-z is already D3D [0,1] (field_camera.h perspectiveFovRH M33=
  // far/(near-far)) → matches Metal [0,1] depth + LessEqual, NO remap. Single-sample. Makes the FIRST 3D mesh
  // occlude (Tooth B); 2D composites draw depth-DISABLED (Always, write off) → byte-identical (depth inert).
  MTL::Texture* depthTex = nullptr;
  {
    MTL::TextureDescriptor* dtd = MTL::TextureDescriptor::texture2DDescriptor(
        MTL::PixelFormatDepth32Float, c.output->width(), c.output->height(), false);
    dtd->setStorageMode(MTL::StorageModePrivate);
    dtd->setUsage(MTL::TextureUsageRenderTarget);
    depthTex = c.dev->newTexture(dtd);
  }
  // Two depth-stencil states: dsMesh (DrawKind::Mesh, TiXL LessEqual + ZWrite=true + ZTest=true) and
  // dsDisabled (every 2D kind: compare Always + write off → the depth attachment never affects them).
  MTL::DepthStencilState* dsMesh = nullptr;
  MTL::DepthStencilState* dsDisabled = nullptr;
  {
    MTL::DepthStencilDescriptor* dsd = MTL::DepthStencilDescriptor::alloc()->init();
    dsd->setDepthCompareFunction(MTL::CompareFunctionLessEqual);
    dsd->setDepthWriteEnabled(true);
    dsMesh = c.dev->newDepthStencilState(dsd);
    dsd->release();
  }
  {
    MTL::DepthStencilDescriptor* dsd = MTL::DepthStencilDescriptor::alloc()->init();
    dsd->setDepthCompareFunction(MTL::CompareFunctionAlways);
    dsd->setDepthWriteEnabled(false);
    dsDisabled = c.dev->newDepthStencilState(dsd);
    dsd->release();
  }
  MTL::RenderPipelineState* psoMesh = nullptr;  // lazily built in the Mesh case

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  // BASE clear: Output-window view bg (TiXL CommandOutputUi.Recompute:63-67, clears BackgroundColor before the chain) when engaged, else black (byte-id); a RenderTarget's own ClearColor param then wins via cookVecN.
  float cc[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  if (const float* bg = commandViewBackground()) { cc[0]=bg[0]; cc[1]=bg[1]; cc[2]=bg[2]; cc[3]=bg[3]; }
  cookVecN(c, "ClearColor", cc, 4, cc);
  // Chain-clear (TiXL ClearRenderTarget): if the FIRST chain item is a Clear directive, its color
  // overrides the RenderTarget node's own ClearColor — faithful + free (the pass already clears
  // once via LoadActionClear). Non-first Clears (mid-chain re-clear) are skipped in the draw loop.
  if (c.command && !c.command->items.empty() && c.command->items.front().kind == DrawKind::Clear) {
    const float* clr = c.command->items.front().color;
    cc[0] = clr[0]; cc[1] = clr[1]; cc[2] = clr[2]; cc[3] = clr[3];
  }
  ca->setClearColor(MTL::ClearColor::Make(cc[0], cc[1], cc[2], cc[3]));
  ca->setStoreAction(MTL::StoreActionStore);
  // Attach the depth buffer: clear to 1.0 (far), DontCare store (consumed within the pass only).
  auto* da = pass->depthAttachment();
  da->setTexture(depthTex);
  da->setLoadAction(MTL::LoadActionClear);
  da->setClearDepth(1.0);
  da->setStoreAction(MTL::StoreActionDontCare);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  // Default every draw to depth-DISABLED (the 2D composites). The Mesh case flips to dsMesh just for
  // its draw, then restores dsDisabled — so a 2D item after a mesh stays unaffected (byte-identical 2D).
  enc->setDepthStencilState(dsDisabled);
  if (c.command) {
    for (const RenderDrawItem& it : c.command->items) {
      if (it.kind == DrawKind::Clear) continue;  // not a draw — handled by the pass clear color above
      // Point-based kinds need a non-empty bag; ScreenQuad/Layer2d draw from a texture, Mesh from its
      // own vertex+index buffers (none read `points`) — exempt all three from the point-bag guard.
      if (it.kind != DrawKind::ScreenQuad && it.kind != DrawKind::Layer2d &&
          it.kind != DrawKind::Mesh && (!it.points || it.count == 0))
        continue;
      applyFrozenRasterEncoderState(enc, it);  // Seam 2: cull/winding/depthBias (no-op default when unstamped)
      // Seam 2 OutputMerger DEPTH: a STAMPED non-mesh item sets its frozen compare+write (pooled per item); an
      // UNstamped one re-asserts dsDisabled (no stale depth-stencil leaks to the next). The Mesh case is EXEMPT:
      // it hardcodes dsMesh+CCW+CullBack below, never reads it.frozen (census OutputMerger = 2D composite only).
      if (it.kind != DrawKind::Mesh) {
        if (it.hasRenderState) {
          MTL::DepthStencilState* fds = makeFrozenDepthStencilState(c.dev, it.frozen);
          frozenDSPool.push_back(fds);
          enc->setDepthStencilState(fds);
        } else {
          enc->setDepthStencilState(dsDisabled);
        }
      }
      switch (it.kind) {
        case DrawKind::Points: {
          MTL::RenderPipelineState* use =
              pickPSO(it, &psoPoints, "draw_points_vs", "draw_points_fs", false, nullptr);
          if (!use) break;
          enc->setRenderPipelineState(use);
          enc->setVertexBuffer(const_cast<MTL::Buffer*>(it.points), 0, DRAW_Points);
          float viewExtent = it.viewExtent;
          enc->setVertexBytes(&viewExtent, sizeof(float), DRAW_ViewExtent);
          enc->drawPrimitives(MTL::PrimitiveTypePoint, NS::UInteger(0), NS::UInteger(it.count));
          break;
        }
        case DrawKind::Points2: {
          // DrawPoints2 (TiXL DrawPoints2 → DrawPoints.hlsl Radius variant): a screen-facing quad
          // sprite per Point sized by `size` (= Radius*10.8) and optionally scaled by Point.W (FX1).
          // Its own shader/PSO — DrawKind::Points (v1) is untouched. Blends (alpha-over) like billboards.
          MTL::RenderPipelineState* use =
              pickPSO(it, &psoPoints2, "draw_points2_vs", "draw_points2_fs", true, nullptr);
          if (!use) break;
          enc->setRenderPipelineState(use);
          enc->setVertexBuffer(const_cast<MTL::Buffer*>(it.points), 0, DRAWPOINT2_Points);
          DrawPoint2Params pp{};
          pp.color[0] = it.color[0]; pp.color[1] = it.color[1];
          pp.color[2] = it.color[2]; pp.color[3] = it.color[3];
          pp.pointSize = it.size;          // host has already applied Radius*10.8 into `size`
          pp.viewExtent = it.viewExtent;
          pp.useWForSize = it.useWForSize ? 1u : 0u;
          enc->setVertexBytes(&pp, sizeof(pp), DRAWPOINT2_Params);
          // N points × 6 verts (screen-facing quad).
          enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0),
                              NS::UInteger(it.count * 6));
          break;
        }
        case DrawKind::Lines: {
          if (it.count < 2) break;  // need ≥2 points to form one segment
          MTL::RenderPipelineState* use =
              pickPSO(it, &psoLines, "draw_lines_vs", "draw_lines_fs", true, nullptr);
          if (!use) break;
          enc->setRenderPipelineState(use);
          enc->setVertexBuffer(const_cast<MTL::Buffer*>(it.points), 0, DRAWLINE_Points);
          DrawLineParams lp{};
          lp.color[0] = it.color[0]; lp.color[1] = it.color[1];
          lp.color[2] = it.color[2]; lp.color[3] = it.color[3];
          lp.lineWidth = it.lineWidth;
          lp.viewExtent = it.viewExtent;
          lp.closed = it.lineClosed ? 1u : 0u;
          // DrawClosedLines: resolve TiXL's PointsPerShape default 0 ("one shape over all points")
          // to the concrete bag count so the shader's wrap modulo is always >0. Open DrawLines
          // leaves both 0 → the wrap branch is unreached (byte-identical).
          lp.pointsPerShape = it.lineClosed
              ? (it.pointsPerShape > 0 ? it.pointsPerShape : it.count)
              : 0u;
          enc->setVertexBytes(&lp, sizeof(lp), DRAWLINE_Params);
          // Segment count: OPEN draws (count-1) segments (sequential adjacency, TiXL DrawLines).
          // CLOSED draws one segment PER point — the extra wrap segment closes each shape (last→first),
          // matching DrawClosedLines' GetWrappedIndex. With pointsPerShape>0 a PARTIAL trailing shape
          // is discarded (TiXL DrawLinesAlt actualSegmentCount = numShapes*pointsPerShape) so we never
          // emit a malformed wrap over a half-shape. Each segment = 6 verts (screen-space quad).
          uint32_t segs;
          if (!it.lineClosed) {
            segs = it.count - 1;
          } else if (it.pointsPerShape > 0) {
            uint32_t numShapes = it.count / it.pointsPerShape;     // complete shapes only
            segs = numShapes * it.pointsPerShape;                  // one segment per point in them
          } else {
            segs = it.count;                                       // one shape, all points wrap
          }
          if (segs == 0) break;
          enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0),
                              NS::UInteger((size_t)segs * 6));
          break;
        }
        case DrawKind::LinesBuildup: {
          // DrawLinesBuildup (TiXL DrawLinesBuildup → DrawLinesBuildup.hlsl): an open polyline like
          // DrawLines (count-1 segments) with a per-fragment W-reveal (transitionProgress sweeps the
          // visible window). Its own shader/PSO — DrawKind::Lines is untouched. Blends (alpha-over) so
          // the reveal ramp fades in/out. The FS reads the same params cbuffer (reveal math).
          if (it.count < 2) break;  // need ≥2 points to form one segment
          MTL::RenderPipelineState* use = pickPSO(it, &psoLinesBuildup, "draw_lines_buildup_vs",
                                                  "draw_lines_buildup_fs", true, nullptr);
          if (!use) break;
          enc->setRenderPipelineState(use);
          enc->setVertexBuffer(const_cast<MTL::Buffer*>(it.points), 0, DRAWLINEBU_Points);
          DrawLineBuildupParams bp{};
          bp.color[0] = it.color[0]; bp.color[1] = it.color[1];
          bp.color[2] = it.color[2]; bp.color[3] = it.color[3];
          bp.lineWidth = it.lineWidth;
          bp.viewExtent = it.viewExtent;
          bp.transitionProgress = it.transitionProgress;
          bp.visibleRange = it.visibleRange;
          enc->setVertexBytes(&bp, sizeof(bp), DRAWLINEBU_Params);
          enc->setFragmentBytes(&bp, sizeof(bp), DRAWLINEBU_Params);
          // OPEN polyline: (count-1) segments × 6 verts (sequential adjacency, TiXL DrawLines topology).
          enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0),
                              NS::UInteger((size_t)(it.count - 1) * 6));
          break;
        }
        case DrawKind::Billboards: {
          MTL::RenderPipelineState* use =
              pickPSO(it, &psoBb, "draw_billboards_vs", "draw_billboards_fs", true, nullptr);
          if (!use) break;
          enc->setRenderPipelineState(use);
          enc->setVertexBuffer(const_cast<MTL::Buffer*>(it.points), 0, DRAWBB_Points);
          DrawBillboardParams bp{};
          bp.color[0] = it.color[0]; bp.color[1] = it.color[1];
          bp.color[2] = it.color[2]; bp.color[3] = it.color[3];
          bp.size = it.size;
          bp.viewExtent = it.viewExtent;
          enc->setVertexBytes(&bp, sizeof(bp), DRAWBB_Params);
          // N points × 6 verts (camera-facing quad → here screen-facing).
          enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0),
                              NS::UInteger(it.count * 6));
          break;
        }
        case DrawKind::ScreenQuad: {
          // Textured fullscreen quad (TiXL DrawScreenQuad). Unwired texture → defined no-op (skip
          // the draw; the cleared background shows through), NOT a crash — DrawScreenQuad.t3's
          // UseFallbackTexture posture, fork-resolved as "empty result".
          if (!it.srcTexture) break;
          int bmi = (it.blendMode == BlendMode::Additive) ? 1 : 0;
          BlendMode m = it.blendMode;
          MTL::RenderPipelineState* use =
              pickPSO(it, &psoSQ[bmi], "draw_screenquad_vs", "draw_screenquad_fs", false, &m);
          if (!use) break;
          if (!sqSampler) {
            // TiXL DrawScreenQuad.t3 instantiates its OWN SamplerState (child 810afc82) with
            // Filter=MinMagMipLinear + Address Clamp, and routes the op's Filter input (default
            // MinMagMipLinear) into it. Faithful default = bilinear: Linear min/mag/mip + Clamp.
            // (This is the DrawScreenQuad sampler ONLY; point/line/billboard/image-filter paths
            // keep their own samplers — do not unify here.)
            MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
            sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
            sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
            sd->setMipFilter(MTL::SamplerMipFilterLinear);
            sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
            sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
            sqSampler = c.dev->newSamplerState(sd);
            sd->release();
          }
          enc->setRenderPipelineState(use);
          DrawScreenQuadParams P{};
          P.color[0] = it.color[0]; P.color[1] = it.color[1];
          P.color[2] = it.color[2]; P.color[3] = it.color[3];
          P.position[0] = it.position[0]; P.position[1] = it.position[1];
          P.width = it.width; P.height = it.height;
          // TiXL HDR clamp constant float4(1000,1000,1000,1): RGB headroom, alpha capped at 1.
          // The item carries it so a clamp golden can move the ceiling to exercise the shader.
          P.clampMax[0] = it.clampMax[0]; P.clampMax[1] = it.clampMax[1];
          P.clampMax[2] = it.clampMax[2]; P.clampMax[3] = it.clampMax[3];
          enc->setVertexBytes(&P, sizeof(P), DRAWSQ_Params);
          enc->setFragmentBytes(&P, sizeof(P), DRAWSQ_Params);
          enc->setFragmentTexture(const_cast<MTL::Texture*>(it.srcTexture), 0);
          enc->setFragmentSamplerState(sqSampler, 0);
          enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(6));
          break;
        }
        case DrawKind::Layer2d: {
          // TiXL Layer2d → draw-Quad-vs.hlsl: a textured quad PROJECTED by ObjectToClipSpace. Same
          // unwired-texture posture as ScreenQuad (skip → cleared background shows through).
          if (!it.srcTexture) break;
          int bmi = (it.blendMode == BlendMode::Additive) ? 1 : 0;
          BlendMode m = it.blendMode;
          // F2: the xf VS + the SHARED ScreenQuad FS (psMain byte-identical to DrawScreenQuad).
          MTL::RenderPipelineState* use =
              pickPSO(it, &psoL2[bmi], "draw_quad_xf_vs", "draw_screenquad_fs", false, &m);
          if (!use) break;
          if (!sqSampler) {  // reuse the ScreenQuad sampler (TiXL Layer2d Filter default = Linear+Clamp)
            MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
            sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
            sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
            sd->setMipFilter(MTL::SamplerMipFilterLinear);
            sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
            sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
            sqSampler = c.dev->newSamplerState(sd);
            sd->release();
          }
          enc->setRenderPipelineState(use);
          DrawQuadXfParams P{};
          P.color[0] = it.color[0]; P.color[1] = it.color[1];
          P.color[2] = it.color[2]; P.color[3] = it.color[3];
          P.position[0] = it.position[0]; P.position[1] = it.position[1];
          P.width = it.width; P.height = it.height;
          P.clampMax[0] = it.clampMax[0]; P.clampMax[1] = it.clampMax[1];
          P.clampMax[2] = it.clampMax[2]; P.clampMax[3] = it.clampMax[3];
          // F1: the EXECUTOR finishes ObjectToClipSpace with this output's default camera (the
          // resolution-pin aspect). TransformBufferLayout.cs:13-16 order: o2w·worldToCamera·cameraToClipSpace.
          // Cut 2: ObjectToWorld is the SRT stack (TiXL _ProcessLayer2d) composed HERE — the ScaleMode
          // aspect coupling needs viewAspect (camera, executor-local) AND imageAspect (srcTexture).
          // Cut 3: if this item was stamped by a Camera op (it.hasCamera), build ITS WorldToCamera/
          // CameraToClipSpace from the raw params (TiXL Camera.cs BuildProjectionMatrices, v1 scope)
          // instead of the default — reproducing the push/pop context. Aspect: camAspect>0 uses it,
          // else this output's aspect (Camera.cs:53-55 RequestedResolution fallback). Both the SRT
          // viewAspect AND the projection use this camera (faithful — _ProcessLayer2d reads context's
          // CameraToClipSpace, which the Camera op set).
          LayerCameraForward cam = camFwd;
          if (it.hasCamera) {
            float ar = (it.camAspect > 0.0001f) ? it.camAspect : aspectF;  // Camera.cs:53-55 fallback
            cam = stampedCameraForward(it.camEye, it.camTarget, it.camUp, it.camOrtho, it.camFovDeg,
                                       it.camOrthoScale, it.camOrthoStretch, ar, it.camNear, it.camFar);
          }
          Mat4 objectToWorld{};
          if (it.layer2dComposeSRT) {
            // viewAspect = CameraToClipSpace.M22/M11 (_ProcessLayer2d.cs:37). imageAspect = srcW/srcH.
            float viewAspect = viewAspectFromClip(cam.cameraToClipSpace);
            float imgW = (float)it.srcTexture->width(), imgH = (float)it.srcTexture->height();
            float imageAspect = (imgH > 0.0f) ? imgW / imgH : 1.0f;
            // scale = Scale * Stretch (cs:40), then ScaleMode adjusts scale.X/Y (cs:49-101).
            float scaleX = it.layerScale * it.layerStretch[0];
            float scaleY = it.layerScale * it.layerStretch[1];
            layer2dScaleModeApply((Layer2dScaleMode)it.layerScaleMode, imageAspect, viewAspect, scaleX,
                                  scaleY);
            objectToWorld = layer2dObjectToWorld(scaleX, scaleY, it.layerRotateDeg, it.position[0],
                                                 it.position[1], it.layerPosZ);
          } else {
            // Legacy path: the item carries ObjectToWorld verbatim (Cut-1 seam-tooth driving a
            // hand-built matrix). Kept so the seam-presence golden can drive an arbitrary matrix.
            for (int i = 0; i < 16; ++i) objectToWorld.m[i] = it.objectToClipSpace[i];
          }
          // S2b GROUP SRT: a parent Group op stamped its accumulated transform onto this item (TiXL
          // Group.cs context.ObjectToWorld = Multiply(groupSRT, prev)). Right-multiply it (row-vector v·M
          // → the group is the PARENT applied AFTER the child's own O2W = child·group). Identity when no
          // Group → byte-identical to the pre-S2b path.
          if (it.hasGroup) {
            Mat4 grp{}; for (int i = 0; i < 16; ++i) grp.m[i] = it.groupObjectToWorld[i];
            objectToWorld = mat4Mul(objectToWorld, grp);
          }
          Mat4 o2c = objectToClipSpace(objectToWorld, cam.worldToCamera, cam.cameraToClipSpace);
          for (int i = 0; i < 16; ++i) P.objectToClipSpace[i] = o2c.m[i];
          P.applyTransform = it.applyTransform ? 1u : 0u;  // drop-mul golden tooth
          enc->setVertexBytes(&P, sizeof(P), DRAWQUADXF_Params);
          enc->setFragmentBytes(&P, sizeof(P), DRAWQUADXF_Params);
          enc->setFragmentTexture(const_cast<MTL::Texture*>(it.srcTexture), 0);
          enc->setFragmentSamplerState(sqSampler, 0);
          enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(6));
          break;
        }
        case DrawKind::Mesh: {
          // The FIRST 3D mesh (TiXL DrawMeshUnlit → mesh-DrawUnlit.hlsl). SV_VertexID-driven triangle
          // list reading the cooked SwVertex + SwTriIndex buffers; NO Metal index buffer (the VS reads
          // FaceIndices itself). Borrowed buffers (null → nothing to draw). Depth-TESTED: LessEqual +
          // ZWrite (dsMesh) + FrontCounterClockwise (CCW front) + Cull Back (TiXL Rasterizer 6e672779).
          if (!it.meshVtx || !it.meshIdx || it.meshIndexCount == 0) break;
          if (!psoMesh)
            psoMesh = makeDrawPSO(c.dev, c.lib, "mesh_draw_unlit_vs", "mesh_draw_unlit_fs", pf, false);
          if (!psoMesh) break;
          enc->setRenderPipelineState(psoMesh);
          // Compose ObjectToClipSpace EXACTLY like Layer2d (object→world = identity for a mesh; the SRT
          // stack belongs to a parent Transform op, deferred), with the default OR the stamped camera.
          LayerCameraForward cam = camFwd;
          if (it.hasCamera) {
            float ar = (it.camAspect > 0.0001f) ? it.camAspect : aspectF;  // Camera.cs:53-55 fallback
            cam = stampedCameraForward(it.camEye, it.camTarget, it.camUp, it.camOrtho, it.camFovDeg,
                                       it.camOrthoScale, it.camOrthoStretch, ar, it.camNear, it.camFar);
          }
          // S2b GROUP SRT: same per-item group push as Layer2d (a mesh's own O2W is identity here — the
          // SRT belongs to a parent Transform op, deferred — so the group IS its ObjectToWorld). Identity
          // when no Group → byte-identical.
          Mat4 meshO2W = mat4Identity();
          if (it.hasGroup) { for (int i = 0; i < 16; ++i) meshO2W.m[i] = it.groupObjectToWorld[i]; }
          Mat4 o2c = objectToClipSpace(meshO2W, cam.worldToCamera, cam.cameraToClipSpace);
          MeshDrawParams M{};
          M.color[0] = it.color[0]; M.color[1] = it.color[1];
          M.color[2] = it.color[2]; M.color[3] = it.color[3];
          for (int i = 0; i < 16; ++i) M.objectToClipSpace[i] = o2c.m[i];
          M.applyTransform = it.applyTransform ? 1u : 0u;  // drop-mul golden tooth (Tooth A mis-project)
          enc->setVertexBuffer(const_cast<MTL::Buffer*>(it.meshVtx), 0, MESH_PbrVertices);
          enc->setVertexBuffer(const_cast<MTL::Buffer*>(it.meshIdx), 0, MESH_FaceIndices);
          enc->setVertexBytes(&M, sizeof(M), MESH_Params);
          enc->setFragmentBytes(&M, sizeof(M), MESH_Params);
          // Production: dsMesh (LessEqual + ZWrite). Tooth-B bug hook: dsDisabled → no occlusion, draw
          // order decides (the depth tooth bites). The hook is CPU-side (never a shader branch).
          enc->setDepthStencilState(meshDepthDisableForTest() ? dsDisabled : dsMesh);
          enc->setFrontFacingWinding(MTL::WindingCounterClockwise);  // TiXL FrontCounterClockwise=true
          enc->setCullMode(MTL::CullModeBack);                       // TiXL Cull Back
          // 3 verts/face, SV_VertexID-driven (TiXL Draw vertexCount = faceCount × MultiplyInt(3)).
          enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0),
                              NS::UInteger((NS::UInteger)it.meshIndexCount * 3));
          // Restore depth-disabled + default winding/cull so a 2D item after a mesh is byte-identical.
          enc->setDepthStencilState(dsDisabled);
          enc->setFrontFacingWinding(MTL::WindingClockwise);
          enc->setCullMode(MTL::CullModeNone);
          break;
        }
        case DrawKind::Clear:
          break;  // already skipped above (handled by the pass clear color); explicit for -Wswitch
      }
    }
  }
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  if (psoPoints) psoPoints->release();
  if (psoPoints2) psoPoints2->release();
  if (psoLines) psoLines->release();
  if (psoLinesBuildup) psoLinesBuildup->release();
  if (psoBb) psoBb->release();
  if (psoSQ[0]) psoSQ[0]->release();
  if (psoSQ[1]) psoSQ[1]->release();
  if (psoL2[0]) psoL2[0]->release();
  if (psoL2[1]) psoL2[1]->release();
  if (psoMesh) psoMesh->release();
  for (auto& kv : frozenCache) if (kv.second) kv.second->release();   // Seam 2: materialized blend-PSOs
  for (MTL::DepthStencilState* ds : frozenDSPool) if (ds) ds->release();  // Seam 2: per-item frozen depth-stencils
  if (sqSampler) sqSampler->release();
  if (dsMesh) dsMesh->release();
  if (dsDisabled) dsDisabled->release();
  if (depthTex) depthTex->release();
}

// Resolution enum (Float param + Widget::Enum): WindowFollow tracks `windowSize`; the
// fixed modes ignore it and pin a standard output size; Custom reads CustomW/H. The map
// overload is the core (flat AND resident drivers pass their resolved params); the Node*
// overload wraps it (a node's stored params ARE a map) for flat callers/selftests.
RenderResolution resolveRenderResolution(const std::map<std::string, float>& params,
                                         RenderResolution windowSize) {
  int mode = (int)std::lround(paramOr(params, "Resolution", 0.0f));
  switch (mode) {
    case 1: return {1280, 720};    // HD720
    case 2: return {1920, 1080};   // HD1080
    case 3: return {3840, 2160};   // UHD4K
    case 4: {                      // Custom
      uint32_t w = (uint32_t)std::lround(std::fmax(1.0f, paramOr(params, "CustomW", 512.0f)));
      uint32_t h = (uint32_t)std::lround(std::fmax(1.0f, paramOr(params, "CustomH", 512.0f)));
      return {w, h};
    }
    default: return windowSize;    // WindowFollow (0)
  }
}
RenderResolution resolveRenderResolution(const Node* n, RenderResolution windowSize) {
  static const std::map<std::string, float> kEmpty;
  return resolveRenderResolution(n ? n->params : kEmpty, windowSize);
}

void registerRenderTargetOp() { registerTexOp("RenderTarget", cookRenderTarget); }

// Batch 1 golden: drive a CPU-filled point bag through a 1-item RenderCommand into a
// RenderTarget texture, readback, assert lit (non-black). Plus the resolution contract:
// HD1080 -> 1920x1080, WindowFollow -> windowSize. injectBug = 0 points -> all black -> FAIL.
int runRenderTargetSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 64, W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-rendertarget] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerRenderTargetOp();

  // CPU-fill a ring of white points inside the view (radius 1.5 < viewExtent 3.5).
  MTL::Buffer* pts = dev->newBuffer((NS::UInteger)N * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  SwPoint* d = (SwPoint*)pts->contents();
  for (uint32_t i = 0; i < N; ++i) {
    d[i] = SwPoint{};
    float a = 6.2831853f * (float)i / (float)N;
    d[i].Position = {1.5f * std::cos(a), 1.5f * std::sin(a), 0.0f};
    d[i].Color = {1.0f, 1.0f, 1.0f, 1.0f};
    d[i].Scale = {1.0f, 1.0f, 1.0f};
  }

  // Output texture (256² for a cheap readback; resolution contract is checked separately).
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* tex = dev->newTexture(td);

  RenderCommand rc;
  rc.items.push_back(RenderDrawItem{pts, injectBug ? 0u : N, 3.5f});

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.command = &rc; c.output = tex;
  cookRenderTarget(c);

  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  int nonBlack = 0;
  for (size_t i = 0; i < (size_t)W * H; ++i)
    if (px[i * 4] > 30 || px[i * 4 + 1] > 30 || px[i * 4 + 2] > 30) ++nonBlack;

  // Resolution contract (pure function, no giant texture needed).
  Node rt; rt.id = 2; rt.type = "RenderTarget"; rt.params["Resolution"] = 2.0f;  // HD1080
  RenderResolution win{800, 600};
  RenderResolution hd = resolveRenderResolution(&rt, win);
  Node rtw; rtw.id = 3; rtw.type = "RenderTarget"; rtw.params["Resolution"] = 0.0f;  // WindowFollow
  RenderResolution wf = resolveRenderResolution(&rtw, win);
  bool resOK = hd.w == 1920 && hd.h == 1080 && wf.w == 800 && wf.h == 600;

  bool pass = nonBlack > 50 && resOK;
  printf("[selftest-rendertarget] nonBlack=%d(need>50) hd=%ux%u wf=%ux%u resOK=%d -> %s\n",
         nonBlack, hd.w, hd.h, wf.w, wf.h, resOK ? 1 : 0, pass ? "PASS" : "FAIL");

  pts->release(); tex->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// Batch 3 golden (the WIRED three-flow): build RadialPoints -> DrawPoints(Command) ->
// RenderTarget(Custom 256x256) and cook it THROUGH PointGraph as the terminal. Proves: (1) the
// tex node wins defaultDrawTarget, (2) cook sizes RenderTarget's own texture to the Resolution
// pin (256x256, not the 64x64 window), (3) the DrawPoints->RenderTarget Command wire threads the
// bag into a lit image. injectBug omits the Command connection -> empty chain -> black -> FAIL.
int runRenderTargetWiredSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 128, RW = 256, RH = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-rendertargetwired] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // RadialPoints(cook) + DrawPoints(cmd) + RenderTarget(tex) + ...

  PointGraph pg(dev, lib, q, 64, 64);  // window 64x64; the RenderTarget pins its own 256x256
  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = (float)N; gen.params["Radius"] = 2.0f; g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  Node rt; rt.id = 3; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f;  // Custom
  rt.params["CustomW"] = (float)RW; rt.params["CustomH"] = (float)RH; g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points -> DrawPoints.points
  if (!injectBug)
    g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // DrawPoints.out(Command) -> RenderTarget.command

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  int term = pg.defaultDrawTarget(g);  // tex preferred -> the RenderTarget node (id 3)
  pg.cook(g, ctx, nullptr, term);

  MTL::Texture* tex = pg.target();
  bool sized = tex && (uint32_t)tex->width() == RW && (uint32_t)tex->height() == RH;
  int nonBlack = 0;
  if (sized) {
    std::vector<uint8_t> px((size_t)RW * RH * 4, 0);
    tex->getBytes(px.data(), RW * 4, MTL::Region::Make2D(0, 0, RW, RH), 0);
    for (size_t i = 0; i < (size_t)RW * RH; ++i)
      if (px[i * 4] > 30 || px[i * 4 + 1] > 30 || px[i * 4 + 2] > 30) ++nonBlack;
  }
  bool pass = term == 3 && sized && nonBlack > 50;
  printf("[selftest-rendertargetwired] term=%d(want 3) size=%lux%lu(want %ux%u) nonBlack=%d(need>50) -> %s\n",
         term, tex ? tex->width() : 0, tex ? tex->height() : 0, RW, RH, nonBlack,
         pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
