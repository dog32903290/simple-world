// @tixl: DoyleSpiralPoints2   (census authority key — filename stem 'doylespiralpoints' forks the TiXL op id)
// DoyleSpiralPoints2 — point GENERATOR op (generate family): a Doyle circle-packing spiral.
//
// In TiXL this is a COMPOUND operator (.t3 graph). The graph derives the Doyle-spiral algebra
// on the CPU (the _DoyleSpiralRoot Newton-Raphson root finder) and feeds the derived A/B/R plus
// the high-level inputs to a GPU kernel. We reproduce that split here: the cook runs the same
// Newton-Raphson (mirroring _DoyleSpiralRoot.cs), then dispatches the kernel (a verbatim port of
// DoyleSpiralPoints.hlsl) which only consumes the derived values. "cheap-input != trivial-impl".
//
// Authority:
//   external/tixl/Operators/Lib/point/generate/DoyleSpiralPoints2.cs   (InputSlots)
//   external/tixl/Operators/Lib/point/generate/DoyleSpiralPoints2.t3   (compound wiring)
//   external/tixl/Operators/Lib/point/generate/_DoyleSpiralRoot.cs     (Newton-Raphson, ported here)
//   external/tixl/Operators/Lib/Assets/shaders/points/generate/DoyleSpiralPoints.hlsl (kernel)
//
// .t3 wiring resolved (see params header for the full table):
//   P  = clamp(PointsPerStep, 1, 100)        Q = SpiralSteepness
//   (A,B,R) = _DoyleSpiralRoot(P, Q)          Count(buffer) = clamp(Steps, 1, 10000000)
//   cbuffer Scale<-Scale, Offset<-Offset, Bias<-WBias, Bias2<-ScaleBias,
//           CutOff<-CenterPositionScale, CutOff2<-CenterSizeScale.
//
// Self-contained leaf: own capture vector + registerDrawOp for the golden.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"               // calcDispatchCount
#include "runtime/graph.h"                  // Graph/Node/pinId
#include "runtime/point_graph.h"            // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tixl_point.h"             // SwPoint (64B) + EvaluationContext
#include "runtime/doylespiralpoints_params.h"  // DoyleSpiralParams, DoyleSpiralBinding

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// --- _DoyleSpiralRoot.cs Newton-Raphson, ported 1:1 (double precision like System.Math) ------
// The square of the distance between z*e^(it) and (z*e^(it))^(p/q).
double dsq_d(double z, double t, double p, double q) {
  double w = std::pow(z, p / q);
  double s = (p * t + 2.0 * M_PI) / q;
  return std::pow(z * std::cos(t) - w * std::cos(s), 2) +
         std::pow(z * std::sin(t) - w * std::sin(s), 2);
}
double dsq_ddz_d(double z, double t, double p, double q) {
  double w = std::pow(z, p / q);
  double s = (p * t + 2.0 * M_PI) / q;
  double ddz_w = (p / q) * std::pow(z, (p - q) / q);
  return 2.0 * (w * std::cos(s) - z * std::cos(t)) * (ddz_w * std::cos(s) - std::cos(t)) +
         2.0 * (w * std::sin(s) - z * std::sin(t)) * (ddz_w * std::sin(s) - std::sin(t));
}
double dsq_ddt_d(double z, double t, double p, double q) {
  double w = std::pow(z, p / q);
  double s = (p * t + 2.0 * M_PI) / q;
  double dds_t = (p / q);
  return 2.0 * (z * std::cos(t) - w * std::cos(s)) * (-z * std::sin(t) + w * std::sin(s) * dds_t) +
         2.0 * (z * std::sin(t) - w * std::sin(s)) * (z * std::cos(t) - w * std::cos(s) * dds_t);
}
double dsq_s(double z, double t, double p, double q) {
  (void)t;
  return std::pow(z + std::pow(z, p / q), 2);
}
double dsq_ddz_s(double z, double t, double p, double q) {
  (void)t;
  double w = std::pow(z, p / q);
  double ddz_w = (p / q) * std::pow(z, (p - q) / q);
  return 2.0 * (w + z) * (ddz_w + 1.0);
}
double dsq_r(double z, double t, double p, double q) {
  return dsq_d(z, t, p, q) / dsq_s(z, t, p, q);
}
double dsq_ddz_r(double z, double t, double p, double q) {
  return (dsq_ddz_d(z, t, p, q) * dsq_s(z, t, p, q) - dsq_d(z, t, p, q) * dsq_ddz_s(z, t, p, q)) /
         std::pow(dsq_s(z, t, p, q), 2);
}
double dsq_ddt_r(double z, double t, double p, double q) {
  return (dsq_ddt_d(z, t, p, q) * dsq_s(z, t, p, q)) / std::pow(dsq_s(z, t, p, q), 2);
}

struct DoyleAbr { float aMag, aAng, bMag, bAng, r; bool ok; };

// _DoyleSpiralRoot.FindRootAngles(p, q): 2D Newton-Raphson joint root of _f and _g.
DoyleAbr findRootAngles(double p, double q) {
  const double epsilon = 1e-7;
  auto _f    = [&](double z, double t) { return dsq_r(z, t, 0, 1) - dsq_r(z, t, p, q); };
  auto ddz_f = [&](double z, double t) { return dsq_ddz_r(z, t, 0, 1) - dsq_ddz_r(z, t, p, q); };
  auto ddt_f = [&](double z, double t) { return dsq_ddt_r(z, t, 0, 1) - dsq_ddt_r(z, t, p, q); };
  auto _g    = [&](double z, double t) {
    return dsq_r(z, t, 0, 1) - dsq_r(std::pow(z, p / q), (p * t + 2.0 * M_PI) / q, 0, 1);
  };
  auto ddz_g = [&](double z, double t) {
    return dsq_ddz_r(z, t, 0, 1) -
           dsq_ddz_r(std::pow(z, p / q), (p * t + 2.0 * M_PI) / q, 0, 1) * (p / q) *
               std::pow(z, (p - q) / q);
  };
  auto ddt_g = [&](double z, double t) {
    return dsq_ddt_r(z, t, 0, 1) -
           dsq_ddt_r(std::pow(z, p / q), (p * t + 2.0 * M_PI) / q, 0, 1) * (p / q);
  };

  double z = 2.0, t = 0.0;
  bool ok = false;
  double rootR = 0.0;
  for (int loopIndex = 0; loopIndex < 100; ++loopIndex) {
    double v_f = _f(z, t);
    double v_g = _g(z, t);
    if (-epsilon < v_f && v_f < epsilon && -epsilon < v_g && v_g < epsilon) {
      ok = true;
      rootR = std::sqrt(dsq_r(z, t, 0, 1));
      break;
    }
    double a = ddz_f(z, t);
    double b = ddt_f(z, t);
    double c = ddz_g(z, t);
    double d = ddt_g(z, t);
    double det = a * d - b * c;
    if (-epsilon < det && det < epsilon) return {0, 0, 0, 0, 0, false};
    z -= (d * v_f - b * v_g) / det;
    t -= (a * v_g - c * v_f) / det;
    if (z < epsilon) return {0, 0, 0, 0, 0, false};
  }
  if (!ok) return {0, 0, 0, 0, 0, false};

  double w = std::pow(z, p / q);
  double s = (p * t + 2.0 * M_PI) / q;
  return {(float)z, (float)t, (float)w, (float)s, (float)rootR, true};
}

void cookDoyleSpiralPoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  MTL::Function* fn = c.lib->newFunction(
      NS::String::string("doylespiralpoints", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  DoyleSpiralParams P{};
  // P = clamp(PointsPerStep, 1, 100); Q = SpiralSteepness (.t3 clamp + IntToFloat chain).
  float pps = cookParam(c, "PointsPerStep", 8.0f);
  P.P = std::fmin(100.0f, std::fmax(1.0f, std::lround(pps)));
  P.Q = std::lround(cookParam(c, "SpiralSteepness", 31.0f));
  P.Count = (float)c.count;  // buffer element count -> kernel's GetDimensions(count)

  // Newton-Raphson derive A/B/R (CPU, mirrors _DoyleSpiralRoot.cs Update).
  DoyleAbr abr = findRootAngles((double)P.P, (double)P.Q);
  P.AMag = abr.aMag; P.AAng = abr.aAng; P.BMag = abr.bMag; P.BAng = abr.bAng; P.R = abr.r;

  // Straight-through inputs (.t3 routes these verbatim into the cbuffer).
  P.W       = cookParam(c, "W", 1.0f);
  P.Scale   = cookParam(c, "Scale", 1.0f);
  P.Offset  = cookParam(c, "Offset", 100.0f);
  P.Bias    = cookParam(c, "WBias", 1.0f);           // cbuffer.Bias  <- WBias
  P.Bias2   = cookParam(c, "ScaleBias", 1.0f);       // cbuffer.Bias2 <- ScaleBias
  P.CutOff  = cookParam(c, "CenterPositionScale", 0.0f);  // cbuffer.CutOff  <- CenterPositionScale
  P.CutOff2 = cookParam(c, "CenterSizeScale", 0.0f);      // cbuffer.CutOff2 <- CenterSizeScale

  float center[3] = {0.0f, 0.0f, 0.0f};
  float axis[3]   = {0.0f, 0.0f, 1.0f};
  cookVecN(c, "Center", center, 3, center);
  cookVecN(c, "OrientationAxis", axis, 3, axis);
  P.CenterX = center[0]; P.CenterY = center[1]; P.CenterZ = center[2];
  P.OrientAxisX = axis[0]; P.OrientAxisY = axis[1]; P.OrientAxisZ = axis[2];
  P.OrientAngle = cookParam(c, "OrientationAngle", 0.0f);

  MTL::CommandBuffer*         cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(c.output, 0, DOYLE_Points);
  enc->setBytes(&P, sizeof(P), DOYLE_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained) ---
std::vector<SwPoint>* g_capDoyle = nullptr;
void captureDrawDoyle(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capDoyle || !pts || c.count == 0) return;
  g_capDoyle->assign(c.count, SwPoint{});
  std::memcpy(g_capDoyle->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerDoyleSpiralPointsOp() {
  registerPointOp("DoyleSpiralPoints", cookDoyleSpiralPoints);
}

// Golden: DoyleSpiralPoints(Count=200, PointsPerStep=8, SpiralSteepness=31, defaults) -> capture.
//
// A Doyle spiral packs circles of EXPONENTIALLY growing size along logarithmic spiral arms. The
// kernel: ang = AAng*i + BAng*j, mag = pow(AMag^i * BMag^j, Bias2)*Scale + CutOff, pos=(cos*mag,
// sin*mag,0). With AMag>1, BMag>1 (the solved Doyle ratios), |pos| grows with the ring index i.
//
// TEETH:
//   (1) count == 200 (buffer capacity law).
//   (2) Logarithmic growth: the spiral spans a WIDE radius range — the max point radius is much
//       larger than the min (outer rings are exponentially bigger). We require maxR/minR > 3.
//   (3) Per-point W (FX1) is strictly positive and grows with radius (W = (radius*W+CutOff2)^Bias,
//       Bias=1, CutOff2=0 -> W == radius*100*Wport > 0). We require all FX1 > 0 and a wide spread.
//   (4) Rotation quaternion norm ~ 1 for all points (qMul of two unit quats).
//
// injectBug: ScaleBias(=Bias2) = 0 -> pow(.., 0) == 1 for every point -> mag == Scale+CutOff is
//   CONSTANT -> every point lands on a circle of the SAME radius -> the growth check (2) FAILS
//   (maxR/minR ~= 1, not > 3). A real degeneracy: the spiral collapses to a ring.
int runDoyleSpiralPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t TOTAL = 200;

  MTL::Device*       dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue*   q = dev->newCommandQueue();
  NS::Error*         err = nullptr;
  MTL::Library*      lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-doylespiral] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerDoyleSpiralPointsOp();
  std::vector<SwPoint> captured;
  g_capDoyle = &captured;
  registerDrawOp("DrawPoints", captureDrawDoyle);

  Graph g;
  Node gen; gen.id = 1; gen.type = "DoyleSpiralPoints";
  gen.params["Count"]               = (float)TOTAL;
  gen.params["PointsPerStep"]       = 8.0f;
  gen.params["SpiralSteepness"]     = 31.0f;
  gen.params["Offset"]              = 100.0f;
  gen.params["Scale"]               = 1.0f;
  gen.params["ScaleBias"]           = injectBug ? 0.0f : 1.0f;  // Bias2 -> growth on/off
  gen.params["CenterPositionScale"] = 0.0f;   // CutOff
  gen.params["W"]                   = 1.0f;
  gen.params["WBias"]               = 1.0f;   // Bias
  gen.params["CenterSizeScale"]     = 0.0f;   // CutOff2
  gen.params["Center.x"]            = 0.0f;
  gen.params["Center.y"]            = 0.0f;
  gen.params["Center.z"]            = 0.0f;
  gen.params["OrientationAxis.x"]   = 0.0f;
  gen.params["OrientationAxis.y"]   = 0.0f;
  gen.params["OrientationAxis.z"]   = 1.0f;
  gen.params["OrientationAngle"]    = 0.0f;
  g.nodes.push_back(gen);

  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});

  PointGraph pg(dev, lib, q, 64, 64);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  // (1) count
  bool countOK = (captured.size() == TOTAL);

  // (2) logarithmic growth: max radius >> min radius
  float minR = 1e30f, maxR = 0.0f;
  bool wAllPos = countOK;
  float minW = 1e30f, maxW = 0.0f;
  bool quatOK = countOK;
  if (countOK) {
    for (const SwPoint& p : captured) {
      float r = std::sqrt(p.Position.x * p.Position.x + p.Position.y * p.Position.y);
      if (r < minR) minR = r;
      if (r > maxR) maxR = r;
      float w = p.FX1;
      if (w <= 0.0f) wAllPos = false;
      if (w < minW) minW = w;
      if (w > maxW) maxW = w;
      float qn = p.Rotation.x*p.Rotation.x + p.Rotation.y*p.Rotation.y +
                 p.Rotation.z*p.Rotation.z + p.Rotation.w*p.Rotation.w;
      if (std::fabs(qn - 1.0f) > 0.01f) quatOK = false;
    }
  }
  float growth = (minR > 1e-6f) ? (maxR / minR) : (maxR > 1e-6f ? 1e30f : 1.0f);
  bool growthOK = countOK && (growth > 3.0f);

  // (3) W positive + wide spread (outer rings have bigger W). Skip wSpread on bug path where
  //     radius is uniform — but wAllPos must still hold.
  float wSpread = (minW > 1e-6f) ? (maxW / minW) : (maxW > 1e-6f ? 1e30f : 1.0f);
  bool wSpreadOK = countOK && wAllPos && (wSpread > 3.0f);

  // On bug path (Bias2=0): growth collapses (~1), wSpread collapses (~1) -> both fail -> overall FAIL.
  bool pass = countOK && growthOK && wAllPos && wSpreadOK && quatOK;
  printf("[selftest-doylespiral] n=%zu(need %u) growth=%.2f(need>3) wAllPos=%s wSpread=%.2f(need>3) quatNormOK=%s -> %s\n",
         captured.size(), TOTAL, growth, wAllPos ? "yes" : "NO", wSpread,
         quatOK ? "yes" : "NO", pass ? "PASS" : "FAIL");

  g_capDoyle = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
