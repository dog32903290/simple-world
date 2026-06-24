// RotateTowards render golden — the end-to-end proof that the RotateTowards op's facing-rotation push (TiXL
// RotateTowards.cs, cooked by point_ops_rotatetowards.cpp's cookRotateTowards stamp) reaches every subtree item
// on BOTH the flat AND resident cook legs. NO new cook-core code: a HARNESS cut driving the already-landed
// RotateTowards op + the per-item group stamp through a REAL graph:
//
//     EmitItemCmd(identity) → RotateTowards(LookTowards=TowardsPosition, target, RotationOffset) → StubRenderTarget
//
// ── CLOSED-FORM (the load-bearing test) ──
//   RotateTowards builds M = rotateOffset · inverse(LookAtRH(0, sourcePos-target, Up)), sourcePos=(0,0,0)
//   (FORK #1), and stamps it onto the single emitted item (which starts with an IDENTITY group). So the
//   item's groupObjectToWorld MUST equal M element-for-element. We derive the EXPECTED M from the SAME
//   rotateTowardsMatrix() the op calls (no re-derivation drift — the op + the golden share the construction),
//   and assert all 16 elements (eps 1e-4) on BOTH legs. A non-identity target/offset → a non-identity M, so
//   this is NOT the identity-passes-trivially trap (the -bug self-check below proves the tooth bites).
//
// ── TWO CASES (so the matrix is genuinely non-trivial AND distinguishable from identity) ──
//   Case A: target=(1,0,0), RotationOffset=(0,0,0) — pure LookAt facing +x, M is a pure rotation ≠ identity.
//   Case B: target=(0,0,1) (the .t3 default direction), RotationOffset=(0,45,0) — LookAt · a 45° yaw, exercises
//           the RotationOffset compose (rotateOffset · inverse(lookAt)).
//
// ── injectBug (-bug leg) ──
//   The stamp is structural (an identity RotateTowards toward +z with no offset IS a legit near-identity, so we
//   must NOT pick a degenerate case). The -bug drives a CPU op flag g_rotateTowardsDropStamp read by a test
//   wrapper cookRotateTowardsForTest: when set it runs the REAL cookRotateTowards then DROPS the stamp
//   (hasGroup=false, group=identity) → the item is NEVER reoriented → its group ≠ the non-identity M → the
//   element-wise assertion goes RED. A CPU op flag (parallel to g_groupDropPush), NOT a shader test seam
//   (constitution rule). The resident leg runs the identical drop (S2c resident-black-hole precedent).
//   The injectBug self-check (point_ops_loop.cpp:327 pattern): if injectBug still produced the faithful M the
//   tooth did not bite → FAIL.
//
// Zone: runtime leaf. EmitItemCmd / StubRenderTarget are TEST-ONLY harness fixtures (the loop/group golden
// precedent) installed via setDynamicSpecs + registerCmdOp/registerTexOp; RotateTowards is the REAL builtin
// under test (re-registered with the cookRotateTowardsForTest wrapper for the drop flag).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/field_camera.h"         // Mat4 / mat4Identity (the expected-matrix derivation lives in the op)
#include "runtime/graph.h"                // Graph / Node / NodeSpec / PortSpec / pinId / setDynamicSpecs / findSpec
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph → SymbolLibrary)
#include "runtime/point_graph.h"          // PointGraph::cook/cookResident, registerCmdOp/registerTexOp, CmdCookCtx, TexCookCtx
#include "runtime/point_ops_camera_scope.h"  // cameraScopeBugSkipPush (the camera→CmdCookCtx -bug flag)
#include "runtime/render_command.h"       // RenderCommand / RenderDrawItem
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// The REAL op + its shared matrix builder (point_ops_rotatetowards.cpp) — declared here (the golden is their
// only other caller) rather than in the at-cap point_ops.h.
RenderCommand cookRotateTowards(CmdCookCtx& c);
Mat4 rotateTowardsMatrix(const float target[3], const float rotationOffsetDeg[3]);
RenderCommand cookCamera(CmdCookCtx& c);  // the REAL Camera op (point_ops_camera.cpp) — the scope writer

namespace {
// Test-only op flag: when set, cookRotateTowardsForTest runs the REAL cookRotateTowards then DROPS the stamp
// → the executor never reorients the item. OFF in production. A CPU op flag (parallel to g_groupDropPush).
bool g_rotateTowardsDropStamp = false;

RenderCommand cookRotateTowardsForTest(CmdCookCtx& c) {
  RenderCommand rc = cookRotateTowards(c);
  if (g_rotateTowardsDropStamp)
    for (RenderDrawItem& it : rc.items) {
      it.hasGroup = false;
      float id[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
      for (int i = 0; i < 16; ++i) it.groupObjectToWorld[i] = id[i];
    }
  return rc;
}

// EmitItemCmd: a Layer stand-in source — emits ONE item with an IDENTITY group (hasGroup=false). RotateTowards
// then stamps its facing rotation onto exactly this item, so the captured group == the op's matrix.
RenderCommand emitItemCmd(CmdCookCtx&) {
  RenderCommand rc;
  RenderDrawItem item{};
  item.kind = DrawKind::Layer2d;
  // group starts identity (RenderDrawItem default groupObjectToWorld = identity, hasGroup=false).
  rc.items.push_back(item);
  return rc;
}

RenderCommand g_capturedChain;
void stubRenderTarget(TexCookCtx& c) { if (c.command) g_capturedChain = *c.command; }

NodeSpec atomicSpec(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}

void installSpecs() {
  std::map<std::string, NodeSpec> dyn;
  dyn["EmitItemCmd"] = atomicSpec("EmitItemCmd", {{"out", "out", "Command", false}});
  // RotateTowards: ONE Command input + Command out + LookTowards(enum int) + AlternativeTarget(vec3) +
  // RotationOffset(vec3). Mirrors the production NodeSpec (node_registry_draw.cpp). A vec param is N
  // consecutive Float ports keyed "<base>.x/.y/.z" with vecArity on the head (so cookVecN finds them).
  dyn["RotateTowards"] = atomicSpec(
      "RotateTowards",
      {{"command", "command", "Command", true},
       {"out", "out", "Command", false},
       {"LookTowards", "LookTowards", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {}, true},
       {"AlternativeTarget.x", "AlternativeTarget", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
       {"AlternativeTarget.y", "AlternativeTarget.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
       {"AlternativeTarget.z", "AlternativeTarget.z", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
       {"RotationOffset.x", "RotationOffset", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
       {"RotationOffset.y", "RotationOffset.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
       {"RotationOffset.z", "RotationOffset.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1}});
  dyn["StubRenderTarget"] = atomicSpec("StubRenderTarget",
      {{"command", "command", "Command", true}, {"out", "out", "Texture2D", false}});
  setDynamicSpecs(std::move(dyn));
}

// Cook EmitItemCmd → RotateTowards(target, rotOff, TowardsPosition) → StubRenderTarget on whichPath
// (0=flat, 1=resident). Returns the captured chain (caller inspects items[0].groupObjectToWorld).
bool cookGraph(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, int whichPath,
               const float target[3], const float rotOff[3], RenderCommand& outChain) {
  Graph g;
  Node e; e.id = 1; e.type = "EmitItemCmd"; g.nodes.push_back(e);
  Node rt; rt.id = 2; rt.type = "RotateTowards";
  rt.params["LookTowards"] = 1.0f;  // TowardsPosition (closed-form path; avoids the camera-coupled FORK #2)
  rt.params["AlternativeTarget.x"] = target[0];
  rt.params["AlternativeTarget.y"] = target[1];
  rt.params["AlternativeTarget.z"] = target[2];
  rt.params["RotationOffset.x"] = rotOff[0];
  rt.params["RotationOffset.y"] = rotOff[1];
  rt.params["RotationOffset.z"] = rotOff[2];
  g.nodes.push_back(rt);
  Node sr; sr.id = 3; sr.type = "StubRenderTarget"; g.nodes.push_back(sr);

  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // EmitItemCmd.out → RotateTowards.command
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // RotateTowards.out → StubRenderTarget.command

  g_capturedChain = RenderCommand{};
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 64, 64);
  if (whichPath == 0) {
    pg.cook(g, ctx, nullptr, /*terminal=*/3);
  } else {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    pg.cookResident(rg, ctx, nullptr, /*StubRenderTarget path=*/"3");
  }
  outChain = g_capturedChain;
  return true;
}

bool approx(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// Assert the captured item's group == the expected M (element-wise). Returns true if FAITHFUL.
bool checkCase(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, int path, const char* caseName,
               const float target[3], const float rotOff[3]) {
  Mat4 expected = rotateTowardsMatrix(target, rotOff);
  RenderCommand chain;
  cookGraph(dev, lib, q, path, target, rotOff, chain);
  bool ok = (chain.items.size() == 1);
  float maxErr = 0.0f;
  for (int i = 0; ok && i < 16; ++i) {
    float got = chain.items[0].groupObjectToWorld[i];
    maxErr = std::fmax(maxErr, std::fabs(got - expected.m[i]));
    ok = ok && approx(got, expected.m[i]);
  }
  const char* pathName[2] = {"flat", "resident"};
  std::printf("[selftest-rotatetowards] %s %s: items=%zu m[0]=%.4f(want %.4f) maxErr=%.5f -> %s\n",
              pathName[path], caseName, chain.items.size(),
              chain.items.size() > 0 ? (double)chain.items[0].groupObjectToWorld[0] : -9.0,
              (double)expected.m[0], (double)maxErr, ok ? "faithful-ok" : "tripped");
  return ok;
}
}  // namespace

int runRotateTowardsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-rotatetowards] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerCmdOp("RotateTowards", cookRotateTowardsForTest);  // OVERRIDE with the drop-flag test wrapper
  registerCmdOp("EmitItemCmd", emitItemCmd);                 // the identity-group source
  registerTexOp("StubRenderTarget", stubRenderTarget);
  installSpecs();

  g_rotateTowardsDropStamp = injectBug;  // ★the stamp-DROP bug (the item is never reoriented)

  // Case A: pure LookAt facing +x (non-identity rotation). Case B: .t3 default direction + a 45° yaw offset.
  const float targetA[3] = {1.0f, 0.0f, 0.0f}, offA[3] = {0.0f, 0.0f, 0.0f};
  const float targetB[3] = {0.0f, 0.0f, 1.0f}, offB[3] = {0.0f, 45.0f, 0.0f};

  bool allFaithful = true;
  for (int path = 0; path < 2; ++path) {  // BOTH legs (S2c blood lesson)
    allFaithful = checkCase(dev, lib, q, path, "caseA target(1,0,0) off(0,0,0)", targetA, offA) && allFaithful;
    allFaithful = checkCase(dev, lib, q, path, "caseB target(0,0,1) off(0,45,0)", targetB, offB) && allFaithful;
  }

  g_rotateTowardsDropStamp = false;  // reset (process hygiene)
  setDynamicSpecs({});
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      // injectBug self-check (point_ops_loop.cpp:327 pattern): the drop must trip a tooth.
      std::printf("[selftest-rotatetowards] FAIL: injectBug still produced the faithful matrix (the stamp drop "
                  "did not change the item group → the tooth does not bite)\n");
      return 1;
    }
    std::printf("[selftest-rotatetowards] injectBug correctly RED — stamp dropped → item group stayed identity "
                "≠ the facing rotation, on BOTH legs\n");
    return 1;
  }
  std::printf("[selftest-rotatetowards] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/333, {"rotatetowards", runRotateTowardsSelfTest});

// ───────────────── camera→CmdCookCtx bridge golden (RotateTowards FORK#2, BOTH legs) ─────────────────
// PROVES the bridge: a Camera op WRAPPING a RotateTowards(TowardsCamera) subtree surfaces its CameraToWorld
// onto CmdCookCtx so RotateTowards faces the CAMERA WORLD POSITION not TiXL's default. The C1 LiveCameraScope
// is set by the cook DRIVER around the Camera SubGraph cook; the cc-fill sites consult liveActiveCamera().
// Topology (RotateTowards INSIDE the Camera subtree → scope live when it cooks):
//   EmitItemCmd → RotateTowards(LookTowards=0=TowardsCamera) → Camera(Position=eye).Command → StubRT
// CLOSED-FORM: TowardsCamera target = transform((0,0,0), CameraToWorld) = camera world pos. Stamp M MUST equal
// rotateTowardsMatrix(camWorld,0) AND DIFFER from the default derivation (op's OWN builder, no drift). injectBug
// = cameraScopeBugSkipPush() → driver SKIPS scope → hasCamera=false → FORK#2 falls back to default → stamp
// collapses to the default derivation → RED on BOTH legs. Mirror gate: flat==resident byte-identical.
namespace {
void installCameraSpecs() {
  std::map<std::string, NodeSpec> dyn;
  dyn["EmitItemCmd"] = atomicSpec("EmitItemCmd", {{"out", "out", "Command", false}});
  dyn["RotateTowards"] = atomicSpec(
      "RotateTowards",
      {{"command", "command", "Command", true},
       {"out", "out", "Command", false},
       {"LookTowards", "LookTowards", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {}, true},
       {"AlternativeTarget.x", "AlternativeTarget", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
       {"AlternativeTarget.y", "AlternativeTarget.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
       {"AlternativeTarget.z", "AlternativeTarget.z", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
       {"RotationOffset.x", "RotationOffset", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
       {"RotationOffset.y", "RotationOffset.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
       {"RotationOffset.z", "RotationOffset.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1}});
  dyn["Camera"] = atomicSpec(
      "Camera",
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
  dyn["StubRenderTarget"] = atomicSpec("StubRenderTarget",
      {{"command", "command", "Command", true}, {"out", "out", "Texture2D", false}});
  setDynamicSpecs(std::move(dyn));
}

// Cook EmitItemCmd → RotateTowards(TowardsCamera) → Camera(eye) → StubRenderTarget on whichPath (0=flat,
// 1=resident). Returns the captured chain (caller inspects items[0].groupObjectToWorld).
bool cookCameraGraph(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, int whichPath,
                     const float eye[3], RenderCommand& outChain) {
  Graph g;
  Node e; e.id = 1; e.type = "EmitItemCmd"; g.nodes.push_back(e);
  Node rt; rt.id = 2; rt.type = "RotateTowards";
  rt.params["LookTowards"] = 0.0f;  // TowardsCamera (the .t3 DEFAULT — the FORK#2 path the bridge closes)
  g.nodes.push_back(rt);
  Node cam; cam.id = 3; cam.type = "Camera";
  cam.params["Position.x"] = eye[0]; cam.params["Position.y"] = eye[1]; cam.params["Position.z"] = eye[2];
  g.nodes.push_back(cam);
  Node sr; sr.id = 4; sr.type = "StubRenderTarget"; g.nodes.push_back(sr);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // EmitItemCmd.out → RotateTowards.command
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // RotateTowards.out → Camera.Command (port 0)
  g.connections.push_back({103, pinId(3, 1), pinId(4, 0)});  // Camera.out → StubRenderTarget.command
  g_capturedChain = RenderCommand{};
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 64, 64);
  if (whichPath == 0) {
    pg.cook(g, ctx, nullptr, /*terminal=*/4);
  } else {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    pg.cookResident(rg, ctx, nullptr, /*StubRenderTarget path=*/"4");
  }
  outChain = g_capturedChain;
  return true;
}
}  // namespace

int runRotateTowardsCameraSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-rotatetowards-camera] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerCmdOp("RotateTowards", cookRotateTowards);  // the REAL op (reads c.hasCamera — NOT the drop wrapper)
  registerCmdOp("Camera", cookCamera);                // the REAL Camera (the scope WRITER; driver sets the scope)
  registerCmdOp("EmitItemCmd", emitItemCmd);          // identity-group source
  registerTexOp("StubRenderTarget", stubRenderTarget);
  installCameraSpecs();

  // OFF-AXIS eye (3,1,5): the look DIRECTION from source (0,0,0) differs from the default (0,0,2.4142). An
  // on-axis eye (0,0,z) is DEGENERATE — LookAtRH toward (0,0,-z) is the SAME rotation for any z>0, so the
  // stamp would not move (camera-distance-only trap). Off-axis makes the tooth bite.
  const float eye[3] = {3.0f, 1.0f, 5.0f};
  // targetCam = camera WORLD pos the op resolves: transform((0,0,0), CameraToWorld) from activeCameraMatrices —
  // derived the SAME way the bridge does (no hand assumption). Default = the scope-skip fallback.
  ActiveCamera ac; ac.active = true; ac.eye[0] = eye[0]; ac.eye[1] = eye[1]; ac.eye[2] = eye[2];
  float o2cUnused[16], c2w[16];
  activeCameraMatrices(ac, o2cUnused, c2w);
  Mat4 C2W; for (int i = 0; i < 16; ++i) C2W.m[i] = c2w[i];
  float targetCam[3]; mat4TransformPointDivW(C2W, 0.0f, 0.0f, 0.0f, targetCam);
  const float targetDef[3] = {0.0f, 0.0f, defaultCameraDistance()};
  const float noOff[3] = {0.0f, 0.0f, 0.0f};
  Mat4 expectedCam = rotateTowardsMatrix(targetCam, noOff);  // faithful (camera in scope → camera world pos)
  Mat4 expectedDef = rotateTowardsMatrix(targetDef, noOff);  // -bug fallback (scope skipped → default camera)
  cameraScopeBugSkipPush() = injectBug;  // ★the bug: driver SKIPS the camera scope → FORK#2 falls back to default

  bool allFaithful = true;
  Mat4 stamps[2];  // [flat, resident] — for the byte-identical mirror gate
  const char* pathName[2] = {"flat", "resident"};
  for (int path = 0; path < 2; ++path) {  // BOTH legs (S2c blood lesson — resident is production)
    RenderCommand chain;
    cookCameraGraph(dev, lib, q, path, eye, chain);
    bool ok = (chain.items.size() == 1);
    const Mat4& want = injectBug ? expectedDef : expectedCam;  // bug skipped scope → expect the default stamp
    float maxErr = 0.0f, diffFromDefault = 0.0f;
    for (int i = 0; ok && i < 16; ++i) {
      float got = chain.items[0].groupObjectToWorld[i];
      stamps[path].m[i] = got;
      maxErr = std::fmax(maxErr, std::fabs(got - want.m[i]));
      diffFromDefault = std::fmax(diffFromDefault, std::fabs(got - expectedDef.m[i]));
      ok = ok && approx(got, want.m[i]);
    }
    // DISCRIMINATOR (faithful only): the camera stamp MUST differ from the default stamp (eye 5≠2.4142).
    ok = ok && (injectBug ? true : (diffFromDefault > 1e-3f));
    allFaithful = allFaithful && ok;
    std::printf("[selftest-rotatetowards-camera] %s eye=(3,1,5) TowardsCamera: items=%zu maxErr=%.5f "
                "diffFromDefault=%.5f -> %s\n", pathName[path], chain.items.size(), (double)maxErr,
                (double)diffFromDefault, ok ? "faithful-ok" : "tripped");
  }
  bool mirror = true;  // MIRROR GATE: flat == resident byte-identical (C1 runCameraScopeSelfTest precedent)
  for (int i = 0; i < 16; ++i) mirror = mirror && approx(stamps[0].m[i], stamps[1].m[i]);
  std::printf("[selftest-rotatetowards-camera] mirror flat==resident: %s\n", mirror ? "byte-identical" : "DIVERGED");
  allFaithful = allFaithful && mirror;
  cameraScopeBugSkipPush() = false;  // reset (process hygiene)
  setDynamicSpecs({});
  lib->release(); q->release(); dev->release(); pool->release();
  if (injectBug) {
    // injectBug self-check: the scope-skip must collapse the stamp to the DEFAULT derivation. allFaithful here
    // = stamp == default + mirror held → the tooth bit on both legs. Stayed == camera stamp → bug no-op → FAIL.
    if (!allFaithful) {
      std::printf("[selftest-rotatetowards-camera] FAIL: injectBug did not collapse the stamp to the default "
                  "camera derivation (the scope-skip changed nothing → the tooth does not bite)\n");
      return 1;
    }
    std::printf("[selftest-rotatetowards-camera] injectBug correctly RED — camera scope skipped → RotateTowards "
                "faced the DEFAULT camera (0,0,%.4f) not the wired eye (3,1,5), on BOTH legs\n",
                defaultCameraDistance());
    return 1;
  }
  std::printf("[selftest-rotatetowards-camera] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/335, {"rotatetowards-camera", runRotateTowardsCameraSelfTest});

}  // namespace sw
