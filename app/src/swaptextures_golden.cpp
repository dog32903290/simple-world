// swaptextures_golden — --selftest-swaptextures. The multi-tex-output golden (the SECOND feedback-seam
// leaf — STATELESS, no cross-frame pair). Proves the cook driver routes a node's TWO Texture2D outputs
// to DIFFERENT textures by output-port ordinal, on BOTH flat and resident (PRODUCTION) cooks (R-2).
//
// Scenario: RenderTarget A (red) → TextureAInput, RenderTarget B (green) → TextureBInput of a
// SwapTextures node.
//   EnableSwap = FALSE (.t3 default): TextureA out == A (red),  TextureB out == B (green) [PASSTHROUGH].
//   EnableSwap = TRUE              : TextureA out == B (green),  TextureB out == A (red)   [SWAPPED].
// We read both outputs via debugCookedFeedbackOutput (ordinal 0 = TextureA, 1 = TextureB).
//
// injectBug (swapTexturesInjectBug): forces the passthrough branch regardless of EnableSwap → the
// EnableSwap=TRUE assertion (expects SWAPPED) diverges → exit 1.
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"
#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"
#include "runtime/point_graph.h"
#include "runtime/resident_eval_graph.h"

namespace sw {

bool& swapTexturesInjectBug();  // point_ops_swaptextures.cpp

namespace {

bool readTexel00(MTL::Texture* tex, uint8_t out[4]) {
  if (!tex) return false;
  uint32_t w = (uint32_t)tex->width(), h = (uint32_t)tex->height();
  if (w == 0 || h == 0) return false;
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  tex->getBytes(px.data(), w * 4, MTL::Region::Make2D(0, 0, w, h), 0);
  out[0] = px[0]; out[1] = px[1]; out[2] = px[2]; out[3] = px[3];
  return true;
}
bool texelNear(const uint8_t got[4], const uint8_t want[4], int tol = 2) {
  for (int i = 0; i < 4; ++i) { int d = (int)got[i] - (int)want[i]; if (d < 0) d = -d; if (d > tol) return false; }
  return true;
}

const uint8_t kA[4] = {255, 0, 0, 255};  // red
const uint8_t kB[4] = {0, 255, 0, 255};  // green

// node 1 = RenderTarget A (red), node 2 = RenderTarget B (green), node 10 = SwapTextures.
// A.out (port 1) -> TextureAInput (port 0); B.out (port 1) -> TextureBInput (port 1).
void buildSwapGraph(Graph& g, bool enableSwap) {
  auto rt = [&](int id, float r, float gr, float b) {
    Node n; n.id = id; n.type = "RenderTarget";
    n.params["Resolution"] = 4.0f; n.params["CustomW"] = 4.0f; n.params["CustomH"] = 4.0f;
    n.params["ClearColor.x"] = r; n.params["ClearColor.y"] = gr; n.params["ClearColor.z"] = b;
    n.params["ClearColor.w"] = 1.0f;
    g.nodes.push_back(n);
  };
  rt(1, 1.0f, 0.0f, 0.0f);  // A = red
  rt(2, 0.0f, 1.0f, 0.0f);  // B = green
  Node sw; sw.id = 10; sw.type = "SwapTextures";
  sw.params["EnableSwap"] = enableSwap ? 1.0f : 0.0f;
  g.nodes.push_back(sw);
  g.connections.push_back({501, pinId(1, /*out*/ 1), pinId(10, /*TextureAInput*/ 0)});
  g.connections.push_back({502, pinId(2, /*out*/ 1), pinId(10, /*TextureBInput*/ 1)});
}

// Cook one config on flat, assert TextureA/TextureB outputs. Returns ok.
bool checkOne(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, bool enableSwap,
              bool injectBug, bool resident) {
  PointGraph pg(dev, lib, q, 64, 64);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  Graph g; buildSwapGraph(g, enableSwap);
  swapTexturesInjectBug() = injectBug;
  if (resident) {
    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    pg.cookResident(rg, ctx, nullptr, /*targetPath=*/"10");
  } else {
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/10);
  }
  swapTexturesInjectBug() = false;

  MTL::Texture* outA = pg.debugCookedFeedbackOutput(10, 0, resident);  // TextureA
  MTL::Texture* outB = pg.debugCookedFeedbackOutput(10, 1, resident);  // TextureB
  // Expectations are the TRUE routing for this EnableSwap (NOT adjusted for injectBug — the bug forces
  // passthrough, so for the SWAP case the actual output diverges from these expected swapped values →
  // the tooth bites). For the passthrough case (enableSwap=false) the bug is a no-op (already passthru).
  const uint8_t* wantA = enableSwap ? kB : kA;  // SWAPPED: A out = B
  const uint8_t* wantB = enableSwap ? kA : kB;  // SWAPPED: B out = A
  uint8_t gotA[4], gotB[4];
  bool ok = true;
  if (!readTexel00(outA, gotA) || !texelNear(gotA, wantA)) {
    std::printf("[selftest-swaptextures] %s swap=%d TextureA got=(%d,%d,%d,%d) want=(%d,%d,%d,%d) FAIL\n",
                resident ? "resident" : "flat", enableSwap ? 1 : 0,
                outA ? gotA[0] : -1, gotA[1], gotA[2], gotA[3], wantA[0], wantA[1], wantA[2], wantA[3]);
    ok = false;
  }
  if (!readTexel00(outB, gotB) || !texelNear(gotB, wantB)) {
    std::printf("[selftest-swaptextures] %s swap=%d TextureB got=(%d,%d,%d,%d) want=(%d,%d,%d,%d) FAIL\n",
                resident ? "resident" : "flat", enableSwap ? 1 : 0,
                outB ? gotB[0] : -1, gotB[1], gotB[2], gotB[3], wantB[0], wantB[1], wantB[2], wantB[3]);
    ok = false;
  }
  return ok;
}

}  // namespace

int runSwapTexturesSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-swaptextures] FAIL: no metallib\n");
    dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // RenderTarget (the input sources) is registered here
  MTL::CommandQueue* q = dev->newCommandQueue();

  bool ok = true;
  // PASSTHROUGH (EnableSwap=false) — flat + resident. Always asserted (the bug only flips the swap case).
  ok &= checkOne(dev, lib, q, /*enableSwap=*/false, injectBug, /*resident=*/false);
  // SWAP (EnableSwap=true) — flat + resident. injectBug forces passthrough → this assert bites.
  ok &= checkOne(dev, lib, q, /*enableSwap=*/true, injectBug, /*resident=*/false);
  if (!injectBug) {
    ok &= checkOne(dev, lib, q, /*enableSwap=*/false, false, /*resident=*/true);
    ok &= checkOne(dev, lib, q, /*enableSwap=*/true, false, /*resident=*/true);
  }

  if (!injectBug && ok)
    std::printf("[selftest-swaptextures] flat+resident passthrough+swap routing match\n");

  lib->release();
  q->release();
  dev->release();
  pool->release();
  std::printf("[selftest-swaptextures] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
