// Headless RED->GREEN proof of the production-swap bridge (graph_bridge.h): the REAL default
// particle graph — REAL ops, real metallib, stateful GPU sim — mirrored through libFromGraph
// and cooked via cookResident must produce a BYTE-IDENTICAL target texture to the flat cook,
// across multiple frames (state persistence on both sides). The graph is extended with the
// two live-app value mechanisms the swap must carry:
//   • Const(3) --wire--> RadialPoints.Radius      (value wire into a Float param)
//   • AudioReaction.Level --wire--> ParticleSystem.Speed, with the per-frame cooker simulated
//     by writing outCache (flat) / extOut (resident mirror) each frame
// Plus direct probes: the resident AudioReaction extOut resolves through the wire, and the
// bridged Const resolves to 3. injectBug drops the bridged Turbulence wire -> the resident
// sim diverges -> the byte-compare FAILS (teeth).
#include "runtime/graph_bridge.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/point_graph.h"
#include "runtime/resident_eval_graph.h"
#include "runtime/tixl_point.h"  // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

int runGraphBridgeSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;
  const int FRAMES = 3;
  const float kRadius = 3.0f, kLevel = 0.6f;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-graphbridge] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();

  // The live document graph + the two value mechanisms (see header comment).
  Graph g = defaultParticleGraph();
  Node cn; cn.id = 9; cn.type = "Const"; cn.params["value"] = kRadius; g.nodes.push_back(cn);
  g.connections.push_back({104, pinId(9, 1), pinId(1, 2)});  // Const.out -> RadialPoints.Radius
  g.connections.push_back({105, pinId(8, 0), pinId(2, 3)});  // AudioReaction.Level -> ParticleSystem.Speed

  SymbolLibrary mlib = libFromGraph(g);
  if (injectBug) {  // drop the bridged Turbulence wire: resident sim loses turbulence
    auto& conns = mlib.symbols["Root"].connections;
    for (size_t i = 0; i < conns.size(); ++i)
      if (conns[i].srcChild == 6) { conns.erase(conns.begin() + i); break; }
  }
  ResidentEvalGraph rg = buildEvalGraph(mlib, "Root");

  // Direct probes: bridged Const value + the AudioReaction extOut mirror through the wire.
  ResidentEvalCtx rc0; rc0.frameIndex = 0; rc0.localTime = 0.0f; rc0.localFxTime = 0.0f;
  bool constOk = evalResidentFloat(rg, "9", "out", rc0) == kRadius;
  bool extOk = false;
  {
    auto it = rg.byPath.find("8");
    if (it != rg.byPath.end()) {
      rg.nodes[it->second].extOut[0] = kLevel;
      extOk = evalResidentFloat(rg, "8", "Level", rc0) == kLevel;
    }
  }

  PointGraph fpg(dev, lib, q, W, H);
  PointGraph rpg(dev, lib, q, W, H);
  const int term = 7;  // DrawPoints terminal (cmd flow through the RenderTarget executor)

  for (int i = 0; i < FRAMES; ++i) {
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)i; ctx.time = 0.05f * (float)i; ctx.deltaTime = 1.0f / 60.0f;
    // Simulate the app's per-frame AudioReaction cooker on BOTH sides.
    if (Node* ar = g.node(8)) ar->outCache[0] = kLevel;
    if (auto it = rg.byPath.find("8"); it != rg.byPath.end()) rg.nodes[it->second].extOut[0] = kLevel;
    fpg.cook(g, ctx, nullptr, term);
    rpg.cookResident(rg, ctx, nullptr, "7");
  }

  auto readback = [&](MTL::Texture* t, std::vector<uint8_t>& px) {
    px.assign((size_t)W * H * 4, 0);
    if (t) t->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  };
  std::vector<uint8_t> fpx, rpx;
  readback(fpg.target(), fpx);
  readback(rpg.target(), rpx);
  int nonBlack = 0;
  for (size_t i = 0; i < (size_t)W * H; ++i)
    if (fpx[i * 4] > 30 || fpx[i * 4 + 1] > 30 || fpx[i * 4 + 2] > 30) ++nonBlack;
  bool identical = fpx == rpx;
  bool pass = constOk && extOk && identical && nonBlack > 50;

  printf("[selftest-graphbridge] const(3)=%d extOut(0.6)=%d frames=%d identical=%d nonBlack=%d(need>50) -> %s\n",
         constOk ? 1 : 0, extOk ? 1 : 0, FRAMES, identical ? 1 : 0, nonBlack,
         pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
