// runtime/t3import_brdflookup_golden — --selftest-t3-brdflookup (TEXTURE-COMPUTE SEAM 首證, stage 1).
//
// The FIRST texture-OUT compute node through the generic tex-compute seam (TEXTURE_COMPUTE_SEAM_SPEC.md).
// Imports the REAL _ComputeBRDFLookup.t3 (guid 08d526d0…), which the importer COLLAPSES onto ONE tex-
// track ComputeShaderStageTex atom (t3_import_texcompute.cpp), cooks it through the PRODUCTION path
// (buildEvalGraph → cookResident), reads the RGBA16_UNorm texture back, and compares to the split-sum
// BRDF oracle. Four gates, each a MEASURED RED→GREEN tooth (GOLDEN_STANDARD.md 三特徵):
//
//  ① TAKEOVER (import FOLD): import → zero "unmapped SymbolId" warnings + compound atomic==false +
//     a ComputeShaderStageTex child (the framework glue Texture2d/UavFromTexture2d/CalcInt2Dispatch/
//     ExecuteTextureUpdate all folded away). injectBug = t3TexComputeCollapseDisable() → the collapse is
//     off → the glue ops become "unmapped skipped" + the stage imports as the buffer ComputeShaderStage,
//     not the tex atom → BITE. (The load-bearing "UavFromTexture2d fold" is exactly what the collapse does.)
//  ② PARITY (cook-driven): buildEvalGraph → cookResident → pg.target() (the redirected 512² texture) →
//     readback, compare a sparse interior probe grid to mathv_ref::brdfLookupTexel. injectBug =
//     computeShaderStageTexSkipDispatch() → the UAV dispatch is dropped → black texture → readback
//     diverges from the oracle → BITE ("UAV 未綁 → 黑貼圖", TEXTURE_COMPUTE_SEAM_SPEC §4.3.1).
//  ③ REFERENCE reachability + cookability: findSpec(guid) → spec with boundary I/O + non-empty
//     buildEvalGraph. injectBug = setDynamicSpecs({}) → findSpec null → BITE.
//  ④ LAYOUT: .t3ui Size-input / stage-child / output pins land on their Position constants; injectBug =
//     t3LayoutDisable() → every position reverts to 0,0 → BITE.
//
// A single injectBug bool drives all four. did-not-trip → return 0 (GOLDEN_STANDARD 特徵3 / P1).
// The parity oracle is the TiXL HLSL (via mathv_ref_brdflookup.h), NEVER sw's own output (P5-safe).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"
#include "runtime/point_graph.h"
#include "runtime/resident_eval_graph.h"
#include "runtime/t3_import.h"
#include "runtime/tixl_point.h"
#include "mathv_ref_brdflookup.h"

namespace sw {

void registerBuiltinPointOps();
bool& computeShaderStageTexSkipDispatch();  // point_ops_computeshaderstagetex.cpp (②parity injectBug)

namespace {

static const char* kT3 =
#include "runtime/brdflookup_t3_embed.inc"
;
static const char* kT3ui =
#include "runtime/brdflookup_t3ui_embed.inc"
;

const char* const kGuid = "08d526d0-d5e5-4fc9-a039-98189721d2b8";
const char* const kSizeInputId = "e22057e4-1aae-4698-b7f6-120dde027a5d";  // boundary Size (Int2, 512×512)
const char* const kOutputId = "21e0ee79-8e98-45aa-86e9-194ca6d70989";     // boundary BRDF output
const char* const kStageChildGuid = "1ae5cbfa-5887-4059-a134-10749e67ec3e";  // ComputeShaderStage child (.t3)

// .t3ui Position constants (verbatim from _ComputeBRDFLookup.t3ui).
constexpr float kSizeX = -420.40683f, kSizeY = 162.29959f;
constexpr float kStageX = 350.31635f, kStageY = 39.86669f;
constexpr float kOutX = 693.7523f, kOutY = 44.14441f;
constexpr float kLayoutEps = 0.01f;
bool nearf(float a, float b, float e = kLayoutEps) { return std::fabs(a - b) < e; }

// Parity: sparse INTERIOR probe grid over the 512² LUT (gx>0 avoids the grazing column that amplifies
// fast-math via 1/cosLo). eps in 16-bit UNorm LSB (measured; a body-formula error moves the split-sum by
// O(0.1)=thousands of LSB, so this is far tighter than the seam it guards yet absorbs fast-math).
constexpr uint32_t kW = 512, kH = 512;
constexpr int kParityEpsLsb = 3;  // MEASURED interior max ~0.5 LSB on the 512² probe grid; 3× fast-math margin

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}
void countPorts(const NodeSpec& s, int& nIn, int& nOut) {
  nIn = nOut = 0; for (const PortSpec& p : s.ports) (p.isInput ? nIn : nOut)++;
}
bool warningsHaveUnmapped(const std::vector<std::string>& w) {
  for (const std::string& m : w) if (m.find("unmapped SymbolId") != std::string::npos) return true;
  return false;
}

// ── ②parity: cook the imported compound + compare the readback to the oracle. Returns 0 GREEN / 1 RED. ──
int runBrdfParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  SymbolLibrary lib;
  std::string rootId; std::vector<std::string> warnings;
  if (!importT3Symbol(kT3, lib, &rootId, &warnings) || rootId != std::string(kGuid)) {
    printf("[brdf-seam] ②parity FAIL: import bad\n"); pool->release(); return 1;
  }
  const Symbol* sym = lib.find(rootId);
  const int stageId = sym ? childIdOfType(*sym, "ComputeShaderStageTex") : 0;
  if (!stageId) { printf("[brdf-seam] ②parity FAIL: no ComputeShaderStageTex child\n"); pool->release(); return 1; }

  ResidentEvalGraph g = buildEvalGraph(lib, rootId);
  initResidentCache(g);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[brdf-seam] ②parity FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }

  computeShaderStageTexSkipDispatch() = injectBug;  // -bug: drop the UAV dispatch → black texture
  PointGraph pg(dev, mlib, q, 64, 64);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cookResident(g, ctx, nullptr, std::to_string(stageId));
  MTL::Texture* tex = pg.target();
  const bool haveOut = tex && (uint32_t)tex->width() == kW && (uint32_t)tex->height() == kH;

  double maxErrLsb = 0.0, maxRefMag = 0.0;
  if (haveOut) {
    std::vector<uint16_t> px((size_t)kW * kH * 4, 0);
    tex->getBytes(px.data(), (NS::UInteger)kW * 4 * sizeof(uint16_t), MTL::Region::Make2D(0, 0, kW, kH), 0);
    for (uint32_t gy = 64; gy < kH; gy += 64) {
      for (uint32_t gx = 64; gx < kW; gx += 64) {  // interior grid (skips gx=0 grazing column)
        float ref[4];
        mathv_ref::brdfLookupTexel(gx, gy, kW, kH, ref[0], ref[1], ref[2], ref[3]);
        const size_t i = ((size_t)gy * kW + gx) * 4;
        for (int ch = 0; ch < 4; ++ch) {
          const float gpu = (float)px[i + ch] / 65535.0f;
          maxErrLsb = std::max(maxErrLsb, std::fabs((double)gpu - (double)ref[ch]) * 65535.0);
          maxRefMag = std::max(maxRefMag, (double)std::fabs(ref[ch]));
        }
      }
    }
  }
  computeShaderStageTexSkipDispatch() = false;
  mlib->release(); q->release(); dev->release();

  const bool nonTrivial = maxRefMag > 0.1;  // the DFG1 interior reaches ~0.9 → black is a real divergence
  const bool green = haveOut && nonTrivial && (maxErrLsb <= (double)kParityEpsLsb);
  printf("[brdf-seam] ②parity: haveOut=%d maxErr=%.3f LSB (eps=%d) refMax=%.4f -> %s\n",
         haveOut ? 1 : 0, maxErrLsb, kParityEpsLsb, maxRefMag, green ? "GREEN" : "RED");
  pool->release();
  // 0 = parity holds; 1 = diverges. The combined runner reads this: no-bug wants 0, -bug (black) wants 1.
  return green ? 0 : 1;
}

// ── ④layout: import with the .t3ui + assert the three pins land on their Position constants. ──
int runBrdfLayout(bool injectBug) {
  std::string id;
  if (!symbolIdOfT3(kT3, &id) || id != std::string(kGuid)) { printf("[brdf-seam] ④layout FAIL: id\n"); return 1; }
  const T3LayoutResolver layoutResolve = [&id](const std::string& guid, std::string& out) -> bool {
    if (guid != id) return false; out = kT3ui; return true; };
  t3LayoutDisable() = injectBug;
  SymbolLibrary lib; std::string rootId; std::vector<std::string> warnings;
  const bool ok = importT3Symbol(kT3, lib, &rootId, &warnings, T3Resolver{}, layoutResolve);
  t3LayoutDisable() = false;
  if (!ok || rootId != std::string(kGuid)) { printf("[brdf-seam] ④layout FAIL: import\n"); return 1; }
  const Symbol* s = lib.find(rootId);
  if (!s) { printf("[brdf-seam] ④layout FAIL: no root\n"); return 1; }

  const SlotDef* sz = nullptr; for (const SlotDef& d : s->inputDefs) if (d.id == kSizeInputId) sz = &d;
  const SlotDef* out = nullptr; for (const SlotDef& d : s->outputDefs) if (d.id == kOutputId) out = &d;
  const SymbolChild* stage = nullptr;
  for (const SymbolChild& c : s->children) if (c.symbolId == "ComputeShaderStageTex") stage = &c;
  if (!sz || !out || !stage) { printf("[brdf-seam] ④layout FAIL: sz=%p out=%p stage=%p\n", (void*)sz, (void*)out, (void*)stage); return 1; }

  const bool szOk = injectBug ? (nearf(sz->x, 0) && nearf(sz->y, 0)) : (nearf(sz->x, kSizeX) && nearf(sz->y, kSizeY));
  const bool stOk = injectBug ? (nearf(stage->x, 0) && nearf(stage->y, 0)) : (nearf(stage->x, kStageX) && nearf(stage->y, kStageY));
  const bool outOk = injectBug ? (nearf(out->x, 0) && nearf(out->y, 0)) : (nearf(out->x, kOutX) && nearf(out->y, kOutY));
  printf("[brdf-seam] ④layout: size(%.2f,%.2f) stage(%.2f,%.2f) out(%.2f,%.2f)\n",
         sz->x, sz->y, stage->x, stage->y, out->x, out->y);
  const bool nonZero = !(nearf(sz->x, 0) && nearf(sz->y, 0));
  const bool distinct = !(nearf(sz->x, stage->x) && nearf(sz->y, stage->y)) &&
                        !(nearf(stage->x, out->x) && nearf(stage->y, out->y));
  if (!injectBug) {
    if (!(nonZero && distinct)) { printf("[brdf-seam] ④layout NO-BITE: seam not exercised\n"); return 0; }
    return (szOk && stOk && outOk) ? 0 : 1;
  }
  return (szOk && stOk && outOk) ? 1 : 0;  // -bug: all reverted to 0,0 → BITES
}

}  // namespace

int runT3BrdfLookupGates(bool injectBug) {
  registerBuiltinPointOps();

  // ── ①takeover: the collapse folds the framework subgraph onto ONE ComputeShaderStageTex atom. ──
  t3TexComputeCollapseDisable() = injectBug;  // -bug: collapse off → glue "unmapped skipped" + buffer stage
  SymbolLibrary lib; std::string rootId; std::vector<std::string> warnings;
  const bool imported = importT3Symbol(kT3, lib, &rootId, &warnings) && rootId == std::string(kGuid);
  t3TexComputeCollapseDisable() = false;
  refreshCompoundSpecs(lib);
  const Symbol* csym = lib.find(kGuid);
  const bool hasTexStage = csym && childIdOfType(*csym, "ComputeShaderStageTex") != 0;
  const bool noUnmapped = !warningsHaveUnmapped(warnings);
  const bool g1green = imported && csym && !csym->atomic && !csym->children.empty() && hasTexStage && noUnmapped;
  const bool g1bit = imported && (!hasTexStage || !noUnmapped);  // -bug: no tex stage + unmapped warnings
  printf("[brdf-seam] ①takeover: atomic=%d children=%d texStage=%d noUnmapped=%d -> %s\n",
         csym ? (int)csym->atomic : -1, csym ? (int)csym->children.size() : -1, hasTexStage ? 1 : 0,
         noUnmapped ? 1 : 0, injectBug ? (g1bit ? "BITES" : "TOOTHLESS") : (g1green ? "GREEN" : "RED"));

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
  printf("[brdf-seam] ③reference: in=%d out=%d nodes=%zu -> %s\n", g3in, g3out, g3nodes,
         injectBug ? (g3bit ? "BITES" : "TOOTHLESS") : (g3green ? "GREEN" : "RED"));

  // ── ②parity (cook-driven readback). ──
  const int g2 = runBrdfParity(injectBug);
  const bool g2green = (g2 == 0);
  const bool g2bit = !g2green;
  printf("[brdf-seam] ②parity -> %s\n", injectBug ? (g2bit ? "BITES" : "TOOTHLESS") : (g2green ? "GREEN" : "RED"));

  // ── ④layout. ──
  const int g4 = runBrdfLayout(injectBug);
  const bool g4green = (g4 == 0), g4bit = (g4 != 0);
  printf("[brdf-seam] ④layout -> %s\n", injectBug ? (g4bit ? "BITES" : "TOOTHLESS") : (g4green ? "GREEN" : "RED"));

  setDynamicSpecs({});

  if (!injectBug) {
    const bool green = g1green && g3green && g2green && g4green;
    printf("[brdf-seam] VERDICT: %s (①%d ③%d ②%d ④%d)\n",
           green ? "PASS (texture-compute seam LIVE)" : "FAIL", g1green, g3green, g2green, g4green);
    return green ? 0 : 1;
  }
  const bool allBit = g1bit && g3bit && g2bit && g4bit;
  printf("[brdf-seam] -bug VERDICT: %s (①%d ③%d ②%d ④%d)\n",
         allBit ? "ALL TEETH BITE" : "DEAD TOOTH (NO-BITE)", g1bit, g3bit, g2bit, g4bit);
  return allBit ? 1 : 0;
}

}  // namespace sw
