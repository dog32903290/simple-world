// C1 + C0 camera-seam HARD-GATE goldens (CAMERA3D_BLUEPRINT §1/§2). Split out of point_ops_camera.cpp to
// keep that file ≤400 (ARCHITECTURE.md rule 4). The mechanism under test (the active-camera scope) lives in
// point_ops_camera.cpp; these two goldens drive it through REAL graphs on BOTH cook legs.
//
// ── C1 (--selftest-camera-scope): the point-camera hole is CLOSED ──
//   THE SEAM: fillPointCamera (point_graph_internal.h) now reads the live ActiveCamera a wired Camera op set
//   around its SubGraph cook (point_ops_camera.cpp LiveCameraScope), instead of ALWAYS the default camera.
//   So SamplePointsByCameraDistance, cooked UNDER a Camera op, weights each point's W by the WIRED camera's
//   depth — and a Camera-MOVE changes the result. Before C1 it always used the default → a Camera move did
//   nothing (the live black-hole).
//
//   GRAPH:  RadialPoints(W=1 grid) → SamplePointsByCameraDistance → Camera(eye=z) → [read]
//   The Camera op wraps the point chain (its Command subtree); the point op cooks UNDER the live camera.
//   SamplePointsByCameraDistance scales p.W(FX1) by a linear-0→1 curve sampled at normalized camera depth
//   (NearRange=0, FarRange=10): W = (-ObjectToCamera·pos).z / 10. A FARTHER eye → a LARGER |depth| → a
//   LARGER W. So the SAME point gets a DIFFERENT W under eye=2.4142 (default-equiv) vs eye=8.
//
//   FLAT tooth (exact, byte-read): cook the graph twice (eye=2.4142 vs eye=8) through the flat terminal;
//   byte-read SamplePointsByCameraDistance's output FX1; assert W(eye=8) > W(eye=2.4142) AND each matches the
//   HOST closed-form for THAT eye (computed via activeCameraMatrices + mat4TransformPointDivW, NOT hardcoded).
//   RESIDENT tooth (production leg LIVES): cook the same two graphs through the resident terminal; byte-read
//   is flat-only, so the resident leg asserts the rendered W-tied sprite output CHANGES between the two eyes
//   (DrawPoints2 UseWForSize=1 → a larger W → a larger sprite → a different lit-pixel count). Both legs.
//
//   injectBug (cameraScopeBugSkipPush): BOTH legs skip the scope → the point op reads the DEFAULT camera
//   regardless of the wired eye → W(eye=8) == W(eye=2.4142) (flat) and lit(eye=8) == lit(eye=default) on the
//   resident leg → the camera-move change DISAPPEARS → RED on flat AND resident (the S2c blood lesson: a
//   resident-only miss = a prod black-hole, so the resident leg is its own assertion).
//
// ── C0 (--selftest-camera-resident): the Command-rail camera survives the RESIDENT terminal ──
//   runCameraSelfTest (point_ops_camera.cpp) builds the item by hand + calls cookRenderTarget directly — it
//   never exercises resident cookCommand's dispatch of cookCamera. C0 cooks Camera→Layer2d→RenderTarget
//   through cookResident (the production terminal) and asserts the eye-distance flip: a farther eye SHRINKS
//   the quad past a mid-radius probe (blueprint §2 C0). injectBug skips the scope is irrelevant here (the
//   DRAW rail uses the per-item stamp, not the scope) — so C0's -bug drops the Camera op's stamp via a CPU
//   flag mirror, exactly like the group golden's g_groupDropPush, so the resident dispatch miss bites.
//
// runtime leaf: pure CPU + Metal (cooks through PointGraph). SolidImage is the test source fixture (the
// group/layercompose precedent); Layer2d/Camera/SamplePointsByCameraDistance/DrawPoints2/RenderTarget are
// REAL builtins under test.
#include "runtime/point_ops.h"

#include "runtime/field_camera.h"             // Mat4 / mat4TransformPointDivW / objectToClipSpace / defaultCameraDistance
#include "runtime/point_graph.h"              // PointGraph::cook/cookResident, registerBuiltinPointOps, debugCookedBuffer
#include "runtime/point_ops_camera_scope.h"   // ActiveCamera / activeCameraMatrices / cameraScopeBugSkipPush

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph/Node/NodeSpec/PortSpec/pinId/setDynamicSpecs/findSpec
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph → SymbolLibrary)
#include "runtime/render_command.h"       // RenderCommand / RenderDrawItem
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/tixl_point.h"           // EvaluationContext / SwPoint

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

NodeSpec atomicSpec(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}

// A grid of W(FX1)=1 points at z=0 (mirror of the SamplePointsByCameraDistance golden's gridPointsW1Gen).
// GPU RadialPoints hardcodes FX1=0 (no W to scale), so the W-tied chain needs a gen that emits non-zero W.
void gridPointsW1Gen(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  SwPoint* dst = (SwPoint*)c.output->contents();
  for (uint32_t i = 0; i < c.count; ++i) {
    float fx = (float)(i % 8) / 7.0f - 0.5f, fy = (float)((i / 8) % 8) / 7.0f - 0.5f;  // [-0.5,0.5]
    dst[i] = SwPoint{};
    dst[i].Color = {1, 1, 1, 1}; dst[i].Scale = {1, 1, 1};
    dst[i].FX1 = 1.0f;  // W = 1 — the op scales this by the depth curve under the live camera
    dst[i].Position = {0.6f * fx, 0.6f * fy, 0.0f};
  }
}

// ─────────────────────────────── C1 graph build ───────────────────────────────
// RadialPoints(1) → SamplePointsByCameraDistance(2) → Camera(3,eye) → RenderTarget(4 terminal) ; plus a
// DrawPoints2(5) between the point op and Camera for the RESIDENT render tooth. node ids:
//   1 RadialPoints | 2 SamplePointsByCameraDistance | 3 DrawPoints2 | 4 Camera | 5 RenderTarget(terminal)
void installC1Specs() {
  std::map<std::string, NodeSpec> dyn;
  dyn["RadialPoints"] = atomicSpec("RadialPoints",
      {{"points", "points", "Points", false},
       {"Count", "Count", "Float", true, 64.0f, 1.0f, 4096.0f, Widget::Slider, {}, true}});
  dyn["SamplePointsByCameraDistance"] = atomicSpec("SamplePointsByCameraDistance",
      {{"Points", "Points", "Points", true},
       {"Camera", "Camera", "Camera", true},   // ← the marker port fillPointCamera scans for
       {"out", "out", "Points", false},
       {"NearRange", "NearRange", "Float", true, 0.0f, 0.0f, 100.0f, Widget::Slider, {}, true},
       {"FarRange", "FarRange", "Float", true, 10.0f, 0.0f, 100.0f, Widget::Slider, {}, true}});
  dyn["DrawPoints2"] = atomicSpec("DrawPoints2",
      {{"points", "points", "Points", true},
       {"out", "out", "Command", false},
       {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
       {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
       {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
       {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
       {"Radius", "Radius", "Float", true, 0.40f, 0.0f, 10.0f, Widget::Slider, {}, true},
       {"UseWForSize", "UseWForSize", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true}});
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

Graph buildC1Graph(float eyeZ, uint32_t W, uint32_t H) {
  Graph g;
  Node rp; rp.id = 1; rp.type = "RadialPoints"; rp.params["Count"] = 64.0f; g.nodes.push_back(rp);
  Node sp; sp.id = 2; sp.type = "SamplePointsByCameraDistance";
  sp.params["NearRange"] = 0.0f; sp.params["FarRange"] = 10.0f; g.nodes.push_back(sp);
  Node dp; dp.id = 3; dp.type = "DrawPoints2";
  dp.params["Radius"] = 0.40f; dp.params["UseWForSize"] = 1.0f; g.nodes.push_back(dp);
  Node cam; cam.id = 4; cam.type = "Camera";
  cam.params["Position.x"] = 0.0f; cam.params["Position.y"] = 0.0f; cam.params["Position.z"] = eyeZ;
  g.nodes.push_back(cam);
  Node rt; rt.id = 5; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
  g.nodes.push_back(rt);

  g.connections.push_back({101, pinId(1, outPortIdx("RadialPoints")), pinId(2, inPortIdx("SamplePointsByCameraDistance", "Points"))});
  g.connections.push_back({102, pinId(2, outPortIdx("SamplePointsByCameraDistance")), pinId(3, inPortIdx("DrawPoints2", "Points"))});
  g.connections.push_back({103, pinId(3, outPortIdx("DrawPoints2")), pinId(4, inPortIdx("Camera", "Command"))});
  g.connections.push_back({104, pinId(4, outPortIdx("Camera")), pinId(5, inPortIdx("RenderTarget", "Command"))});
  return g;
}

// Host closed-form W for a point at (px,py,pz) under camera eye=(0,0,eyeZ): W = (-ObjectToCamera·pos).z / 10.
float hostWForEye(float eyeZ, float px, float py, float pz) {
  ActiveCamera cam;
  cam.active = true; cam.eye[0] = 0; cam.eye[1] = 0; cam.eye[2] = eyeZ;
  cam.target[0] = cam.target[1] = cam.target[2] = 0; cam.up[0] = 0; cam.up[1] = 1; cam.up[2] = 0;
  float o2c[16], c2wUnused[16];
  activeCameraMatrices(cam, o2c, c2wUnused);
  Mat4 O2C; std::memcpy(O2C.m, o2c, 16 * sizeof(float));
  float cs[3];
  mat4TransformPointDivW(O2C, px, py, pz, cs);
  float d = cs[2];
  return (-d) / 10.0f;  // NearRange 0, FarRange 10; linear-0→1 curve → value == normalized depth
}

// Count lit pixels (R or G or B > 30) in the rendered target.
int litPixels(PointGraph& pg, uint32_t W, uint32_t H) {
  MTL::Texture* tex = pg.target();
  if (!tex) return -1;
  uint32_t tw = (uint32_t)tex->width(), th = (uint32_t)tex->height();
  if (tw != W || th != H) return -1;
  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  int lit = 0;
  for (size_t i = 0; i < (size_t)W * H; ++i)
    if (px[i * 4 + 0] > 30 || px[i * 4 + 1] > 30 || px[i * 4 + 2] > 30) ++lit;
  return lit;
}

}  // namespace

int runCameraScopeSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-camera-scope] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();
  registerPointOp("RadialPoints", gridPointsW1Gen);  // W(FX1)=1 grid gen (GPU RadialPoints = W0)
  installC1Specs();

  const float eyeNear = defaultCameraDistance();  // 2.4142 — the default-equivalent eye
  const float eyeFar = 8.0f;                       // a clearly-farther eye → larger |depth| → larger W
  cameraScopeBugSkipPush() = injectBug;            // ★bug = skip the scope → point op reads default both eyes

  bool allFaithful = true;

  // ── FLAT tooth (exact byte-read of node 2's output FX1 under the two eyes) ──
  auto flatW = [&](float eyeZ) -> float {
    Graph g = buildC1Graph(eyeZ, W, H);
    PointGraph pg(dev, lib, q, W, H);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, /*terminal=*/5);
    const MTL::Buffer* buf = pg.debugCookedBuffer(2);  // SamplePointsByCameraDistance output
    uint32_t cnt = pg.debugCookedCount(2);
    if (!buf || cnt == 0) return -999.0f;
    const SwPoint* pts = (const SwPoint*)const_cast<MTL::Buffer*>(buf)->contents();
    return pts[0].FX1;  // point 0 at (-0.3,-0.3,0) (i=0 → fx=fy=-0.5 → 0.6·-0.5=-0.3); z=0
  };
  float wNearFlat = flatW(eyeNear), wFarFlat = flatW(eyeFar);
  // Host closed-form for point 0 (i=0 → world (-0.3,-0.3,0)). Faithful: each W matches ITS eye; the farther
  // eye gives a LARGER W; injectBug → both read default eye=2.4142 → wFar == wNear → no change.
  float wantNear = hostWForEye(eyeNear, -0.3f, -0.3f, 0.0f);
  float wantFar = hostWForEye(eyeFar, -0.3f, -0.3f, 0.0f);
  bool flatExact = std::fabs(wNearFlat - wantNear) < 1e-2f && std::fabs(wFarFlat - wantFar) < 1e-2f;
  bool flatChanged = std::fabs(wFarFlat - wNearFlat) > 0.05f && wFarFlat > wNearFlat;  // the camera MOVE changed W
  bool flatPass = flatExact && flatChanged;
  allFaithful = allFaithful && flatPass;
  std::printf("[selftest-camera-scope] FLAT: W(eye=%.3f)=%.4f(want %.4f) W(eye=%.1f)=%.4f(want %.4f) "
              "exact=%d changed=%d -> %s\n", eyeNear, wNearFlat, wantNear, eyeFar, wFarFlat, wantFar,
              flatExact ? 1 : 0, flatChanged ? 1 : 0, flatPass ? "faithful-ok" : "tripped");

  // ── RESIDENT tooth (production leg: rendered W-tied sprite output CHANGES between the two eyes) ──
  auto resLit = [&](float eyeZ) -> int {
    Graph g = buildC1Graph(eyeZ, W, H);
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    PointGraph pg(dev, lib, q, W, H);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cookResident(rg, ctx, nullptr, /*RenderTarget path=*/"5");
    return litPixels(pg, W, H);
  };
  int litNear = resLit(eyeNear), litFar = resLit(eyeFar);
  // Faithful: a larger W (farther eye) → larger sprites → MORE lit pixels → litFar != litNear (a real change).
  // injectBug: both read the default eye → litFar == litNear (the camera move did nothing) → RED.
  bool resPass = (litNear > 20) && (litFar > 20) && (std::abs(litFar - litNear) > 50);
  allFaithful = allFaithful && resPass;
  std::printf("[selftest-camera-scope] RESIDENT: lit(eye=%.3f)=%d lit(eye=%.1f)=%d |diff|=%d(>50) -> %s\n",
              eyeNear, litNear, eyeFar, litFar, std::abs(litFar - litNear), resPass ? "faithful-ok" : "tripped");

  cameraScopeBugSkipPush() = false;  // reset (process hygiene)
  setDynamicSpecs({});
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-camera-scope] FAIL: injectBug still passed (the camera move changed the point "
                  "weighting despite the skipped scope — the seam is not actually scoping the camera)\n");
      return 1;
    }
    std::printf("[selftest-camera-scope] injectBug correctly RED (skipped active-camera scope → point op read "
                "the DEFAULT camera under both eyes → the camera move changed nothing on flat AND resident)\n");
    return 1;
  }
  std::printf("[selftest-camera-scope] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

}  // namespace sw
