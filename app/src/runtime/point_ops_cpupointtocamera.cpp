// runtime/point_ops_cpupointtocamera — CpuPointToCamera: build a camera from the FIRST point of a
// Points buffer (CPU readback of position + orientation quaternion) and render a Command subtree
// through it. + the --selftest-cpupointtocamera golden (flat AND resident legs).
//
// TiXL authority: external/tixl/Operators/Lib/point/helper/CpuPointToCamera.cs
//   Update (:21-79):
//     p = pointList.TypedElements[0]                                  (:27 — FIRST point, CPU-side)
//     aspectRatio < 0.0001 → RequestedResolution aspect               (:42-45)
//     position = p.Position                                           (:47)
//     forward  = Vector3.Transform(UnitZ, p.Orientation)              (:48 — quaternion rotate)
//     target   = position + forward                                   (:50)
//     up       = Vector3.Transform(UnitY, p.Orientation)              (:51)
//     CameraDefinition{NearFarClip, Position, Target, Up, AspectRatio, FieldOfView.ToRadians(), ...}
//     → BuildProjectionMatrices → CamReference.Value = this (ICamera)  (:55-78)
//   .t3 defaults: FieldOfView=45, ClipPlanes=(0.01,1000), AspectRatio=-1 (→ resolution aspect),
//   SamplePos=0, Roll=0, offsets=0, Up=(0,1,0).
//
// ★NAMED FORKS (the sw Camera-op posture, point_ops_camera.cpp):
//   1. OUTPUT CURRENCY: TiXL emits an ICamera Object reference consumed by ReuseCamera-style ops; sw has
//      no Object rail. This op WRAPS a Command subtree directly (the per-item camera-stamp mechanism the
//      Camera op established) — Command in → Command out; the output keeps TiXL's name "CamReference".
//      When an Object rail lands, split the reference output back out.
//   2. v1 param scope == the Camera op's: Roll / PositionOffset / RotationOffset / LensShift /
//      AlsoOffsetTarget dropped (the same CameraDefinition embellishments Camera.cs:82-103 dropped);
//      the Up INPUT is dead in TiXL's own Update (:51 uses the orientation-derived up, not the slot) —
//      not shipped. SamplePos is dead in TiXL too (:29-39 the interpolation is commented out; :27 reads
//      element 0 unconditionally) — port shipped for .t3 parity, unread.
//   3. No points wired / empty bag → forward the subtree UNSTAMPED (TiXL :24-25 returns without setting
//      CamReference → the consumer falls back; sw items keep hasCamera=false → default camera).
//   4. C1 point-rail ActiveCamera scope not pushed (resolveActiveCamera reads params only; this camera
//      needs the cooked buffer). Draw-rail stamp only, v1.
//
// Quaternion rotate = System.Numerics Vector3.Transform(v, q): t = 2·(q.xyz × v); v' = v + q.w·t + q.xyz × t.
//
// runtime leaf: pure CPU + Metal (the golden cooks through PointGraph); no UI, no upward deps.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph / Node / NodeSpec / PortSpec / pinId / setDynamicSpecs / findSpec
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph → SymbolLibrary)
#include "runtime/point_graph.h"          // CmdCookCtx / registerCmdOp / registerPointOp / registerTexOp / cookParam
#include "runtime/render_command.h"       // RenderCommand / RenderDrawItem
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/tixl_point.h"           // SwPoint (64B; Rotation = quaternion xyzw) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Test-only cook flag (the orientation tooth): true → the REAL cook skips the quaternion rotate and uses
// raw UnitZ/UnitY (a dropped Orientation readback). The golden's non-identity-orientation case diverges
// on target AND up → RED. OFF in production.
bool& cpuPointToCameraBugIdentityOrientation() {
  static bool v = false;
  return v;
}

namespace {
// System.Numerics Vector3.Transform(v, Quaternion q) — the exact formula the .cs relies on (:48,:51).
void quatRotate(const float q[4], const float v[3], float out[3]) {
  // t = 2 * cross(q.xyz, v)
  const float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
  const float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
  const float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
  // v' = v + q.w * t + cross(q.xyz, t)
  out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
  out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
  out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}
}  // namespace

// CpuPointToCamera: Points (CamPointBuffer) + Command subtree in → Command out. Reads point[0] from the
// already-cooked upstream bag (CPU — every SwPoint bag is MTL StorageModeShared), derives eye/target/up
// per the .cs (:47-51), and stamps them onto every subtree item that has no camera yet (the Camera-op
// push/pop mechanism: innermost wins). The executor builds the matrices (it owns the output aspect).
RenderCommand cookCpuPointToCamera(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.inputCommand) return rc;     // no subtree wired → empty (the Camera-op posture)
  rc.items = c.inputCommand->items;   // COPY the subtree (we re-emit it, possibly stamped)

  // FORK #3: no/empty CamPointBuffer → TiXL returns without setting the reference (:24-25); sw leaves
  // the items unstamped → they render under the ambient/default camera.
  if (!c.points || c.count == 0) return rc;
  const SwPoint* pts = (const SwPoint*)const_cast<MTL::Buffer*>(c.points)->contents();
  const SwPoint& p = pts[0];  // :27 TypedElements[0] (SamplePos interpolation is commented out in TiXL)

  const float pos[3] = {p.Position.x, p.Position.y, p.Position.z};
  const float q[4] = {p.Rotation.x, p.Rotation.y, p.Rotation.z, p.Rotation.w};
  const float unitZ[3] = {0.0f, 0.0f, 1.0f};
  const float unitY[3] = {0.0f, 1.0f, 0.0f};
  float fwd[3], up[3];
  if (cpuPointToCameraBugIdentityOrientation()) {
    // ★tooth: drop the Orientation readback → forward/up stay the raw axes → target/up diverge → RED.
    fwd[0] = unitZ[0]; fwd[1] = unitZ[1]; fwd[2] = unitZ[2];
    up[0] = unitY[0]; up[1] = unitY[1]; up[2] = unitY[2];
  } else {
    quatRotate(q, unitZ, fwd);  // :48 forward = Transform(UnitZ, p.Orientation)
    quatRotate(q, unitY, up);   // :51 up      = Transform(UnitY, p.Orientation)
  }

  const float fovDeg = cookParam(c, "FieldOfView", 45.0f);  // .t3 default 45 (:64 ToRadians in TiXL;
                                                            // sw stamps degrees, executor converts)
  float clipDef[2] = {0.01f, 1000.0f};
  float clip[2];
  cookVecN(c, "ClipPlanes", clipDef, 2, clip);              // :57 NearFarClip, .t3 (0.01,1000)
  float aspect = cookParam(c, "AspectRatio", -1.0f);        // .t3 default -1
  if (aspect < 0.0001f) aspect = 0.0f;                      // :42-45 → RequestedResolution == executor
                                                            // output aspect (stamped 0 = that fallback)

  for (RenderDrawItem& it : rc.items) {
    if (it.hasCamera) continue;  // a NESTED camera already stamped this item (innermost wins = pop)
    it.hasCamera = true;
    it.camEye[0] = pos[0]; it.camEye[1] = pos[1]; it.camEye[2] = pos[2];      // :47 Position
    it.camTarget[0] = pos[0] + fwd[0]; it.camTarget[1] = pos[1] + fwd[1];     // :50 position + forward
    it.camTarget[2] = pos[2] + fwd[2];
    it.camUp[0] = up[0]; it.camUp[1] = up[1]; it.camUp[2] = up[2];            // :51
    it.camFovDeg = fovDeg;
    it.camNear = clip[0];
    it.camFar = clip[1];
    it.camAspect = aspect;  // <=0 → executor uses output aspect (:42-45)
  }
  return rc;
}

void registerCpuPointToCameraOp() { registerCmdOp("CpuPointToCamera", cookCpuPointToCamera); }

// ───────────────────────────────────────── GOLDEN ─────────────────────────────────────────
// --selftest-cpupointtocamera (BOTH cook legs). Harness = the Execute-golden capture shape: a stub POINT
// op writes ONE hand-authored SwPoint (CPU write into its shared output bag); a stub Command child emits
// ONE unstamped item; a stub RenderTarget captures the chain CpuPointToCamera forwarded. Assert the
// stamped camEye/camTarget/camUp/camFovDeg/camNear/camFar/camAspect against the .cs closed form.
//
// CASE (non-identity orientation — the P2 guard): p0.Position=(0.5,-1,2), p0.Orientation = 90° about X
//   = (sin45°,0,0,cos45°) = (0.70710678,0,0,0.70710678). Hand-derived via the System.Numerics formula
//   (t=2·q×v; v'=v+w·t+q×t — worked through in the header comment of quatRotate):
//     forward = Transform(UnitZ,q) = (0,-1, 0)  → target = (0.5, -2, 2)      (:48,:50)
//     up      = Transform(UnitY,q) = (0, 0, 1)                                (:51)
//   Params: FieldOfView=60, ClipPlanes=(0.5,800), AspectRatio=2 → stamped verbatim; a second leg leaves
//   params unauthored → defaults 45/(0.01,1000)/-1→0 (the .t3 default path).
// -bug: cpuPointToCameraBugIdentityOrientation() → the REAL cook skips the rotate → target reads
//   (0.5,-1,3) and up (0,1,0) → both orientation assertions RED on both legs.
namespace {
RenderCommand g_cpc_captured;   // chain the stub RenderTarget was handed
bool g_cpc_haveCapture = false;

// Stub POINT op: write ONE hand-authored SwPoint into the REAL output bag (CPU, StorageModeShared).
void cookStubCamPoint(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  SwPoint* out = (SwPoint*)c.output->contents();
  SwPoint& p = out[0];
  p.Position = {0.5f, -1.0f, 2.0f};
  p.FX1 = 1.0f;
  p.Rotation = {0.70710678f, 0.0f, 0.0f, 0.70710678f};  // 90° about X (sin45,0,0,cos45)
  p.Color = {1.0f, 1.0f, 1.0f, 1.0f};
  p.Scale = {1.0f, 1.0f, 1.0f};
  p.FX2 = 1.0f;
}
uint32_t stubCamPointCount(uint32_t) { return 1u; }

// Stub Command child: ONE unstamped item (hasCamera=false) the camera op must stamp.
RenderCommand cookStubCamChild(CmdCookCtx&) {
  RenderCommand rc;
  RenderDrawItem it{};
  it.count = 7u;  // witness tag
  rc.items.push_back(it);
  return rc;
}

// Stub RenderTarget: capture the chain (the Execute-golden introspection).
void cookStubCamCapture(TexCookCtx& c) {
  if (c.command) { g_cpc_captured = *c.command; g_cpc_haveCapture = true; }
}

NodeSpec atomicSpecCpc(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}

void installCpcSpecs() {
  std::map<std::string, NodeSpec> dyn;
  dyn["StubCamPoint"] = atomicSpecCpc("StubCamPoint", {{"out", "out", "Points", false}});
  dyn["StubCamChild"] = atomicSpecCpc("StubCamChild", {{"out", "out", "Command", false}});
  dyn["StubCamCapture"] = atomicSpecCpc(
      "StubCamCapture", {{"command", "command", "Command", true}, {"out", "out", "Texture2D", false}});
  setDynamicSpecs(std::move(dyn));
}

int cpcInPort(const char* type, const char* id) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (s->ports[i].isInput && s->ports[i].id == id) return (int)i;
  return -1;
}
int cpcOutPort(const char* type) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (!s->ports[i].isInput) return (int)i;
  return -1;
}

// 1=StubCamPoint → 3.CamPointBuffer; 2=StubCamChild → 3.Command; 3=CpuPointToCamera → 4=StubCamCapture.
Graph buildCpcGraph(bool authorParams) {
  Graph g;
  Node sp; sp.id = 1; sp.type = "StubCamPoint"; g.nodes.push_back(sp);
  Node ch; ch.id = 2; ch.type = "StubCamChild"; g.nodes.push_back(ch);
  Node cam; cam.id = 3; cam.type = "CpuPointToCamera";
  if (authorParams) {
    cam.params["FieldOfView"] = 60.0f;
    cam.params["ClipPlanes.x"] = 0.5f; cam.params["ClipPlanes.y"] = 800.0f;
    cam.params["AspectRatio"] = 2.0f;
  }
  g.nodes.push_back(cam);
  Node rt; rt.id = 4; rt.type = "StubCamCapture"; g.nodes.push_back(rt);

  g.connections.push_back({101, pinId(1, cpcOutPort("StubCamPoint")),
                           pinId(3, cpcInPort("CpuPointToCamera", "CamPointBuffer"))});
  g.connections.push_back({102, pinId(2, cpcOutPort("StubCamChild")),
                           pinId(3, cpcInPort("CpuPointToCamera", "Command"))});
  g.connections.push_back({103, pinId(3, cpcOutPort("CpuPointToCamera")),
                           pinId(4, cpcInPort("StubCamCapture", "command"))});
  return g;
}

bool nearCpc(float a, float b) { return std::fabs(a - b) < 1e-5f; }
}  // namespace

int runCpuPointToCameraSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-cpupointtocamera] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // CpuPointToCamera (the REAL builtin under test)
  registerPointOp("StubCamPoint", cookStubCamPoint, nullptr, nullptr, stubCamPointCount);
  registerCmdOp("StubCamChild", cookStubCamChild);
  registerTexOp("StubCamCapture", cookStubCamCapture);
  installCpcSpecs();

  cpuPointToCameraBugIdentityOrientation() = injectBug;  // ★the orientation tooth (REAL cook seam)

  bool allFaithful = true;
  bool anyTripped = false;
  const char* pathName[2] = {"flat", "resident"};

  for (int path = 0; path < 2; ++path) {
    for (int authored = 0; authored < 2; ++authored) {
      Graph g = buildCpcGraph(authored == 1);
      PointGraph pg(dev, lib, q, 64, 64);
      EvaluationContext ctx{};
      ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
      g_cpc_captured = RenderCommand{}; g_cpc_haveCapture = false;
      if (path == 0) {
        pg.cook(g, ctx, nullptr, /*terminal=*/4);
      } else {
        SymbolLibrary slib = libFromGraph(g);
        ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
        pg.cookResident(rg, ctx, nullptr, /*path=*/"4");
      }
      bool ok = g_cpc_haveCapture && g_cpc_captured.items.size() == 1;
      if (ok) {
        const RenderDrawItem& it = g_cpc_captured.items[0];
        // Closed form (CpuPointToCamera.cs:47-51 + the hand-derived quaternion rotation above).
        bool eyeOk = it.hasCamera && nearCpc(it.camEye[0], 0.5f) && nearCpc(it.camEye[1], -1.0f) &&
                     nearCpc(it.camEye[2], 2.0f);
        bool tgtOk = nearCpc(it.camTarget[0], 0.5f) && nearCpc(it.camTarget[1], -2.0f) &&
                     nearCpc(it.camTarget[2], 2.0f);                    // pos + (0,-1,0)
        bool upOk = nearCpc(it.camUp[0], 0.0f) && nearCpc(it.camUp[1], 0.0f) &&
                    nearCpc(it.camUp[2], 1.0f);                          // UnitY rotated → (0,0,1)
        bool prmOk = authored
                         ? (nearCpc(it.camFovDeg, 60.0f) && nearCpc(it.camNear, 0.5f) &&
                            nearCpc(it.camFar, 800.0f) && nearCpc(it.camAspect, 2.0f))
                         : (nearCpc(it.camFovDeg, 45.0f) && nearCpc(it.camNear, 0.01f) &&
                            nearCpc(it.camFar, 1000.0f) && nearCpc(it.camAspect, 0.0f));  // -1 → 0
        bool tagOk = it.count == 7u;  // the subtree item survived the stamp untouched otherwise
        ok = eyeOk && tgtOk && upOk && prmOk && tagOk;
        if (!ok) anyTripped = true;
        std::printf("[selftest-cpupointtocamera] %s %s: eye=(%.2f,%.2f,%.2f) tgt=(%.2f,%.2f,%.2f) "
                    "up=(%.2f,%.2f,%.2f) fov=%.1f clip=(%.2f,%.0f) aspect=%.2f -> %s\n",
                    pathName[path], authored ? "authored" : "defaults", it.camEye[0], it.camEye[1],
                    it.camEye[2], it.camTarget[0], it.camTarget[1], it.camTarget[2], it.camUp[0],
                    it.camUp[1], it.camUp[2], it.camFovDeg, it.camNear, it.camFar, it.camAspect,
                    ok ? "faithful-ok" : "tripped");
      } else {
        std::printf("[selftest-cpupointtocamera] %s %s: FAIL (no capture / wrong item count %zu)\n",
                    pathName[path], authored ? "authored" : "defaults", g_cpc_captured.items.size());
      }
      allFaithful = allFaithful && ok;
    }
  }

  cpuPointToCameraBugIdentityOrientation() = false;  // reset (process hygiene)
  setDynamicSpecs({});
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (!anyTripped) {
      std::printf("[selftest-cpupointtocamera] injectBug did not trip (identity-orientation collapse "
                  "changed no stamp)\n");
      return 0;  // dead tooth → exit 0 so --bite's NO-BITE list catches it (GOLDEN_STANDARD P1)
    }
    std::printf("[selftest-cpupointtocamera] injectBug correctly RED (dropped Orientation readback → "
                "target/up fell back to raw axes, both legs)\n");
    return 1;
  }
  std::printf("[selftest-cpupointtocamera] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/381, {"cpupointtocamera", runCpuPointToCameraSelfTest});

}  // namespace sw
