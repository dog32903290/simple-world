// C0 resident-ratify golden (CAMERA3D_BLUEPRINT §2 C0). Split from point_ops_camera_scope_golden.cpp to keep
// each golden file ≤400 (ARCHITECTURE.md rule 4); C1 (the point-camera scope) lives there, C0 (the Command-
// rail camera through the RESIDENT terminal) lives here.
//
// runCameraSelfTest (point_ops_camera.cpp) builds the item by hand + calls cookRenderTarget directly — it
// NEVER exercises resident cookCommand's dispatch of cookCamera. C0 cooks Camera→Layer2d→RenderTarget through
// cookResident (the PRODUCTION terminal) and asserts the eye-distance flip: a farther eye SHRINKS the quad
// past a mid-radius probe. This is the only golden that proves the resident Command-rail camera dispatch is
// not a black-hole (blueprint §3 checklist + §5 risk 7).
//
// injectBug: the DRAW rail uses the per-item STAMP (not the C1 scope), so C0's -bug drops the Camera op's
// stamp via a CPU op flag (g_cameraDropStamp, the group golden's g_groupDropPush precedent — a CPU flag, NOT
// a shader bug-branch) → the resident executor falls back to the default camera → the quad stays large → the
// NDC0.45 probe reads RED (should be background) → RED. A resident dispatch that lost cookCamera is caught here.
//
// runtime leaf: pure CPU + Metal. SolidImage is the test source fixture; Layer2d/Camera/RenderTarget are REAL
// builtins (Camera re-registered with the cookCameraForTest wrapper for the drop flag).
#include "runtime/point_ops.h"

#include "runtime/field_camera.h"   // Mat4 / lookAtRH / perspectiveFovRH / objectToClipSpace / mat4Identity / mat4TransformPointDivW / defaultCameraDistance / kDefaultCamFovDegrees
#include "runtime/point_graph.h"    // PointGraph::cookResident, registerBuiltinPointOps/registerCmdOp/registerTexOp, CmdCookCtx, TexCookCtx
#include "runtime/point_ops_camera_scope.h"  // runCameraResidentSelfTest decl

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph/Node/NodeSpec/PortSpec/pinId/setDynamicSpecs/findSpec
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph → SymbolLibrary)
#include "runtime/render_command.h"       // RenderCommand / RenderDrawItem
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

NodeSpec atomicSpec(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}
int outPortIdx(const char* type) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (!s->ports[i].isInput) return (int)i;
  return -1;
}
int inPortIdx(const char* type, const char* dataType) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (s->ports[i].isInput && s->ports[i].dataType == dataType) return (int)i;
  return -1;
}

// Test-only flag: drop the Camera op's per-item STAMP (the DRAW-rail push) so the executor falls back to the
// default camera. Mirror of the group golden's g_groupDropPush (a CPU op flag, NOT a shader bug-branch). OFF
// in production.
bool g_cameraDropStamp = false;

// Test wrapper around the REAL cookCamera (point_ops_camera.cpp), registered over "Camera" for C0 so the
// production op stays flag-free. The drop clears the stamp the real op just applied → the executor uses the
// default camera → the quad does not shrink. The gather stays real; only the camera stamp is lost.
RenderCommand cookCameraForTest(CmdCookCtx& c) {
  RenderCommand rc = cookCamera(c);
  if (g_cameraDropStamp)
    for (RenderDrawItem& it : rc.items) it.hasCamera = false;  // → executor defaultLayerCameraForward
  return rc;
}

void cookSolidImageC0(TexCookCtx& c) {
  if (!c.output) return;
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(1.0, 0.0, 0.0, 1.0));  // RED, opaque
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  cmd->renderCommandEncoder(pass)->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

void installC0Specs() {
  std::map<std::string, NodeSpec> dyn;
  dyn["SolidImage"] = atomicSpec("SolidImage", {{"out", "out", "Texture2D", false}});
  dyn["Layer2d"] = atomicSpec("Layer2d",
      {{"Image", "Image", "Texture2D", true},
       {"out", "out", "Command", false},
       {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
       {"ScaleMode", "ScaleMode", "Float", true, 0.0f, 0.0f, 5.0f, Widget::Enum, {}, true},
       {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {}, true}});
  dyn["Camera"] = atomicSpec("Camera",
      {{"Command", "Command", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
       {"out", "out", "Command", false},
       {"Position.x", "Position", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
       {"Position.y", "Position.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
       {"Position.z", "Position.z", "Float", true, defaultCameraDistance(), -100.0f, 100.0f, Widget::Vec, {}, true, 1},
       {"Target.x", "Target", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
       {"Target.y", "Target.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
       {"Target.z", "Target.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
       {"Up.x", "Up", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
       {"Up.y", "Up.y", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
       {"Up.z", "Up.z", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
       {"FieldOfView", "FieldOfView", "Float", true, 45.0f, 1.0f, 179.0f, Widget::Slider, {}, true},
       {"ClipPlanes.x", "ClipPlanes", "Float", true, 0.01f, 0.0001f, 100.0f, Widget::Vec, {}, true, 2},
       {"ClipPlanes.y", "ClipPlanes.y", "Float", true, 1000.0f, 1.0f, 100000.0f, Widget::Vec, {}, true, 1},
       {"AspectRatio", "AspectRatio", "Float", true, 0.0f, 0.0f, 10.0f, Widget::Slider, {}, true}});
  dyn["RenderTarget"] = atomicSpec("RenderTarget",
      {{"command", "command", "Command", true},
       {"out", "out", "Texture2D", false},
       {"Resolution", "Resolution", "Float", true, 4.0f, 0.0f, 4.0f, Widget::Enum, {}, true},
       {"CustomW", "CustomW", "Float", true, 256.0f, 1.0f, 4096.0f, Widget::Slider, {}, true},
       {"CustomH", "CustomH", "Float", true, 256.0f, 1.0f, 4096.0f, Widget::Slider, {}, true}});
  setDynamicSpecs(std::move(dyn));
}

// SolidRed(1) → Layer2d(2,Scale=0.6) → Camera(3,eye) → RenderTarget(4 terminal).
Graph buildC0Graph(float eyeZ, float quadScale, uint32_t W, uint32_t H) {
  Graph g;
  Node sa; sa.id = 1; sa.type = "SolidImage"; g.nodes.push_back(sa);
  Node l; l.id = 2; l.type = "Layer2d";
  l.params["Scale"] = quadScale; l.params["ScaleMode"] = 4.0f; l.params["BlendMode"] = 0.0f;  // Stretch/Normal
  g.nodes.push_back(l);
  Node cam; cam.id = 3; cam.type = "Camera";
  cam.params["Position.x"] = 0.0f; cam.params["Position.y"] = 0.0f; cam.params["Position.z"] = eyeZ;
  g.nodes.push_back(cam);
  Node rt; rt.id = 4; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
  g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, outPortIdx("SolidImage")), pinId(2, inPortIdx("Layer2d", "Texture2D"))});
  g.connections.push_back({102, pinId(2, outPortIdx("Layer2d")), pinId(3, inPortIdx("Camera", "Command"))});
  g.connections.push_back({103, pinId(3, outPortIdx("Camera")), pinId(4, inPortIdx("RenderTarget", "Command"))});
  return g;
}

int readTargetR(PointGraph& pg, uint32_t W, uint32_t H, float ndcX, float ndcY) {
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

int runCameraResidentSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;  // square → aspect 1
  const float kHalf = 0.6f;         // Layer2d Scale → quad object half-extent 0.6 (Stretch, square)
  const float kEye = 5.0f;          // Camera op eye z (farther than default 2.4142)

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-camera-resident] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();
  registerCmdOp("Camera", cookCameraForTest);    // OVERRIDE Camera with the stamp-drop test wrapper
  registerTexOp("SolidImage", cookSolidImageC0); // test source
  installC0Specs();

  // HOST-derived projected x-half under the Camera eye=5 (the SAME mat4TransformPointDivW the VS reproduces).
  // At eye=5, fov=45° → x-half 0.6 projects to 0.6/(5·tan22.5°)=0.290. Probe NDC 0.45: under the Camera the
  // quad shrank past it (background); under the DEFAULT camera (the -bug drop) the quad x-half is 0.6 → 0.45
  // is INSIDE (RED). So the SAME probe flips between faithful (background) and -bug (RED) — the resident-leg
  // bite. center (0,0) is always RED (origin on the camera axis → NDC center, a deep plateau).
  auto projHalfX = [&](float eyeZ) -> float {
    float eye[3] = {0, 0, eyeZ}, tgt[3] = {0, 0, 0}, up[3] = {0, 1, 0};
    Mat4 w2c = lookAtRH(eye, tgt, up);
    Mat4 c2c = perspectiveFovRH(kDefaultCamFovDegrees * 3.14159265358979323846f / 180.0f, 1.0f, 0.01f, 1000.0f);
    Mat4 o2w = mat4Identity(); o2w.m[0] = kHalf; o2w.m[5] = kHalf;
    Mat4 o2c = objectToClipSpace(o2w, w2c, c2c);
    float ndc[3]; mat4TransformPointDivW(o2c, 1.0f, 0.0f, 0.0f, ndc);
    return ndc[0];
  };
  const float projCamera = projHalfX(kEye);  // ≈ 0.290
  const float kProbe = 0.45f;                 // between projCamera (0.29) and the default x-half (0.6)

  g_cameraDropStamp = injectBug;  // ★bug: drop the Camera stamp → executor uses default → quad stays large

  // Cook Camera→Layer2d→RenderTarget through the RESIDENT terminal (the production cookCommand dispatch of
  // cookCamera — the path runCameraSelfTest's by-hand harness never exercises).
  Graph g = buildC0Graph(kEye, kHalf, W, H);
  SymbolLibrary slib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
  PointGraph pg(dev, lib, q, W, H);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cookResident(rg, ctx, nullptr, /*RenderTarget path=*/"4");

  int centerR = readTargetR(pg, W, H, 0.0f, 0.0f);   // origin on axis → always RED
  int probeR = readTargetR(pg, W, H, kProbe, 0.0f);  // FLIP probe
  // faithful: center RED, probe BACKGROUND (the Camera shrank the quad past it via the resident dispatch).
  bool pass = (centerR > 200) && (probeR < 40);
  std::printf("[selftest-camera-resident] RESIDENT Camera→Layer2d: projCamera=%.3f probe@%.2f center=%d(>200) "
              "probe=%d(<40) -> %s\n", projCamera, kProbe, centerR, probeR, pass ? "PASS" : "FAIL");

  g_cameraDropStamp = false;  // reset (process hygiene)
  setDynamicSpecs({});
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (pass) {
      std::printf("[selftest-camera-resident] FAIL: injectBug still passed (the resident dispatch shrank the "
                  "quad despite the dropped Camera stamp — the resident cookCamera dispatch is not live)\n");
      return 1;
    }
    std::printf("[selftest-camera-resident] injectBug correctly RED (dropped Camera stamp → resident executor "
                "used the default camera → quad stayed large → NDC0.45 probe read RED)\n");
    return 1;
  }
  return pass ? 0 : 1;
}

}  // namespace sw
