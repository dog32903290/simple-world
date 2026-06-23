// OrthographicCamera command op + golden — see point_ops_orthographiccamera.h. TiXL
// Operators/Lib/render/camera/OrthographicCamera.cs. Command subtree in → Command out; stamps an
// ORTHOGRAPHIC camera onto every subtree item (the perspective Camera op's per-item stamp mechanism,
// point_ops_camera.cpp, with camOrtho=true + the Scale/Stretch ortho-size params). The RenderTarget
// executor builds CameraToClipSpace = orthoRH(size) instead of perspectiveFovRH (point_ops_rendertarget.cpp).
#include "runtime/point_ops_orthographiccamera.h"

#include "runtime/field_camera.h"   // Mat4 / lookAtRH / orthoRH / objectToClipSpace / mat4* / defaultCameraDistance
#include "runtime/point_graph.h"    // CmdCookCtx, registerCmdOp/registerTexOp, cookParam/cookVecN, cookResident
#include "runtime/render_command.h" // RenderCommand / RenderDrawItem

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

// cookOrthographicCamera: the perspective Camera op's twin (point_ops_camera.cpp cookCamera) — same gather,
// same innermost-wins stamp — but it sets camOrtho=true + the ortho Scale/Stretch (and leaves FoV unused).
RenderCommand cookOrthographicCamera(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.inputCommand) return rc;     // no subtree wired → empty (TiXL: eval an empty Command)
  rc.items = c.inputCommand->items;   // COPY the subtree (we re-emit it, possibly stamped)

  // OrthographicCamera.cs inputs (v1 scope). Position default = the EXACT .t3 value (faithful, NOT rounded):
  // (-0.0015059264, 0.0014562709, 10.0). Target=(0,0,0); Up=(0,1,0); Scale=1; Stretch=(1,1);
  // NearFarClip=(0.1,1000); AspectRatio default 0 → executor's output aspect (OrthographicCamera.cs:25-28).
  float posDef[3] = {-0.0015059264f, 0.0014562709f, 10.0f};
  float pos[3];
  cookVecN(c, "Position", posDef, 3, pos);
  float tgtDef[3] = {0.0f, 0.0f, 0.0f};
  float tgt[3];
  cookVecN(c, "Target", tgtDef, 3, tgt);
  float upDef[3] = {0.0f, 1.0f, 0.0f};
  float up[3];
  cookVecN(c, "Up", upDef, 3, up);
  float scale = cookParam(c, "Scale", 1.0f);
  float stretchDef[2] = {1.0f, 1.0f};
  float stretch[2];
  cookVecN(c, "Stretch", stretchDef, 2, stretch);
  float clipDef[2] = {0.1f, 1000.0f};
  float clip[2];
  cookVecN(c, "NearFarClip", clipDef, 2, clip);
  float aspect = cookParam(c, "AspectRatio", 0.0f);  // 0 → executor output aspect (cs:25-28)

  for (RenderDrawItem& it : rc.items) {
    if (it.hasCamera) continue;  // a NESTED camera already stamped this item (innermost wins = pop)
    it.hasCamera = true;
    it.camOrtho = true;
    it.camEye[0] = pos[0]; it.camEye[1] = pos[1]; it.camEye[2] = pos[2];
    it.camTarget[0] = tgt[0]; it.camTarget[1] = tgt[1]; it.camTarget[2] = tgt[2];
    it.camUp[0] = up[0]; it.camUp[1] = up[1]; it.camUp[2] = up[2];
    it.camOrthoScale = scale;
    it.camOrthoStretch[0] = stretch[0]; it.camOrthoStretch[1] = stretch[1];
    it.camNear = clip[0];
    it.camFar = clip[1];
    it.camAspect = aspect;  // <=0 → executor uses output aspect
    // camFovDeg is DEAD for ortho (left at its default); the executor ignores it when camOrtho.
  }
  return rc;
}

namespace {
// Test-only flag: drop the ortho flag the op just stamped → the executor falls back to perspectiveFovRH
// (the perspective Camera projection). Mirror of the C0 golden's g_cameraDropStamp (a CPU op flag, NOT a
// shader bug-branch). OFF in production. When set, the quad shrinks (perspective at eye=10) past the probe.
bool g_orthoDropFlag = false;

// Test wrapper over the REAL cookOrthographicCamera, registered over "OrthographicCamera" for the golden so
// the production op stays flag-free. The drop clears camOrtho but keeps the eye/clip stamp → the executor
// builds the PERSPECTIVE matrix from the same eye → the quad shrinks (distance-dependent) instead of staying.
RenderCommand cookOrthoForTest(CmdCookCtx& c) {
  RenderCommand rc = cookOrthographicCamera(c);
  if (g_orthoDropFlag)
    for (RenderDrawItem& it : rc.items)
      if (it.hasCamera) it.camOrtho = false;  // → executor perspectiveFovRH (distance-dependent shrink)
  return rc;
}

void cookSolidImageOrtho(TexCookCtx& c) {
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

void installOrthoSpecs() {
  std::map<std::string, NodeSpec> dyn;
  dyn["SolidImage"] = atomicSpec("SolidImage", {{"out", "out", "Texture2D", false}});
  dyn["Layer2d"] = atomicSpec("Layer2d",
      {{"Image", "Image", "Texture2D", true},
       {"out", "out", "Command", false},
       {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
       {"ScaleMode", "ScaleMode", "Float", true, 0.0f, 0.0f, 5.0f, Widget::Enum, {}, true},
       {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {}, true}});
  dyn["OrthographicCamera"] = atomicSpec("OrthographicCamera",
      {{"Command", "Command", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
       {"out", "out", "Command", false},
       {"Position.x", "Position", "Float", true, -0.0015059264f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
       {"Position.y", "Position.y", "Float", true, 0.0014562709f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
       {"Position.z", "Position.z", "Float", true, 10.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
       {"Scale", "Scale", "Float", true, 1.0f, 0.0001f, 100.0f, Widget::Slider, {}, true}});
  dyn["RenderTarget"] = atomicSpec("RenderTarget",
      {{"command", "command", "Command", true},
       {"out", "out", "Texture2D", false},
       {"Resolution", "Resolution", "Float", true, 4.0f, 0.0f, 4.0f, Widget::Enum, {}, true},
       {"CustomW", "CustomW", "Float", true, 256.0f, 1.0f, 4096.0f, Widget::Slider, {}, true},
       {"CustomH", "CustomH", "Float", true, 256.0f, 1.0f, 4096.0f, Widget::Slider, {}, true}});
  setDynamicSpecs(std::move(dyn));
}

// SolidRed(1) → Layer2d(2,Scale=0.6) → OrthographicCamera(3,Scale,eye) → RenderTarget(4 terminal).
Graph buildOrthoGraph(float eyeZ, float quadScale, float orthoScale, uint32_t W, uint32_t H) {
  Graph g;
  Node sa; sa.id = 1; sa.type = "SolidImage"; g.nodes.push_back(sa);
  Node l; l.id = 2; l.type = "Layer2d";
  l.params["Scale"] = quadScale; l.params["ScaleMode"] = 4.0f; l.params["BlendMode"] = 0.0f;  // Stretch/Normal
  g.nodes.push_back(l);
  Node cam; cam.id = 3; cam.type = "OrthographicCamera";
  cam.params["Position.x"] = 0.0f; cam.params["Position.y"] = 0.0f; cam.params["Position.z"] = eyeZ;
  cam.params["Scale"] = orthoScale;
  g.nodes.push_back(cam);
  Node rt; rt.id = 4; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
  g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, outPortIdx("SolidImage")), pinId(2, inPortIdx("Layer2d", "Texture2D"))});
  g.connections.push_back({102, pinId(2, outPortIdx("Layer2d")), pinId(3, inPortIdx("OrthographicCamera", "Command"))});
  g.connections.push_back({103, pinId(3, outPortIdx("OrthographicCamera")), pinId(4, inPortIdx("RenderTarget", "Command"))});
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

// HOST-derived ortho projected x-half (the SAME orthoRH/objectToClipSpace the executor's VS reproduces).
// ObjectToWorld = S(kHalf). worldToCamera = LookAtRH(eye). cameraToClipSpace = orthoRH(size, size) (square).
float orthoProjHalfX(float eyeZ, float kHalf, float orthoSize) {
  float eye[3] = {0, 0, eyeZ}, tgt[3] = {0, 0, 0}, up[3] = {0, 1, 0};
  Mat4 w2c = lookAtRH(eye, tgt, up);
  Mat4 c2c = orthoRH(orthoSize, orthoSize, 0.1f, 1000.0f);
  Mat4 o2w = mat4Identity(); o2w.m[0] = kHalf; o2w.m[5] = kHalf;
  Mat4 o2c = objectToClipSpace(o2w, w2c, c2c);
  float ndc[3]; mat4TransformPointDivW(o2c, 1.0f, 0.0f, 0.0f, ndc);  // object corner (+1,0,0) → world (kHalf,0,0)
  return ndc[0];
}
}  // namespace

void registerOrthographicCameraOp() { registerCmdOp("OrthographicCamera", cookOrthographicCamera); }

int runOrthographicCameraSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;   // square → aspect 1
  const float kHalf = 0.6f;          // Layer2d Scale → quad object half-extent 0.6 (Stretch, square)
  const float kOrthoScale = 2.0f;    // OrthographicCamera Scale → ortho view width = Scale·aspect = 2
  const float kEyeNear = 10.0f;      // OrthographicCamera default eye z
  const float kEyeFar = 20.0f;       // a FARTHER eye — ortho is distance-INVARIANT, perspective would shrink

  // ── TOOTH B (host math): distance-invariance + the closed-form ortho boundary. ──
  // size = Scale·aspect = 2 (square). NDC.x of world x-half 0.6 = 0.6·(2/size) = 0.6·(2/2) = 0.6, INDEPENDENT
  // of eye z. So proj(eye=10) == proj(eye=20) == 0.6. A perspective matrix would give different values.
  const float orthoSize = kOrthoScale * 1.0f;  // square aspect
  float projNear = orthoProjHalfX(kEyeNear, kHalf, orthoSize);
  float projFar = orthoProjHalfX(kEyeFar, kHalf, orthoSize);
  float expBoundary = kHalf * (2.0f / orthoSize);  // closed form: world_x · M11, M11 = 2/size
  bool b = std::fabs(projNear - expBoundary) < 1e-4f && std::fabs(projFar - expBoundary) < 1e-4f &&
           std::fabs(projNear - projFar) < 1e-5f;  // the distance-invariance signature
  if (injectBug) {
    // ★bug for the math leg: project the FAR eye through a PERSPECTIVE matrix instead → it shrinks (distance-
    // dependent) → no longer equals the ortho boundary → the invariance breaks.
    float eye[3] = {0, 0, kEyeFar}, tgt[3] = {0, 0, 0}, up[3] = {0, 1, 0};
    Mat4 w2c = lookAtRH(eye, tgt, up);
    Mat4 c2c = perspectiveFovRH(kDefaultCamFovDegrees * 3.14159265358979323846f / 180.0f, 1.0f, 0.1f, 1000.0f);
    Mat4 o2w = mat4Identity(); o2w.m[0] = kHalf; o2w.m[5] = kHalf;
    Mat4 o2c = objectToClipSpace(o2w, w2c, c2c);
    float ndc[3]; mat4TransformPointDivW(o2c, 1.0f, 0.0f, 0.0f, ndc);
    b = std::fabs(ndc[0] - expBoundary) < 1e-4f;  // a perspective far-eye proj will NOT equal the ortho boundary
  }
  std::printf("[selftest-orthographiccamera] B math: projNear=%.4f projFar=%.4f want=%.4f invariant=%d -> %s\n",
              projNear, projFar, expBoundary, std::fabs(projNear - projFar) < 1e-5f ? 1 : 0,
              b ? "faithful-ok" : "tripped");

  // ── TOOTH A (GPU render flip, through the PRODUCTION RESIDENT terminal). ──
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-orthographiccamera] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();
  registerCmdOp("OrthographicCamera", cookOrthoForTest);  // OVERRIDE with the flag-drop test wrapper
  registerTexOp("SolidImage", cookSolidImageOrtho);       // test source
  installOrthoSpecs();

  // Under ortho(Scale=2) the quad x-half projects to NDC 0.6 (above). A perspective Camera at eye=10 would
  // shrink it to 0.6/(10·tan22.5°)=0.145. Probe NDC 0.45: INSIDE the ortho quad (RED), OUTSIDE perspective
  // (background). center (0,0) is always RED (origin on axis). So the SAME probe flips faithful(ortho→RED)
  // vs -bug(perspective→background) — the ortho-vs-perspective bite through the resident dispatch.
  const float kProbe = 0.45f;

  g_orthoDropFlag = injectBug;  // ★bug: drop the ortho flag → executor uses perspectiveFovRH → quad shrinks

  Graph g = buildOrthoGraph(kEyeNear, kHalf, kOrthoScale, W, H);
  SymbolLibrary slib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
  PointGraph pg(dev, lib, q, W, H);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cookResident(rg, ctx, nullptr, /*RenderTarget path=*/"4");

  int centerR = readTargetR(pg, W, H, 0.0f, 0.0f);   // origin on axis → always RED
  int probeR = readTargetR(pg, W, H, kProbe, 0.0f);  // FLIP probe (RED under ortho, background under perspective)
  // faithful: center RED, probe RED (the ortho quad x-half 0.6 covers NDC 0.45). injectBug: probe background.
  bool a = (centerR > 200) && (probeR > 200);
  std::printf("[selftest-orthographiccamera] A RESIDENT ortho(Scale=2,eye=10): projBoundary=%.3f probe@%.2f "
              "center=%d(>200) probe=%d(>200) -> %s\n", expBoundary, kProbe, centerR, probeR,
              a ? "faithful-ok" : "tripped");

  g_orthoDropFlag = false;  // reset (process hygiene)
  setDynamicSpecs({});
  lib->release(); q->release(); dev->release(); pool->release();

  bool allFaithful = a && b;
  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-orthographiccamera] FAIL: injectBug tripped no tooth\n");
      return 1;
    }
    std::printf("[selftest-orthographiccamera] injectBug correctly RED (dropped ortho flag → perspective "
                "shrinks the quad past NDC0.45 → probe reads background; far-eye perspective math no longer "
                "matches the distance-invariant ortho boundary)\n");
    return 1;
  }
  std::printf("[selftest-orthographiccamera] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

}  // namespace sw
