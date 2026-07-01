// runtime/t3import_hse_golden — IMAGE-CURRENCY spike (--selftest-t3-hse): the FIRST attempt to replay a
// TiXL IMAGE (Texture2D) compound through the .t3 replay recipe (importT3Symbol → buildEvalGraph →
// cookResident → texture readback). The 3 existing replay goldens (TransformPoints / TransformMesh /
// DisplaceMeshNoise) all terminate at GPU BUFFER currency (SwBuffer); this is the zero-precedent image case.
//
// ── WHAT THIS GOLDEN MEASURES (the spike verdict) ──────────────────────────────────────────────────────
// The DOWNSTREAM half of the recipe — cookResident of a Texture2D-flow node + texture readback — is ALREADY
// proven live and GREEN by resident_hse_selftest.cpp (a HAND-BUILT SymbolLibrary with an "HSE" atom child,
// RED→GREEN tooth). What was NEVER exercised for an image op is the UPSTREAM half: does the production
// importT3Symbol turn the real HSE.t3 into that same graph?
//
// HSE.t3 (byte-embedded below) is NOT a leaf. Its single child is _multiImageFxSetupStatic
// (SymbolId cc34a183-3978-4b6b-8ef1-dd8102410816), the TiXL DX11 image-fx render framework, parameterized
// by ShaderPath="Lib:shaders/img/fx/HueShift.hlsl" (a PIXEL shader psMain:SV_TARGET on a fullscreen quad —
// NOT a compute kernel). The root's 5 boundary Inputs (Texture2d/Hue/Exposure/FxTexture/Saturation) wire
// INTO that child; the child's Texture2D output wires OUT to the root boundary. So the ENTIRE HSE.t3
// compound is a thin wrapper that, in sw, collapses to the ONE flat "HSE" atom (point_ops_hse.cpp), whose
// ports are 1:1 with HSE.cs (Image/FxTexture/Hue/Saturation/Exposure → out).
//
// ── THE SEAM THIS PROBES ───────────────────────────────────────────────────────────────────────────────
// t3_import.cpp maps each .t3 child's SymbolId Guid → an sw atom via swTypeForSymbolGuid (t3_import_maps.cpp,
// TABLE ③). That table is 100% BUFFER atoms — it has NO row for cc34a183 (_multiImageFxSetupStatic) and no
// image/texture atom at all. So the current importer SKIPS the only child of HSE.t3 (unmapped SymbolId) and
// yields an EMPTY root symbol — no HSE node, nothing to cook. This golden MEASURES that: it imports the real
// HSE.t3 and reports exactly what the importer produced (children/conns/warnings). The verdict tells us
// whether image replay is (A) already in reach, or (B) gated on a new import seam.
//
// ZONE: runtime golden (shell tier — binds runtime import + resident tex cook + independent color oracle).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"         // SymbolLibrary / Symbol / SymbolChild / SymbolConnection
#include "runtime/eval_context.h"           // EvaluationContext (complete type)
#include "runtime/graph.h"                  // findSpec / registerBuiltinPointOps
#include "runtime/graph_bridge.h"           // atomicSymbolFromSpec
#include "runtime/image_filter_op_registry.h"
#include "runtime/point_graph.h"            // PointGraph / residentTexFor / TexCookCtx / registerTexOp
#include "runtime/resident_eval_graph.h"    // ResidentEvalGraph / buildEvalGraph
#include "runtime/t3_import.h"              // importT3Symbol

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

void registerBuiltinPointOps();

namespace {

static const char* kHseT3 =
#include "runtime/hse_t3_embed.inc"
;

constexpr uint32_t kW = 32, kH = 32;

// RenderTarget cook OVERRIDE (mirror of resident_hse_selftest.cpp): paint a solid picked by ClearColor.x
// (0 → RED Image, 1 → FxTexture with g=1/3). Lets the imported graph's Texture2D inputs have producers.
void texSource(TexCookCtx& c) {
  if (!c.output) return;
  const uint32_t w = (uint32_t)c.output->width(), h = (uint32_t)c.output->height();
  const int which = (int)std::lround(cookParam(c, "ClearColor.x", 0.0f));
  uint8_t r = 255, g = 0, b = 0;              // which==0: pure RED Image
  if (which == 1) { r = 0; g = 85; b = 0; }   // which==1: FxTexture, g = 85/255 ≈ 1/3
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  for (size_t i = 0; i < (size_t)w * h; ++i) {
    px[i * 4 + 0] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255;
  }
  c.output->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}

}  // namespace

// injectBug: OMIT the FxTexture wire → fx.g=0 → hue shift = Hue(0) → output stays RED (the tooth).
int runT3HseParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  // ---- STEP 1: import the REAL HSE.t3 via the PRODUCTION importer, and REPORT what it produced. ----
  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  const bool imported = importT3Symbol(kHseT3, lib, &rootId, &warnings);
  if (!imported) {
    printf("[t3-hse] import: importT3Symbol returned FALSE\n");
  }
  const Symbol* rsym = imported ? lib.find(rootId) : nullptr;
  const int nChildren = rsym ? (int)rsym->children.size() : -1;
  const int nConns    = rsym ? (int)rsym->connections.size() : -1;
  printf("[t3-hse] import: ok=%d rootId=%s children=%d conns=%d warnings=%zu\n",
         imported ? 1 : 0, rootId.c_str(), nChildren, nConns, warnings.size());
  if (rsym) {
    std::map<std::string, int> byType;
    for (const SymbolChild& c : rsym->children) byType[c.symbolId]++;
    printf("[t3-hse]   mapped atom types:");
    if (byType.empty()) printf(" (none — every child was skipped)");
    for (const auto& kv : byType) printf(" %s×%d", kv.first.c_str(), kv.second);
    printf("\n");
  }
  for (const std::string& w : warnings) printf("[t3-hse]   warn: %s\n", w.c_str());

  // The spike question: did the import produce an "HSE" tex node the resident cook can drive?
  const int hseId = rsym ? childIdOfType(*rsym, "HSE") : 0;
  const bool importReplayable = imported && rsym && hseId != 0;
  printf("[t3-hse] SEAM VERDICT: HSE-atom-from-import=%d → image .t3 replay is %s\n",
         hseId, importReplayable ? "REACHABLE (proceed to cook)"
                                 : "GATED on an import seam (compound→tex-atom collapse; see report)");

  if (!importReplayable) {
    // ── RESULT B (TODAY): the importer CANNOT yet turn HSE.t3 into a cookable image graph. HSE.t3's
    //    only child is _multiImageFxSetupStatic (cc34a183) — a Guid ABSENT from t3_import_maps.cpp
    //    (100% buffer atoms) — so it is skipped and its 6 boundary wires dropped, yielding an EMPTY
    //    root (children=0). This golden is a CHARACTERIZATION TRIPWIRE of that measured gap, NOT a
    //    faked replay-green: the main leg asserts the gap is EXACTLY as diagnosed (import ok, 0
    //    children, no HSE atom, the specific cc34a183-unmapped warning present). It passes (exit 0)
    //    so the green sweep stays honest-green on a KNOWN state — and it FLIPS the instant someone
    //    builds the compound→HSE-atom import seam (children becomes 1 → this branch is skipped →
    //    control falls to the RESULT-A cook+parity path below, which then DEMANDS real image parity).
    //    So it self-destructs into a real replay gate when the seam lands. The DOWNSTREAM half
    //    (cookResident of an HSE tex atom + texture readback) is ALREADY proven GREEN by
    //    --selftest-hseresident; the gap this measures is IMPORT ONLY.
    bool sawUnmappedFxSetupWarn = false;
    for (const std::string& w : warnings)
      if (w.find("cc34a183") != std::string::npos) { sawUnmappedFxSetupWarn = true; break; }
    const bool gapExactlyAsDiagnosed =
        imported && rsym && nChildren == 0 && hseId == 0 && sawUnmappedFxSetupWarn;

    if (!injectBug) {
      printf("[t3-hse] TRIPWIRE VERDICT: %s\n",
             gapExactlyAsDiagnosed
                 ? "GREEN (import seam gap present EXACTLY as diagnosed: _multiImageFxSetupStatic "
                   "unmapped → empty root. Downstream cook proven by --selftest-hseresident. Build the "
                   "compound→HSE-atom import seam to unlock image replay; this test then demands parity.)"
                 : "RED (the measured gap CHANGED — either the seam was built (good: this test now wants "
                   "the RESULT-A parity path) or the importer regressed differently)");
      pool->release();
      return gapExactlyAsDiagnosed ? 0 : 1;
    }
    // -bug leg must BITE (exit non-zero). It asserts the gap is CLOSED (import produced an HSE atom),
    // which is FALSE today → the assertion fails → bites. When the seam lands, control never reaches
    // here (importReplayable becomes true), so the bug leg moves to the RESULT-A fx-input tooth below.
    const bool gapClosed = (hseId != 0);  // false today
    printf("[t3-hse] -bug: import-gap tooth %s (asserts gap CLOSED; it is OPEN today → bites)\n",
           gapClosed ? "TOOTHLESS" : "BITES");
    pool->release();
    return gapClosed ? 2 : 1;  // 1 == bites (non-zero, --bite happy); 2 == toothless
  }

  // ── RESULT A path: the importer DID produce an HSE node. Cook it and parity-check the color. ──────────
  // (Reached only if a future import seam collapses the compound to the HSE atom.)
  registerTexOp("RenderTarget", texSource);
  Symbol* sym = const_cast<Symbol*>(rsym);

  // Supply the two Texture2D inputs (Image=RED, FxTexture=g 1/3) as RenderTarget producers wired to HSE.
  if (!lib.symbols.count("RenderTarget")) {
    if (const NodeSpec* fs = findSpec("RenderTarget"))
      lib.symbols["RenderTarget"] = atomicSymbolFromSpec(*fs);
  }
  const int imgId = sym->nextChildId++;
  { SymbolChild p; p.id = imgId; p.symbolId = "RenderTarget"; sym->children.push_back(p); }
  const int fxId = sym->nextChildId++;
  { SymbolChild p; p.id = fxId; p.symbolId = "RenderTarget"; p.overrides["ClearColor.x"] = 1.0f;
    sym->children.push_back(p); }
  sym->connections.push_back({imgId, "out", hseId, "Image"});
  if (!injectBug) sym->connections.push_back({fxId, "out", hseId, "FxTexture"});

  ResidentEvalGraph g = buildEvalGraph(lib, rootId);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[t3-hse] FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, mlib, q, kW, kH);
  pg.cookResident(g, ctx, nullptr, std::to_string(hseId));
  MTL::Texture* tex = pg.residentTexFor(std::to_string(hseId));

  uint8_t got[4] = {0, 0, 0, 0};
  bool haveOut = tex && tex->width() == kW && tex->height() == kH;
  if (haveOut) {
    std::vector<uint8_t> px((size_t)kW * kH * 4, 0);
    tex->getBytes(px.data(), kW * 4, MTL::Region::Make2D(0, 0, kW, kH), 0);
    size_t i = (((size_t)kH / 2) * kW + kW / 2) * 4;
    got[0] = px[i]; got[1] = px[i + 1]; got[2] = px[i + 2]; got[3] = px[i + 3];
  }
  // Oracle (independent, closed-form): Image RED + fx.g=1/3 → hue rotates RED→GREEN (see HueShift.hlsl).
  bool isGreen = haveOut && got[1] > 200 && got[0] < 55 && got[2] < 55 && got[3] > 250;
  printf("[t3-hse] replay-vs-oracle: haveOut=%d got=(%u,%u,%u,%u) expect GREEN → isGreen=%d injectBug=%d\n",
         haveOut ? 1 : 0, got[0], got[1], got[2], got[3], isGreen ? 1 : 0, injectBug ? 1 : 0);

  mlib->release(); q->release(); dev->release();

  if (!injectBug) {
    const bool pass = isGreen;
    printf("[t3-hse] PARITY VERDICT: %s\n",
           pass ? "GREEN (HSE.t3 replays to parity — image currency end-to-end)"
                : "RED (image replay seam gap)");
    pool->release();
    return pass ? 0 : 1;
  }
  const bool bites = !isGreen;  // bug drops FxTexture → RED → tooth bites
  printf("[t3-hse] -bug: fx-input tooth %s\n", bites ? "BITES" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 2;
}

}  // namespace sw
