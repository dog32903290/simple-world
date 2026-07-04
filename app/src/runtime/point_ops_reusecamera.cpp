// ReuseCamera command op + golden — see point_ops_reusecamera.h.
// TiXL authority: external/tixl/Operators/Lib/render/camera/ReuseCamera.cs (GUID 484bec1b).
#include "runtime/point_ops_reusecamera.h"

#include "runtime/field_camera.h"    // defaultCameraDistance/kDefaultCamFovDegrees (golden probes)
#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp/registerTexOp, cook()/cookResident
#include "runtime/render_command.h"  // RenderCommand / RenderDrawItem

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph/Node/NodeSpec/PortSpec/pinId/setDynamicSpecs/findSpec
#include "runtime/graph_bridge.h"         // libFromGraph
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// cookReuseCamera: Command subtree in → Command out through the REFERENCED camera (cc.refCam*, resolved
// by the driver off the "Object" wire). Missing/invalid reference → EMPTY chain (ReuseCamera.cs:17-29:
// warn + the subtree is not rendered). Valid → stamp every !hasCamera item (innermost wins), exactly
// cookCamera's push (:31-41 — TiXL pushes the referenced WorldToCamera/CameraToClipSpace; the executor
// rebuilds the same pair from these raw params).
RenderCommand cookReuseCamera(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.hasRefCamera) return rc;   // cs:17-21/:25-29 — no/invalid reference: subtree dropped
  if (!c.inputCommand) return rc;   // no wired subtree → empty (TiXL: eval an empty Command)
  rc.items = c.inputCommand->items;
  for (RenderDrawItem& it : rc.items) {
    if (it.hasCamera) continue;  // a NESTED camera already stamped this item (innermost wins = pop)
    it.hasCamera = true;
    it.camEye[0] = c.refCamEye[0]; it.camEye[1] = c.refCamEye[1]; it.camEye[2] = c.refCamEye[2];
    it.camTarget[0] = c.refCamTarget[0]; it.camTarget[1] = c.refCamTarget[1];
    it.camTarget[2] = c.refCamTarget[2];
    it.camUp[0] = c.refCamUp[0]; it.camUp[1] = c.refCamUp[1]; it.camUp[2] = c.refCamUp[2];
    it.camFovDeg = c.refCamFovDeg;
    it.camNear = c.refCamNear;
    it.camFar = c.refCamFar;
    it.camAspect = c.refCamAspect;  // <=0 → executor output aspect (Camera.cs:53-55)
  }
  return rc;
}

void registerReuseCameraOp() { registerCmdOp("ReuseCamera", cookReuseCamera); }

// ───────────────────────────────── GOLDEN ─────────────────────────────────
namespace {
// ★injectBug: drop the referenced-camera stamp in the REAL cook (items pass through unstamped → the
// executor falls back to the DEFAULT camera — the exact corruption of a lost :31-41 push). OFF in prod.
bool g_reuseDropStamp = false;

RenderCommand cookReuseForTest(CmdCookCtx& c) {
  if (g_reuseDropStamp) {
    RenderCommand rc;
    if (c.inputCommand) rc.items = c.inputCommand->items;  // push lost → default camera renders it big
    return rc;
  }
  return cookReuseCamera(c);
}

void cookSolidImageRc(TexCookCtx& c) {
  if (!c.output) return;
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(1.0, 0.0, 0.0, 1.0));  // solid RED
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  cmd->renderCommandEncoder(pass)->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

NodeSpec atomicSpecRc(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}
int outPortIdxRc(const char* type, const char* dataType) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (!s->ports[i].isInput && s->ports[i].dataType == dataType) return (int)i;
  return -1;
}
int inPortIdxRc(const char* type, const char* dataType) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (s->ports[i].isInput && s->ports[i].dataType == dataType) return (int)i;
  return -1;
}

void installReuseSpecs() {
  std::map<std::string, NodeSpec> dyn;
  dyn["SolidImage"] = atomicSpecRc("SolidImage", {{"out", "out", "Texture2D", false}});
  dyn["Layer2d"] = atomicSpecRc("Layer2d",
      {{"Image", "Image", "Texture2D", true},
       {"out", "out", "Command", false},
       {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
       {"ScaleMode", "ScaleMode", "Float", true, 0.0f, 0.0f, 5.0f, Widget::Enum, {}, true},
       {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {}, true}});
  setDynamicSpecs(std::move(dyn));
}

// SolidRed(1) → Layer2d(2, half 0.6) → ReuseCamera(3) → RenderTarget(5); Camera(4, eye z) standalone,
// Camera.Reference (Object out) → ReuseCamera.CameraReference (Object in). `wireRef` = false builds the
// MISSING-REFERENCE leg (cs:17-21: the subtree must not render).
Graph buildReuseGraph(float eyeZ, bool wireRef, uint32_t W, uint32_t H) {
  Graph g;
  Node sa; sa.id = 1; sa.type = "SolidImage"; g.nodes.push_back(sa);
  Node l; l.id = 2; l.type = "Layer2d";
  l.params["Scale"] = 0.6f; l.params["ScaleMode"] = 4.0f; l.params["BlendMode"] = 0.0f;  // Stretch/Normal
  g.nodes.push_back(l);
  Node rc; rc.id = 3; rc.type = "ReuseCamera"; g.nodes.push_back(rc);
  Node cam; cam.id = 4; cam.type = "Camera";  // the REFERENCED camera; its own subtree stays unwired
  cam.params["Position.x"] = 0.0f; cam.params["Position.y"] = 0.0f; cam.params["Position.z"] = eyeZ;
  g.nodes.push_back(cam);
  Node rt; rt.id = 5; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
  g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, outPortIdxRc("SolidImage", "Texture2D")),
                           pinId(2, inPortIdxRc("Layer2d", "Texture2D"))});
  g.connections.push_back({102, pinId(2, outPortIdxRc("Layer2d", "Command")),
                           pinId(3, inPortIdxRc("ReuseCamera", "Command"))});
  if (wireRef)
    g.connections.push_back({103, pinId(4, outPortIdxRc("Camera", "Object")),
                             pinId(3, inPortIdxRc("ReuseCamera", "Object"))});
  g.connections.push_back({104, pinId(3, outPortIdxRc("ReuseCamera", "Command")),
                           pinId(5, inPortIdxRc("RenderTarget", "Command"))});
  return g;
}

int readTargetRRc(PointGraph& pg, uint32_t W, uint32_t H, float ndcX, float ndcY) {
  MTL::Texture* tex = pg.target();
  if (!tex || (uint32_t)tex->width() != W || (uint32_t)tex->height() != H) return -1;
  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  int x = (int)((ndcX * 0.5f + 0.5f) * (float)(W - 1) + 0.5f);
  int y = (int)((1.0f - (ndcY * 0.5f + 0.5f)) * (float)(H - 1) + 0.5f);
  x = x < 0 ? 0 : (x >= (int)W ? (int)W - 1 : x);
  y = y < 0 ? 0 : (y >= (int)H ? (int)H - 1 : y);
  return px[((size_t)y * W + x) * 4 + 0];
}
}  // namespace

// --selftest-reusecamera: the eye=5 shrink flip on BOTH driver legs + the missing-reference drop.
// Closed-form probe geometry (the Camera golden's TOOTH B formula, NDC = halfExtent/(d·tan(fov/2))):
// default eye d=2.4142 → d·tan22.5°=1 → quad half NDC 0.6; referenced eye=5 → 0.6/2.071=0.290. The
// NDC-0.45 probe sits between (≥0.15 from both edges); the inside probe 0.145 = half the shrunk extent.
int runReuseCameraSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;  // square → aspect 1
  const float kEye = 5.0f;
  const float kProbeFlip = 0.45f;   // inside the default-camera quad (0.6), outside the eye=5 quad (0.290)
  const float tanHalf = std::tan(kDefaultCamFovDegrees * 0.5f * 3.14159265358979323846f / 180.0f);
  const float projRef = 0.6f / (kEye * tanHalf);  // 0.290 closed form (ICamera PerspectiveFovRH)
  const float kProbeInside = 0.5f * projRef;      // deep inside the shrunk quad

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-reusecamera] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();
  registerCmdOp("ReuseCamera", cookReuseForTest);  // OVERRIDE with the stamp-drop test wrapper
  registerTexOp("SolidImage", cookSolidImageRc);   // test source
  installReuseSpecs();

  g_reuseDropStamp = injectBug;  // ★bug: drop the referenced push → default camera keeps the quad LARGE

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  bool allFaithful = true;

  // ── LEG A: wired reference, BOTH cook legs (the Object gather is per-leg driver code). ──
  for (int leg = 0; leg < 2; ++leg) {
    Graph g = buildReuseGraph(kEye, /*wireRef=*/true, W, H);
    PointGraph pg(dev, lib, q, W, H);
    if (leg == 0) {
      pg.cook(g, ctx, nullptr, /*RenderTarget id=*/5);  // FLAT leg
    } else {
      SymbolLibrary slib = libFromGraph(g);
      ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
      pg.cookResident(rg, ctx, nullptr, /*path=*/"5");  // RESIDENT leg (production)
    }
    int centerR = readTargetRRc(pg, W, H, 0.0f, 0.0f);          // on-axis → always quad color
    int flipR = readTargetRRc(pg, W, H, kProbeFlip, 0.0f);      // background under eye=5, RED under default
    int insideR = readTargetRRc(pg, W, H, kProbeInside, 0.0f);  // still RED (shrunk, not vanished)
    bool ok = (centerR > 200) && (flipR < 40) && (insideR > 200);
    allFaithful = allFaithful && ok;
    std::printf("[selftest-reusecamera] A %s eye=%g: projRef=%.3f center=%d(>200) flip@%.2f=%d(<40) "
                "inside=%d(>200) -> %s\n", leg == 0 ? "FLAT" : "RESIDENT", kEye, projRef, centerR,
                kProbeFlip, flipR, insideR, ok ? "faithful-ok" : "tripped");
  }

  // ── LEG B: MISSING reference (cs:17-21) — the subtree must NOT render. ──
  // (The -bug wrapper ignores the reference entirely, so this leg ALSO trips under injectBug: the
  //  dropped guard renders the quad that TiXL's warn-return would skip.)
  {
    Graph g = buildReuseGraph(kEye, /*wireRef=*/false, W, H);
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    PointGraph pg(dev, lib, q, W, H);
    pg.cookResident(rg, ctx, nullptr, /*path=*/"5");
    int centerR = readTargetRRc(pg, W, H, 0.0f, 0.0f);
    bool ok = (centerR < 40);  // nothing rendered
    allFaithful = allFaithful && ok;
    std::printf("[selftest-reusecamera] B missing-ref: center=%d(<40) -> %s\n", centerR,
                ok ? "faithful-ok" : "tripped");
  }

  g_reuseDropStamp = false;  // reset (process hygiene)
  setDynamicSpecs({});
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-reusecamera] FAIL: injectBug tripped no tooth\n");
      return 0;  // did-not-trip → NO-BITE latch catches the dead tooth (GOLDEN_STANDARD polarity)
    }
    std::printf("[selftest-reusecamera] injectBug correctly RED (dropped referenced push → default "
                "camera keeps the quad large → the 0.45 probe reads quad color; the missing-ref guard "
                "is gone → leg B renders)\n");
    return 1;
  }
  std::printf("[selftest-reusecamera] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

}  // namespace sw
