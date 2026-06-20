// Layer2d command op + render golden — TiXL Operators/Lib/render/basic/Layer2d.cs (+ its .t3 →
// Lib:shaders/dx11/draw-Quad-vs.hlsl). The camera-context/Layer2d seam: a textured quad PROJECTED by
// ObjectToClipSpace (vs DrawScreenQuad's raw clip space). This is the unlock for the ~112-op
// camera3d/Layer2d island.
//   CUT 1 landed the seam (the camera matrix threads host→VS) with ObjectToWorld at IDENTITY.
//   CUT 2 (this cut) lands the SRT transform-stack: ObjectToWorld = S·R·T from
//     Scale/Stretch/Rotate/Position/ScaleMode (TiXL _ProcessLayer2d.cs), so Layer2d is now
//     pixel-faithful for the implemented ScaleModes. The Camera op (explicit camera push/pop) is
//     Cut 3 — Cut 2 still uses the driver-local DEFAULT camera (F1).
//
// BACKWARD-TRACE (Layer2d.t3): the .t3 SetPixelAndVertexShaderStage child binds VertexShader
//   "Lib:shaders/dx11/draw-Quad-vs.hlsl" (confirmed: two `draw-Quad-vs.hlsl` references in the .t3).
// draw-Quad-vs.hlsl vsMain (:48-58, LIVE) ported VERBATIM into draw_quad_xf.metal:
//   quadVertexInObject = quadVertex * float2(Width, Height);
//   output.position = mul(float4(quadVertexInObject, 0, 1), ObjectToClipSpace);   // ROW-VECTOR v·M
// psMain (:70-74) is BYTE-IDENTICAL to DrawScreenQuad's → we reuse draw_screenquad_fs
//   (clamp(Color * tex.Sample(uv), 0, (1000,1000,1000,1))).
// cbuffer layout: Transforms(b0) has 10 matrices but vsMain reads ONLY ObjectToClipSpace (F3 — the
//   other 9 are dead for Layer2d, NOT carried); Params(b1) = Color/Width/Height. We collapse both
//   into DrawQuadXfParams (draw_params.h), the executor binds it at slot 0.
//
// FORKS:
//   F1 — the default camera (no Camera op) lives FUNCTION-LOCAL in cookRenderTarget's driver (the
//        resolution-pin point knows the aspect), NOT a runtime global. EvaluationContext.SetDefaultCamera.
//   F2 — DrawKind::Layer2d is a SEPARATE kind/shader, parallel to ScreenQuad (TiXL ships 2 distinct
//        shaders); the clip-space ScreenQuad leaf is untouched.
//   F3 — only ObjectToClipSpace is built (the 1 matrix the VS reads).
//   ★Cut-2 SRT RESOLVED: ObjectToWorld is now the faithful S·R·T from _ProcessLayer2d (Scale/Stretch/
//        Rotate/Position) + the ScaleMode aspect coupling. The op stamps the RAW params into the item
//        (layer*/position); the EXECUTOR composes ObjectToWorld (it has viewAspect from the camera +
//        imageAspect from srcTexture — see point_ops_rendertarget.cpp Layer2d case). Implemented
//        ScaleModes: FitHeight/FitWidth/FitBoth/Cover/Stretch (the 5 the .t3 exposes). DEFERRED (named):
//        MatchPixelResolution (needs context.RequestedResolution, not threaded by this seam — treated
//        as Stretch). deg→rad for Rotate (fork).
//   Cut-1 NOTE retained: the render golden proves STRUCTURE — seam-presence (the mul is load-bearing),
//        scale, rotate+stretch, SRT row order. It is NOT expected to catch projection PARAMETERS (fov /
//        matrix-entries / multiply-order); those are pinned ANALYTICALLY by the math tooth
//        --selftest-field-camera. The two teeth are COMPLEMENTARY by design (render=structure,
//        math=parameters), not a gap. (A Cut-2 attempt to add a fov "edge-probe" tooth on the render leg
//        was REMOVED as hollow: at the default camera the visible half-extent at z=0 is d·tan(fov/2)=1.0
//        for EVERY fov, so a z=0 world point maps to the same NDC regardless of fov — the render leg
//        cannot see fov. See the removed-T4 note in the golden below.)
#include "runtime/point_ops.h"

#include "runtime/field_camera.h"    // Mat4 / mat4Identity / objectToClipSpace / defaultLayerCameraForward / mat4TransformPointDivW
#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp, cookParam/cookVecN, TexCookCtx
#include "runtime/render_command.h"  // RenderCommand / RenderDrawItem / DrawKind / BlendMode

#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"       // Graph/Node, pinId
#include "runtime/tixl_point.h"  // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Map TiXL SharedEnums.BlendModes int → the subset shipped (Normal=0, Additive=1). Mirrors
// DrawScreenQuad's blendModeFromInt (point_ops_drawscreenquad.cpp).
static BlendMode layerBlendModeFromInt(int v) {
  return v == 1 ? BlendMode::Additive : BlendMode::Normal;
}

// Layer2d: Texture2D in → Command out (DrawKind::Layer2d). Emits ONE transformed-quad item, stamping
// the RAW SRT params (TiXL _ProcessLayer2d inputs). The EXECUTOR composes ObjectToWorld = S·R·T and
// finishes ObjectToClipSpace with the output's default camera (F1) — because the ScaleMode aspect
// coupling needs viewAspect (camera, executor-local) AND imageAspect (srcTexture). This is the faithful
// op/executor split: the op is a pure data-stamper, the executor evals against the camera context.
RenderCommand cookLayer2d(CmdCookCtx& c) {
  RenderCommand rc;
  // Unwired texture → empty result (no item), NOT a crash (TiXL UseFallbackTexture posture).
  if (!c.inputTexture) return rc;
  RenderDrawItem it{};
  it.kind = DrawKind::Layer2d;
  it.srcTexture = c.inputTexture;  // borrowed (PointGraph-owned, single-frame); never retained
  float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  cookVecN(c, "Color", white, 4, it.color);
  // TiXL Layer2d quad is UNIT ([-1,1]², draw-Quad-vs Quad[]); the SRT scale alone sizes it — Width/
  // Height are NOT Layer2d inputs (faithful: unit quad). Kept at 1 so the VS's quadVertex*float2(W,H)
  // is a no-op and ObjectToWorld's scale does all the sizing (the golden overrides W/H to probe).
  it.width = 1.0f;
  it.height = 1.0f;
  it.blendMode = layerBlendModeFromInt((int)(cookParam(c, "BlendMode", 0.0f) + 0.5f));
  // RAW SRT params (TiXL _ProcessLayer2d inputs). PositionXy → position[], PositionZ separate.
  float zero2[2] = {0.0f, 0.0f};
  float pos[2] = {0.0f, 0.0f};
  cookVecN(c, "Position", zero2, 2, pos);
  it.position[0] = pos[0]; it.position[1] = pos[1];
  it.layerPosZ = cookParam(c, "PositionZ", 0.0f);
  it.layerScale = cookParam(c, "Scale", 1.0f);          // .t3 default 1.0
  float one2[2] = {1.0f, 1.0f};
  float stretch[2] = {1.0f, 1.0f};
  cookVecN(c, "Stretch", one2, 2, stretch);             // .t3 default (1,1)
  it.layerStretch[0] = stretch[0]; it.layerStretch[1] = stretch[1];
  it.layerRotateDeg = cookParam(c, "Rotate", 0.0f);     // .t3 default 0 (degrees)
  it.layerScaleMode = (uint32_t)(int)(cookParam(c, "ScaleMode", 0.0f) + 0.5f);  // .t3 default 0=FitHeight
  it.layer2dComposeSRT = true;  // executor composes ObjectToWorld = S·R·T (production path)
  it.applyTransform = true;     // production always projects; the golden's bug leg flips this.
  rc.items.push_back(it);
  return rc;
}

void registerLayer2dOp() { registerCmdOp("Layer2d", cookLayer2d); }

namespace {
// A WxH shaderRead texture filled with a uniform RGBA (0..1).
MTL::Texture* makeUniform(MTL::Device* dev, uint32_t W, uint32_t H, float r, float g, float b,
                          float a) {
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
MTL::Texture* makeTarget(MTL::Device* dev, uint32_t W, uint32_t H) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  return dev->newTexture(td);
}
// Map an NDC x or y in [-1,1] to a pixel coordinate (NDC +1 = right/top). The viewport Y is flipped
// for sampling but the RASTER maps NDC.y=+1 → top row (row 0) in Metal's default viewport; we read
// rows top-to-bottom so NDC.y=+1 → row 0, NDC.y=-1 → row H-1.
int ndcXToPx(float ndcX, uint32_t W) { return (int)((ndcX * 0.5f + 0.5f) * (float)(W - 1) + 0.5f); }
int ndcYToPx(float ndcY, uint32_t H) { return (int)((1.0f - (ndcY * 0.5f + 0.5f)) * (float)(H - 1) + 0.5f); }

// Render ONE Layer2d item into a WxH RGBA8 target (the cookRenderTarget executor path, default camera)
// and return the readback. Solid-RED source × white tint → quad pixels = clamp(Color*tex)=red(255).
std::vector<uint8_t> renderLayer2d(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q,
                                   const RenderDrawItem& item, MTL::Texture* src, uint32_t W,
                                   uint32_t H) {
  MTL::Texture* tex = makeTarget(dev, W, H);
  RenderCommand rc;
  RenderDrawItem it = item;
  it.kind = DrawKind::Layer2d;
  it.srcTexture = src;
  it.blendMode = BlendMode::Normal;
  rc.items.push_back(it);
  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.command = &rc; c.output = tex;
  cookRenderTarget(c);
  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  tex->release();
  return px;
}
}  // namespace

// Layer2d RENDER golden (Cut 2) — THREE teeth, all closed-form (NDC host-computed via the SAME
// mat4TransformPointDivW the VS reproduces), single-sample, no depth, probing ONLY deep interior /
// far exterior plateaus (NEVER a quad edge). At z=0 with the default camera (aspect=1) the projected
// NDC of object (x,y,0) is EXACTLY (x,y): the visible half-extent at z=0 is d·tan(fov/2)=2.4142·tan
// (22.5°)=1.0, so NDC.x = x (this is the load-bearing camera identity the math tooth pins).
//
// SCOPE — what the render leg tests and what it does NOT: the render leg pins STRUCTURE (seam-presence,
// scale, rotate+stretch, SRT row order). It is NOT expected to catch projection PARAMETERS (fov /
// matrix-entries / multiply-order). At the default camera the visible half-extent at z=0 is
// d·tan(fov/2)=1.0 for EVERY fov, so a z=0 world point maps to the SAME NDC regardless of fov — the
// render leg structurally cannot see fov. Projection parameters are pinned ANALYTICALLY by the math
// tooth --selftest-field-camera. The two are COMPLEMENTARY by design, not a gap.
// (REMOVED-T4: a Cut-2 "edge-probe fov tooth" once lived here. It was HOLLOW — its render leg (a)
// rendered through the only camera the executor has (default 45°), making it a plain edge-coverage
// check already covered by TOOTH 2 (scale grows); its (b) "wrong 60° fov" leg was pure host-side math
// comparing perspectiveFovRH(60°) against perspectiveFovRH(45°), never routed through the shipped
// render path, so it could not catch a wrong fov in production. Injecting fov=50° left it GREEN. It is
// removed, not relabeled, because nothing non-redundant remained.)
//
// Source: solid RED (1,0,0,1), white tint → quad pixels = clamp(Color·tex) = red(255). Background=0.
//
//   TOOTH 1 — SEAM-PRESENCE (Cut-1, kept): a TRANSLATED ObjectToWorld (+0.6,0,0) via the LEGACY matrix
//     path (layer2dComposeSRT=false). Faithful → quad center NDC≈(0.6,0)=RED; drop-mul (applyTransform
//     =false, raw clip) → quad never moves → that probe = BACKGROUND. Proves the mul is LOAD-BEARING.
//
//   TOOTH 2 — SCALE grows (SRT path): ScaleMode=Stretch, Scale=2, base half-extent W=H=0.3. scaleX=
//     Scale·Stretch.x·viewAspect = 2·1·1 = 2 → quad x-half in NDC = 0.3·2 = 0.6. A probe at NDC 0.45
//     is OUTSIDE at Scale=1 (x-half 0.3) and INSIDE at Scale=2 (x-half 0.6): Scale=2 → RED. injectBug
//     drops the 2× (feeds Scale=1 via the legacy transposed-matrix bug below) → 0.45 → BACKGROUND.
//
//   TOOTH 3 — ROTATE 90° + non-square STRETCH (SRT path): Stretch=(2,0.5), Scale=1, base 0.3 → scaleX=
//     1·2·1=2 (x-half 0.6), scaleY=1·0.5=0.5 (y-half 0.15). WIDE axis = x. Rotate=90° swaps the extents
//     (object (0.6,0)→(0,0.6)). Probes: wide-X NDC(0.4,0) = INSIDE pre-rot (0.4<0.6) → BACKGROUND post-
//     rot (x-half now 0.15); wide-Y NDC(0,0.4) = OUTSIDE pre-rot (0.4>0.15) → RED post-rot (y-half now
//     0.6). injectBug=Rotate 0 → both flip. Proves rotation + the Stretch coupling + SRT row order.
//
// injectBug (the -bug leg) corrupts the REAL emit, NOT the expected value: TOOTH 1 drops the mul; TOOTH
//   2/3 feed a TRANSPOSED S·R·T matrix (the legacy path carrying transpose(layer2dObjectToWorld(...)))
//   → the SRT row order is load-bearing → the transform probes flip → RED.

namespace {
// Transpose a row-major Mat4 (the SRT-row-order injectBug for the transform teeth).
Mat4 transposeMat(const Mat4& a) {
  Mat4 r{};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) r.m[i * 4 + j] = a.m[j * 4 + i];
  return r;
}
}  // namespace

int runLayer2dSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;  // square → aspect 1 (matches the camera selftest aspect)

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-layer2d] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::Texture* src = makeUniform(dev, 64, 64, 1.0f, 0.0f, 0.0f, 1.0f);  // solid RED (square → imgAspect 1)
  const float aspect = (float)W / (float)H;  // 1.0
  const LayerCameraForward camFwd = defaultLayerCameraForward(aspect);
  const float kBase = 0.3f;  // golden base half-extent (Width/Height); production Layer2d uses unit (1)
  // Each tooth's FAITHFUL predicate (true = the faithful render matched expectation). The non-bug leg
  // passes iff ALL are true; the bug leg is RED iff AT LEAST ONE flipped to false (a tooth bit).
  bool allFaithful = true;

  auto readR = [&](const std::vector<uint8_t>& px, int x, int y) -> int {
    x = x < 0 ? 0 : (x >= (int)W ? (int)W - 1 : x);
    y = y < 0 ? 0 : (y >= (int)H ? (int)H - 1 : y);
    return px[((size_t)y * W + x) * 4 + 0];
  };

  // ── TOOTH 1: SEAM-PRESENCE (translate ObjectToWorld via the legacy matrix path) ──
  {
    Mat4 o2w = mat4Identity();
    o2w.m[12] = 0.6f;  // translate +0.6 x
    Mat4 o2c = objectToClipSpace(o2w, camFwd.worldToCamera, camFwd.cameraToClipSpace);
    float movedNdc[3];
    mat4TransformPointDivW(o2c, 0.0f, 0.0f, 0.0f, movedNdc);  // host-derived, not hardcoded

    RenderDrawItem it{};
    it.color[0] = it.color[1] = it.color[2] = it.color[3] = 1.0f;
    it.width = 0.4f; it.height = 0.4f;            // quad half-extent 0.4 in object space
    it.layer2dComposeSRT = false;                 // legacy: carry ObjectToWorld verbatim
    for (int i = 0; i < 16; ++i) it.objectToClipSpace[i] = o2w.m[i];
    it.applyTransform = injectBug ? false : true;  // ★DROP-MUL bite
    std::vector<uint8_t> px = renderLayer2d(dev, lib, q, it, src, W, H);

    int movedR = readR(px, ndcXToPx(movedNdc[0], W), ndcYToPx(movedNdc[1], H));
    int leftR = readR(px, ndcXToPx(-0.2f, W), ndcYToPx(0.0f, H));  // inside un-moved, outside moved
    int farR = readR(px, ndcXToPx(0.95f, W), ndcYToPx(0.95f, H));
    bool t1 = (movedR > 200) && (leftR < 40) && (farR < 40);  // faithful expectation
    allFaithful = allFaithful && t1;
    std::printf("[selftest-layer2d] T1 seam: movedNDC=(%.3f,%.3f) movedR=%d(>200) leftR=%d(<40) "
                "farR=%d(<40) -> %s\n", movedNdc[0], movedNdc[1], movedR, leftR, farR,
                t1 ? "faithful-ok" : "tripped");
  }

  // ── TOOTH 2: SCALE grows (SRT path; injectBug = TRANSPOSED S·R·T via legacy path) ──
  {
    RenderDrawItem it{};
    it.color[0] = it.color[1] = it.color[2] = it.color[3] = 1.0f;
    it.width = kBase; it.height = kBase;
    if (!injectBug) {
      it.layer2dComposeSRT = true;
      it.layerScaleMode = (uint32_t)Layer2dScaleMode::Stretch;
      it.layerScale = 2.0f;  // ← the 2× under test
    } else {
      // injectBug: DROP THE SCALE (Scale=1 instead of 2) → quad x-half = 0.3 → the NDC0.45 probe falls
      // OUTSIDE → flips RED→background. (Scale is on the diagonal, so a transpose is a no-op for it; the
      // independent bite for THIS tooth is the dropped magnitude — T3 covers the SRT row order.)
      it.layer2dComposeSRT = true;
      it.layerScaleMode = (uint32_t)Layer2dScaleMode::Stretch;
      it.layerScale = 1.0f;  // ← scale dropped
    }
    std::vector<uint8_t> px = renderLayer2d(dev, lib, q, it, src, W, H);
    // x-half NDC = base·scaleX = 0.3·2 = 0.6. Probe NDC 0.45: inside@Scale2, outside@Scale1.
    int r45 = readR(px, ndcXToPx(0.45f, W), ndcYToPx(0.0f, H));
    int rFar = readR(px, ndcXToPx(0.85f, W), ndcYToPx(0.0f, H));  // 0.85>0.6 → background both
    bool t2 = (r45 > 200) && (rFar < 40);
    allFaithful = allFaithful && t2;
    std::printf("[selftest-layer2d] T2 scale=2: NDC0.45 R=%d(>200) NDC0.85 R=%d(<40) -> %s\n",
                r45, rFar, t2 ? "faithful-ok" : "tripped");
  }

  // ── TOOTH 3: ROTATE 90° + non-square STRETCH (SRT path; injectBug = Rotate 0 via transposed legacy) ──
  {
    RenderDrawItem it{};
    it.color[0] = it.color[1] = it.color[2] = it.color[3] = 1.0f;
    it.width = kBase; it.height = kBase;
    if (!injectBug) {
      it.layer2dComposeSRT = true;
      it.layerScaleMode = (uint32_t)Layer2dScaleMode::Stretch;
      it.layerScale = 1.0f;
      it.layerStretch[0] = 2.0f; it.layerStretch[1] = 0.5f;  // wide x (0.6) / narrow y (0.15)
      it.layerRotateDeg = 90.0f;                              // swap the extents
    } else {
      // injectBug: TRANSPOSED S·R·T with Rotate=0 → no rotation + wrong rows → probes flip.
      Mat4 srt = layer2dObjectToWorld(/*sx*/2.0f, /*sy*/0.5f, /*rot*/0.0f, 0.0f, 0.0f, 0.0f);
      Mat4 bad = transposeMat(srt);
      it.layer2dComposeSRT = false;
      for (int i = 0; i < 16; ++i) it.objectToClipSpace[i] = bad.m[i];
    }
    std::vector<uint8_t> px = renderLayer2d(dev, lib, q, it, src, W, H);
    // Post-rot: x-half=0.15, y-half=0.6. wide-X NDC(0.4,0)=background; wide-Y NDC(0,0.4)=red.
    int rWideX = readR(px, ndcXToPx(0.4f, W), ndcYToPx(0.0f, H));
    int rWideY = readR(px, ndcXToPx(0.0f, W), ndcYToPx(0.4f, H));
    bool t3 = (rWideX < 40) && (rWideY > 200);
    allFaithful = allFaithful && t3;
    std::printf("[selftest-layer2d] T3 rot90+stretch(2,0.5): wideX NDC(0.4,0) R=%d(<40) "
                "wideY NDC(0,0.4) R=%d(>200) -> %s\n", rWideX, rWideY, t3 ? "faithful-ok" : "tripped");
  }

  // NO TOOTH 4. The render leg deliberately does NOT test projection PARAMETERS (fov / matrix-entries /
  // multiply-order). At the default camera the z=0 visible half-extent is d·tan(fov/2)=1.0 for EVERY fov,
  // so the render leg structurally cannot see fov. Those parameters are pinned by --selftest-field-camera
  // (the analytic math tooth). See the REMOVED-T4 note in the golden header.

  src->release();
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {  // every tooth stayed faithful under the bug → the golden is hollow
      std::printf("[selftest-layer2d] FAIL: injectBug tripped no tooth\n");
      return 1;
    }
    std::printf("[selftest-layer2d] injectBug correctly RED (seam drop-mul + dropped-scale + "
                "transposed-SRT transform probes flipped)\n");
    return 1;
  }
  std::printf("[selftest-layer2d] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

}  // namespace sw
