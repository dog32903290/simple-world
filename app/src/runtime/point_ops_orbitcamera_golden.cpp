// OrbitCamera GOLDEN — see point_ops_orbitcamera.h for the op + fork ledger.
// TiXL authority: external/tixl/Operators/Lib/render/camera/OrbitCamera.cs:50-155 +
// Core/Utils/MathUtils.cs:16-41 (PerlinNoise) / :141-144 (Fade) / :305-308 (Lerp).
//
// ORACLE INDEPENDENCE: the expected values are an INDEPENDENT golden-local transcription of the .cs
// pipeline — its OWN PerlinNoise copy (NOT anim_math.h, which the op uses) anchored by a HAND-COMPUTED
// integer-hash value, and its rotations built through the QUATERNION path (System.Numerics
// Quaternion.CreateFromYawPitchRoll + q·v·q*) instead of the op's explicit Ry·Rx matrices — a shared
// transcription slip in one construction does not silently agree with the other. On top of the
// transcription equality, IMPL-INDEPENDENT geometric invariants pin the semantics at a non-trivial
// orbit position (|eye|==radius, |target-eye|==1, aim=0 → looks at the orbit center, roll=0 → up
// unchanged) — truths of the .cs geometry, not of any transcription.
#include "runtime/point_ops_orbitcamera.h"

#include "runtime/field_camera.h"    // lookAtRH/perspectiveFovRH/objectToClipSpace/mat4TransformPointDivW
#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp/registerTexOp, cookResident
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
namespace {
constexpr float kToRadO = 3.14159265358979323846f / 180.0f;

// ── ORACLE transcription (independent copy; MathUtils.cs line cites) ──────────────────────────────
float oFade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }        // :141-144
float oNoise(int x, int seed) {                                                       // :36-41
  // C# does this in UNCHECKED int32 (silent two's-complement wrap); C++ signed overflow is UB, so the
  // arithmetic runs in uint32_t — bit-identical wrap, and the & 0x7fffffff makes the sign irrelevant.
  uint32_t n = (uint32_t)x + (uint32_t)seed * 137u;
  n = (n << 13) ^ n;
  const uint32_t m = n * (n * n * 15731u + 789221u) + 1376312589u;
  return (float)(1.0 - (double)(m & 0x7fffffffu) / 1073741824.0);
}
float oPerlin(float value, float period, int octaves, int seed) {                     // :16-34
  float sum = 0.0f;
  octaves = octaves < 1 ? 1 : (octaves > 20 ? 20 : octaves);
  float freq = period, amp = 0.5f;
  for (int o = 0; o < octaves - 1; ++o) {
    const float v = value * freq + seed * 12.468f;
    const float a = oNoise((int)v, seed), b = oNoise((int)v + 1, seed);
    const float t = oFade(v - (float)std::floor(v));
    sum += (a + (b - a) * t) * amp;
    freq *= 2.0f;
    amp *= 0.5f;
  }
  return sum;
}
// Quaternion.CreateFromYawPitchRoll(yaw,pitch,0) + v' = q·v·q* (System.Numerics; roll-pitch-yaw order).
void oYawPitchRotate(const float v[3], float yaw, float pitch, float out[3]) {
  const float sp = std::sin(pitch * 0.5f), cp = std::cos(pitch * 0.5f);
  const float sy = std::sin(yaw * 0.5f), cy = std::cos(yaw * 0.5f);
  const float qx = cy * sp, qy = sy * cp, qz = -sy * sp, qw = cy * cp;
  // v' = v + 2·(qv × (qv × v + w·v))
  const float t1[3] = {qy * v[2] - qz * v[1] + qw * v[0], qz * v[0] - qx * v[2] + qw * v[1],
                       qx * v[1] - qy * v[0] + qw * v[2]};
  out[0] = v[0] + 2.0f * (qy * t1[2] - qz * t1[1]);
  out[1] = v[1] + 2.0f * (qz * t1[0] - qx * t1[2]);
  out[2] = v[2] + 2.0f * (qx * t1[1] - qy * t1[0]);
}
void oAxisAngle(const float v[3], const float k[3], float ang, float out[3]) {  // Rodrigues
  const float c = std::cos(ang), s = std::sin(ang);
  const float kv = k[0] * v[0] + k[1] * v[1] + k[2] * v[2];
  const float cr[3] = {k[1] * v[2] - k[2] * v[1], k[2] * v[0] - k[0] * v[2],
                       k[0] * v[1] - k[1] * v[0]};
  for (int i = 0; i < 3; ++i) out[i] = v[i] * c + cr[i] * s + k[i] * kv * (1.0f - c);
}
void oNorm(float v[3]) {
  const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (l > 0.0f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}
struct OrbitOracleOut { float eye[3], target[3], up[3]; };
// The OrbitCamera.cs:50-155 pipeline (Damping=0), transcribed on the oracle helpers above.
OrbitOracleOut orbitOracle(const std::map<std::string, float>& p, float localFxTime) {
  auto P = [&](const char* id, float def) {
    auto it = p.find(id);
    return it != p.end() ? it->second : def;
  };
  const float ot = P("OverrideTime", 0.0f);
  const float time = ot != 0.0f ? ot : localFxTime;                              // :55-56 (fork)
  const float dist = P("DistanceToTarget", 3.0f);
  const float radius = dist == 0.0f ? 0.0001f : dist;                            // :71
  const int seed = (int)P("Seed", 0.0f);
  const float wSpeed = P("WobbleSpeed", 0.2f);
  int wCplx = (int)P("WobbleComplexity", 2.0f);
  wCplx = wCplx < 1 ? 1 : (wCplx > 8 ? 8 : wCplx);                               // :76
  auto angle = [&](const char* x, const char* y, float xd, float yd, int si) {   // :143-154
    const float ax = P(x, xd), ay = P(y, yd);
    const float w = std::fabs(ay) < 0.001f
                        ? 0.0f
                        : (oPerlin(time * wSpeed, 1.0f, wCplx, seed + 123 * si) - 0.5f) * 2.0f * ay;
    return kToRadO * (ax + w);
  };
  const float yaw = angle("SpinAngleAndWobble.x", "SpinAngleAndWobble.y", 0, 0, 1) +
                    kToRadO * ((P("SpinRate", 0.05f) * time) * 360.0f + P("SpinOffset", 0.0f) +
                               oPerlin(0.0f, 1.0f, 6, seed) * 360.0f);           // :85-88
  const float pitch = -angle("OrbitAngleAndWobble.x", "OrbitAngleAndWobble.y", 30, 5, 2);  // :89
  const float p0[3] = {0, 0, radius};
  float eye[3];
  oYawPitchRotate(p0, yaw, pitch, eye);                                          // :90-95
  const float center[3] = {P("CameraTargetPosition.x", 0), P("CameraTargetPosition.y", 0),
                           P("CameraTargetPosition.z", 0)};
  float vd[3] = {center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]};    // :98
  const float vy = angle("AimYawAngleAndWobble.x", "AimYawAngleAndWobble.y", 0, 0, 3) +
                   P("RotationOffset.x", 0) * kToRadO;                           // :100
  const float vp = angle("AimPitchAngleAndWobble.x", "AimPitchAngleAndWobble.y", 0, 0, 4) +
                   P("RotationOffset.y", 0) * kToRadO;                           // :101
  float adj[3];
  oYawPitchRotate(vd, vy, vp, adj);                                              // :102-107
  oNorm(adj);                                                                    // :108
  float up[3] = {P("Up.x", 0), P("Up.y", 1), P("Up.z", 0)};                      // :114
  const float roll = angle("AimRollAngleAndWobble.x", "AimRollAngleAndWobble.y", 0, 5, 5) +
                     P("RotationOffset.z", 0) * kToRadO;                         // :115
  float upR[3];
  oAxisAngle(up, adj, roll, upR);                                                // :117-118
  oNorm(upR);                                                                    // :119
  const float po[3] = {P("PositionOffset.x", 0), P("PositionOffset.y", 0), P("PositionOffset.z", 0)};
  float poR[3];
  oYawPitchRotate(po, yaw, pitch, poR);                                          // :121
  OrbitOracleOut o;
  for (int i = 0; i < 3; ++i) {
    o.eye[i] = eye[i] + poR[i];
    o.target[i] = o.eye[i] + adj[i];                                             // :123
    o.up[i] = upR[i];
  }
  return o;
}

// ── injectBug wrapper: DROP the stamp in the real cook (lost camera push) ──
bool g_orbitDropStamp = false;
RenderCommand cookOrbitForTest(CmdCookCtx& c) {
  if (g_orbitDropStamp) {
    RenderCommand rc;
    if (c.inputCommand) rc.items = c.inputCommand->items;  // push lost → executor default camera
    return rc;
  }
  return cookOrbitCamera(c);
}

void cookSolidImageOc(TexCookCtx& c) {
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
NodeSpec atomicSpecOc(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}
int portIdxOc(const char* type, const char* dataType, bool input) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (s->ports[i].isInput == input && s->ports[i].dataType == dataType) return (int)i;
  return -1;
}
void installOcSpecs() {
  std::map<std::string, NodeSpec> dyn;
  dyn["SolidImage"] = atomicSpecOc("SolidImage", {{"out", "out", "Texture2D", false}});
  dyn["Layer2d"] = atomicSpecOc("Layer2d",
      {{"Image", "Image", "Texture2D", true},
       {"out", "out", "Command", false},
       {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
       {"ScaleMode", "ScaleMode", "Float", true, 0.0f, 0.0f, 5.0f, Widget::Enum, {}, true},
       {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {}, true}});
  setDynamicSpecs(std::move(dyn));
}
int readTargetROc(PointGraph& pg, uint32_t W, uint32_t H, float ndcX, float ndcY) {
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
bool near3(const float a[3], const float b[3], float tol) {
  return std::fabs(a[0] - b[0]) < tol && std::fabs(a[1] - b[1]) < tol && std::fabs(a[2] - b[2]) < tol;
}
}  // namespace

int runOrbitCameraSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  bool allFaithful = true;
  g_orbitDropStamp = injectBug;

  // ── ANCHOR: hand-computed PerlinNoise(0,1,6,0) (MathUtils.cs:16-41 arithmetic done by hand):
  // Noise(0,0): n=0 → hash = 0·(…)+1376312589 → 1 − 1376312589/2^30 = −0.2817909839; t=Fade(0)=0 →
  // every octave contributes the SAME a; amps 0.5+0.25+0.125+0.0625+0.03125 = 0.96875 →
  // −0.2817909839 × 0.96875 = −0.2729850156 (float32 accumulation −0.27298501). Pins the oracle's
  // noise leg to the .cs integer hash independent of any transcription.
  {
    const float anchor = oPerlin(0.0f, 1.0f, 6, 0);
    bool ok = std::fabs(anchor - (-0.27298502f)) < 1e-6f;
    allFaithful = allFaithful && ok;
    std::printf("[selftest-orbitcamera] ANCHOR perlin(0,1,6,0)=%.9f want=-0.272985016 -> %s\n",
                anchor, ok ? "faithful-ok" : "tripped");
  }

  // ── TOOTH B1 (transcription equality, ALL wobble legs active, mid-range param set) ──
  {
    std::map<std::string, float> P = {
        {"FOV", 50.0f}, {"AspectRatio", 0.0f}, {"NearFarClip.x", 0.05f}, {"NearFarClip.y", 500.0f},
        {"DistanceToTarget", 4.0f}, {"Seed", 7.0f}, {"WobbleSpeed", 0.3f}, {"WobbleComplexity", 3.0f},
        {"SpinRate", 0.08f}, {"SpinOffset", 20.0f},
        {"SpinAngleAndWobble.x", 12.0f}, {"SpinAngleAndWobble.y", 4.0f},
        {"OrbitAngleAndWobble.x", 35.0f}, {"OrbitAngleAndWobble.y", 10.0f},
        {"AimYawAngleAndWobble.x", 10.0f}, {"AimYawAngleAndWobble.y", 3.0f},
        {"AimPitchAngleAndWobble.x", -15.0f}, {"AimPitchAngleAndWobble.y", 2.0f},
        {"AimRollAngleAndWobble.x", 8.0f}, {"AimRollAngleAndWobble.y", 6.0f},
        {"RotationOffset.x", 5.0f}, {"RotationOffset.y", -3.0f}, {"RotationOffset.z", 2.0f},
        {"PositionOffset.x", 0.1f}, {"PositionOffset.y", 0.2f}, {"PositionOffset.z", -0.15f},
        {"CameraTargetPosition.x", 0.5f}, {"CameraTargetPosition.y", -0.2f},
        {"CameraTargetPosition.z", 0.3f}, {"Up.x", 0.0f}, {"Up.y", 1.0f}, {"Up.z", 0.0f}};
    const float fxTime = 2.5f;
    EvaluationContext ctx{}; ctx.localFxTime = fxTime;
    RenderCommand sub; sub.items.push_back(RenderDrawItem{});  // one unstamped item
    CmdCookCtx cc; cc.ctx = &ctx; cc.params = &P; cc.inputCommand = &sub;
    RenderCommand out = cookOrbitForTest(cc);  // the REAL cook (or the -bug stamp-drop)
    OrbitOracleOut exp = orbitOracle(P, fxTime);
    bool ok = out.items.size() == 1 && out.items[0].hasCamera &&
              near3(out.items[0].camEye, exp.eye, 3e-5f) &&
              near3(out.items[0].camTarget, exp.target, 3e-5f) &&
              near3(out.items[0].camUp, exp.up, 3e-5f) &&
              std::fabs(out.items[0].camFovDeg - 50.0f) < 1e-6f;
    allFaithful = allFaithful && ok;
    std::printf("[selftest-orbitcamera] B1 wobble-set: eye=(%.5f,%.5f,%.5f) want=(%.5f,%.5f,%.5f) "
                "-> %s\n", out.items.empty() ? 0.0f : out.items[0].camEye[0],
                out.items.empty() ? 0.0f : out.items[0].camEye[1],
                out.items.empty() ? 0.0f : out.items[0].camEye[2], exp.eye[0], exp.eye[1], exp.eye[2],
                ok ? "faithful-ok" : "tripped");
  }

  // ── TOOTH B2 (impl-independent geometric invariants at a NON-trivial orbit position) ──
  // .cs geometry truths: the eye orbits the ORIGIN at |eye|==radius (:72-95, rotation preserves norm);
  // target−eye is the NORMALIZED view direction (:108/:123); with zero aim/rotationOffset the camera
  // LOOKS AT CameraTargetPosition (:98 viewDirection = center − eye, unrotated); roll=0 keeps Up (:117).
  {
    std::map<std::string, float> P = {
        {"DistanceToTarget", 2.5f}, {"Seed", 3.0f}, {"SpinRate", 0.1f}, {"SpinOffset", 15.0f},
        {"SpinAngleAndWobble.x", 25.0f}, {"SpinAngleAndWobble.y", 0.0f},
        {"OrbitAngleAndWobble.x", 40.0f}, {"OrbitAngleAndWobble.y", 0.0f},
        {"AimRollAngleAndWobble.x", 0.0f}, {"AimRollAngleAndWobble.y", 0.0f},
        {"CameraTargetPosition.x", 0.4f}, {"CameraTargetPosition.y", 0.1f},
        {"CameraTargetPosition.z", -0.2f}};
    const float fxTime = 1.75f;
    EvaluationContext ctx{}; ctx.localFxTime = fxTime;
    RenderCommand sub; sub.items.push_back(RenderDrawItem{});
    CmdCookCtx cc; cc.ctx = &ctx; cc.params = &P; cc.inputCommand = &sub;
    RenderCommand out = cookOrbitForTest(cc);
    bool ok = out.items.size() == 1 && out.items[0].hasCamera;
    if (ok) {
      const float* e = out.items[0].camEye;
      const float* t = out.items[0].camTarget;
      const float* u = out.items[0].camUp;
      const float eyeLen = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
      float dir[3] = {t[0] - e[0], t[1] - e[1], t[2] - e[2]};
      const float dirLen = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
      float toC[3] = {0.4f - e[0], 0.1f - e[1], -0.2f - e[2]};
      oNorm(toC);
      const float upExp[3] = {0.0f, 1.0f, 0.0f};
      ok = std::fabs(eyeLen - 2.5f) < 1e-4f &&        // |eye| == radius
           std::fabs(dirLen - 1.0f) < 1e-4f &&        // unit view direction
           near3(dir, toC, 1e-4f) &&                  // aim=0 → looks AT the center
           near3(u, upExp, 1e-5f);                    // roll=0 → up unchanged
    }
    allFaithful = allFaithful && ok;
    std::printf("[selftest-orbitcamera] B2 invariants (|eye|=r, |dir|=1, looks-at-center, up) -> %s\n",
                ok ? "faithful-ok" : "tripped");
  }

  // ── TOOTH A (render, PRODUCTION resident terminal): AimYaw=20° slides the quad off center ──
  {
    const uint32_t W = 256, H = 256;
    MTL::Device* dev = MTL::CreateSystemDefaultDevice();
    MTL::CommandQueue* q = dev->newCommandQueue();
    NS::Error* err = nullptr;
    MTL::Library* lib =
        dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
    if (!lib) {
      std::printf("[selftest-orbitcamera] FAIL: no metallib\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    registerBuiltinPointOps();
    registerCmdOp("OrbitCamera", cookOrbitForTest);  // OVERRIDE with the stamp-drop wrapper
    registerTexOp("SolidImage", cookSolidImageOc);
    installOcSpecs();

    // Orbit neutralized to eye=(0,0,3): pitch 0, spin 0, and SpinOffset cancels the constant per-seed
    // Perlin yaw term (:85-88) — the cancel value comes from the ORACLE's noise; if the op's noise
    // disagreed, the yaw≠0 residue would ALSO show up in B1. AimYaw=20° then displaces the projected
    // orbit center to NDC ≈ +tan(20°)/tan(22.5°) ≈ 0.88 (fov 45, square).
    const float spinCancel = -oPerlin(0.0f, 1.0f, 6, 0) * 360.0f;
    Graph g;
    Node sa; sa.id = 1; sa.type = "SolidImage"; g.nodes.push_back(sa);
    Node l; l.id = 2; l.type = "Layer2d";
    l.params["Scale"] = 0.6f; l.params["ScaleMode"] = 4.0f; l.params["BlendMode"] = 0.0f;
    g.nodes.push_back(l);
    Node oc; oc.id = 3; oc.type = "OrbitCamera";
    oc.params["OrbitAngleAndWobble.x"] = 0.0f; oc.params["OrbitAngleAndWobble.y"] = 0.0f;
    oc.params["AimRollAngleAndWobble.x"] = 0.0f; oc.params["AimRollAngleAndWobble.y"] = 0.0f;
    oc.params["AimYawAngleAndWobble.x"] = 20.0f; oc.params["AimYawAngleAndWobble.y"] = 0.0f;
    oc.params["SpinRate"] = 0.0f; oc.params["SpinOffset"] = spinCancel;
    g.nodes.push_back(oc);
    Node rt; rt.id = 4; rt.type = "RenderTarget";
    rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
    g.nodes.push_back(rt);
    g.connections.push_back({101, pinId(1, portIdxOc("SolidImage", "Texture2D", false)),
                             pinId(2, portIdxOc("Layer2d", "Texture2D", true))});
    g.connections.push_back({102, pinId(2, portIdxOc("Layer2d", "Command", false)),
                             pinId(3, portIdxOc("OrbitCamera", "Command", true))});
    g.connections.push_back({103, pinId(3, portIdxOc("OrbitCamera", "Command", false)),
                             pinId(4, portIdxOc("RenderTarget", "Command", true))});

    // Probe placement from the ORACLE camera projected through the SAME field_camera math the VS
    // reproduces: center probe = projected world origin; outside probe = NDC(0,0), guarded ≥0.1 away
    // from every projected quad corner's x-range (misconfig → loud FAIL, not a silent pass).
    OrbitOracleOut ocam = orbitOracle(
        {{"OrbitAngleAndWobble.x", 0.0f}, {"OrbitAngleAndWobble.y", 0.0f},
         {"AimRollAngleAndWobble.x", 0.0f}, {"AimRollAngleAndWobble.y", 0.0f},
         {"AimYawAngleAndWobble.x", 20.0f}, {"AimYawAngleAndWobble.y", 0.0f},
         {"SpinRate", 0.0f}, {"SpinOffset", spinCancel}},
        0.0f);
    Mat4 w2c = lookAtRH(ocam.eye, ocam.target, ocam.up);
    Mat4 c2c = perspectiveFovRH(45.0f * kToRadO, 1.0f, 0.01f, 1000.0f);
    Mat4 o2c = objectToClipSpace(mat4Identity(), w2c, c2c);
    float centerNdc[3];
    mat4TransformPointDivW(o2c, 0.0f, 0.0f, 0.0f, centerNdc);
    float minX = 10.0f;  // nearest projected quad-corner x to NDC 0 (quad = world [-0.6,0.6]^2, z=0)
    for (int cx = -1; cx <= 1; cx += 2)
      for (int cy = -1; cy <= 1; cy += 2) {
        float n[3];
        mat4TransformPointDivW(o2c, 0.6f * (float)cx, 0.6f * (float)cy, 0.0f, n);
        if (n[0] < minX) minX = n[0];
      }
    const bool placement = (std::fabs(centerNdc[0]) < 0.95f && minX > 0.1f);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    PointGraph pg(dev, lib, q, W, H);
    pg.cookResident(rg, ctx, nullptr, /*path=*/"4");
    int centerR = readTargetROc(pg, W, H, centerNdc[0], centerNdc[1]);  // quad center → RED
    int originR = readTargetROc(pg, W, H, 0.0f, 0.0f);                  // slid away → background
    bool ok = placement && (centerR > 200) && (originR < 40);
    allFaithful = allFaithful && ok;
    std::printf("[selftest-orbitcamera] A RESIDENT aimYaw=20: center@(%.3f,%.3f)=%d(>200) "
                "origin=%d(<40) placement=%d -> %s\n", centerNdc[0], centerNdc[1], centerR, originR,
                placement ? 1 : 0, ok ? "faithful-ok" : "tripped");
    setDynamicSpecs({});
    lib->release(); q->release(); dev->release();
  }

  g_orbitDropStamp = false;  // reset (process hygiene)
  pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-orbitcamera] FAIL: injectBug tripped no tooth\n");
      return 0;  // did-not-trip → NO-BITE latch catches the dead tooth (GOLDEN_STANDARD polarity)
    }
    std::printf("[selftest-orbitcamera] injectBug correctly RED (dropped orbit stamp → no camera on "
                "the items → B1/B2 lose the stamp, the render legs fall back to the default camera)\n");
    return 1;
  }
  std::printf("[selftest-orbitcamera] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

}  // namespace sw
