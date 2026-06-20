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
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/draw_params.h"      // DrawLineParams/DrawBillboardParams/DrawScreenQuadParams + bindings
#include "runtime/field_camera.h"     // defaultLayerCameraForward / objectToClipSpace (Layer2d seam, F1)
#include "runtime/graph.h"            // Graph/Node
#include "runtime/particle_params.h"  // DRAW_Points, DRAW_ViewExtent
#include "runtime/point_graph.h"      // TexCookCtx, RenderResolution, registerTexOp
#include "runtime/render_command.h"   // RenderCommand / RenderDrawItem / DrawKind
#include "runtime/tixl_point.h"       // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

float paramOr(const std::map<std::string, float>& params, const char* id, float def) {
  auto it = params.find(id);
  return it != params.end() ? it->second : def;
}

// Build a render PSO for a vs/fs function pair into `pixelFormat`. `blend` turns on standard
// premultiplied-style src-alpha blending (lines/billboards composite over what's drawn before);
// DrawPoints stays opaque (blend=false), matching its pre-batch-13 behavior. When `mode` is
// non-null it overrides `blend` and selects the EXACT TiXL BlendMode factor table (Normal/Add,
// from Core/Rendering/DefaultRenderingStates.cs) — used by DrawScreenQuad. Returns nullptr (and
// the caller skips that kind) if either function is missing.
MTL::RenderPipelineState* makeDrawPSO(MTL::Device* dev, MTL::Library* lib, const char* vsName,
                                      const char* fsName, MTL::PixelFormat pf, bool blend,
                                      const BlendMode* mode = nullptr) {
  MTL::Function* vs = lib->newFunction(NS::String::string(vsName, NS::UTF8StringEncoding));
  MTL::Function* fs = lib->newFunction(NS::String::string(fsName, NS::UTF8StringEncoding));
  MTL::RenderPipelineState* rps = nullptr;
  if (vs && fs) {
    MTL::RenderPipelineDescriptor* rpd = MTL::RenderPipelineDescriptor::alloc()->init();
    rpd->setVertexFunction(vs);
    rpd->setFragmentFunction(fs);
    auto* att = rpd->colorAttachments()->object(0);
    att->setPixelFormat(pf);
    if (mode) {
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
// + primitive type. PSOs are built lazily per kind per call (only the kinds actually present) — the
// live loop's per-frame caching is a follow-up (same posture as batch 1's per-call note).
// NOT file-local (out of the anon namespace) so the draw-op leaf selftests can drive a chain
// straight through it (point_ops_drawlines.cpp / point_ops_drawbillboards.cpp).
void cookRenderTarget(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat pf = c.output->pixelFormat();
  MTL::RenderPipelineState* psoPoints = nullptr;
  MTL::RenderPipelineState* psoLines = nullptr;
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

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  float cc[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // ClearColor param (Vec4); default black, opaque.
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
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  if (c.command) {
    for (const RenderDrawItem& it : c.command->items) {
      if (it.kind == DrawKind::Clear) continue;  // not a draw — handled by the pass clear color above
      // Point-based kinds need a non-empty bag; ScreenQuad/Layer2d draw from a texture (no buffer).
      if (it.kind != DrawKind::ScreenQuad && it.kind != DrawKind::Layer2d &&
          (!it.points || it.count == 0))
        continue;
      switch (it.kind) {
        case DrawKind::Points: {
          if (!psoPoints)
            psoPoints = makeDrawPSO(c.dev, c.lib, "draw_points_vs", "draw_points_fs", pf, false);
          if (!psoPoints) break;
          enc->setRenderPipelineState(psoPoints);
          enc->setVertexBuffer(const_cast<MTL::Buffer*>(it.points), 0, DRAW_Points);
          float viewExtent = it.viewExtent;
          enc->setVertexBytes(&viewExtent, sizeof(float), DRAW_ViewExtent);
          enc->drawPrimitives(MTL::PrimitiveTypePoint, NS::UInteger(0), NS::UInteger(it.count));
          break;
        }
        case DrawKind::Lines: {
          if (it.count < 2) break;  // need ≥2 points to form one segment
          if (!psoLines)
            psoLines = makeDrawPSO(c.dev, c.lib, "draw_lines_vs", "draw_lines_fs", pf, true);
          if (!psoLines) break;
          enc->setRenderPipelineState(psoLines);
          enc->setVertexBuffer(const_cast<MTL::Buffer*>(it.points), 0, DRAWLINE_Points);
          DrawLineParams lp{};
          lp.color[0] = it.color[0]; lp.color[1] = it.color[1];
          lp.color[2] = it.color[2]; lp.color[3] = it.color[3];
          lp.lineWidth = it.lineWidth;
          lp.viewExtent = it.viewExtent;
          enc->setVertexBytes(&lp, sizeof(lp), DRAWLINE_Params);
          // (count-1) segments × 6 verts (screen-space quad). Sequential adjacency (TiXL).
          enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0),
                              NS::UInteger((it.count - 1) * 6));
          break;
        }
        case DrawKind::Billboards: {
          if (!psoBb)
            psoBb = makeDrawPSO(c.dev, c.lib, "draw_billboards_vs", "draw_billboards_fs", pf, true);
          if (!psoBb) break;
          enc->setRenderPipelineState(psoBb);
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
          if (!psoSQ[bmi]) {
            BlendMode m = it.blendMode;
            psoSQ[bmi] =
                makeDrawPSO(c.dev, c.lib, "draw_screenquad_vs", "draw_screenquad_fs", pf, false, &m);
          }
          if (!psoSQ[bmi]) break;
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
          enc->setRenderPipelineState(psoSQ[bmi]);
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
          if (!psoL2[bmi]) {
            BlendMode m = it.blendMode;
            // F2: the xf VS + the SHARED ScreenQuad FS (psMain byte-identical to DrawScreenQuad).
            psoL2[bmi] =
                makeDrawPSO(c.dev, c.lib, "draw_quad_xf_vs", "draw_screenquad_fs", pf, false, &m);
          }
          if (!psoL2[bmi]) break;
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
          enc->setRenderPipelineState(psoL2[bmi]);
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
            float ar = (it.camAspect > 0.0001f) ? it.camAspect : aspectF;
            cam.worldToCamera = lookAtRH(it.camEye, it.camTarget, it.camUp);
            cam.cameraToClipSpace =
                perspectiveFovRH(it.camFovDeg * 3.14159265358979323846f / 180.0f, ar, it.camNear,
                                 it.camFar);
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
        case DrawKind::Clear:
          break;  // already skipped above (handled by the pass clear color); explicit for -Wswitch
      }
    }
  }
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  if (psoPoints) psoPoints->release();
  if (psoLines) psoLines->release();
  if (psoBb) psoBb->release();
  if (psoSQ[0]) psoSQ[0]->release();
  if (psoSQ[1]) psoSQ[1]->release();
  if (psoL2[0]) psoL2[0]->release();
  if (psoL2[1]) psoL2[1]->release();
  if (sqSampler) sqSampler->release();
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
