// DrawScreenQuad + ClearRenderTarget command ops — TiXL Operators/Lib/render/basic/DrawScreenQuad.cs
// + Operators/Lib/render/_dx11/api/ClearRenderTarget.cs. The dx11-equiv-render-graph seam (block #2):
// rebuild TiXL's render-graph BEHAVIOR on Metal, NOT the DX11 API. TiXL's DrawScreenQuad.t3 expands
// into 19 dx11 sub-ops (RTV/DSV/BlendState/OutputMerger/InputAssembler/...) that all collapse into
// ONE Metal render pass — here a 1-item RenderCommand the executor (cookRenderTarget) rasterizes.
// We do NOT port the sub-ops (the retained-mode Metal executor owns the pass boundaries).
//
//   DrawScreenQuad: Texture2D in → Command out. Emits a DrawKind::ScreenQuad item carrying the
//   borrowed source texture (FORK#1: gathered onto the Command path via CmdCookCtx::inputTexture),
//   the Color tint, Position/Width/Height quad placement, and the BlendMode. The shader
//   (draw_screenquad.metal) is clamp(Color * tex.sample(uv), 0, 1000) — verbatim from
//   vs-draw-viewport-quad.hlsl (NO camera; the ObjectToClipSpace mul is commented out in TiXL).
//
//   ClearRenderTarget: Command out (no input). Emits a DrawKind::Clear directive carrying ClearColor;
//   when it is the FIRST chain item the executor sets the pass clear color from it (faithful + free —
//   the retained-mode pass clears once via LoadActionClear). Proves Command-chain ordering with a 2nd
//   op type (clear → draw). A non-first / mid-chain re-clear (clear-quad) is DEFERRED.
//
// BLEND (this batch): Normal + Additive only — pure Metal blend equations whose factor tables are
// backward-traced verbatim from Core/Rendering/DefaultRenderingStates.cs (confirmed by PickBlendMode.t3).
// Screen/Multiply are NOT a single Metal blend equation (shader-side compositing) → DEFERRED. The
// BlendMode param values >1 (Multiply/Invert/...) fall back to Normal until those land.
#include "runtime/point_ops.h"

#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp, cookParam/cookVecN
#include "runtime/render_command.h"  // RenderCommand / RenderDrawItem / DrawKind / BlendMode

#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"       // Graph/Node, pinId (selftest)
#include "runtime/tixl_point.h"  // pulls in eval_context.h → full EvaluationContext (wired selftest)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Map the TiXL SharedEnums.BlendModes int (Normal=0, Additive=1, Multiply=2, ...) to the subset of
// Metal blend equations this batch ships. Anything beyond Additive is deferred → Normal.
static BlendMode blendModeFromInt(int v) {
  return v == 1 ? BlendMode::Additive : BlendMode::Normal;
}

RenderCommand cookDrawScreenQuad(CmdCookCtx& c) {
  RenderCommand rc;
  // Unwired texture → empty result (no item), NOT a crash (TiXL UseFallbackTexture posture,
  // fork-resolved as "draws nothing"). The cleared RenderTarget background shows through.
  if (!c.inputTexture) return rc;
  RenderDrawItem it{};
  it.kind = DrawKind::ScreenQuad;
  it.srcTexture = c.inputTexture;  // borrowed (PointGraph-owned, single-frame); never retained
  float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  cookVecN(c, "Color", white, 4, it.color);
  float zero2[2] = {0.0f, 0.0f};
  float pos[2] = {0.0f, 0.0f};
  cookVecN(c, "Position", zero2, 2, pos);
  it.position[0] = pos[0]; it.position[1] = pos[1];
  it.width = cookParam(c, "Width", 1.0f);
  it.height = cookParam(c, "Height", 1.0f);
  it.blendMode = blendModeFromInt((int)(cookParam(c, "BlendMode", 0.0f) + 0.5f));
  rc.items.push_back(it);
  return rc;
}

RenderCommand cookClearRenderTarget(CmdCookCtx& c) {
  RenderCommand rc;
  RenderDrawItem it{};
  it.kind = DrawKind::Clear;
  float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // TiXL ClearColor default
  cookVecN(c, "ClearColor", black, 4, it.color);
  rc.items.push_back(it);
  return rc;
}

void registerDrawScreenQuadOps() {
  registerCmdOp("DrawScreenQuad", cookDrawScreenQuad);      // Texture2D → Command (DrawKind::ScreenQuad)
  registerCmdOp("ClearRenderTarget", cookClearRenderTarget);  // Command (DrawKind::Clear)
}

namespace {
// Shared selftest scaffold: device/queue/metallib + a uniform-fill source texture.
struct SqHarness {
  NS::AutoreleasePool* pool = nullptr;
  MTL::Device* dev = nullptr;
  MTL::CommandQueue* q = nullptr;
  MTL::Library* lib = nullptr;
  bool ok = false;
  SqHarness() {
    pool = NS::AutoreleasePool::alloc()->init();
    dev = MTL::CreateSystemDefaultDevice();
    q = dev->newCommandQueue();
    NS::Error* err = nullptr;
    lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
    ok = lib != nullptr;
  }
  ~SqHarness() {
    if (lib) lib->release();
    if (q) q->release();
    if (dev) dev->release();
    if (pool) pool->release();
  }
  // A WxH shaderRead texture filled with a uniform RGBA (0..1).
  MTL::Texture* makeUniform(uint32_t W, uint32_t H, float r, float g, float b, float a) {
    MTL::TextureDescriptor* td =
        MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
    td->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageRenderTarget);
    td->setStorageMode(MTL::StorageModeShared);
    MTL::Texture* t = dev->newTexture(td);
    std::vector<uint8_t> px((size_t)W * H * 4);
    auto q8 = [](float v) { return (uint8_t)(v < 0 ? 0 : v > 1 ? 255 : v * 255.0f + 0.5f); };
    for (size_t i = 0; i < (size_t)W * H; ++i) {
      px[i * 4 + 0] = q8(r); px[i * 4 + 1] = q8(g);
      px[i * 4 + 2] = q8(b); px[i * 4 + 3] = q8(a);
    }
    t->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, px.data(), W * 4);
    return t;
  }
  MTL::Texture* makeTarget(uint32_t W, uint32_t H) {
    MTL::TextureDescriptor* td =
        MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
    td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    td->setStorageMode(MTL::StorageModeShared);
    return dev->newTexture(td);
  }
  // A float32 render target: values >1.0 survive store+readback (unlike RGBA8Unorm which
  // hardware-clamps to 1.0 on store). Used by the clamp golden to prove the SHADER clamp, not
  // the format clamp.
  MTL::Texture* makeFloatTarget(uint32_t W, uint32_t H) {
    MTL::TextureDescriptor* td =
        MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA32Float, W, H, false);
    td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    td->setStorageMode(MTL::StorageModeShared);
    return dev->newTexture(td);
  }
  // A 2x2 RGBA8 texture with four distinct corner texels (for the bilinear/sampler golden). The
  // texels are laid out row-major: (0,0)=t00, (1,0)=t10, (0,1)=t01, (1,1)=t11.
  MTL::Texture* make2x2(const float t00[4], const float t10[4],
                        const float t01[4], const float t11[4]) {
    MTL::TextureDescriptor* td =
        MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 2, 2, false);
    td->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageRenderTarget);
    td->setStorageMode(MTL::StorageModeShared);
    MTL::Texture* t = dev->newTexture(td);
    auto q8 = [](float v) { return (uint8_t)(v < 0 ? 0 : v > 1 ? 255 : v * 255.0f + 0.5f); };
    uint8_t px[16];
    const float* rows[4] = {t00, t10, t01, t11};
    for (int i = 0; i < 4; ++i)
      for (int c = 0; c < 4; ++c) px[i * 4 + c] = q8(rows[i][c]);
    t->replaceRegion(MTL::Region::Make2D(0, 0, 2, 2), 0, px, 2 * 4);
    return t;
  }
};
}  // namespace

// DrawScreenQuad golden: source = uniform gray 0.5, Color tint (2,1,1,1), full-screen quad
// (Width=Height=1, Position=0). The shader is clamp(Color*tex, 0, 1000) → center pixel:
//   R = clamp(2.0 * 0.5) = 1.0 (255),  G/B = clamp(1.0 * 0.5) = 0.5 (~128).
// We DON'T use blend here (single opaque-over-clear quad): the alpha of the quad = Color.a*tex.a =
// 1*1 = 1, Normal blend over the cleared (a=1) background leaves RGB = src. injectBug drops the
// source texture wire → cookDrawScreenQuad emits no item → background black → center stays ~0 → FAIL.
int runDrawScreenQuadSelfTest(bool injectBug) {
  SqHarness h;
  if (!h.ok) { printf("[selftest-drawscreenquad] FAIL: no metallib\n"); return 1; }
  const uint32_t W = 256, H = 256;

  MTL::Texture* src = h.makeUniform(64, 64, 0.5f, 0.5f, 0.5f, 1.0f);
  MTL::Texture* tex = h.makeTarget(W, H);

  RenderCommand rc;
  if (!injectBug) {
    RenderDrawItem it{};
    it.kind = DrawKind::ScreenQuad;
    it.srcTexture = src;
    it.color[0] = 2.0f; it.color[1] = 1.0f; it.color[2] = 1.0f; it.color[3] = 1.0f;
    it.width = 1.0f; it.height = 1.0f;
    it.blendMode = BlendMode::Normal;
    rc.items.push_back(it);
  }  // injectBug: empty chain → cleared black

  TexCookCtx c;
  c.dev = h.dev; c.lib = h.lib; c.queue = h.q;
  c.nodeId = 1; c.command = &rc; c.output = tex;
  cookRenderTarget(c);

  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  size_t ci = ((size_t)(H / 2) * W + (W / 2)) * 4;
  int R = px[ci], G = px[ci + 1], B = px[ci + 2];
  // Closed-form clamp(Color*tex): R saturated to 255 (2*0.5=1.0), G/B ~128 (1*0.5=0.5).
  bool pass = R > 250 && G > 110 && G < 145 && B > 110 && B < 145;
  printf("[selftest-drawscreenquad] center RGB=(%d,%d,%d) want (≈255,≈128,≈128) -> %s\n",
         R, G, B, pass ? "PASS" : "FAIL");

  src->release(); tex->release();
  return pass ? 0 : 1;
}

// DrawScreenQuad CLAMP golden (float RT — the clamp tooth made REAL). The shader is
//   clamp(Color*tex, 0, clampMax) with clampMax = TiXL (1000,1000,1000,1).
// We readback from a float32 target so values >1.0 SURVIVE (an RGBA8Unorm target would hardware-
// clamp every channel to 1.0 on store, making the shader clamp untestable — the original hollow
// tooth). Drive Color*tex to exceed 1.0: Color=(4,4,4,4), tex=0.5 → product 2.0 on all channels.
// Assert: RGB read back ≈2.0 (survives → NOT format-clamped, and the 1000 ceiling didn't cut 2.0)
// AND alpha ≈1.0 (the clampMax.w=1 upper bound caps alpha even though 4*0.5=2.0 would exceed it).
// injectBug corrupts the REAL clampMax the cook path feeds the shader (cap RGB at 1.0 instead of
// 1000): then RGB reads back ≈1.0, not 2.0 → the float-RT assertion FAILS. This bites the shader's
// per-channel clamp, not a flipped expected value.
int runDrawScreenQuadClampSelfTest(bool injectBug) {
  SqHarness h;
  if (!h.ok) { printf("[selftest-drawscreenquad-clamp] FAIL: no metallib\n"); return 1; }
  const uint32_t W = 64, H = 64;

  MTL::Texture* src = h.makeUniform(32, 32, 0.5f, 0.5f, 0.5f, 0.5f);  // tex = 0.5 on all channels
  MTL::Texture* tex = h.makeFloatTarget(W, H);

  RenderCommand rc;
  RenderDrawItem it{};
  it.kind = DrawKind::ScreenQuad;
  it.srcTexture = src;
  it.color[0] = 4.0f; it.color[1] = 4.0f; it.color[2] = 4.0f; it.color[3] = 4.0f;  // 4*0.5 = 2.0
  it.width = 1.0f; it.height = 1.0f;
  it.blendMode = BlendMode::Normal;
  // Faithful clamp ceiling = TiXL (1000,1000,1000,1). injectBug caps RGB at 1.0 (a wrong upper
  // bound) — the corrupted bound flows through the cook path into the real shader clamp.
  if (injectBug) {
    it.clampMax[0] = 1.0f; it.clampMax[1] = 1.0f; it.clampMax[2] = 1.0f; it.clampMax[3] = 1.0f;
  } else {
    it.clampMax[0] = 1000.0f; it.clampMax[1] = 1000.0f; it.clampMax[2] = 1000.0f; it.clampMax[3] = 1.0f;
  }
  rc.items.push_back(it);

  TexCookCtx c;
  c.dev = h.dev; c.lib = h.lib; c.queue = h.q;
  c.nodeId = 1; c.command = &rc; c.output = tex;
  cookRenderTarget(c);

  std::vector<float> px((size_t)W * H * 4, 0.0f);
  tex->getBytes(px.data(), W * 4 * sizeof(float), MTL::Region::Make2D(0, 0, W, H), 0);
  size_t ci = ((size_t)(H / 2) * W + (W / 2)) * 4;
  float R = px[ci], G = px[ci + 1], B = px[ci + 2], A = px[ci + 3];
  // Faithful: RGB ≈ 2.0 (survives float RT, under the 1000 ceiling), A ≈ 1.0 (capped by clampMax.w).
  // injectBug (RGB ceiling=1.0): RGB ≈ 1.0 → the RGB>1.5 probe fails.
  bool rgbSurvives = R > 1.5f && R < 2.5f && G > 1.5f && G < 2.5f && B > 1.5f && B < 2.5f;
  bool alphaCapped = A > 0.95f && A < 1.05f;  // 4*0.5=2.0 would exceed → proves the .w=1 cap bit
  bool pass = rgbSurvives && alphaCapped;
  printf("[selftest-drawscreenquad-clamp] RGBA=(%.3f,%.3f,%.3f,%.3f) want RGB≈2.0 (survives float RT, "
         "<1000) A≈1.0 (capped) -> %s\n", R, G, B, A, pass ? "PASS" : "FAIL");

  src->release(); tex->release();
  return pass ? 0 : 1;
}

// DrawScreenQuad FILTER golden (the sampler-Linear tooth). A 2x2 texture with four DISTINCT texels
// drawn full-quad: the quad center samples uv=(0.5,0.5), the exact meeting point of all four texels.
// With bilinear (Linear) the center = the average of the four corners; with Nearest it snaps to ONE
// texel (an integer corner value Linear's average can't equal). We pick texels whose average is a
// value no single texel holds → Nearest cannot produce it.
//   t00=(0.2,..), t10=(0.4,..), t01=(0.6,..), t11=(0.8,..) → bilinear center = (0.2+0.4+0.6+0.8)/4 = 0.5.
// Color=(1,1,1,1) (no tint). Assert center ≈0.5 (bilinear). Under Nearest it would be one of
// {0.2,0.4,0.6,0.8} → all outside the 0.5 band → FAIL. This makes the sampler-Linear fix load-bearing:
// flipping the ScreenQuad sampler back to Nearest makes this leg fail.
// injectBug: pick a single-corner texel value (0.8) as the expected center via a degenerate texture
// (all four texels = 0.8): then BOTH Linear and Nearest yield 0.8, but we keep the distinct-texel
// texture and instead read the WRONG corner — i.e. injectBug forces the texCoord to a corner so the
// result is a single texel (0.2), failing the 0.5 band. We model that by sampling at a corner uv.
int runDrawScreenQuadFilterSelfTest(bool injectBug) {
  SqHarness h;
  if (!h.ok) { printf("[selftest-drawscreenquad-filter] FAIL: no metallib\n"); return 1; }
  const uint32_t W = 64, H = 64;

  float t00[4] = {0.2f, 0.2f, 0.2f, 1.0f};
  float t10[4] = {0.4f, 0.4f, 0.4f, 1.0f};
  float t01[4] = {0.6f, 0.6f, 0.6f, 1.0f};
  // injectBug: collapse the 4th texel toward a corner so the bilinear center is NOT 0.5 (it pulls
  // the average to 0.2+0.4+0.6+0.2 /4 = 0.35) — a value outside the 0.5 band. This perturbs the
  // sampled data the REAL shader reads; with the correct distinct texels the Linear average is 0.5,
  // a value Nearest (snap to one corner ∈{0.2,0.4,0.6,0.8}) can never produce.
  float t11[4] = {injectBug ? 0.2f : 0.8f, injectBug ? 0.2f : 0.8f, injectBug ? 0.2f : 0.8f, 1.0f};
  MTL::Texture* src = h.make2x2(t00, t10, t01, t11);
  MTL::Texture* tex = h.makeFloatTarget(W, H);

  RenderCommand rc;
  RenderDrawItem it{};
  it.kind = DrawKind::ScreenQuad;
  it.srcTexture = src;
  it.color[0] = it.color[1] = it.color[2] = it.color[3] = 1.0f;  // no tint
  it.width = 1.0f; it.height = 1.0f;
  it.blendMode = BlendMode::Normal;
  rc.items.push_back(it);

  TexCookCtx c;
  c.dev = h.dev; c.lib = h.lib; c.queue = h.q;
  c.nodeId = 1; c.command = &rc; c.output = tex;
  cookRenderTarget(c);

  std::vector<float> px((size_t)W * H * 4, 0.0f);
  tex->getBytes(px.data(), W * 4 * sizeof(float), MTL::Region::Make2D(0, 0, W, H), 0);
  size_t ci = ((size_t)(H / 2) * W + (W / 2)) * 4;
  float R = px[ci];
  // Bilinear center of the four distinct texels = 0.5 (avg). Under NEAREST the center snaps to one
  // texel ∈{0.2,0.4,0.6,0.8} — none in [0.45,0.55] → FAIL (this is what makes Linear load-bearing).
  // injectBug skews the average to 0.35 → also outside the band → FAIL.
  bool pass = R > 0.45f && R < 0.55f;
  printf("[selftest-drawscreenquad-filter] center R=%.4f want ≈0.5 (bilinear avg; Nearest snaps to a "
         "single texel) -> %s\n", R, pass ? "PASS" : "FAIL");

  src->release(); tex->release();
  return pass ? 0 : 1;
}

// DrawScreenQuad BLEND golden: stack two ScreenQuad items into ONE chain (one pass) —
//   base: uniform gray 0.3, Normal (lays down 0.3 over black)
//   over: uniform gray 0.3, Additive (adds 0.3 → result ≈ 0.6)
// Additive blend = src*SrcA + dst*One; with SrcA = base.a = 1 the result RGB = 0.3 + 0.3 = 0.6.
// injectBug forces the overlay to Normal blend (src*SrcA + dst*(1-SrcA)); with SrcA=1 it REPLACES
// (result = 0.3), so the "summed brighter than either" probe fails.
int runDrawScreenQuadBlendSelfTest(bool injectBug) {
  SqHarness h;
  if (!h.ok) { printf("[selftest-drawscreenquad-blend] FAIL: no metallib\n"); return 1; }
  const uint32_t W = 128, H = 128;

  MTL::Texture* src = h.makeUniform(32, 32, 0.3f, 0.3f, 0.3f, 1.0f);
  MTL::Texture* tex = h.makeTarget(W, H);

  RenderCommand rc;
  RenderDrawItem base{};
  base.kind = DrawKind::ScreenQuad; base.srcTexture = src;
  base.color[0] = base.color[1] = base.color[2] = base.color[3] = 1.0f;
  base.width = 1.0f; base.height = 1.0f; base.blendMode = BlendMode::Normal;
  rc.items.push_back(base);

  RenderDrawItem over = base;
  over.blendMode = injectBug ? BlendMode::Normal : BlendMode::Additive;
  rc.items.push_back(over);

  TexCookCtx c;
  c.dev = h.dev; c.lib = h.lib; c.queue = h.q;
  c.nodeId = 1; c.command = &rc; c.output = tex;
  cookRenderTarget(c);

  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  size_t ci = ((size_t)(H / 2) * W + (W / 2)) * 4;
  int R = px[ci];
  // Additive: 0.3+0.3=0.6 → ~153. Normal-replace (injectBug): 0.3 → ~77. Probe the sum band.
  bool pass = R > 140 && R < 175;
  printf("[selftest-drawscreenquad-blend] center R=%d want ≈153 (additive sum 0.6) -> %s\n",
         R, pass ? "PASS" : "FAIL");

  src->release(); tex->release();
  return pass ? 0 : 1;
}

// ClearRenderTarget golden: a Clear(red) directive as the first (only) chain item → the executor
// sets the pass clear color to red → readback all-red. injectBug drops the Clear item → empty chain
// → the RenderTarget's own ClearColor default (black) → FAIL.
int runClearRenderTargetSelfTest(bool injectBug) {
  SqHarness h;
  if (!h.ok) { printf("[selftest-clearrendertarget] FAIL: no metallib\n"); return 1; }
  const uint32_t W = 64, H = 64;
  MTL::Texture* tex = h.makeTarget(W, H);

  RenderCommand rc;
  if (!injectBug) {
    RenderDrawItem it{};
    it.kind = DrawKind::Clear;
    it.color[0] = 1.0f; it.color[1] = 0.0f; it.color[2] = 0.0f; it.color[3] = 1.0f;  // red
    rc.items.push_back(it);
  }  // injectBug: empty chain → executor falls back to ClearColor default (black)

  TexCookCtx c;
  c.dev = h.dev; c.lib = h.lib; c.queue = h.q;
  c.nodeId = 1; c.command = &rc; c.output = tex;
  cookRenderTarget(c);

  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  // Every pixel must be red (R≈255, G/B≈0).
  int redPixels = 0;
  for (size_t i = 0; i < (size_t)W * H; ++i)
    if (px[i * 4] > 250 && px[i * 4 + 1] < 8 && px[i * 4 + 2] < 8) ++redPixels;
  bool pass = redPixels == (int)(W * H);
  printf("[selftest-clearrendertarget] redPixels=%d/%d -> %s\n",
         redPixels, W * H, pass ? "PASS" : "FAIL");

  tex->release();
  return pass ? 0 : 1;
}

// WIRED golden (FORK#1 end-to-end): the Texture2D-on-the-Command-path gather, cooked THROUGH
// PointGraph (not a hand-built chain). Graph:
//   RadialPoints → DrawPoints → RenderTarget(srcRT, Custom 128²) → DrawScreenQuad(Color=2,1,1,1)
//                                                                  → RenderTarget(termRT, terminal)
// Proves: (1) the cook driver gathers DrawScreenQuad's Texture2D input by recursively cooking the
// upstream RenderTarget (cookCommand → cookTexNode, FORK#1); (2) the borrowed PointGraph-owned tex
// reaches the DrawKind::ScreenQuad item; (3) the terminal RenderTarget executes it into a lit image.
// injectBug OMITS the srcRT→DrawScreenQuad Texture2D wire → cookDrawScreenQuad sees no input → empty
// chain → the terminal stays black → FAIL.
int runDrawScreenQuadWiredSelfTest(bool injectBug) {
  SqHarness h;
  if (!h.ok) { printf("[selftest-drawscreenquadwired] FAIL: no metallib\n"); return 1; }
  const uint32_t RW = 128, RH = 128;
  registerBuiltinPointOps();  // RadialPoints + DrawPoints + RenderTarget + DrawScreenQuad

  PointGraph pg(h.dev, h.lib, h.q, 64, 64);
  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = 128.0f; gen.params["Radius"] = 2.0f; g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  Node srcRT; srcRT.id = 3; srcRT.type = "RenderTarget";
  srcRT.params["Resolution"] = 4.0f;  // Custom
  srcRT.params["CustomW"] = (float)RW; srcRT.params["CustomH"] = (float)RH; g.nodes.push_back(srcRT);
  Node sq; sq.id = 4; sq.type = "DrawScreenQuad";
  sq.params["Color"] = 2.0f;  // R tint x2 (Color.x); G/B/A default 1
  sq.params["Width"] = 1.0f; sq.params["Height"] = 1.0f; g.nodes.push_back(sq);
  Node termRT; termRT.id = 5; termRT.type = "RenderTarget";
  termRT.params["Resolution"] = 4.0f;
  termRT.params["CustomW"] = (float)RW; termRT.params["CustomH"] = (float)RH; g.nodes.push_back(termRT);

  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points → DrawPoints.points
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // DrawPoints.out → srcRT.command
  if (!injectBug)
    g.connections.push_back({103, pinId(3, 1), pinId(4, 0)});  // srcRT.out(Texture2D) → DrawScreenQuad.Texture
  g.connections.push_back({104, pinId(4, 1), pinId(5, 0)});  // DrawScreenQuad.out → termRT.command

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  int term = pg.defaultDrawTarget(g);  // tex preferred → first RenderTarget reachable; pin the terminal explicitly
  (void)term;
  pg.cook(g, ctx, nullptr, 5);  // terminal = termRT

  MTL::Texture* out = pg.target();
  int nonBlack = 0;
  if (out && (uint32_t)out->width() == RW) {
    std::vector<uint8_t> px((size_t)RW * RH * 4, 0);
    out->getBytes(px.data(), RW * 4, MTL::Region::Make2D(0, 0, RW, RH), 0);
    for (size_t i = 0; i < (size_t)RW * RH; ++i)
      if (px[i * 4] > 30 || px[i * 4 + 1] > 30 || px[i * 4 + 2] > 30) ++nonBlack;
  }
  bool pass = nonBlack > 50;
  printf("[selftest-drawscreenquadwired] nonBlack=%d(need>50, tex gathered onto Command path) -> %s\n",
         nonBlack, pass ? "PASS" : "FAIL");

  return pass ? 0 : 1;
}

}  // namespace sw
