// RepetitionPoints — batch 36 GENERATOR op. GPU NAMED FORK of external/tixl
// .../point/generate/RepetitionPoints.cs, a CPU StructuredList generator (TiXL has NO .hlsl).
// thread i computes the i-th transformed point by the exact per-point recipe in the .cs
// Update() (see repetitionpoints.metal header for the line-by-line map). No input bag.
//
// COUNT POLICY (AddSeparator):
//   TiXL: listCount = count + (AddSeparator ? 1 : 0), with one Point.Separator() appended.
//   The Count Float port natively drives c.count (PointGraph::nodeCount). The +1 for the
//   separator can't be derived from count alone (it depends on the AddSeparator bool), so we
//   use the established PairPointsForLines pattern: a file-static g_repAddSeparator set by the
//   cook fn, read by repCountTransform. The first cook after an AddSeparator flip lags one
//   cook (the driver sizes the bag BEFORE calling cook); the golden warms up before asserting.
//   Single-threaded cook -> the static is selftest-safe.
//
// Self-contained leaf: own capture vector + registerDrawOp (mirrors point_ops_spherepoints.cpp).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"                  // calcDispatchCount
#include "runtime/graph.h"                     // Graph/Node/readVecN/pinId
#include "runtime/point_graph.h"               // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/repetitionpoints_params.h"   // RepetitionPointsParams, RepetitionPointsBinding
#include "runtime/tex_op_cache.h"              // cachedComputePSO
#include "runtime/tixl_point.h"                // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// AddSeparator (0/1) of the LAST-cooked RepetitionPoints node — written by cookRepetitionPoints,
// read by repCountTransform so the bag grows to realCount+1. Single-threaded cook -> safe static.
uint32_t g_repAddSeparator = 0;

// repCountTransform: the driver passes the resolved Count (already clamped via the Count Float
// port, then re-clamped here to TiXL's 1..10000) and we add the separator slot if g_repAddSeparator.
uint32_t repCountTransform(uint32_t natural) {
  uint32_t count = natural;
  if (count < 1u) count = 1u;            // RepetitionPoints.cs:30 Count.Clamp(1,10000)
  if (count > 10000u) count = 10000u;
  return count + g_repAddSeparator;      // listCount = count + (AddSeparator ? 1 : 0)
}

void cookRepetitionPoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;

  // Resolve AddSeparator FIRST so the static is up to date for the next sizing pass.
  bool addSep = cookParam(c, "AddSeparator", 1.0f) > 0.5f;
  g_repAddSeparator = addSep ? 1u : 0u;

  // realCount = the bag minus the separator slot (the driver sized c.count via repCountTransform).
  uint32_t realCount = c.count;
  if (addSep && realCount > 0u) realCount -= 1u;
  if (realCount < 1u) realCount = 1u;
  if (realCount > 10000u) realCount = 10000u;

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "repetitionpoints");
  if (!pso) return;

  RepetitionPointsParams P{};
  P.Count        = realCount;
  P.Phase        = cookParam(c, "Phase", 0.0f);
  P.Scale        = cookParam(c, "Scale", 1.0f);
  P.StartW       = cookParam(c, "StartW", 0.0f);
  P.AddSeparator = addSep ? 1.0f : 0.0f;

  float startPos[3] = {0.0f, 0.0f, 0.0f};
  cookVecN(c, "StartPosition", startPos, 3, startPos);
  P.StartPosX = startPos[0]; P.StartPosY = startPos[1]; P.StartPosZ = startPos[2];

  float translate[3] = {0.0f, 0.0f, 0.0f};
  cookVecN(c, "Translate", translate, 3, translate);
  P.TranslateX = translate[0]; P.TranslateY = translate[1]; P.TranslateZ = translate[2];

  float rotate[3] = {0.0f, 0.0f, 0.0f};
  cookVecN(c, "Rotate", rotate, 3, rotate);
  P.RotateX = rotate[0]; P.RotateY = rotate[1]; P.RotateZ = rotate[2];

  float pivot[3] = {0.0f, 0.0f, 0.0f};
  cookVecN(c, "Pivot", pivot, 3, pivot);
  P.PivotX = pivot[0]; P.PivotY = pivot[1]; P.PivotZ = pivot[2];

  // Dispatch over the full bag (realCount + separator slot) — the shader writes Separator() at
  // index == realCount when AddSeparator.
  uint32_t total = realCount + (addSep ? 1u : 0u);

  MTL::CommandBuffer*         cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(c.output, 0, REPETITION_Points);
  enc->setBytes(&P, sizeof(P), REPETITION_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(total, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capRep = nullptr;
void captureDrawRep(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capRep || !pts || c.count == 0) return;
  g_capRep->assign(c.count, SwPoint{});
  std::memcpy(g_capRep->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerRepetitionPointsOp() {
  registerPointOp("RepetitionPoints", cookRepetitionPoints,
                  /*stateNew=*/nullptr, /*stateFree=*/nullptr,
                  repCountTransform,
                  /*countFromFirstPointsInput=*/false);
}

// Golden — three teeth, all through the real pg.cook + readback (capture draw op):
//
//   CASE A (clean, hand-computable): Rotate=0, Pivot=0, Scale=0 (-> scale = (1-0)*u + 1... no:
//     scale = (1 - Scale)*u + 1 = (1-0)*u + 1 = u+1? NO — re-read .cs:50:
//        scale = (Vector3.One - Vector3(Scale)) * u + Vector3.One
//     With Scale=0: scale = (1-0)*u + 1 = u + 1 per axis -> NOT constant. The task wants a
//     CONSTANT scale=1 so position is purely translation; that needs Scale=1:
//        scale = (1-1)*u + 1 = 0*u + 1 = 1 (constant). USE Scale=1.
//     Translate=(1,0,0), StartPosition=0, Phase=0, Count=4, AddSeparator=false ->
//        translation = (1,0,0)*u, u=i+1 -> Position[i] = ((i+1),0,0).
//        scale=1 -> F1 = |1,1,1|/sqrt(3) + 0 = sqrt(3)/sqrt(3) = 1.
//
//   CASE B (rotation): Rotate=(0, 0, 90) deg/step (roll=Rotate.Z about Z), Translate=(1,0,0),
//     Scale=1, Pivot=0, Phase=0, Count=4, AddSeparator=false. For point i, u=i+1, the rotation
//     is YawPitchRoll(0,0, 90°/360*2pi*u) = roll of (90*u) degrees about Z. Position is
//     translation only (the (0,0,0,1) point is rotated about the origin -> still origin, then
//     translated): Position[i] = ((i+1),0,0). But the ORIENTATION quat must encode the Z-roll.
//     We assert Orientation.z/.w match a pure-Z quaternion of angle 90°*u (proves YawPitchRoll
//     uses Rotate.Z as roll-about-Z, and the per-step *u multiply).
//
//   CASE C (separator): Count=3, AddSeparator=true -> bag = 4, last point Scale.x == NaN; the
//     first 3 are the normal series. (Warm-up cook settles g_repAddSeparator before asserting.)
//
//   injectBug: forces the shader-side bug by re-running CASE A but asserting the WRONG formula
//     (u = i instead of i+1). The real shader uses i+1 so Position[0] = (1,0,0); asserting it
//     equals (0,0,0) -> FAIL. (A real logic flip, not an inverted assert: it checks the i-vs-i+1
//     off-by-one that the .cs:44 u = i+1+offset pins down.)
int runRepetitionPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device*      dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue*  q = dev->newCommandQueue();
  NS::Error*        err = nullptr;
  MTL::Library*     lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-repetitionpoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerRepetitionPointsOp();
  std::vector<SwPoint> captured;
  g_capRep = &captured;
  registerDrawOp("DrawPoints", captureDrawRep);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  // ---- helper: build a 1-node RepetitionPoints graph + DrawPoints capture ----
  auto cookCase = [&](PointGraph& pg, Node gen) -> void {
    Graph g;
    gen.id = 1; gen.type = "RepetitionPoints";
    g.nodes.push_back(gen);
    Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
    g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
    pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));
  };

  bool pass = true;

  // ===== CASE A: clean translation, scale constant 1 =====
  {
    PointGraph pg(dev, lib, q, 64, 64);
    Node gen;
    gen.params["Count"]         = 4.0f;
    gen.params["Translate.x"]   = 1.0f;
    gen.params["Translate.y"]   = 0.0f;
    gen.params["Translate.z"]   = 0.0f;
    gen.params["Scale"]         = 1.0f;   // -> scale constant (1,1,1)
    gen.params["Rotate.x"]      = 0.0f;
    gen.params["Rotate.y"]      = 0.0f;
    gen.params["Rotate.z"]      = 0.0f;
    gen.params["Pivot.x"]       = 0.0f;
    gen.params["Phase"]         = 0.0f;
    gen.params["StartW"]        = 0.0f;
    gen.params["AddSeparator"]  = 0.0f;
    cookCase(pg, gen);

    bool countOk = (captured.size() == 4);
    bool posOk = countOk, f1Ok = countOk;
    for (uint32_t i = 0; i < captured.size(); ++i) {
      float ex = injectBug ? (float)i : (float)(i + 1);  // bug asserts u=i -> RED (real is i+1)
      const SwPoint& p = captured[i];
      if (std::fabs(p.Position.x - ex) > 1e-3f) posOk = false;
      if (std::fabs(p.Position.y - 0.0f) > 1e-3f) posOk = false;
      if (std::fabs(p.Position.z - 0.0f) > 1e-3f) posOk = false;
      if (std::fabs(p.FX1 - 1.0f) > 1e-3f) f1Ok = false;   // scale=1 -> F1 = sqrt(3)/sqrt(3) = 1
    }
    printf("[selftest-repetitionpoints] A count=%zu pos=%s f1=%s\n",
           captured.size(), posOk ? "ok" : "NO", f1Ok ? "ok" : "NO");
    pass = pass && countOk && posOk && f1Ok;
  }

  // ===== CASE B: Z-roll rotation -> Orientation quaternion =====
  {
    PointGraph pg(dev, lib, q, 64, 64);
    Node gen;
    gen.params["Count"]         = 4.0f;
    gen.params["Translate.x"]   = 1.0f;
    gen.params["Scale"]         = 1.0f;
    gen.params["Rotate.z"]      = 90.0f;  // roll about Z, 90°/step
    gen.params["AddSeparator"]  = 0.0f;
    cookCase(pg, gen);

    bool countOk = (captured.size() == 4);
    bool quatOk = countOk, posOk = countOk;
    for (uint32_t i = 0; i < captured.size(); ++i) {
      float u = (float)(i + 1);
      float angle = 90.0f / 360.0f * (2.0f * (float)M_PI) * u;  // roll radians
      // pure-Z roll quaternion: (0,0, sin(a/2), cos(a/2)).
      float ez = std::sin(angle * 0.5f), ew = std::cos(angle * 0.5f);
      const SwPoint& p = captured[i];
      // quaternion sign can be globally flipped (q and -q same rotation); compare up to sign.
      float s = (p.Rotation.w * ew + p.Rotation.z * ez) < 0.0f ? -1.0f : 1.0f;
      if (std::fabs(s * p.Rotation.z - ez) > 2e-3f) quatOk = false;
      if (std::fabs(s * p.Rotation.w - ew) > 2e-3f) quatOk = false;
      if (std::fabs(p.Rotation.x) > 2e-3f) quatOk = false;
      if (std::fabs(p.Rotation.y) > 2e-3f) quatOk = false;
      // position still pure translation (rotation about origin leaves origin fixed)
      if (std::fabs(p.Position.x - u) > 1e-3f) posOk = false;
    }
    printf("[selftest-repetitionpoints] B count=%zu quat=%s pos=%s\n",
           captured.size(), quatOk ? "ok" : "NO", posOk ? "ok" : "NO");
    pass = pass && countOk && quatOk && posOk;
  }

  // ===== CASE C: AddSeparator -> bag = Count+1, last point Scale.x = NaN =====
  {
    PointGraph pg(dev, lib, q, 64, 64);
    Node gen;
    gen.params["Count"]        = 3.0f;
    gen.params["Translate.x"]  = 1.0f;
    gen.params["Scale"]        = 1.0f;
    gen.params["AddSeparator"] = 1.0f;
    cookCase(pg, gen);  // warm-up: settles g_repAddSeparator so the next cook sizes to Count+1
    cookCase(pg, gen);  // assert pass

    bool sizeOk = (captured.size() == 4);  // 3 real + 1 separator
    bool sepOk = sizeOk && std::isnan(captured[3].Scale.x);
    // the 3 real points must NOT be NaN and follow the series
    bool realOk = sizeOk;
    for (uint32_t i = 0; i < 3 && sizeOk; ++i) {
      if (std::isnan(captured[i].Scale.x)) realOk = false;
      if (std::fabs(captured[i].Position.x - (float)(i + 1)) > 1e-3f) realOk = false;
    }
    printf("[selftest-repetitionpoints] C size=%zu sep=%s real=%s\n",
           captured.size(), sepOk ? "ok" : "NO", realOk ? "ok" : "NO");
    pass = pass && sizeOk && sepOk && realOk;
  }

  printf("[selftest-repetitionpoints] -> %s\n", pass ? "PASS" : "FAIL");

  g_capRep = nullptr;
  g_repAddSeparator = 0;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
