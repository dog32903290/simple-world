// runtime/t3import_depthtolinear_cblive_golden — --selftest-t3-depthtolinear-cblive (TEXTURE-COMPUTE
// SEAM STAGE 2, CB-LIVE refinement / TEXTURE_COMPUTE_SEAM_SPEC.md §5 row 2 "留白" now closed).
//
// The SRV-tex compute seam (t3import_depthtolinear_golden.cpp) sealed the FOLD + cook, but BAKED the b0
// CB at the .t3 defaults (fork computestagetex-cb-defaults-baked): a compound instance could not change
// Near/Far. collapseTextureComputeStageSrv now WIRES the direct-boundary scalars (Near→CB0, Far→CB1) LIVE
// (fork computestagetex-cb-live-boundary-wire); the value-op-mediated CB entries (OutRange/Clamp/Mode)
// stay baked. This gate proves the wire is LOAD-BEARING: SEEDING different Near/Far through
// buildEvalGraph's boundary injection MOVES the cooked output, tracking its OWN oracle.
//
// ONE gate, a MEASURED RED→GREEN tooth (GOLDEN_STANDARD.md 三特徵):
//  ⑤ LIVENESS: import _ComputeDepthToLinear.t3 → cook TWICE (seed Near/Far = the .t3 defaults A, then an
//     off-default B). Each readback must match its OWN CPU oracle (mathv_ref::depthToLinearTexel with
//     THAT Near/Far — the TiXL HLSL, P5-safe) AND A≠B (the seed genuinely drove the kernel → non-vacuous;
//     if Near/Far were dead, A==B and the tooth couldn't tell). injectBug = computeShaderStageTexForceBakeCb()
//     → the collapse BAKES the whole CB at the .t3 defaults → the B seed is inert → outB == outA → outB
//     diverges from oracleB → BITE. A real cook-path revert (wire→bake), not an assert flip.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "mathv_ref_depthtolinear.h"
#include "runtime/compound_graph.h"
#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp (the depth-source fixture)
#include "runtime/point_graph.h"               // TexCookCtx, PointGraph
#include "runtime/resident_eval_graph.h"
#include "runtime/t3_import.h"

namespace sw {

void registerBuiltinPointOps();
bool& computeShaderStageTexForceBakeCb();  // t3_import_texcompute.cpp (⑤liveness injectBug: force bake)

namespace {

static const char* kT3 =
#include "runtime/depthtolinear_t3_embed.inc"
;

const char* const kGuid = "ade1d03d-db80-41ad-bcfa-8a2b900e9d41";
const char* const kNearInputId = "a5f6347a-9c57-46f2-be39-80499b35cdf7";  // boundary Near → CB0 (live)
const char* const kFarInputId = "9f42b73c-d6f1-4907-ba55-9fb56514aa23";   // boundary Far  → CB1 (live)
// The value-op-mediated CB tail [OutrangeMin,OutrangeMax,ClampRange,Mode] = the .t3 defaults (still baked).
constexpr float kCbTail[4] = {0.0f, 0.0f, 0.0f, 0.0f};
constexpr double kRelEps = 1e-3;  // division class, no transcendentals (mirror of the parity gate)

// A known R32Float depth gradient spanning [-0.15,0.85] (left edge depth<0 → checker branch, rest
// linearized). The fixture redirects it, exactly like the parity gate (own copy — this TU is standalone).
constexpr uint32_t kW = 48, kH = 32;
inline float depthAt(uint32_t gx) { return -0.15f + 1.0f * (kW > 1 ? (float)gx / (float)(kW - 1) : 0.0f); }
MTL::Texture* g_depthSrc = nullptr;
void cookDepthSourceFixture(TexCookCtx& c) { c.redirectTexture = g_depthSrc; }
NodeSpec depthSourceFixtureSpec() {
  NodeSpec s;
  s.type = "t3xf_depth_source_cblive";
  s.title = "t3xf_depth_source_cblive";
  s.ports = {{"out", "out", "Texture2D", false}};
  s.evaluate = nullptr;
  return s;
}
const ImageFilterOp _reg_depthsourcecblive{depthSourceFixtureSpec(), "t3xf_depth_source_cblive",
                                           cookDepthSourceFixture};

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}

}  // namespace

int runT3DepthToLinearCbLiveGates(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();
  computeShaderStageTexForceBakeCb() = injectBug;  // -bug: bake the CB → the Near/Far seed is inert

  SymbolLibrary lib; std::string rootId; std::vector<std::string> warnings;
  const bool ok = importT3Symbol(kT3, lib, &rootId, &warnings) && rootId == std::string(kGuid);
  Symbol* sym = lib.find(rootId);
  const int stageId = sym ? childIdOfType(*sym, "ComputeShaderStageTex") : 0;
  if (!ok || !sym || !stageId) {
    printf("[depth-cblive] ⑤liveness FAIL: import/stage\n");
    computeShaderStageTexForceBakeCb() = false; pool->release(); return 1;
  }

  // Inject the fixture producer + repoint the SRV wire (boundary DepthBuffer → stage.ShaderResourceTextures).
  const int fixId = sym->nextChildId++;
  { SymbolChild p; p.id = fixId; p.symbolId = "t3xf_depth_source_cblive"; sym->children.push_back(p); }
  if (!lib.symbols.count("t3xf_depth_source_cblive"))
    if (const NodeSpec* fs = findSpec("t3xf_depth_source_cblive"))
      lib.symbols["t3xf_depth_source_cblive"] = atomicSymbolFromSpec(*fs);
  for (SymbolConnection& c : sym->connections)
    if (c.dstChild == stageId && c.dstSlot == "ShaderResourceTextures" && c.srcChild == kSymbolBoundary) {
      c.srcChild = fixId; c.srcSlot = "out";
    }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) {
    printf("[depth-cblive] ⑤liveness FAIL: no metallib\n");
    q->release(); dev->release(); computeShaderStageTexForceBakeCb() = false; pool->release(); return 1;
  }

  MTL::TextureDescriptor* td = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatR32Float, kW, kH, false);
  td->setUsage(MTL::TextureUsageShaderRead); td->setStorageMode(MTL::StorageModeShared);
  g_depthSrc = dev->newTexture(td);
  std::vector<float> depth((size_t)kW * kH, 0.0f);
  for (uint32_t y = 0; y < kH; ++y) for (uint32_t x = 0; x < kW; ++x) depth[(size_t)y * kW + x] = depthAt(x);
  g_depthSrc->replaceRegion(MTL::Region::Make2D(0, 0, kW, kH), 0, depth.data(), (NS::UInteger)kW * sizeof(float));

  PointGraph pg(dev, mlib, q, 64, 64);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  // Cook once seeding Near/Far via buildEvalGraph's boundary injection (= a real parent wiring producers
  // into Near/Far). Readback happens BEFORE the next cook overwrites the cache-keyed output (same cookKey).
  auto cookOnce = [&](float nearV, float farV, std::vector<float>& px) -> bool {
    ResidentEvalGraph g = buildEvalGraph(lib, rootId, {{kNearInputId, {nearV}}, {kFarInputId, {farV}}});
    initResidentCache(g);
    pg.cookResident(g, ctx, nullptr, std::to_string(stageId));
    MTL::Texture* tex = pg.target();
    if (!tex || (uint32_t)tex->width() != kW || (uint32_t)tex->height() != kH) return false;
    px.assign((size_t)kW * kH, 0.0f);
    tex->getBytes(px.data(), (NS::UInteger)kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
    return true;
  };

  const float nearA = 0.1f, farA = 1000.0f;  // the .t3 defaults
  const float nearB = 0.5f, farB = 250.0f;   // off-default → a clearly different linearized band
  std::vector<float> pxA, pxB;
  const bool okA = cookOnce(nearA, farA, pxA);
  const bool okB = cookOnce(nearB, farB, pxB);

  double maxRelA = 0.0, maxRelB = 0.0, maxAbDiff = 0.0;
  if (okA && okB) {
    mathv_ref::DepthParams pA{nearA, farA, kCbTail[0], kCbTail[1], kCbTail[2], kCbTail[3]};
    mathv_ref::DepthParams pB{nearB, farB, kCbTail[0], kCbTail[1], kCbTail[2], kCbTail[3]};
    for (uint32_t y = 0; y < kH; ++y)
      for (uint32_t x = 0; x < kW; ++x) {
        const float d = depthAt(x);
        if (d < 0.0f) continue;  // checker branch = Near/Far-independent; skip
        const size_t idx = (size_t)y * kW + x;
        const float refA = mathv_ref::depthToLinearTexel(d, x, y, pA);
        const float refB = mathv_ref::depthToLinearTexel(d, x, y, pB);
        maxRelA = std::max(maxRelA, std::fabs((double)pxA[idx] - refA) / std::max((double)std::fabs(refA), 1e-6));
        maxRelB = std::max(maxRelB, std::fabs((double)pxB[idx] - refB) / std::max((double)std::fabs(refB), 1e-6));
        maxAbDiff = std::max(maxAbDiff, (double)std::fabs(pxB[idx] - pxA[idx]));
      }
  }
  computeShaderStageTexForceBakeCb() = false;
  g_depthSrc->release(); g_depthSrc = nullptr;
  mlib->release(); q->release(); dev->release();

  // GREEN: both cooks match their OWN oracle AND the seeds genuinely moved the output (A≠B, non-vacuous).
  // Force-bake bug → A==B (maxAbDiff→0) AND outB misses oracleB (relB blows) → not green → return 1 (BITES).
  const bool matched = okA && okB && (maxRelA <= kRelEps) && (maxRelB <= kRelEps);
  const bool moved = maxAbDiff > 0.05;  // 0.1/1000 → 0.5/250 shifts the linearized band far past this
  const bool green = matched && moved;
  printf("[depth-cblive] ⑤liveness: okA=%d okB=%d relA=%.6f relB=%.6f A!=B=%.4f -> %s\n",
         okA ? 1 : 0, okB ? 1 : 0, maxRelA, maxRelB, maxAbDiff,
         injectBug ? (green ? "TOOTHLESS" : "BITES") : (green ? "GREEN" : "RED"));
  printf("[depth-cblive] VERDICT: %s\n",
         injectBug ? (green ? "DEAD TOOTH (NO-BITE)" : "TOOTH BITES")
                   : (green ? "PASS (CB Near/Far LIVE through the boundary)" : "FAIL"));
  pool->release();
  return green ? 0 : 1;  // no-bug wants 0; -bug (baked CB → outB frozen) wants 1
}

}  // namespace sw
