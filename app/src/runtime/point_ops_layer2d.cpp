// Layer2d command op + render golden — TiXL Operators/Lib/render/basic/Layer2d.cs (+ its .t3 →
// Lib:shaders/dx11/draw-Quad-vs.hlsl). The camera-context/Layer2d seam, CUT 1: a textured quad
// PROJECTED by ObjectToClipSpace (vs DrawScreenQuad's raw clip space). This is the unlock for the
// ~112-op camera3d/Layer2d island — Cut 1 lands the seam (the matrix threads from the host camera
// math through a new VS) with ObjectToWorld at IDENTITY; Cut 2 adds the Camera op + the SRT
// (Scale/Stretch/Rotate/ScaleMode) ObjectToWorld composition.
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
//   ★Cut-1 SRT-DEFERRED (NAMED): TiXL's _ProcessLayer2d at ScaleMode=Stretch applies
//        `scale.X *= viewAspect` (a non-identity ObjectToWorld). Cut 1 INTENTIONALLY uses
//        ObjectToWorld = Identity (the seam-proving transform), so Cut-1 Layer2d is NOT yet pixel-
//        faithful to TiXL's Stretch aspect-scale — that ObjectToWorld composition is Cut 2. What Cut 1
//        proves is the SEAM (the ObjectToClipSpace mul wires through the camera math and is load-
//        bearing), via the drop-mul render tooth below; NOT full Layer2d parity, and NOT the projection
//        PARAMETERS (fov/matrix entries/multiply-order) — those are pinned analytically by the math
//        tooth --selftest-field-camera (see SCOPE note on the golden below).
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

// Layer2d: Texture2D in → Command out (DrawKind::Layer2d). Emits ONE transformed-quad item.
// ObjectToWorld = Identity (Cut 1; the SRT math is Cut 2 — see SRT-DEFERRED note). The executor
// finishes ObjectToClipSpace with the output's default camera (F1). Item carries ObjectToWorld in
// its objectToClipSpace[16] field (the driver multiplies in the camera).
RenderCommand cookLayer2d(CmdCookCtx& c) {
  RenderCommand rc;
  // Unwired texture → empty result (no item), NOT a crash (TiXL UseFallbackTexture posture).
  if (!c.inputTexture) return rc;
  RenderDrawItem it{};
  it.kind = DrawKind::Layer2d;
  it.srcTexture = c.inputTexture;  // borrowed (PointGraph-owned, single-frame); never retained
  float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  cookVecN(c, "Color", white, 4, it.color);
  // TiXL Layer2d.Position is a Vector2 (clip-space offset folded into ObjectToWorld translation in
  // Cut 2). Cut 1: Identity ObjectToWorld → Position unused (kept for the executor's quad placement).
  float zero2[2] = {0.0f, 0.0f};
  float pos[2] = {0.0f, 0.0f};
  cookVecN(c, "Position", zero2, 2, pos);
  it.position[0] = pos[0]; it.position[1] = pos[1];
  // Layer2d has no Width/Height inputs (the quad is unit; the SRT scale sizes it). Cut 1: unit quad
  // (Width=Height=1) → object verts span [-1,1]² before projection. Width/Height live in the item so
  // the golden can size the quad to a known band; the production op uses the unit default.
  it.width = cookParam(c, "Width", 1.0f);
  it.height = cookParam(c, "Height", 1.0f);
  it.blendMode = layerBlendModeFromInt((int)(cookParam(c, "BlendMode", 0.0f) + 0.5f));
  // ObjectToWorld = Identity (Cut 1). The executor multiplies in defaultWorldToCamera·CameraToClipSpace.
  Mat4 o2w = mat4Identity();
  for (int i = 0; i < 16; ++i) it.objectToClipSpace[i] = o2w.m[i];
  it.applyTransform = true;  // production always projects; the golden's bug leg flips this.
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
}  // namespace

// Layer2d RENDER golden (the seam tooth, Option B closed-form, non-tautological — the DROP-MUL bite).
//
// ★Why a TRANSLATED ObjectToWorld (not Identity) in the golden: at z=0 with the DEFAULT camera, the
// projected NDC of object (x,y,0) is EXACTLY (x,y) — the camera's visible half-extent at z=0 is
// d·tan(fov/2) = 2.4142·tan(22.5°) = 1.0, so NDC.x = x·yScale/(aspect·d) = x (aspect=1). That means a
// quad at Identity·default-camera projects to the SAME NDC as the raw-clip (drop-mul) path → an
// Identity golden could NOT distinguish "mul applied" from "mul dropped" (a HOLLOW tooth). We break
// the symmetry by putting a TRANSLATION in ObjectToWorld (the golden ONLY — production stays Identity
// per the SRT-DEFERRED fork): translate the quad to world (+0.6, 0, 0). Faithful (mul applied) →
// quad center projects to NDC (0.6, 0); drop-mul (raw clip) IGNORES ObjectToWorld → quad stays at
// object origin → NDC (0,0). A probe at NDC (0.6, 0) reads RED iff the mul ran, BACKGROUND iff dropped.
//
// Setup: a SOLID red (1,0,0,1) source through Layer2d, quad half-extent 0.4 (Width=Height=0.4), tint
// (1,1,1,1) → quad pixels = clamp(Color*texColor) = red (255). ObjectToWorld = translate(0.6,0,0) →
// ObjectToClipSpace = ObjectToWorld·defaultWorldToCamera·defaultCameraToClipSpace.
//
// EXPECTED NDC (HOST-computed via the SAME mat4TransformPointDivW the VS reproduces — NOT hardcoded):
//   moved-center  = ObjectToClipSpace·(0,0,0,1)/w  → NDC ≈ (0.6, 0)  (the quad center after translate)
//   origin-NDC    = (0,0)  (where the drop-mul quad center lands; left of the moved quad)
// PROBES (deep interior / far exterior plateaus, NEVER a quad edge; single-sample, no depth):
//   - MOVED-CENTER pixel (NDC 0.6,0): faithful = RED; drop-mul = BACKGROUND (quad moved away). ← TOOTH
//   - LEFT pixel (NDC -0.6,0): faithful = BACKGROUND (the quad moved right, no longer covers here);
//                              drop-mul = RED (the un-moved quad spans NDC [-0.4,0.4]... -0.6 is just
//                              outside even un-moved → use NDC -0.2: inside the drop-mul quad, outside
//                              the moved quad). We probe NDC -0.2 for the inverse bite.
//   - FAR-CORNER (NDC 0.95,0.95): BACKGROUND in both (pins the quad does not fill the screen).
//
// injectBug = DROP THE MUL (applyTransform=false → the raw-clip VS, exactly draw_screenquad's path):
// the MOVED-CENTER probe flips RED→BACKGROUND (the quad never moved) → RED. This is the genuine seam
// tooth: it fails iff the ObjectToClipSpace projection is not applied.
//
// ★SCOPE (what this render golden DOES / DOES NOT prove — refuter-audited):
//   DOES: proves the camera matrix THREADS host→VS and the projection `mul` is LOAD-BEARING — a
//     dropped or collapsed transform (drop-mul, x-axis collapse) fails it (seam-presence).
//   DOES NOT: verify the projection PARAMETERS (fov / matrix entries / multiply-order). A wrong-but-
//     plausible projection can PASS this render leg — e.g. a wrong fov (60° vs 45°) passes, and a
//     two-translation multiply-order swap passes — because the moved quad is a wide center plateau and
//     those errors don't move the probed plateau off the probe. Those PARAMETERS are pinned
//     ANALYTICALLY by the math tooth --selftest-field-camera (origin→NDC dead-center; a wrong
//     multiply-order/handedness moves that center). The two teeth are complementary: render = structure,
//     math = parameters.
//
// ★TODO (Cut-2 tightening, refuter-suggested): once SRT lands and the quad is NOT a wide center
//   plateau (Scale/Stretch shrink it), add an EDGE / right-edge probe whose NDC is computed under the
//   CORRECT camera, so a fov-scale error flips coverage across that probe (inside↔outside). That turns
//   this render leg into a real projection-PARAMETER tooth instead of only a seam-presence tooth.
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

  MTL::Texture* src = makeUniform(dev, 64, 64, 1.0f, 0.0f, 0.0f, 1.0f);  // solid RED
  MTL::Texture* tex = makeTarget(dev, W, H);

  // Build the faithful ObjectToClipSpace host-side via the SAME primitives the shader trusts. The
  // golden's ObjectToWorld is a +x translation (see header) — production Layer2d stays Identity.
  const float aspect = (float)W / (float)H;  // 1.0
  LayerCameraForward camFwd = defaultLayerCameraForward(aspect);
  Mat4 o2w = mat4Identity();
  o2w.m[12] = 0.6f;  // translation row x (row-vector convention: m[12..14] = translation)
  Mat4 o2c = objectToClipSpace(o2w, camFwd.worldToCamera, camFwd.cameraToClipSpace);

  const float quadHalf = 0.4f;  // Width=Height=0.4 → quad spans ±0.4 in object x/y before translate

  // Host-derive the projected NDC of the moved quad center (object origin) — NOT hardcoded.
  float movedCenterNdc[3];
  mat4TransformPointDivW(o2c, 0.0f, 0.0f, 0.0f, movedCenterNdc);

  RenderCommand rc;
  RenderDrawItem it{};
  it.kind = DrawKind::Layer2d;
  it.srcTexture = src;
  it.color[0] = it.color[1] = it.color[2] = it.color[3] = 1.0f;  // no tint → quad = texColor (red)
  it.width = quadHalf; it.height = quadHalf;
  it.blendMode = BlendMode::Normal;
  // The item carries ObjectToWorld (the executor multiplies in the camera). Golden = translate(0.6,0,0).
  for (int i = 0; i < 16; ++i) it.objectToClipSpace[i] = o2w.m[i];
  it.applyTransform = injectBug ? false : true;  // ★DROP-MUL bite: bug uses the raw-clip VS
  rc.items.push_back(it);

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.command = &rc; c.output = tex;
  cookRenderTarget(c);

  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  auto readR = [&](int x, int y) -> int {
    x = x < 0 ? 0 : (x >= (int)W ? (int)W - 1 : x);
    y = y < 0 ? 0 : (y >= (int)H ? (int)H - 1 : y);
    return px[((size_t)y * W + x) * 4 + 0];
  };

  // MOVED-CENTER probe at the HOST-computed NDC (≈0.6,0): faithful=RED (quad translated here),
  // drop-mul=BACKGROUND (quad never moved). THE TOOTH.
  int mx = ndcXToPx(movedCenterNdc[0], W), my = ndcYToPx(movedCenterNdc[1], H);
  int movedR = readR(mx, my);
  // LEFT probe at NDC (-0.2,0): inside the UN-MOVED (drop-mul) quad [-0.4,0.4] but OUTSIDE the moved
  // quad [0.2,1.0]. faithful=BACKGROUND, drop-mul=RED (the inverse confirmation).
  int lx = ndcXToPx(-0.2f, W), ly = ndcYToPx(0.0f, H);
  int leftR = readR(lx, ly);
  // FAR-CORNER at NDC (0.95,0.95): BACKGROUND in both (the quad does not fill the screen).
  int fx = ndcXToPx(0.95f, W), fy = ndcYToPx(0.95f, H);
  int farR = readR(fx, fy);

  bool movedRed = movedR > 200;       // faithful: quad projected to +x → red here
  bool leftBackground = leftR < 40;   // faithful: quad moved away from the left → background
  bool farBackground = farR < 40;     // quad does not fill the screen
  bool pass = movedRed && leftBackground && farBackground;

  std::printf("[selftest-layer2d] movedCenterNDC=(%.3f,%.3f) moved px(%d,%d) R=%d(want>200) "
              "left=NDC(-0.2,0)px(%d,%d) R=%d(want<40) far=NDC(0.95,0.95)px(%d,%d) R=%d(want<40) -> %s\n",
              movedCenterNdc[0], movedCenterNdc[1], mx, my, movedR, lx, ly, leftR, fx, fy, farR,
              pass ? "PASS" : "FAIL");

  src->release(); tex->release();
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    // DROP-MUL: the quad never translates → the moved-center probe reads BACKGROUND (movedRed false)
    // AND the left probe reads RED (the un-moved quad covers NDC -0.2). Either flip → pass==false.
    if (pass) {
      std::printf("[selftest-layer2d] FAIL: injectBug (drop-mul) tripped no tooth\n");
      return 1;
    }
    std::printf("[selftest-layer2d] injectBug correctly RED (drop-mul: quad never projected to +x)\n");
    return 1;
  }
  return pass ? 0 : 1;
}

}  // namespace sw
