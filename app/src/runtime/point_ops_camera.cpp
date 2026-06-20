// Camera command op + camera goldens — TiXL Operators/Lib/render/camera/Camera.cs (the explicit
// camera push/pop). This is the LAST core piece of the camera3d/Layer2d island: a Camera op wraps a
// Command subtree and renders it through ITS camera (Position/Target/Up/FOV/ClipPlanes) instead of
// the driver-local DEFAULT camera (Cut 1/2).
//
// BACKWARD-TRACE (Camera.cs:36-45, UpdateOutputWithSubtree):
//   var prevW2C = context.WorldToCamera; var prevC2C = context.CameraToClipSpace;
//   context.WorldToCamera = WorldToCamera;          // push
//   context.CameraToClipSpace = CameraToClipSpace;
//   Command.GetValue(context);                       // eval subtree → its draws read the new context
//   context.CameraToClipSpace = prevC2C;             // pop (restore previous)
//   context.WorldToCamera = prevW2C;
// The matrices come from CameraDefinition.BuildProjectionMatrices (ICamera.cs:110-130). v1 scope (the
// commented-out embellishments Camera.cs:82-103 / the offset/roll/lensShift terms of BuildProjection-
// Matrices are a NAMED FORK, dropped):
//   camToClipSpace = PerspectiveFovRH(FieldOfView, AspectRatio, NearFarClip.X, NearFarClip.Y)
//                    (LensShift M31/M32 → 0, dropped)
//   eye            = Position                         (PositionOffset = 0, dropped)
//   worldToCamera  = LookAtRH(eye, Target, Up)        (roll/rotationOffset/translation = Identity, dropped)
//   AspectRatio    : if < 0.0001 → RequestedResolution aspect (Camera.cs:53-55) → executor's output aspect.
//
// ★INTEGRATION MECHANISM — Option (a), per-item camera stamp (see render_command.h hasCamera/cam*):
// SW is retained-mode with a per-item executor (cookRenderTarget composes each item's ObjectToClipSpace).
// There is no runtime scope stack to push context onto. So the Camera op STAMPS its raw camera params
// onto every RenderDrawItem its subtree produced (the items that have no camera yet), and the executor
// uses that per-item camera when composing ObjectToClipSpace. This reproduces TiXL's push/pop WITHOUT a
// scope stack — the same host-context precedent as the FloatList/context-var seam.
//   push  = stamp camera onto every subtree item where !hasCamera.
//   pop   = "where !hasCamera" → a NESTED Camera (deeper in the subtree) already stamped its own items,
//           so an OUTER Camera leaves them alone = the inner camera wins = restoring prev on pop.
// Option (b) (a camera-context field on the cook ctx threaded into the subtree's cook) was rejected:
// it requires re-architecting the Command-input gather to carry a context and re-cooking the subtree
// under it — more surface, more risk, and the Cut-1 executor ALREADY composes per-item ObjectToClipSpace,
// so per-item params are the natural fit (the executor change is a 5-line "use it.cam* if it.hasCamera").
//
// FORKS (named): offset/roll/lensShift dropped (Camera.cs:82-103 commented embellishments); default
// fallback = SetDefaultCamera (an unwired/absent Camera op → items keep hasCamera=false → executor uses
// defaultLayerCameraForward). OrbitCamera/ActionCamera (temporal/input) deferred. No depth.
#include "runtime/point_ops.h"

#include "runtime/field_camera.h"    // lookAtRH/perspectiveFovRH/mat4* + selftest convention
#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp, cookParam/cookVecN
#include "runtime/render_command.h"  // RenderCommand / RenderDrawItem / DrawKind

#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"       // Graph/Node
#include "runtime/tixl_point.h"  // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Camera: Command subtree in → Command out. Reads its camera params, stamps them onto every subtree
// item that has no camera yet (push/pop, innermost-wins). Unwired Command → empty chain (no items),
// faithful (TiXL would eval an empty subtree). The matrices are NOT built here — the executor builds
// them (it owns the output aspect for the AspectRatio<0.0001 fallback, exactly like Layer2d's SRT).
RenderCommand cookCamera(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.inputCommand) return rc;     // no subtree wired → empty (TiXL: eval an empty Command)
  rc.items = c.inputCommand->items;   // COPY the subtree (we re-emit it, possibly stamped)

  // TiXL Camera.cs inputs (v1 scope). Position default = (0,0,DefaultCameraDistance) (CameraDefinition
  // ctor, ICamera.cs:28); Target=(0,0,0); Up=(0,1,0); FOV=45° (.t3); ClipPlanes=(0.01,1000); AspectRatio
  // default 0 → executor's output aspect.
  float posDef[3] = {0.0f, 0.0f, defaultCameraDistance()};
  float pos[3];
  cookVecN(c, "Position", posDef, 3, pos);
  float tgtDef[3] = {0.0f, 0.0f, 0.0f};
  float tgt[3];
  cookVecN(c, "Target", tgtDef, 3, tgt);
  float upDef[3] = {0.0f, 1.0f, 0.0f};
  float up[3];
  cookVecN(c, "Up", upDef, 3, up);
  float fovDeg = cookParam(c, "FieldOfView", kDefaultCamFovDegrees);
  float clipDef[2] = {0.01f, 1000.0f};
  float clip[2];
  cookVecN(c, "ClipPlanes", clipDef, 2, clip);
  float aspect = cookParam(c, "AspectRatio", 0.0f);  // 0 → executor output aspect (Camera.cs:53-55)

  for (RenderDrawItem& it : rc.items) {
    if (it.hasCamera) continue;  // a NESTED camera already stamped this item (innermost wins = pop)
    it.hasCamera = true;
    it.camEye[0] = pos[0]; it.camEye[1] = pos[1]; it.camEye[2] = pos[2];
    it.camTarget[0] = tgt[0]; it.camTarget[1] = tgt[1]; it.camTarget[2] = tgt[2];
    it.camUp[0] = up[0]; it.camUp[1] = up[1]; it.camUp[2] = up[2];
    it.camFovDeg = fovDeg;
    it.camNear = clip[0];
    it.camFar = clip[1];
    it.camAspect = aspect;  // <=0 → executor uses output aspect
  }
  return rc;
}

void registerCameraOp() { registerCmdOp("Camera", cookCamera); }

// ───────────────────────────────── GOLDENS ─────────────────────────────────
namespace {
MTL::Texture* makeUniformCam(MTL::Device* dev, uint32_t W, uint32_t H, float r, float g, float b,
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
MTL::Texture* makeTargetCam(MTL::Device* dev, uint32_t W, uint32_t H) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  return dev->newTexture(td);
}
int ndcXToPxCam(float ndcX, uint32_t W) { return (int)((ndcX * 0.5f + 0.5f) * (float)(W - 1) + 0.5f); }
int ndcYToPxCam(float ndcY, uint32_t H) {
  return (int)((1.0f - (ndcY * 0.5f + 0.5f)) * (float)(H - 1) + 0.5f);
}
// Render ONE item (already camera-stamped or not) through the real cookRenderTarget executor.
std::vector<uint8_t> renderItemCam(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q,
                                   const RenderDrawItem& item, MTL::Texture* src, uint32_t W,
                                   uint32_t H) {
  MTL::Texture* tex = makeTargetCam(dev, W, H);
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

// Camera GOLDEN — the GENUINE eye-distance render tooth (NOT hollow). Unlike fov-at-default-camera
// (where d·tan(fov/2)=1.0 for every fov, so the render leg can't see fov — the removed Cut-2 T4), an
// explicit EYE-DISTANCE change genuinely MOVES the z=0 projection: NDC.x = x / (d·tan(fov/2)). At the
// default eye d=DefaultCameraDistance(2.4142), fov=45° → d·tan(22.5°)=1.0 → NDC.x=x. With a Camera op
// setting eye=(0,0,5), fov=45° → 5·tan(22.5°)=2.071 → NDC.x = x/2.071 (the quad projects SMALLER). A
// WRONG eye/WorldToCamera (or a dropped camera push) moves the asserted pixel → this tooth bites.
//
//   THE FLIP: a Layer2d quad of object half-extent 0.6 (Scale=0.6, Stretch=1, ScaleMode=Stretch, square
//   aspect → scaleX=0.6). Under the DEFAULT camera its x-half projects to NDC 0.6. A probe at NDC 0.45:
//     • default camera  → 0.45 < 0.6 → INSIDE  → quad color (RED).
//     • Camera eye=(0,0,5) → x-half projects to 0.6/2.071 = 0.290 → 0.45 > 0.290 → OUTSIDE → background.
//   So the SAME probe pixel that is quad-color under the default camera is BACKGROUND under the farther
//   Camera. That coverage FLIP is the genuine camera tooth. The expected boundary NDC is computed HOST-
//   SIDE via the SAME mat4TransformPointDivW the executor's VS reproduces (NOT hardcoded): project the
//   quad's object corner (+halfExtent,0,0) through ObjectToWorld·worldToCamera·cameraToClipSpace.
//
//   TOOTH A (genuine eye-distance render flip): render the quad through a Camera op eye=(0,0,5). Assert:
//     - center pixel = RED (origin is on the camera axis → always projects to NDC center, a deep plateau).
//     - the NDC-0.45 probe = BACKGROUND (was RED under default; the farther eye shrank the quad past it).
//     - a probe just INSIDE the shrunk quad (NDC = 0.5·projHalf, deep interior) = RED (the quad still
//       renders, it is only smaller — distinguishes "shrunk" from "vanished/dropped").
//   injectBug = DROP the camera push (clear hasCamera → fall back to default camera) in the REAL emit →
//     the quad stays LARGE (x-half 0.6) → the NDC-0.45 probe reads RED (should be background) → RED.
//
//   TOOTH B (math leg, reuse the field-camera convention): the Camera op's WorldToCamera/CameraToClip-
//     Space for eye=(0,0,5) match lookAtRH/perspectiveFovRH closed-form (a wrong handedness/multiply
//     order/fov is caught analytically). Asserts the projected boundary NDC = halfExtent/(d·tan(fov/2)).
int runCameraSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;  // square → aspect 1
  const float aspect = 1.0f;
  const float kHalf = 0.6f;         // quad object half-extent (Scale=0.6, Stretch=1, Stretch mode)
  const float kEye = 5.0f;          // Camera op eye z (farther than default 2.4142)

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-camera] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  bool allFaithful = true;
  auto readR = [&](const std::vector<uint8_t>& px, int x, int y) -> int {
    x = x < 0 ? 0 : (x >= (int)W ? (int)W - 1 : x);
    y = y < 0 ? 0 : (y >= (int)H ? (int)H - 1 : y);
    return px[((size_t)y * W + x) * 4 + 0];
  };

  // ── Build the Camera-op item EXACTLY as cookCamera would stamp it onto a Layer2d quad ──
  auto makeQuadItem = [&]() {
    RenderDrawItem it{};
    it.color[0] = it.color[1] = it.color[2] = it.color[3] = 1.0f;
    it.width = 1.0f; it.height = 1.0f;   // production unit quad; SRT scale sizes it
    it.layer2dComposeSRT = true;
    it.layerScaleMode = (uint32_t)Layer2dScaleMode::Stretch;
    it.layerScale = kHalf;               // scaleX = Scale·Stretch·1 = 0.6 (Stretch mode, square)
    it.layerStretch[0] = 1.0f; it.layerStretch[1] = 1.0f;
    it.layerRotateDeg = 0.0f;
    it.applyTransform = true;
    return it;
  };
  // The Camera-op stamp (cookCamera output for eye=(0,0,5), default target/up/fov/clip, aspect=output).
  auto stampCamera = [&](RenderDrawItem& it) {
    it.hasCamera = true;
    it.camEye[0] = 0.0f; it.camEye[1] = 0.0f; it.camEye[2] = kEye;
    it.camTarget[0] = it.camTarget[1] = it.camTarget[2] = 0.0f;
    it.camUp[0] = 0.0f; it.camUp[1] = 1.0f; it.camUp[2] = 0.0f;
    it.camFovDeg = kDefaultCamFovDegrees;
    it.camNear = 0.01f; it.camFar = 1000.0f;
    it.camAspect = 0.0f;  // → executor output aspect (square)
  };

  MTL::Texture* src = makeUniformCam(dev, 64, 64, 1.0f, 0.0f, 0.0f, 1.0f);  // solid RED, square

  // HOST-derived expected NDC of the quad's x-half corner under (default) vs (Camera eye=5). Computed
  // via the SAME mat4TransformPointDivW the VS reproduces — NOT hardcoded. ObjectToWorld = S(0.6) (no
  // rotate/translate); worldToCamera/cameraToClipSpace from the two cameras.
  auto projHalfX = [&](float eyeZ) -> float {
    float eye[3] = {0.0f, 0.0f, eyeZ}, tgt[3] = {0, 0, 0}, up[3] = {0, 1, 0};
    Mat4 w2c = lookAtRH(eye, tgt, up);
    Mat4 c2c = perspectiveFovRH(kDefaultCamFovDegrees * 3.14159265358979323846f / 180.0f, aspect,
                                0.01f, 1000.0f);
    Mat4 o2w = mat4Identity();
    o2w.m[0] = kHalf; o2w.m[5] = kHalf;  // S(0.6,0.6,1)
    Mat4 o2c = objectToClipSpace(o2w, w2c, c2c);
    float ndc[3];
    mat4TransformPointDivW(o2c, 1.0f, 0.0f, 0.0f, ndc);  // object corner (+1,0,0) → world (0.6,0,0)
    return ndc[0];
  };
  const float projDefault = projHalfX(defaultCameraDistance());  // expect ≈ 0.6 (d·tan=1 → x/1)
  const float projCamera = projHalfX(kEye);                       // expect ≈ 0.6/2.071 = 0.290
  // The flip probe sits BETWEEN the two projected half-extents (inside default, outside Camera).
  const float kProbe = 0.45f;  // chosen so projCamera < 0.45 < projDefault (deep on both sides)

  // ── TOOTH A: the genuine eye-distance render flip ──
  {
    RenderDrawItem it = makeQuadItem();
    if (!injectBug)
      stampCamera(it);                 // faithful: Camera op pushes eye=(0,0,5)
    else
      it.hasCamera = false;            // ★injectBug: DROP the camera push → default camera (large quad)
    std::vector<uint8_t> px = renderItemCam(dev, lib, q, it, src, W, H);

    int centerR = readR(px, ndcXToPxCam(0.0f, W), ndcYToPxCam(0.0f, H));        // origin on axis → always RED
    int probeR = readR(px, ndcXToPxCam(kProbe, W), ndcYToPxCam(0.0f, H));       // FLIP probe
    int insideR = readR(px, ndcXToPxCam(0.5f * projCamera, W), ndcYToPxCam(0.0f, H));  // deep inside shrunk quad
    // faithful: center RED, probe BACKGROUND (shrunk past it), inside-shrunk RED (quad still drawn).
    bool a = (centerR > 200) && (probeR < 40) && (insideR > 200);
    allFaithful = allFaithful && a;
    std::printf("[selftest-camera] A eye=5: projDefault=%.3f projCamera=%.3f probe@%.2f center=%d(>200) "
                "probe=%d(<40) insideShrunk=%d(>200) -> %s\n", projDefault, projCamera, kProbe,
                centerR, probeR, insideR, a ? "faithful-ok" : "tripped");
  }

  // ── TOOTH B: the math leg (closed-form projection under the non-default eye) ──
  {
    // Closed-form: NDC.x of world x=0.6 at eye z=d, target origin, fov 45° = 0.6 / ((d)·tan(22.5°)).
    // (worldToCamera moves the point to camera z' = -(d-0)= -d → -z'·... ; perspectiveFovRH yScale=
    //  1/tan(fov/2), xScale=yScale/aspect; the divide-by-w with w=-z'=d gives x·xScale/d.) For the
    // default eye d=DefaultCameraDistance, tan(22.5°)·d = 1 → NDC=0.6. For eye=5 → 0.6/(5·tan22.5°).
    const float tanHalf = std::tan(kDefaultCamFovDegrees * 0.5f * 3.14159265358979323846f / 180.0f);
    float expDefault = kHalf / (defaultCameraDistance() * tanHalf);
    float expCamera = kHalf / (kEye * tanHalf);
    bool b = std::fabs(projDefault - expDefault) < 1e-3f && std::fabs(projCamera - expCamera) < 1e-3f &&
             projCamera < projDefault;  // the farther eye MUST shrink the projection
    if (injectBug) {
      // ★injectBug for the math leg: use a WRONG eye (default distance) for the "camera" projection →
      // it no longer shrinks vs default → b flips. (Mirrors a dropped/identity WorldToCamera push.)
      float wrong = projHalfX(defaultCameraDistance());
      b = std::fabs(wrong - expCamera) < 1e-3f;  // wrong-eye projection will NOT equal the eye=5 closed form
    }
    allFaithful = allFaithful && b;
    std::printf("[selftest-camera] B math: projDefault=%.4f(want %.4f) projCamera=%.4f(want %.4f) "
                "shrink=%d -> %s\n", projDefault, expDefault, projCamera, expCamera,
                projCamera < projDefault ? 1 : 0, b ? "faithful-ok" : "tripped");
  }

  src->release();
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-camera] FAIL: injectBug tripped no tooth\n");
      return 1;
    }
    std::printf("[selftest-camera] injectBug correctly RED (dropped camera push → quad stays large → "
                "NDC0.45 probe reads quad-color; wrong-eye math no longer shrinks)\n");
    return 1;
  }
  std::printf("[selftest-camera] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

}  // namespace sw
