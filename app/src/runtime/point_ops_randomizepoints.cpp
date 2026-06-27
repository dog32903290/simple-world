// RandomizePoints — lane-A MODIFIER op: cook fn + register + golden. Faithful port of
// external/tixl .../point/modify/RandomizePoints (.cs slots, .hlsl math). Reads an input bag
// (c.inputs[0]) and writes a count-preserving bag (c.output) whose every point has its
// Position/Rotation/Scale/F1/F2/Color jittered by a per-point hash-driven pseudo-random offset.
// The point count is INHERITED from the upstream bag (no Count param — PointGraph::nodeCount
// gives a modifier its input's count). Copies the TransformPoints modifier TEMPLATE shape.
//
// Self-contained leaf (its own capture vector + registerDrawOp). The cook reads scalar params
// via paramOr on the node being cooked (c.nodeId) and vector params via readVecN(*n,...).
#include "runtime/point_ops.h"
#include "runtime/tex_op_cache.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"                // calcDispatchCount
#include "runtime/graph.h"                   // Graph/Node/readVecN/pinId
#include "runtime/point_graph.h"             // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/randomizepoints_params.h"  // RandomizeParams, RandomizeBinding
#include "runtime/tixl_point.h"              // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// RandomizePoints modifier: dispatch the randomizepoints kernel input bag -> output bag.
// count comes from c.count (inherited from the upstream Points bag). No input bag = safe no-op.
void cookRandomizePoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;  // unwired input -> nothing to randomize

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "randomizepoints");
  if (!pso) return;

  RandomizeParams P{};
  P.Count = c.count;
  P.Strength = cookParam(c, "Strength", 1.0f);

  float pos[3] = {0, 0, 0}, rot[3] = {0, 0, 0}, str[3] = {0, 0, 0};
  float col[4] = {0, 0, 0, 0}, gb[2] = {0.5f, 0.5f};
  cookVecN(c, "Position", pos, 3, pos);
  cookVecN(c, "Rotation", rot, 3, rot);
  cookVecN(c, "Stretch", str, 3, str);
  cookVecN(c, "ColorHSB", col, 4, col);
  cookVecN(c, "GainAndBias", gb, 2, gb);
  P.RandomizePositionX = pos[0]; P.RandomizePositionY = pos[1]; P.RandomizePositionZ = pos[2];
  P.RandomizeRotationX = rot[0]; P.RandomizeRotationY = rot[1]; P.RandomizeRotationZ = rot[2];
  P.StretchX = str[0]; P.StretchY = str[1]; P.StretchZ = str[2];
  P.RandomizeColorX = col[0]; P.RandomizeColorY = col[1];
  P.RandomizeColorZ = col[2]; P.RandomizeColorW = col[3];
  P.GainAndBiasX = gb[0]; P.GainAndBiasY = gb[1];

  P.Scale = cookParam(c, "Scale", 0.0f);
  P.RandomizeF1 = cookParam(c, "F1", 0.0f);
  P.RandomizeF2 = cookParam(c, "F2", 0.0f);
  P.RandomSeed = cookParam(c, "RandomPhase", 0.0f);

  P.OffsetMode = (uint32_t)(cookParam(c, "OffsetMode", 0.0f) + 0.5f);
  P.UsePointSpace = (uint32_t)(cookParam(c, "Space", 0.0f) + 0.5f);
  P.Interpolation = (uint32_t)(cookParam(c, "Interpolation", 1.0f) + 0.5f);
  P.ClampColorsEtc = cookParam(c, "ClampColorsEtc", 0.0f) > 0.5f ? 1 : 0;
  P.Repeat = (int32_t)(cookParam(c, "Repeat", 0.0f) + 0.5f);
  P.StrengthFactor = (int32_t)(cookParam(c, "StrengthFactor", 0.0f) + 0.5f);

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, RANDOMIZE_SourcePoints);
  enc->setBuffer(c.output, 0, RANDOMIZE_ResultPoints);
  enc->setBytes(&P, sizeof(P), RANDOMIZE_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capRnd = nullptr;
void captureDrawRnd(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capRnd || !pts || c.count == 0) return;
  g_capRnd->assign(c.count, SwPoint{});
  std::memcpy(g_capRnd->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerRandomizePointsOp() { registerPointOp("RandomizePoints", cookRandomizePoints); }

// Golden: SpherePoints(R at origin) -> RandomizePoints(ObjectSpace, Position=(P,P,P),
// OffsetMode=Scatter, Strength=1) -> capture. TEETH:
//  (1) count is PRESERVED (a modifier never changes the point count).
//  (2) the jitter actually MOVED points off the clean sphere: the per-point radius from origin
//      now SCATTERS (max |r-R| is well above 0 — a clean SpherePoints has every r==R exactly),
//      AND the displacements are not all identical (different points get different offsets),
//      AND the mean displacement is ~0 (Scatter centers the noise on [-1,1] -> no net drift).
// injectBug: Strength=0 -> the kernel adds 0 everywhere -> points stay exactly on the clean
// sphere (max |r-R| ~ 0, displacement spread ~ 0) -> the "it actually jittered" assertion FAILs.
int runRandomizePointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 1024;
  const float R = 2.0f;
  const float JIT = 0.5f;  // per-axis position jitter amplitude

  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-randomizepoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();      // SpherePoints (the input generator) + friends
  registerRandomizePointsOp();    // RandomizePoints (this op; explicit -> self-contained)
  std::vector<SwPoint> captured;
  g_capRnd = &captured;
  registerDrawOp("DrawPoints", captureDrawRnd);

  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "SpherePoints";
  gen.params["Count"] = (float)N;
  gen.params["Radius"] = R;
  g.nodes.push_back(gen);

  Node rnd; rnd.id = 2; rnd.type = "RandomizePoints";
  rnd.params["Strength"] = injectBug ? 0.0f : 1.0f;  // bug: 0 strength -> identity passthrough
  rnd.params["StrengthFactor"] = 0.0f;               // None -> strength is the raw Strength
  rnd.params["Space"] = 1.0f;                        // ObjectSpace -> raw world-axis jitter
  rnd.params["OffsetMode"] = 1.0f;                   // Scatter -> centered [-1,1] noise
  rnd.params["Interpolation"] = 0.0f;                // None -> deterministic per-point
  rnd.params["Repeat"] = 0.0f;
  rnd.params["RandomPhase"] = 0.0f;
  rnd.params["Position.x"] = JIT; rnd.params["Position.y"] = JIT; rnd.params["Position.z"] = JIT;
  rnd.params["GainAndBias.x"] = 0.5f; rnd.params["GainAndBias.y"] = 0.5f;  // neutral remap
  g.nodes.push_back(rnd);

  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // SpherePoints.out -> RandomizePoints.in(port0)
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // RandomizePoints.out(port1) -> DrawPoints.in

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  // Measure how far the modifier moved each point off the clean sphere (|pos|==R originally).
  bool countOk = captured.size() == N;
  float maxRadErr = 0.0f;           // peak |r - R| -> the jitter shoved points off the sphere
  float dispMin = 1e9f, dispMax = -1e9f;  // spread of per-point radial displacement
  double meanSignedDisp = 0.0;      // Scatter should center on ~0 (no net drift)
  for (const SwPoint& p : captured) {
    float r = std::sqrt(p.Position.x * p.Position.x + p.Position.y * p.Position.y +
                        p.Position.z * p.Position.z);
    float disp = r - R;
    float e = std::fabs(disp);
    if (e > maxRadErr) maxRadErr = e;
    if (disp < dispMin) dispMin = disp;
    if (disp > dispMax) dispMax = disp;
    meanSignedDisp += disp;
  }
  if (!captured.empty()) meanSignedDisp /= (double)captured.size();
  float dispSpread = (captured.size() == N) ? (dispMax - dispMin) : 0.0f;

  // TEETH: with Strength=1 the jitter is real (points left the sphere by a meaningful amount AND
  // by DIFFERENT amounts) and centered (Scatter -> |mean| small). injectBug (Strength=0) makes
  // maxRadErr ~ 0 and dispSpread ~ 0 -> both jitter assertions FAIL.
  bool jittered = maxRadErr > 0.05f;                 // points actually left the clean sphere
  bool varied = dispSpread > 0.1f;                   // not a uniform shift -> per-point noise
  bool centered = std::fabs((float)meanSignedDisp) < 0.15f;  // Scatter -> ~no net drift
  bool pass = countOk && jittered && varied && centered;
  printf("[selftest-randomizepoints] n=%zu maxRadErr=%.4f(need>0.05) dispSpread=%.4f(need>0.1) "
         "meanDisp=%.4f(|.|<0.15) -> %s\n",
         captured.size(), maxRadErr, dispSpread, (float)meanSignedDisp, pass ? "PASS" : "FAIL");

  g_capRnd = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// ============================================================================
// ROTATION-ORDER REGRESSION LOCK (batch 17, permanent bite tooth). RandomizePoints.hlsl:124-128
// applies the per-point rotation jitter as THREE successive, individually-re-normalized axis
// rotations, accumulated by RIGHT-multiplying onto the running rot:
//     rot = normalize(qMul(rot, qFromAngleAxis(a.x, X)))   // then Y, then Z
// This is the INCREMENTAL form (order- and renormalize-dependent), NOT a single combined Euler
// quaternion. We verified app/shaders/randomizepoints.metal:169-171 is byte-identical to the .hlsl
// (the batch-16 transformpoints "Z·Y·X combined" bug does NOT apply here — different shape).
// This lock pins that shape: if anyone reorders the three axes OR collapses them into one combined
// quaternion (qMul(Rz,qMul(Ry,Rx)) etc.), this tooth turns RED.
//
// Trick to avoid re-implementing TiXL's hash chain: with input Position=(0,0,0), identity Rotation,
// UsePointSpace=ObjectSpace (raw, position offset NOT rotated), OffsetMode=Add (no [-1,1] remap),
// RandomizePosition=(1,1,1), neutral GainAndBias, Strength=1, StrengthFactor=None, Interpolation=
// None, the kernel writes p.Position = biasedA.xyz EXACTLY. So we recover each point's biasedA.xyz
// straight from the captured Position, then REPLAY the incremental composition in C++ from those
// recovered values and compare against the captured Rotation (double-cover-safe).
//   injectBug=true replays a COMBINED single quaternion qMul(Ry,qMul(Rx,Rz)) instead of the
//   incremental product -> mismatches the faithful incremental shader -> FAIL (proves the lock
//   discriminates incremental-vs-combined, the exact regression it guards).
// ============================================================================
namespace {
struct RV4 { double x, y, z, w; };
static RV4 rqFromAngleAxis(double angle, double ax, double ay, double az) {
  double s = std::sin(angle * 0.5), c = std::cos(angle * 0.5);
  return { ax * s, ay * s, az * s, c };
}
static RV4 rqMul(RV4 a, RV4 b) {
  return { b.x*a.w + a.x*b.w + (a.y*b.z - a.z*b.y),
           b.y*a.w + a.y*b.w + (a.z*b.x - a.x*b.z),
           b.z*a.w + a.z*b.w + (a.x*b.y - a.y*b.x),
           a.w*b.w - (a.x*b.x + a.y*b.y + a.z*b.z) };
}
static RV4 rqNorm(RV4 q) {
  double n = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
  if (n <= 0.0) return { 0, 0, 0, 1 };
  return { q.x/n, q.y/n, q.z/n, q.w/n };
}
static double rDot4(RV4 a, RV4 b) { return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w; }
}  // namespace

int runRandomizePointsRotationLock(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 256;
  const float PI = 3.14159265358979323846f;
  // Large, UNEQUAL per-axis rotation magnitudes -> the three axis rotations strongly non-commute,
  // so incremental X→Y→Z differs sharply from any reorder or combined product.
  const float RROT[3] = {140.0f, 95.0f, 65.0f};  // degrees, RandomizeRotation per axis

  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[rndrotlock] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Dispatch randomizepoints directly over a bag of N identity points at the origin.
  std::vector<SwPoint> in(N);
  for (uint32_t i = 0; i < N; ++i) {
    in[i] = SwPoint{};
    in[i].Position = SW_PACKED3{0.0f, 0.0f, 0.0f};
    in[i].Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};  // identity orgRot
  }
  RandomizeParams P{};
  P.Count = N; P.Strength = 1.0f; P.StrengthFactor = 0;  // None -> raw Strength
  P.RandomizePositionX = 1.0f; P.RandomizePositionY = 1.0f; P.RandomizePositionZ = 1.0f;  // readback biasedA
  P.RandomizeRotationX = RROT[0]; P.RandomizeRotationY = RROT[1]; P.RandomizeRotationZ = RROT[2];
  P.GainAndBiasX = 0.5f; P.GainAndBiasY = 0.5f;  // neutral remap
  P.OffsetMode = 0u;       // Add -> biasedA stays [0,1] (no [-1,1] scatter remap)
  P.UsePointSpace = 1u;    // ObjectSpace -> position offset is RAW (NOT rotated) -> Position==biasedA
  P.Interpolation = 0u;    // None -> deterministic per-point, t=0
  P.ClampColorsEtc = 0; P.Repeat = 0;

  MTL::Function* fn = lib->newFunction(NS::String::string("randomizepoints", NS::UTF8StringEncoding));
  MTL::ComputePipelineState* pso = fn ? dev->newComputePipelineState(fn, &err) : nullptr;
  if (fn) fn->release();
  if (!pso) {
    printf("[rndrotlock] FAIL: no pipeline\n");
    lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }
  const size_t bytes = (size_t)N * sizeof(SwPoint);
  MTL::Buffer* src = dev->newBuffer(in.data(), bytes, MTL::ResourceStorageModeShared);
  MTL::Buffer* dst = dev->newBuffer(bytes, MTL::ResourceStorageModeShared);
  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(src, 0, RANDOMIZE_SourcePoints);
  enc->setBuffer(dst, 0, RANDOMIZE_ResultPoints);
  enc->setBytes(&P, sizeof(P), RANDOMIZE_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(N, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  std::vector<SwPoint> out(N);
  std::memcpy(out.data(), dst->contents(), bytes);
  src->release(); dst->release(); pso->release();

  // Replay the incremental composition from the recovered biasedA (== captured Position), compare to
  // the captured Rotation. injectBug replays a COMBINED product instead -> mismatch.
  const double DEG = M_PI / 180.0;
  double maxRotErr = 0.0; int worstI = -1;
  double anyAngle = 0.0;  // sanity: the jitter must actually rotate (not a no-op lock)
  for (uint32_t i = 0; i < N; ++i) {
    double bx = out[i].Position.x, by = out[i].Position.y, bz = out[i].Position.z;  // biasedA.xyz
    double ax = RROT[0]*DEG * 1.0 * bx;  // randomRotate = RandomizeRotation*deg * strength(1) * biasedA
    double ay = RROT[1]*DEG * 1.0 * by;
    double az = RROT[2]*DEG * 1.0 * bz;
    RV4 ref;
    if (!injectBug) {
      // FAITHFUL incremental: ((I·Rx)·Ry)·Rz, renormalized each step (matches shader 169-171).
      RV4 r = { 0, 0, 0, 1 };
      r = rqNorm(rqMul(r, rqFromAngleAxis(ax, 1, 0, 0)));
      r = rqNorm(rqMul(r, rqFromAngleAxis(ay, 0, 1, 0)));
      r = rqNorm(rqMul(r, rqFromAngleAxis(az, 0, 0, 1)));
      ref = r;
    } else {
      // BUGGY combined single quaternion (the kind of "cleanup" the lock must reject).
      ref = rqNorm(rqMul(rqFromAngleAxis(ay, 0, 1, 0),
                         rqMul(rqFromAngleAxis(ax, 1, 0, 0), rqFromAngleAxis(az, 0, 0, 1))));
    }
    RV4 got = { out[i].Rotation.x, out[i].Rotation.y, out[i].Rotation.z, out[i].Rotation.w };
    double er = 1.0 - std::fabs(rDot4(ref, got));  // double-cover-safe
    if (er > maxRotErr) { maxRotErr = er; worstI = (int)i; }
    anyAngle += std::fabs(ax) + std::fabs(ay) + std::fabs(az);
  }
  anyAngle /= (double)N;

  // Lock passes when the captured rotation matches the FAITHFUL incremental replay AND the jitter is
  // non-trivial (the rotation actually moved). injectBug's combined replay won't match -> FAIL.
  bool matches = (maxRotErr < 1e-4);
  bool nonTrivial = (anyAngle > 0.1);  // guards a degenerate "everything identity" false-green
  bool pass = matches && nonTrivial;
  printf("[rndrotlock] n=%u maxRotErr=%.7f(need<1e-4) worstI=%d mean|angle|=%.3frad ref=%s -> %s "
         "(PASS=shader matches INCREMENTAL X→Y→Z; FAIL=order/shape regressed)\n",
         N, maxRotErr, worstI, anyAngle, injectBug ? "COMBINED(bug)" : "incremental",
         pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
