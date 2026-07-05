// runtime/point_ops_getposition_golden — GetPosition transform-scope read golden.
//
// EXPECTED VALUES (independent-of-impl, hand-derived from TiXL GetPosition.cs:39-51 + GraphicsMath LookAtRH
// — NOT from sw's matrix helpers, P5 guard):
//   matrix by Space (cs:39-45): World→ObjectToWorld(=Identity, sw fork); Camera→WorldToCamera; Clip→
//     CameraToClipSpace·WorldToCamera. p=(Offset,1)·M, take xyz, NO perspective divide (cs:51).
//   LookAtRH(eye=(0,0,d), target=0, up=(0,1,0)) ⇒ world (px,py,pz)·WorldToCamera = (px, py, pz−d)
//     (GraphicsMath LookAtRH; the SAME view formula GetScreenPos's golden cites).
//
//   Tooth A (WORLDSPACE, no camera): Space=0, Offset=(0.8,0.6,0.5). ObjectToWorld=Identity → Position =
//     the raw offset (0.8, 0.6, 0.5). Off every axis, non-identity offset (P2 guard: the body IS exercised
//     — a dropped read of any component shows). This leg has NO camera and is therefore -bug-INSENSITIVE
//     (identity is already the WorldSpace matrix); it pins the pure offset path.
//   Tooth B (CAMERASPACE, LIVE Camera scope, cook-through): Camera Position=(0,0,5) wraps GetPosition
//     Space=1, Offset=(1,2,0) ⇒ Position = (1, 2, 0−5) = (1, 2, −5) (view formula; NO divide).
//   Both teeth read the result through the VALUE RAIL (cookStatefulValueOp out[1..3] = the Position.x/y/z
//   latch step) after cooking through BOTH legs — flat and resident must land the identical latch.
//
// injectBug (real cook seam, GOLDEN_STANDARD polarity): getPositionForceIdentityForTest() forces the Space
// matrix to Identity inside the REAL cook — so CameraSpace (Tooth B) reads the raw offset (1,2,0) instead of
// (1,2,−5) → RED. Tooth A (WorldSpace) is identity either way, so B is the biting leg. did-not-trip → 0.
#include "runtime/point_ops_getposition.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                  // Graph/Node/pinId
#include "runtime/graph_bridge.h"           // libFromGraph
#include "runtime/point_graph.h"            // PointGraph::cook/cookResident, registerBuiltinPointOps
#include "runtime/resident_eval_graph.h"    // buildEvalGraph
#include "runtime/stateful_value_ops.h"     // cookStatefulValueOp/StatefulValueState (value-rail read)
#include "runtime/tixl_point.h"             // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// GetPosition(1) [→ Camera(2) when withCamera] → RenderTarget. The Command wire is what makes the transform
// read RUN (TiXL: UpdateCommand evaluated inside the render graph, where the scope is live).
Graph buildGraph(bool withCamera) {
  Graph g;
  Node gp; gp.id = 1; gp.type = "GetPosition";
  if (withCamera) {
    gp.params["Space"] = 1.0f;  // CameraSpace
    gp.params["PositionOffset.x"] = 1.0f; gp.params["PositionOffset.y"] = 2.0f;
    gp.params["PositionOffset.z"] = 0.0f;
  } else {
    gp.params["Space"] = 0.0f;  // WorldSpace
    gp.params["PositionOffset.x"] = 0.8f; gp.params["PositionOffset.y"] = 0.6f;
    gp.params["PositionOffset.z"] = 0.5f;
  }
  g.nodes.push_back(gp);
  int rtId = 2;
  if (withCamera) {
    Node cam; cam.id = 2; cam.type = "Camera";
    cam.params["Position.x"] = 0.0f; cam.params["Position.y"] = 0.0f; cam.params["Position.z"] = 5.0f;
    g.nodes.push_back(cam);
    g.connections.push_back({100, pinId(1, 0), pinId(2, 0)});  // GetPosition.UpdateCommand → Camera.command
    rtId = 3;
  }
  Node rt; rt.id = rtId; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = 64.0f; rt.params["CustomH"] = 64.0f;
  g.nodes.push_back(rt);
  if (withCamera)
    g.connections.push_back({101, pinId(2, 1), pinId(rtId, 0)});  // Camera.out → RT.command
  else
    g.connections.push_back({101, pinId(1, 0), pinId(rtId, 0)});  // UpdateCommand → RT.command
  return g;
}

// Cook one leg (the latch is written during the Command cook), then read the VALUE RAIL: the
// stateful-value step copies the latch onto out[1..3] (= Position.x/y/z spec port indices).
void cookAndReadLatch(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, const Graph& g,
                      bool resident, const char* rtPath, float out3[3]) {
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  if (resident) {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    PointGraph pg(dev, lib, q, 64, 64);
    pg.cookResident(rg, ctx, nullptr, rtPath);
  } else {
    PointGraph pg(dev, lib, q, 64, 64);
    pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));
  }
  StatefulValueState st;
  float out[8] = {0};
  cookStatefulValueOp("GetPosition", std::map<std::string, float>{}, 1.0f / 60.0f, 0.0f, st, out);
  out3[0] = out[1]; out3[1] = out[2]; out3[2] = out[3];
}

bool near1(float a, double b) { return std::fabs((double)a - b) < 1e-4; }

}  // namespace

int runGetPositionSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-getposition] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();

  // Hand-derived expectations (header formulas; NO sw helper). Tooth A = raw offset (WorldSpace, Identity
  // O2W). Tooth B = view transform (px,py,pz−5) of (1,2,0) = (1,2,−5).
  const double eA[3] = {0.8, 0.6, 0.5};
  const double eB[3] = {1.0, 2.0, -5.0};

  getPositionForceIdentityForTest() = injectBug;  // -bug: force the Space matrix to Identity in the REAL cook
  Graph gA = buildGraph(/*withCamera=*/false);
  float flatA[3], resA[3];
  cookAndReadLatch(dev, lib, q, gA, /*resident=*/false, "2", flatA);
  cookAndReadLatch(dev, lib, q, gA, /*resident=*/true, "2", resA);
  Graph gB = buildGraph(/*withCamera=*/true);
  float flatB[3], resB[3];
  cookAndReadLatch(dev, lib, q, gB, /*resident=*/false, "3", flatB);
  cookAndReadLatch(dev, lib, q, gB, /*resident=*/true, "3", resB);
  getPositionForceIdentityForTest() = false;

  bool okA = near1(flatA[0], eA[0]) && near1(flatA[1], eA[1]) && near1(flatA[2], eA[2]) &&
             near1(resA[0], eA[0]) && near1(resA[1], eA[1]) && near1(resA[2], eA[2]);
  bool okB = near1(flatB[0], eB[0]) && near1(flatB[1], eB[1]) && near1(flatB[2], eB[2]) &&
             near1(resB[0], eB[0]) && near1(resB[1], eB[1]) && near1(resB[2], eB[2]);
  // flat/resident mirror: the two legs must land the identical latch.
  bool legs = flatA[0] == resA[0] && flatA[1] == resA[1] && flatA[2] == resA[2] &&
              flatB[0] == resB[0] && flatB[1] == resB[1] && flatB[2] == resB[2];
  bool ok = okA && okB && legs;

  std::printf("[selftest-getposition] A(World) res=(%.5f,%.5f,%.5f) want (%.2f,%.2f,%.2f) | B(Camera) "
              "res=(%.5f,%.5f,%.5f) want (%.2f,%.2f,%.2f) legs=%d -> %s\n", resA[0], resA[1], resA[2],
              eA[0], eA[1], eA[2], resB[0], resB[1], resB[2], eB[0], eB[1], eB[2], legs,
              ok ? "PASS" : "tripped");

  lib->release(); q->release(); dev->release(); pool->release();
  // Polarity (GOLDEN_STANDARD): -bug bit → ok=false → 1; did-not-trip → 0 (NO-BITE catches it).
  return ok ? 0 : 1;
}

}  // namespace sw
