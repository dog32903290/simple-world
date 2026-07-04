// ShiftCamera command op + golden — see point_ops_shiftcamera.h.
// TiXL authority: external/tixl/Operators/Lib/render/camera/ShiftCamera.cs (GUID 1a8d2a8d).
//
// BACKWARD-TRACE (ShiftCamera.cs:14-44 Update):
//   var t = Translation.GetValue(context);            // .t3 default (0,0,0)
//   var previous = context.CameraToClipSpace;         // the AMBIENT projection (default or a pushed Camera)
//   var newCamToClip = previous;
//   newCamToClip.M31 += t.X;                          // :34  row-major (3,1) = m[8]
//   newCamToClip.M32 += t.Y;                          // :35  row-major (3,2) = m[9]
//   newCamToClip.M33 += (float)((double)t.Z/1000.0);  // :36  row-major (3,3) = m[10], double divide
//   context.CameraToClipSpace = newCamToClip;
//   Command.GetValue(context);                        // eval subtree under the shifted projection
//   context.CameraToClipSpace = previous;             // restore
// (Scale/UniformScale computed at :18 but NEVER used — the Matrix.Transformation block :24-30 is
// commented out → fork-shiftcamera-scale-dead-inputs, dropped.)
//
// STAMP MODEL (mirror of cookCamera's per-item push, point_ops_camera.cpp): accumulate the delta onto
// every subtree item where !hasCamera (an item under an INNER Camera gets a REPLACED matrix in TiXL, so
// the ambient shift never reaches it = skip; an item this op stamps that an OUTER Camera later claims
// keeps the shift = TiXL's inner-Shift-reads-the-outer-Camera's-pushed-matrix). Nested ShiftCameras
// ACCUMULATE (+=), exactly like TiXL's two sequential += on the live matrix. The executor applies the
// delta after composing the item's cameraToClipSpace (point_ops_rendertarget.cpp Layer2d/Mesh).
#include "runtime/point_ops_shiftcamera.h"

#include "runtime/field_camera.h"    // Mat4 / lookAtRH / perspectiveFovRH / objectToClipSpace / mat4* helpers
#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp/registerTexOp, cookVecN, cookResident
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

// cookShiftCamera: Command subtree in → Command out; accumulate the projection-matrix delta onto every
// !hasCamera subtree item (innermost-Camera-wins, see header). Unwired Command → empty chain (TiXL
// would eval an empty subtree).
RenderCommand cookShiftCamera(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.inputCommand) return rc;
  rc.items = c.inputCommand->items;  // COPY the subtree (we re-emit it, shifted)

  float tDef[3] = {0.0f, 0.0f, 0.0f};  // ShiftCamera.t3 Translation default (0,0,0)
  float t[3];
  cookVecN(c, "Translation", tDef, 3, t);
  const float dz = (float)((double)t[2] / 1000.0);  // ShiftCamera.cs:36 double divide

  for (RenderDrawItem& it : rc.items) {
    if (it.hasCamera) continue;  // an INNER Camera replaced the ambient matrix → the shift is lost (TiXL)
    it.hasClipShift = true;
    it.clipShift[0] += t[0];  // M31 += t.X (ShiftCamera.cs:34)
    it.clipShift[1] += t[1];  // M32 += t.Y (:35)
    it.clipShift[2] += dz;    // M33 += t.Z/1000 (:36)
  }
  return rc;
}

void registerShiftCameraOp() { registerCmdOp("ShiftCamera", cookShiftCamera); }

// ───────────────────────────────── GOLDEN ─────────────────────────────────
namespace {
// ★injectBug: drop the clipShift stamp the REAL cook just produced (the push lost) — a CPU op-wrapper
// flag over the production cookShiftCamera, same seam as the ortho golden's g_orthoDropFlag. OFF in prod.
bool g_shiftDropStamp = false;

RenderCommand cookShiftForTest(CmdCookCtx& c) {
  RenderCommand rc = cookShiftCamera(c);
  if (g_shiftDropStamp)
    for (RenderDrawItem& it : rc.items) {
      it.hasClipShift = false;
      it.clipShift[0] = it.clipShift[1] = it.clipShift[2] = 0.0f;
    }
  return rc;
}

void cookSolidImageShift(TexCookCtx& c) {
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

NodeSpec atomicSpecShift(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}
int outPortIdxShift(const char* type) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (!s->ports[i].isInput) return (int)i;
  return -1;
}
int inPortIdxShift(const char* type, const char* dataType) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (s->ports[i].isInput && s->ports[i].dataType == dataType) return (int)i;
  return -1;
}

// SolidImage + Layer2d have no production NodeSpec (test-graph helpers, same rows the ortho golden
// installs); ShiftCamera/RenderTarget resolve to their PRODUCTION built-in rows (built-ins win on clash).
void installShiftSpecs() {
  std::map<std::string, NodeSpec> dyn;
  dyn["SolidImage"] = atomicSpecShift("SolidImage", {{"out", "out", "Texture2D", false}});
  dyn["Layer2d"] = atomicSpecShift("Layer2d",
      {{"Image", "Image", "Texture2D", true},
       {"out", "out", "Command", false},
       {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
       {"ScaleMode", "ScaleMode", "Float", true, 0.0f, 0.0f, 5.0f, Widget::Enum, {}, true},
       {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {}, true}});
  setDynamicSpecs(std::move(dyn));
}

// SolidRed(1) → Layer2d(2, Scale) → ShiftCamera(3, Translation) → RenderTarget(4 terminal).
Graph buildShiftGraph(float quadScale, float tx, float ty, float tz, uint32_t W, uint32_t H) {
  Graph g;
  Node sa; sa.id = 1; sa.type = "SolidImage"; g.nodes.push_back(sa);
  Node l; l.id = 2; l.type = "Layer2d";
  l.params["Scale"] = quadScale; l.params["ScaleMode"] = 4.0f; l.params["BlendMode"] = 0.0f;  // Stretch/Normal
  g.nodes.push_back(l);
  Node sc; sc.id = 3; sc.type = "ShiftCamera";
  sc.params["Translation.x"] = tx; sc.params["Translation.y"] = ty; sc.params["Translation.z"] = tz;
  g.nodes.push_back(sc);
  Node rt; rt.id = 4; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
  g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, outPortIdxShift("SolidImage")), pinId(2, inPortIdxShift("Layer2d", "Texture2D"))});
  g.connections.push_back({102, pinId(2, outPortIdxShift("Layer2d")), pinId(3, inPortIdxShift("ShiftCamera", "Command"))});
  g.connections.push_back({103, pinId(3, outPortIdxShift("ShiftCamera")), pinId(4, inPortIdxShift("RenderTarget", "Command"))});
  return g;
}

int readTargetRShift(PointGraph& pg, uint32_t W, uint32_t H, float ndcX, float ndcY) {
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

// Project a world point through (default camera projection + the ShiftCamera delta), the SAME
// perspectiveFovRH/mat4TransformPointDivW pipeline the executor's VS reproduces.
void shiftedProject(float wx, float wy, float wz, float tx, float ty, float tz, float ndc[3]) {
  float eye[3] = {0.0f, 0.0f, defaultCameraDistance()}, tgt[3] = {0, 0, 0}, up[3] = {0, 1, 0};
  Mat4 w2c = lookAtRH(eye, tgt, up);
  Mat4 c2c = perspectiveFovRH(kDefaultCamFovDegrees * 3.14159265358979323846f / 180.0f, 1.0f, 0.01f,
                              1000.0f);
  c2c.m[8] += tx;                             // M31 += t.X (ShiftCamera.cs:34)
  c2c.m[9] += ty;                             // M32 += t.Y (:35)
  c2c.m[10] += (float)((double)tz / 1000.0);  // M33 += t.Z/1000 (:36)
  Mat4 o2c = objectToClipSpace(mat4Identity(), w2c, c2c);
  mat4TransformPointDivW(o2c, wx, wy, wz, ndc);
}
}  // namespace

// --selftest-shiftcamera. TOOTH B = closed-form math; TOOTH A = resident-terminal render flip.
int runShiftCameraSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;  // square → aspect 1
  const float kHalf = 0.6f;         // Layer2d Scale → quad x-half 0.6 (Stretch mode, square)
  const float kTx = 0.9f;           // render-tooth shift: quad [-0.6,0.6] slides to [-1.5,-0.3] in NDC

  // ── TOOTH B (math): NDC deltas of a MID-plateau world point under a NON-trivial 3-axis shift. ──
  // Hand-derivation (perspectiveFovRH, row-vector v·M): clip = (x_cam·M11 + z_cam·(M31+tx),
  // y_cam·M22 + z_cam·(M32+ty), z_cam·(M33+tz/1000) + M43, w = -z_cam). A z=0-plane world point sits at
  // z_cam = -d (d = eye distance) → w = d → the shift terms contribute z_cam/w = -1 of themselves:
  //   NDC.x' = NDC.x - tx,  NDC.y' = NDC.y - ty,  NDC.z' = NDC.z - tz/1000.       (exact, closed form)
  const float tX = 0.35f, tY = -0.2f, tZ = 40.0f;  // mid-range, all three axes non-zero
  float base[3], shifted[3];
  shiftedProject(0.3f, 0.25f, 0.0f, 0, 0, 0, base);        // world probe point OFF the axes/origin
  shiftedProject(0.3f, 0.25f, 0.0f, tX, tY, tZ, shifted);
  bool b = std::fabs((shifted[0] - base[0]) + tX) < 1e-4f &&
           std::fabs((shifted[1] - base[1]) + tY) < 1e-4f &&
           std::fabs((shifted[2] - base[2]) + tZ / 1000.0f) < 1e-5f;
  if (injectBug) {
    // ★bug (math leg): the M31-vs-M41 transpose trap — add tx to m[12] (M41, a CONSTANT clip offset not
    // scaled by z_cam) instead of m[8]. The z=0-plane delta becomes +tx/d instead of -tx → b flips.
    float eye[3] = {0.0f, 0.0f, defaultCameraDistance()}, tgt[3] = {0, 0, 0}, up[3] = {0, 1, 0};
    Mat4 w2c = lookAtRH(eye, tgt, up);
    Mat4 c2c = perspectiveFovRH(kDefaultCamFovDegrees * 3.14159265358979323846f / 180.0f, 1.0f, 0.01f,
                                1000.0f);
    c2c.m[12] += tX;  // WRONG row: M41 (translation) instead of M31 (z-coupled shift)
    Mat4 o2c = objectToClipSpace(mat4Identity(), w2c, c2c);
    float wrong[3];
    mat4TransformPointDivW(o2c, 0.3f, 0.25f, 0.0f, wrong);
    b = std::fabs((wrong[0] - base[0]) + tX) < 1e-4f;  // the wrong-row delta will NOT equal -tX
  }
  std::printf("[selftest-shiftcamera] B math: dNDC=(%.4f,%.4f,%.5f) want=(%.4f,%.4f,%.5f) -> %s\n",
              shifted[0] - base[0], shifted[1] - base[1], shifted[2] - base[2], -tX, -tY,
              -tZ / 1000.0f, b ? "faithful-ok" : "tripped");

  // ── TOOTH A (render flip, through the PRODUCTION RESIDENT terminal). ──
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-shiftcamera] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();
  registerCmdOp("ShiftCamera", cookShiftForTest);   // OVERRIDE with the stamp-drop test wrapper
  registerTexOp("SolidImage", cookSolidImageShift); // test source
  installShiftSpecs();

  g_shiftDropStamp = injectBug;  // ★bug: drop the stamp → the quad stays centered → both probes flip

  // Default camera (eye z = 2.4142, fov 45°, d·tan(fov/2)=1): the 0.6-half quad spans NDC [-0.6, 0.6].
  // ShiftCamera t.X=0.9 slides the whole projection LEFT by 0.9 (closed form above): quad → [-1.5,-0.3].
  //   center probe (0,0):   RED unshifted, BACKGROUND shifted   (0 > -0.3, outside)
  //   shifted-center (-0.9): BACKGROUND unshifted (|-0.9|>0.6), RED shifted (quad center)
  // Both probes sit ≥0.3 NDC from every edge — deep plateaus on both legs.
  Graph g = buildShiftGraph(kHalf, kTx, 0.0f, 0.0f, W, H);
  SymbolLibrary slib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
  PointGraph pg(dev, lib, q, W, H);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cookResident(rg, ctx, nullptr, /*RenderTarget path=*/"4");

  int centerR = readTargetRShift(pg, W, H, 0.0f, 0.0f);    // faithful: background (<40)
  int shiftedR = readTargetRShift(pg, W, H, -kTx, 0.0f);   // faithful: quad center RED (>200)
  bool a = (centerR < 40) && (shiftedR > 200);
  std::printf("[selftest-shiftcamera] A RESIDENT shift(tx=%.2f): center=%d(<40) shiftedCenter=%d(>200) "
              "-> %s\n", kTx, centerR, shiftedR, a ? "faithful-ok" : "tripped");

  g_shiftDropStamp = false;  // reset (process hygiene)
  setDynamicSpecs({});
  lib->release(); q->release(); dev->release(); pool->release();

  bool allFaithful = a && b;
  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-shiftcamera] FAIL: injectBug tripped no tooth\n");
      return 0;  // did-not-trip → NO-BITE latch catches the dead tooth (GOLDEN_STANDARD polarity)
    }
    std::printf("[selftest-shiftcamera] injectBug correctly RED (dropped clipShift stamp → quad stays "
                "centered → probes read the unshifted image; wrong-row M41 math no longer matches -tX)\n");
    return 1;
  }
  std::printf("[selftest-shiftcamera] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

}  // namespace sw
