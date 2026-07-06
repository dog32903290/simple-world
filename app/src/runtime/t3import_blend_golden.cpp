// runtime/t3import_blend_golden (--selftest-t3-blend) — the MULTI-CHILD generalization proof for the
// image-fx collapse seam.
//
// HSE.t3 (t3import_hse_golden.cpp) is the SINGLE-fx-setup-child wrapper: its only child is
// _multiImageFxSetupStatic, so the collapse is 1-child → 1 atom. But HSE is the ONLY such op in TiXL —
// EVERY other image-fx wrapper is MULTI-child: the fx-setup child is fed by HELPER value ops
// (Vector4Components / IntToFloat / BoolToFloat) that decompose the root's vec4/int/bool boundary Inputs
// into the fx-setup FloatParams scalar rail. If the collapse only handled the single-child case it would
// be an HSE-specific hack. This golden proves it is NOT: Blend.t3 (7 children — 2×Vector4Components,
// 3×IntToFloat, 1×BoolToFloat, 1×fx-setup) imports through the SAME data-driven collapse (t3_import_maps
// TABLE ④b/c/d + the multi-child collapseImageFxWrapper), KEEPING the 6 helper value ops as real sw
// children and re-anchoring the fx-setup child's wires onto the flat sw "Blend" tex atom.
//
// ── WHAT THIS MEASURES ─────────────────────────────────────────────────────────────────────────────
// import Blend.t3 → the collapsed graph = 7 children (6 helpers + 1 Blend atom), NOT an empty root. Then
// cook it resident and parity-check the color: ImageA = solid RED, ImageB = solid GREEN α=0.5, BlendMode
// default 0 (Normal). Blend's Normal-mode oracle (point_ops_blend.cpp, hand-derived from the .hlsl):
//   rgb = (1-tB.a)·tA.rgb + tB.a·tB.rgb = 0.5·(1,0,0) + 0.5·(0,1,0) = (0.5,0.5,0); alpha = 1.0
//   → (128,128,0,255)  ("olive" — G channel is the load-bearing tell it's a real blend, not a passthrough)
// -bug drops the ImageB wire → ImageB self-blends RED → (255,0,0,255): G collapses 128→0 → tooth bites.
//
// NAMED FORK — [fork-blend-vec4-boundary-default-authored-on-child]: the importer's Inputs[] loop only
// captures SCALAR DefaultValue; a Vector4 boundary default ({X,Y,Z,W} object — Blend.t3 ColorA=ColorB=
// (1,1,1,1)) is not yet plumbed to the Vector4Components.Value it feeds. So this golden AUTHORS ColorA/
// ColorB = (1,1,1,1) as overrides ON the two imported Vector4Components children (same precedent as 骨4's
// Const=80 fork). The COLLAPSE STRUCTURE (helpers kept, wires re-anchored) is what's under test; the vec4
// default gap is a known importer limitation recorded here, not hidden.
//
// ZONE: runtime golden (shell tier — runtime import + resident tex cook + independent Normal-blend oracle).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"
#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"
#include "runtime/image_filter_op_registry.h"
#include "runtime/point_graph.h"
#include "runtime/resident_eval_graph.h"
#include "runtime/t3_import.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

void registerBuiltinPointOps();

namespace {

static const char* kBlendT3 =
#include "runtime/blend_t3_embed.inc"
;

constexpr uint32_t kW = 32, kH = 32;

// RenderTarget cook OVERRIDE: paint a solid picked by ClearColor.x
//   0 → ImageA = solid RED (255,0,0,255)
//   1 → ImageB = solid GREEN with alpha 0.5 (0,255,0,128)
void texSource(TexCookCtx& c) {
  if (!c.output) return;
  const uint32_t w = (uint32_t)c.output->width(), h = (uint32_t)c.output->height();
  const int which = (int)std::lround(cookParam(c, "ClearColor.x", 0.0f));
  uint8_t r = 255, g = 0, b = 0, a = 255;         // which==0: RED opaque (ImageA)
  if (which == 1) { r = 0; g = 255; b = 0; a = 128; }  // which==1: GREEN α=0.5 (ImageB)
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  for (size_t i = 0; i < (size_t)w * h; ++i) {
    px[i * 4 + 0] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = a;
  }
  c.output->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}
int countType(const Symbol& s, const std::string& type) {
  int n = 0; for (const SymbolChild& c : s.children) if (c.symbolId == type) n++; return n;
}

}  // namespace

// The op proven is Blend (the simplest MULTI-child fx-wrapper whose helpers all already have sw
// atoms/map rows — see header). injectBug: OMIT the ImageB wire → Normal blend degenerates to RED
// (the tooth).
int runT3BlendParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  // ---- STEP 1: import the REAL Blend.t3 through the PRODUCTION importer (multi-child collapse). ----
  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  const bool imported = importT3Symbol(kBlendT3, lib, &rootId, &warnings);
  const Symbol* rsym = imported ? lib.find(rootId) : nullptr;
  const int nChildren = rsym ? (int)rsym->children.size() : -1;
  const int nConns = rsym ? (int)rsym->connections.size() : -1;
  printf("[t3-blend] import: ok=%d rootId=%s children=%d conns=%d warnings=%zu\n",
         imported ? 1 : 0, rootId.c_str(), nChildren, nConns, warnings.size());
  if (rsym) {
    std::map<std::string, int> byType;
    for (const SymbolChild& c : rsym->children) byType[c.symbolId]++;
    printf("[t3-blend]   collapsed children:");
    for (const auto& kv : byType) printf(" %s×%d", kv.first.c_str(), kv.second);
    printf("\n");
  }
  for (const std::string& w : warnings) printf("[t3-blend]   warn: %s\n", w.c_str());

  const int blendId = rsym ? childIdOfType(*rsym, "Blend") : 0;
  const int vec4Count = rsym ? countType(*rsym, "Vector4Components") : 0;
  const bool structOk = imported && rsym && blendId != 0 && vec4Count == 2;
  printf("[t3-blend] SEAM: Blend-atom=%d Vector4Components×%d → multi-child collapse %s\n",
         blendId, vec4Count, structOk ? "STRUCTURALLY GREEN (proceed to cook)"
                                      : "FAILED (collapse did not keep helpers / build atom)");
  if (!structOk) { pool->release(); return injectBug ? 2 : 1; }

  // ---- STEP 2: ColorA/ColorB = (1,1,1,1) is now PLUMBED by the collapse. Blend.t3's ColorA/ColorB
  // boundary inputs carry a vec4 default {1,1,1,1}; the collapse authors those onto the two kept
  // Vector4Components helpers' Value.x/.y/.z/.w (collapse-boundary-typed-default-plumbed-through-kept-helper,
  // t3_import_collapse.cpp) — the SAME machine that fixed the BubbleZoom GainAndBias hollow-green. So the
  // golden no longer authors them by hand (the former [fork-blend-vec4-boundary-default-authored-on-child]
  // workaround is retired). If the plumb regresses, ColorA/ColorB fall to (0,0,0,0) → the Normal-blend
  // parity oracle diverges → this golden goes RED, so it now GUARDS the vec-default plumb for vec4 too.
  Symbol* sym = const_cast<Symbol*>(rsym);

  // Supply ImageA (RED) + ImageB (GREEN α=0.5) as RenderTarget producers wired to the Blend atom.
  registerTexOp("RenderTarget", texSource);
  if (!lib.symbols.count("RenderTarget"))
    if (const NodeSpec* fs = findSpec("RenderTarget"))
      lib.symbols["RenderTarget"] = atomicSymbolFromSpec(*fs);
  const int aId = sym->nextChildId++;
  { SymbolChild p; p.id = aId; p.symbolId = "RenderTarget"; sym->children.push_back(p); }
  const int bId = sym->nextChildId++;
  { SymbolChild p; p.id = bId; p.symbolId = "RenderTarget"; p.overrides["ClearColor.x"] = 1.0f;
    sym->children.push_back(p); }
  sym->connections.push_back({aId, "out", blendId, "ImageA"});
  if (!injectBug) sym->connections.push_back({bId, "out", blendId, "ImageB"});

  ResidentEvalGraph g = buildEvalGraph(lib, rootId);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[t3-blend] FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, mlib, q, kW, kH);
  pg.cookResident(g, ctx, nullptr, std::to_string(blendId));
  MTL::Texture* tex = pg.residentTexFor(std::to_string(blendId));

  uint8_t got[4] = {0, 0, 0, 0};
  bool haveOut = tex && tex->width() == kW && tex->height() == kH;
  if (haveOut) {
    std::vector<uint8_t> px((size_t)kW * kH * 4, 0);
    tex->getBytes(px.data(), kW * 4, MTL::Region::Make2D(0, 0, kW, kH), 0);
    size_t i = (((size_t)kH / 2) * kW + kW / 2) * 4;
    got[0] = px[i]; got[1] = px[i + 1]; got[2] = px[i + 2]; got[3] = px[i + 3];
  }
  // Oracle (independent, closed-form): Normal blend of RED over GREEN α=0.5 → (128,128,0,255) ±3.
  auto near = [](int v, int t) { return std::abs(v - t) <= 3; };
  bool isOlive = haveOut && near(got[0], 128) && near(got[1], 128) && got[2] < 8 && near(got[3], 255);
  printf("[t3-blend] replay-vs-oracle: haveOut=%d got=(%u,%u,%u,%u) expect (128,128,0,255) → isOlive=%d "
         "injectBug=%d\n", haveOut ? 1 : 0, got[0], got[1], got[2], got[3], isOlive ? 1 : 0,
         injectBug ? 1 : 0);

  mlib->release(); q->release(); dev->release();

  if (!injectBug) {
    printf("[t3-blend] PARITY VERDICT: %s\n",
           isOlive ? "GREEN (Blend.t3 multi-child collapse replays to Normal-blend parity — the collapse "
                     "table generalizes beyond HSE)"
                   : "RED (multi-child collapse gap — helpers/wires did not reach the atom correctly)");
    pool->release();
    return isOlive ? 0 : 1;
  }
  const bool bites = !isOlive;  // bug drops ImageB → RED → tooth bites
  printf("[t3-blend] -bug: ImageB-input tooth %s\n", bites ? "BITES" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 0;  // dead tooth exits 0 → --bite NO-BITE list catches it (GOLDEN_STANDARD 特徵3)
}

}  // namespace sw
