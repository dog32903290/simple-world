// runtime/point_ops_getscreenpos_golden — GetScreenPos world→screen projection golden.
//
// EXPECTED VALUES (independent-of-impl, hand-derived from TiXL GetScreenPos.cs + GraphicsMath
// PerspectiveFovRH/LookAtRH formulas — NOT from sw's matrix helpers, P5 guard):
//   LookAtRH(eye=(0,0,d), target=0, up=(0,1,0)) ⇒ axes x=(1,0,0) y=(0,1,0) z=(0,0,1) ⇒
//     view = (px, py, pz − d)                                                (GraphicsMath LookAtRH)
//   PerspectiveFovRH(fov, aspect, zn, zf): yScale = 1/tan(fov/2), M33 = zf/(zn−zf),
//     M43 = zn·zf/(zn−zf), M34 = −1 ⇒ clip.w = −view.z = d − pz              (GraphicsMath.cs)
//   screenX = ndc.x·(M22/M11) = view.x·yScale/clip.w (the aspect CANCELS — exact for any output
//   aspect); screenY = view.y·yScale/clip.w; screenZ = SetDepthToZero ? 0 : (view.z·M33 + M43)/clip.w
//   (GetScreenPos.cs:22-44).
//
//   Tooth A (DEFAULT camera, no Camera op): eye z = DefaultCameraDistance = cot(22.5°)
//   (EvaluationContext.SetDefaultCamera; == yScale at fov 45°). Probe LocalPosition=(0.8, 0.6, 0.5),
//   SetDepthToZero=FALSE (the unauthored depth path — mid-frustum, off every axis):
//     w = d − 0.5;  sx = 0.8·d/w;  sy = 0.6·d/w;  sz = ((0.5−d)·M33 + M43)/w.
//   Tooth B (LIVE Camera scope, cook-through): Camera Position=(0,0,5) wraps GetScreenPos
//   LocalPosition=(1,1,0), SetDepthToZero default TRUE:
//     w = 5;  sx = sy = cot(22.5°)/5 = 0.4828427;  sz = 0 (cs:40-42).
//   Both teeth read the result through the VALUE RAIL (cookStatefulValueOp out[1..3] = the
//   Position.x/y/z latch step) after cooking the graph through BOTH legs — flat and resident must
//   land the identical latch.
//
// injectBug (real cook seam, GOLDEN_STANDARD polarity): getScreenPosDropDivideForTest() makes the
// REAL cook skip the perspective division (cs:29-33) — raw clip xyz flows — so every expected screen
// value diverges → RED. did-not-trip → return 0 (NO-BITE list).
#include "runtime/point_ops_getscreenpos.h"

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

// GetScreenPos(1) [→ Camera(2) when withCamera] → RenderTarget. The Command wire is what makes the
// projection RUN (TiXL: UpdateCommand evaluated inside the render graph).
Graph buildGraph(bool withCamera) {
  Graph g;
  Node gp; gp.id = 1; gp.type = "GetScreenPos";
  if (withCamera) {
    gp.params["LocalPosition.x"] = 1.0f; gp.params["LocalPosition.y"] = 1.0f;
    gp.params["LocalPosition.z"] = 0.0f;  // SetDepthToZero left default (true)
  } else {
    gp.params["LocalPosition.x"] = 0.8f; gp.params["LocalPosition.y"] = 0.6f;
    gp.params["LocalPosition.z"] = 0.5f;
    gp.params["SetDepthToZero"] = 0.0f;  // exercise the REAL depth path (cs:40-42 false branch)
  }
  g.nodes.push_back(gp);
  int rtId = 2;
  if (withCamera) {
    Node cam; cam.id = 2; cam.type = "Camera";
    cam.params["Position.x"] = 0.0f; cam.params["Position.y"] = 0.0f; cam.params["Position.z"] = 5.0f;
    g.nodes.push_back(cam);
    g.connections.push_back({100, pinId(1, 0), pinId(2, 0)});  // GetScreenPos.UpdateCommand → Camera.command
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
  cookStatefulValueOp("GetScreenPos", std::map<std::string, float>{}, 1.0f / 60.0f, 0.0f, st, out);
  out3[0] = out[1]; out3[1] = out[2]; out3[2] = out[3];
}

bool near1(float a, double b) { return std::fabs((double)a - b) < 1e-4; }

}  // namespace

int runGetScreenPosSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-getscreenpos] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();

  // Hand-derived expectations (header formulas; double precision, GraphicsMath-cited — no sw helper).
  const double d = 1.0 / std::tan(3.14159265358979323846 / 8.0);  // cot(22.5°): DefaultCameraDistance AND yScale@45°
  const double zn = 0.01, zf = 1000.0;
  const double m33 = zf / (zn - zf), m43 = zn * zf / (zn - zf);   // PerspectiveFovRH (GraphicsMath.cs)
  // Tooth A: p=(0.8,0.6,0.5), default camera, depth kept.
  const double wA = d - 0.5;
  const double eA[3] = {0.8 * d / wA, 0.6 * d / wA, ((0.5 - d) * m33 + m43) / wA};
  // Tooth B: p=(1,1,0), Camera at (0,0,5), depth zeroed (cs:40-42 true branch).
  const double eB[3] = {d / 5.0, d / 5.0, 0.0};

  getScreenPosDropDivideForTest() = injectBug;  // -bug: cs:29-33 division dropped inside the REAL cook
  Graph gA = buildGraph(/*withCamera=*/false);
  float flatA[3], resA[3];
  cookAndReadLatch(dev, lib, q, gA, /*resident=*/false, "2", flatA);
  cookAndReadLatch(dev, lib, q, gA, /*resident=*/true, "2", resA);
  Graph gB = buildGraph(/*withCamera=*/true);
  float flatB[3], resB[3];
  cookAndReadLatch(dev, lib, q, gB, /*resident=*/false, "3", flatB);
  cookAndReadLatch(dev, lib, q, gB, /*resident=*/true, "3", resB);
  getScreenPosDropDivideForTest() = false;

  bool okA = near1(flatA[0], eA[0]) && near1(flatA[1], eA[1]) && near1(flatA[2], eA[2]) &&
             near1(resA[0], eA[0]) && near1(resA[1], eA[1]) && near1(resA[2], eA[2]);
  bool okB = near1(flatB[0], eB[0]) && near1(flatB[1], eB[1]) && near1(flatB[2], eB[2]) &&
             near1(resB[0], eB[0]) && near1(resB[1], eB[1]) && near1(resB[2], eB[2]);
  // flat/resident mirror: the two legs must land the identical latch.
  bool legs = flatA[0] == resA[0] && flatA[1] == resA[1] && flatA[2] == resA[2] &&
              flatB[0] == resB[0] && flatB[1] == resB[1] && flatB[2] == resB[2];
  bool ok = okA && okB && legs;

  std::printf("[selftest-getscreenpos] A res=(%.5f,%.5f,%.5f) want (%.5f,%.5f,%.5f) | B res=(%.5f,%.5f,"
              "%.5f) want (%.5f,%.5f,%.5f) legs=%d -> %s\n", resA[0], resA[1], resA[2], eA[0], eA[1],
              eA[2], resB[0], resB[1], resB[2], eB[0], eB[1], eB[2], legs, ok ? "PASS" : "tripped");

  lib->release(); q->release(); dev->release(); pool->release();
  // Polarity (GOLDEN_STANDARD): -bug bit → ok=false → 1; did-not-trip → 0 (NO-BITE catches it).
  return ok ? 0 : 1;
}

}  // namespace sw
