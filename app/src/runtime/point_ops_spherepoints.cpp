// SpherePoints — lane-A point-operator: cook fn + register + golden. Faithful port of
// external/tixl .../point/generate/SpherePoints (.cs ports, .hlsl math). A Fibonacci-sphere
// generator: writes a bag of SwPoints, every one at distance `Radius` from `Center`.
//
// Self-contained leaf (its own capture vector + registerDrawOp), mirroring
// point_ops_selftest.cpp's golden template. The cook reads scalar params via paramOr on the
// node being cooked (c.nodeId — generators get instanced, so first-of-type would feed the 2nd
// instance the 1st's params) and the Center vector via readVecN(*n,"Center",...).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"           // calcDispatchCount
#include "runtime/graph.h"             // Graph/Node/readVecN/pinId
#include "runtime/point_graph.h"       // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/spherepoints_params.h"  // SphereParams, SphereBinding
#include "runtime/tixl_point.h"        // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// SpherePoints generator: dispatch the spherepoints kernel into the node's output bag.
// Reads Radius/StartAngle/Scatter (scalar) + Center (Vec3 via cookVecN). Count comes from
// ctx.count (PointGraph sizes the bag). TiXL's orientation quaternion + Color are baked to
// defaults in the kernel (see spherepoints.metal header).
// NOTE: builds the PSO per cook — fine for the headless golden (one cook). The live loop
// (A1.5) must cache PSOs; flagged there, not here (same as cookRadialPoints).
void cookSpherePoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  MTL::Function* fn = c.lib->newFunction(NS::String::string("spherepoints", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  SphereParams P{};
  P.Count = c.count;
  P.Radius = cookParam(c, "Radius", 2.0f);
  P.StartAngle = cookParam(c, "StartAngle", 0.0f);
  P.Scatter = cookParam(c, "Scatter", 0.0f);
  float center[3] = {0.0f, 0.0f, 0.0f};
  cookVecN(c, "Center", center, 3, center);  // TiXL Center (Vector3), per-node
  P.CenterX = center[0]; P.CenterY = center[1]; P.CenterZ = center[2];

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(c.output, 0, SPHERE_Points);
  enc->setBytes(&P, sizeof(P), SPHERE_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capSphere = nullptr;
void captureDrawSphere(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capSphere || !pts || c.count == 0) return;
  g_capSphere->assign(c.count, SwPoint{});
  std::memcpy(g_capSphere->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerSpherePointsOp() {
  registerPointOp("SpherePoints", cookSpherePoints);
}

// Golden: cook SpherePoints with Center=(3,-1,2), assert the TiXL Fibonacci-sphere invariant —
// EVERY point sits at distance Radius from Center (because |unitPos|==1, so |pos-Center|==R) —
// AND the cloud actually spreads over the sphere (y spans ~[-R,+R], not a degenerate cap).
// injectBug: Radius=0 collapses every point onto Center, so |pos-Center|==0 != R -> the radius
// assertion FAILs (a REAL degeneracy, not a flipped assert).
int runSpherePointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 1024;
  const float R = 2.0f;                       // the sphere radius the assertion checks for
  const float geomRadius = injectBug ? 0.0f : R;  // bug: cook radius 0 -> points collapse onto
                                                  // Center, so |pos-Center|==0 != R -> FAILs
  const float CX = 3.0f, CY = -1.0f, CZ = 2.0f;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-spherepoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerSpherePointsOp();  // registers SpherePoints
  std::vector<SwPoint> captured;
  g_capSphere = &captured;
  registerDrawOp("DrawPoints", captureDrawSphere);  // capture-only draw for the assertion

  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "SpherePoints";
  gen.params["Count"] = (float)N;
  gen.params["Radius"] = geomRadius;
  gen.params["Center.x"] = CX;
  gen.params["Center.y"] = CY;
  gen.params["Center.z"] = CZ;
  g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // SpherePoints.points -> DrawPoints.points

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  // TEETH 1: every point at distance R from Center (the Fibonacci-sphere invariant).
  // TEETH 2: the cloud spreads — y-extent (relative to Center) covers most of [-R,+R].
  bool onSphere = captured.size() == N;
  float maxRadErr = 0.0f;
  float yMin = 1e9f, yMax = -1e9f;
  for (const SwPoint& p : captured) {
    float dx = p.Position.x - CX, dy = p.Position.y - CY, dz = p.Position.z - CZ;
    float r = std::sqrt(dx * dx + dy * dy + dz * dz);
    float e = std::fabs(r - R);
    if (e > maxRadErr) maxRadErr = e;
    onSphere = onSphere && e < 0.02f;
    if (dy < yMin) yMin = dy;
    if (dy > yMax) yMax = dy;
  }
  float ySpread = (captured.size() == N) ? (yMax - yMin) : 0.0f;
  bool spreadOK = ySpread > 0.5f * R + 1e-4f;  // a real sphere: y-extent ~ 2R, not a point
  bool pass = (captured.size() == N) && onSphere && spreadOK;
  printf("[selftest-spherepoints] n=%zu maxRadErr=%.4f(need<0.02,R=%.1f) ySpread=%.3f(need>%.3f) -> %s\n",
         captured.size(), maxRadErr, R, ySpread, 0.5f * R, pass ? "PASS" : "FAIL");

  g_capSphere = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
