// runtime/t3import_depthtolinear_golden — --selftest-t3-depthtolinear (TEXTURE-COMPUTE SEAM STAGE 2).
//
// The FIRST SRV-tex-read tex-out compute node through the generic tex-compute seam
// (TEXTURE_COMPUTE_SEAM_SPEC.md §5 row 2). Imports the REAL _ComputeDepthToLinear.t3 (guid ade1d03d…),
// which the importer COLLAPSES onto ONE tex-track ComputeShaderStageTex atom (t3_import_texcompute.cpp
// collapseTextureComputeStageSrv) — folding away SrvFromTexture2d / UavFromTexture2d / FloatsToBuffer /
// GetTextureSize / CalcInt2DispatchCount / SamplerState + the IntToFloat/BoolToFloat/Vector2Components CB
// value ops. Four gates, each a MEASURED RED→GREEN tooth (GOLDEN_STANDARD.md 三特徵):
//
//  ① TAKEOVER (import FOLD): import → zero "unmapped SymbolId" + compound atomic==false + a
//     ComputeShaderStageTex child + the SRV wire (boundary DepthBuffer → stage.ShaderResourceTextures) +
//     the b0 CB baked onto CB0..CB5 == the hand-derived .t3 boundary defaults [Near,Far,OutMin,OutMax,
//     Clamp,Mode] = [0.1,1000,0,0,0,0] (the INDEPENDENT oracle for the fold's CB extraction — a wrong
//     baked value reddens here, so ②parity is free to reuse the baked CB). injectBug =
//     t3TexComputeCollapseDisable() → collapse off → the SRV/UAV/size glue become "unmapped skipped" +
//     the stage imports as the BUFFER ComputeShaderStage → BITE.
//  ② PARITY (cook-driven readback): inject a t3xf_depth_source fixture (a known R32Float depth gradient)
//     into the imported compound, repoint the SRV wire at it, buildEvalGraph → cookResident → pg.target()
//     → readback the R32Float output → compare each texel to mathv_ref::depthToLinearTexel (the CPU
//     oracle, using the FOLD-baked CB). injectBug = computeShaderStageTexSkipDispatch() → dispatch dropped
//     → black → the linearized band diverges from the oracle → BITE.
//  ③ REFERENCE reachability + cookability: findSpec(guid) → spec with boundary I/O + non-empty
//     buildEvalGraph. injectBug = setDynamicSpecs({}) → findSpec null → BITE.
//  ④ LAYOUT: .t3ui Position constants (stage child / DepthBuffer input / Output pin). injectBug =
//     t3LayoutDisable() → every position reverts to 0,0 → BITE.
//
// A single injectBug bool drives all four. did-not-trip → return 0 (GOLDEN_STANDARD 特徵3 / P1). The
// parity oracle is the TiXL HLSL (via mathv_ref_depthtolinear.h), NEVER sw's own output (P5-safe).
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
#include "runtime/eval_context.h"              // EvaluationContext (full def for the cook ctx)
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp (t3xf_depth_source fixture)
#include "runtime/point_graph.h"               // TexCookCtx, PointGraph
#include "runtime/resident_eval_graph.h"
#include "runtime/t3_import.h"

namespace sw {

void registerBuiltinPointOps();
bool& computeShaderStageTexSkipDispatch();  // point_ops_computeshaderstagetex.cpp (②parity injectBug)

namespace {

static const char* kT3 =
#include "runtime/depthtolinear_t3_embed.inc"
;
static const char* kT3ui =
#include "runtime/depthtolinear_t3ui_embed.inc"
;

const char* const kGuid = "ade1d03d-db80-41ad-bcfa-8a2b900e9d41";
const char* const kDepthBufferInputId = "de65c36d-10a7-446f-a4dd-e55ce42f4203";  // boundary DepthBuffer (Texture2D)
const char* const kOutputId = "eff29dae-87c5-43a4-856b-51ac3abf567a";            // boundary Output

// .t3ui Position constants (verbatim from _ComputeDepthToLinear.t3ui).
constexpr float kStageX = 523.5577f, kStageY = 31.872314f;      // ComputeShaderStage child
constexpr float kDepthInX = 243.55765f, kDepthInY = 136.87231f; // DepthBuffer boundary input
constexpr float kOutX = 523.5577f, kOutY = 241.87231f;          // Output pin
constexpr float kLayoutEps = 0.01f;
bool nearf(float a, float b, float e = kLayoutEps) { return std::fabs(a - b) < e; }

// Hand-derived .t3 boundary DEFAULTS in FloatsToBuffer.Params wire order == the cbuffer layout
// {Near,Far,OutrangeMin,OutrangeMax,ClampRange,Mode}. Independent oracle for the fold's CB baking.
constexpr float kCB[6] = {0.1f, 1000.0f, 0.0f, 0.0f, 0.0f, 0.0f};

// t3xf_depth_source FIXTURE (②parity): a known R32Float depth gradient spanning [-0.15,0.85] so the left
// edge is depth<0 (checker branch) and the rest is the linearized branch. Redirects the golden-built
// texture (like ComputeShaderStageTex / UseTextureReference redirect). g_depthSrc set before cookResident.
constexpr uint32_t kW = 48, kH = 32;
inline float depthAt(uint32_t gx) { return -0.15f + 1.0f * (kW > 1 ? (float)gx / (float)(kW - 1) : 0.0f); }
MTL::Texture* g_depthSrc = nullptr;
void cookDepthSourceFixture(TexCookCtx& c) { c.redirectTexture = g_depthSrc; }
NodeSpec depthSourceFixtureSpec() {
  NodeSpec s;
  s.type = "t3xf_depth_source";
  s.title = "t3xf_depth_source";
  s.ports = {{"out", "out", "Texture2D", false}};
  s.evaluate = nullptr;
  return s;
}
const ImageFilterOp _reg_depthsourcefixture{depthSourceFixtureSpec(), "t3xf_depth_source",
                                            cookDepthSourceFixture};

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}
const SymbolChild* childOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return &c;
  return nullptr;
}
void countPorts(const NodeSpec& s, int& nIn, int& nOut) {
  nIn = nOut = 0; for (const PortSpec& p : s.ports) (p.isInput ? nIn : nOut)++;
}
bool warningsHaveUnmapped(const std::vector<std::string>& w) {
  for (const std::string& m : w) if (m.find("unmapped SymbolId") != std::string::npos) return true;
  return false;
}

// Parity: relative eps (division class, no transcendentals — MEASURED interior max ~2e-5). Checker band
// (depth<0, exact 0/1 coord-parity) uses an exact-ish absolute eps.
constexpr double kParityRelEps = 1e-3;
constexpr double kParityCheckerEps = 1e-4;

// ── ②parity: inject the fixture, cook the imported compound, compare readback to the oracle. ──
int runDepthParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  SymbolLibrary lib;
  std::string rootId; std::vector<std::string> warnings;
  if (!importT3Symbol(kT3, lib, &rootId, &warnings) || rootId != std::string(kGuid)) {
    printf("[depth-seam] ②parity FAIL: import bad\n"); pool->release(); return 1;
  }
  Symbol* sym = lib.find(rootId);
  const int stageId = sym ? childIdOfType(*sym, "ComputeShaderStageTex") : 0;
  if (!sym || !stageId) { printf("[depth-seam] ②parity FAIL: no ComputeShaderStageTex child\n"); pool->release(); return 1; }

  // Inject the fixture producer + repoint the SRV wire (boundary DepthBuffer → stage.ShaderResourceTextures)
  // at it (t3import_nestedcompound_golden.cpp:263-274 fixture pattern).
  const int fixId = sym->nextChildId++;
  { SymbolChild p; p.id = fixId; p.symbolId = "t3xf_depth_source"; sym->children.push_back(p); }
  if (!lib.symbols.count("t3xf_depth_source"))
    if (const NodeSpec* fs = findSpec("t3xf_depth_source")) lib.symbols["t3xf_depth_source"] = atomicSymbolFromSpec(*fs);
  int repointed = 0;
  for (SymbolConnection& c : sym->connections)
    if (c.dstChild == stageId && c.dstSlot == "ShaderResourceTextures" && c.srcChild == kSymbolBoundary) {
      c.srcChild = fixId; c.srcSlot = "out"; ++repointed;
    }
  if (repointed == 0) { printf("[depth-seam] ②parity FAIL: no SRV wire to repoint\n"); pool->release(); return 1; }

  ResidentEvalGraph g = buildEvalGraph(lib, rootId);
  initResidentCache(g);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[depth-seam] ②parity FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }

  // Build the known depth gradient the fixture redirects (SAME depthAt the oracle reads).
  MTL::TextureDescriptor* td = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatR32Float, kW, kH, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  g_depthSrc = dev->newTexture(td);
  std::vector<float> depth((size_t)kW * kH, 0.0f);
  for (uint32_t y = 0; y < kH; ++y) for (uint32_t x = 0; x < kW; ++x) depth[(size_t)y * kW + x] = depthAt(x);
  g_depthSrc->replaceRegion(MTL::Region::Make2D(0, 0, kW, kH), 0, depth.data(), (NS::UInteger)kW * sizeof(float));

  computeShaderStageTexSkipDispatch() = injectBug;  // -bug: drop the dispatch → black
  PointGraph pg(dev, mlib, q, 64, 64);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cookResident(g, ctx, nullptr, std::to_string(stageId));
  MTL::Texture* tex = pg.target();
  const bool haveOut = tex && (uint32_t)tex->width() == kW && (uint32_t)tex->height() == kH;

  double maxRel = 0.0, maxChecker = 0.0, maxRefMag = 0.0;
  if (haveOut) {
    std::vector<float> px((size_t)kW * kH, 0.0f);
    tex->getBytes(px.data(), (NS::UInteger)kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
    mathv_ref::DepthParams p{kCB[0], kCB[1], kCB[2], kCB[3], kCB[4], kCB[5]};
    for (uint32_t y = 0; y < kH; ++y) {
      for (uint32_t x = 0; x < kW; ++x) {
        const float d = depthAt(x);
        const float ref = mathv_ref::depthToLinearTexel(d, x, y, p);
        const float gpu = px[(size_t)y * kW + x];
        if (d < 0.0f) {
          maxChecker = std::max(maxChecker, (double)std::fabs(gpu - ref));
        } else {
          maxRefMag = std::max(maxRefMag, (double)std::fabs(ref));
          maxRel = std::max(maxRel, std::fabs((double)gpu - (double)ref) / std::max((double)std::fabs(ref), 1e-6));
        }
      }
    }
  }
  computeShaderStageTexSkipDispatch() = false;
  g_depthSrc->release(); g_depthSrc = nullptr;
  mlib->release(); q->release(); dev->release();

  const bool nonTrivial = maxRefMag > 0.1;
  const bool green = haveOut && nonTrivial && (maxRel <= kParityRelEps) && (maxChecker <= kParityCheckerEps);
  printf("[depth-seam] ②parity: haveOut=%d rel=%.6f(eps=%.0e) checker=%.6f refMax=%.3f -> %s\n",
         haveOut ? 1 : 0, maxRel, kParityRelEps, maxChecker, maxRefMag, green ? "GREEN" : "RED");
  pool->release();
  return green ? 0 : 1;  // no-bug wants 0; -bug (black) wants 1
}

// ── ④layout: import with the .t3ui + assert the three pins land on their Position constants. ──
int runDepthLayout(bool injectBug) {
  std::string id;
  if (!symbolIdOfT3(kT3, &id) || id != std::string(kGuid)) { printf("[depth-seam] ④layout FAIL: id\n"); return 1; }
  const T3LayoutResolver layoutResolve = [&id](const std::string& guid, std::string& out) -> bool {
    if (guid != id) return false; out = kT3ui; return true; };
  t3LayoutDisable() = injectBug;
  SymbolLibrary lib; std::string rootId; std::vector<std::string> warnings;
  const bool ok = importT3Symbol(kT3, lib, &rootId, &warnings, T3Resolver{}, layoutResolve);
  t3LayoutDisable() = false;
  if (!ok || rootId != std::string(kGuid)) { printf("[depth-seam] ④layout FAIL: import\n"); return 1; }
  const Symbol* s = lib.find(rootId);
  if (!s) { printf("[depth-seam] ④layout FAIL: no root\n"); return 1; }

  const SlotDef* din = nullptr; for (const SlotDef& d : s->inputDefs) if (d.id == kDepthBufferInputId) din = &d;
  const SlotDef* out = nullptr; for (const SlotDef& d : s->outputDefs) if (d.id == kOutputId) out = &d;
  const SymbolChild* stage = childOfType(*s, "ComputeShaderStageTex");
  if (!din || !out || !stage) { printf("[depth-seam] ④layout FAIL: din=%p out=%p stage=%p\n", (void*)din, (void*)out, (void*)stage); return 1; }

  const bool dOk = injectBug ? (nearf(din->x, 0) && nearf(din->y, 0)) : (nearf(din->x, kDepthInX) && nearf(din->y, kDepthInY));
  const bool stOk = injectBug ? (nearf(stage->x, 0) && nearf(stage->y, 0)) : (nearf(stage->x, kStageX) && nearf(stage->y, kStageY));
  const bool outOk = injectBug ? (nearf(out->x, 0) && nearf(out->y, 0)) : (nearf(out->x, kOutX) && nearf(out->y, kOutY));
  printf("[depth-seam] ④layout: depthIn(%.2f,%.2f) stage(%.2f,%.2f) out(%.2f,%.2f)\n",
         din->x, din->y, stage->x, stage->y, out->x, out->y);
  const bool nonZero = !(nearf(stage->x, 0) && nearf(stage->y, 0));
  const bool distinct = !(nearf(din->x, stage->x) && nearf(din->y, stage->y));
  if (!injectBug) {
    if (!(nonZero && distinct)) { printf("[depth-seam] ④layout NO-BITE: seam not exercised\n"); return 0; }
    return (dOk && stOk && outOk) ? 0 : 1;
  }
  return (dOk && stOk && outOk) ? 1 : 0;  // -bug: all reverted to 0,0 → BITES
}

}  // namespace

int runT3DepthToLinearGates(bool injectBug) {
  registerBuiltinPointOps();

  // ── ①takeover: the collapse folds the SRV-tex subgraph onto ONE ComputeShaderStageTex atom. ──
  t3TexComputeCollapseDisable() = injectBug;  // -bug: collapse off → glue "unmapped skipped" + buffer stage
  SymbolLibrary lib; std::string rootId; std::vector<std::string> warnings;
  const bool imported = importT3Symbol(kT3, lib, &rootId, &warnings) && rootId == std::string(kGuid);
  t3TexComputeCollapseDisable() = false;
  refreshCompoundSpecs(lib);
  const Symbol* csym = lib.find(kGuid);
  const SymbolChild* stage = csym ? childOfType(*csym, "ComputeShaderStageTex") : nullptr;
  const bool noUnmapped = !warningsHaveUnmapped(warnings);
  // SRV wire: boundary DepthBuffer → stage.ShaderResourceTextures.
  bool srvWired = false;
  bool cbOk = false;
  if (csym && stage) {
    for (const SymbolConnection& c : csym->connections)
      if (c.srcChild == kSymbolBoundary && c.srcSlot == kDepthBufferInputId &&
          c.dstChild == stage->id && c.dstSlot == "ShaderResourceTextures")
        srvWired = true;
    // b0 CB baked onto CB0..CB5 == the hand-derived .t3 defaults (INDEPENDENT oracle for the fold).
    cbOk = true;
    for (int i = 0; i < 6; ++i) {
      auto it = stage->overrides.find("CB" + std::to_string(i));
      const float v = (it != stage->overrides.end()) ? it->second : 0.0f;
      if (std::fabs(v - kCB[i]) > 1e-4f) cbOk = false;
    }
  }
  const bool g1green = imported && csym && !csym->atomic && stage && noUnmapped && srvWired && cbOk;
  const bool g1bit = imported && (!stage || !noUnmapped);  // -bug: no tex stage + unmapped warnings
  printf("[depth-seam] ①takeover: atomic=%d texStage=%d noUnmapped=%d srvWired=%d cbOk=%d -> %s\n",
         csym ? (int)csym->atomic : -1, stage ? 1 : 0, noUnmapped ? 1 : 0, srvWired ? 1 : 0, cbOk ? 1 : 0,
         injectBug ? (g1bit ? "BITES" : "TOOTHLESS") : (g1green ? "GREEN" : "RED"));

  // ── ③reference: findSpec(guid) → spec with boundary I/O + non-empty buildEvalGraph. ──
  const NodeSpec* g3spec = findSpec(kGuid);
  int g3in = 0, g3out = 0; size_t g3nodes = 0;
  if (g3spec) {
    countPorts(*g3spec, g3in, g3out);
    ResidentEvalGraph eg = buildEvalGraph(lib, g3spec->type);
    g3nodes = eg.nodes.size();
  }
  const bool g3green = g3spec && g3in > 0 && g3out > 0 && g3nodes > 0;
  bool g3bit = false;
  if (injectBug) {
    setDynamicSpecs({});
    g3bit = (findSpec(kGuid) == nullptr);
    refreshCompoundSpecs(lib);
  }
  printf("[depth-seam] ③reference: in=%d out=%d nodes=%zu -> %s\n", g3in, g3out, g3nodes,
         injectBug ? (g3bit ? "BITES" : "TOOTHLESS") : (g3green ? "GREEN" : "RED"));

  // ── ②parity (cook-driven readback). ──
  const int g2 = runDepthParity(injectBug);
  const bool g2green = (g2 == 0);
  const bool g2bit = !g2green;
  printf("[depth-seam] ②parity -> %s\n", injectBug ? (g2bit ? "BITES" : "TOOTHLESS") : (g2green ? "GREEN" : "RED"));

  // ── ④layout. ──
  const int g4 = runDepthLayout(injectBug);
  const bool g4green = (g4 == 0), g4bit = (g4 != 0);
  printf("[depth-seam] ④layout -> %s\n", injectBug ? (g4bit ? "BITES" : "TOOTHLESS") : (g4green ? "GREEN" : "RED"));

  setDynamicSpecs({});

  if (!injectBug) {
    const bool green = g1green && g3green && g2green && g4green;
    printf("[depth-seam] VERDICT: %s (①%d ③%d ②%d ④%d)\n",
           green ? "PASS (SRV-tex compute seam LIVE)" : "FAIL", g1green, g3green, g2green, g4green);
    return green ? 0 : 1;
  }
  const bool allBit = g1bit && g3bit && g2bit && g4bit;
  printf("[depth-seam] -bug VERDICT: %s (①%d ③%d ②%d ④%d)\n",
         allBit ? "ALL TEETH BITE" : "DEAD TOOTH (NO-BITE)", g1bit, g3bit, g2bit, g4bit);
  return allBit ? 1 : 0;
}

}  // namespace sw
