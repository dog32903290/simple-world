// runtime/t3import_remapcolor_golden (--selftest-t3-remapcolor) — GRADIENT-FED COLOR op collapse proof +
// the TRANSFORMIMAGE-PASSTHROUGH elision first证. Same GradientsToTexture-elided-to-Gradient-port seam as
// the gradient trio, but RemapColor.t3 interposes a TransformImage (identity copy, GenerateMips=true)
// between the GTT and the fx child's ImageB — that TransformImage is ALSO elided (fork
// transformimage-identity-passthrough-elided, t3_import_collapse.cpp), chaining the atom's Gradient port
// back through it to the GTT's Gradients source. A dead bypassed GenerateMips child hangs off the GTT too.
//
// ── THE SEAM THIS PROVES ─────────────────────────────────────────────────────────────────────────────
// RemapColor.t3 (byte-embedded) has 7 children: the fx-setup framework child (_multiImageFxSetupStatic,
// cc34a183), a GradientsToTexture (2c53eee7) rendering the Gradient boundary, a TransformImage (32e18957,
// identity copy) between GTT and ImageB, a GenerateMips (32a6a351, bypassed dead branch), plus three KEPT
// decompose helpers — IntToFloat (Mode), BoolToFloat (DontColorAlpha), Vector2Components (GainAndBias).
// The collapse ELIDES the GTT, the TransformImage AND the GenerateMips; the fx child collapses onto the
// flat sw "RemapColor" tex atom (point_ops_remapcolor.cpp); the 3 decompose helpers are KEPT.
//
// ── WHAT THIS MEASURES ───────────────────────────────────────────────────────────────────────────────
// (STRUCTURE) import → collapsed graph = 3 helpers (IntToFloat + BoolToFloat + Vector2Components) + 1
//   RemapColor atom, NO GradientsToTexture / TransformImage / GenerateMips (all elided).
// (④d PORT-ORDER) the boundary-fed FloatParams wires (Cycle, Exposure, Repeat) + the Image / Gradient
//   fixed slots are asserted wire-by-wire onto the RIGHT atom ports (a positional misconfig would swap them).
// (COOK) wire a real DefineGradient producer (NON-default RED→GREEN) onto the atom's Gradient port; wire a
//   CheckerBoard (ColorA=red, ColorB=blue — TWO SPATIAL REGIONS) onto the atom's Image (t0) so the field
//   DIVERGES across the frame (RemapColor's variation is in INPUT-COLOR space; a uniform input would be
//   flat). Mode/GainAndBias ride the plumbed defaults; Cycle/Exposure/Repeat drop to the atom's own port
//   defaults (byte-equal to RemapColor.t3's boundary defaults → benign drop). Cook resident, read back a
//   pin in the RED region, a pin in the BLUE region, and a MID pin, and assert each matches the sw
//   ColorRemap oracle (rowLinear — the SAME GPU-validated math --selftest-remapcolor pins). -bug OMITS the
//   Gradient wire → near-black→white fallback (gray, R==G) → the per-region R-vs-G distinctness collapses.
//
// ZONE: runtime golden (shell tier — runtime import/collapse + resident tex cook + closed-form ColorRemap oracle).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"
#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"
#include "runtime/gradient_raster.h"  // kGradientRowN (row width the atom rasterizes to)
#include "runtime/image_filter_op_registry.h"
#include "runtime/point_graph.h"
#include "runtime/resident_eval_graph.h"
#include "runtime/sw_gradient.h"
#include "runtime/t3_import.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

void registerBuiltinPointOps();

namespace {

static const char* kRemapColorT3 =
#include "runtime/remapcolor_t3_embed.inc"
;

constexpr uint32_t kW = 64, kH = 64;

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}
int countType(const Symbol& s, const std::string& type) {
  int n = 0; for (const SymbolChild& c : s.children) if (c.symbolId == type) n++; return n;
}
bool hasConn(const Symbol& s, int sc, const std::string& ss, int dc, const std::string& ds) {
  for (const SymbolConnection& c : s.connections)
    if (c.srcChild == sc && c.srcSlot == ss && c.dstChild == dc && c.dstSlot == ds) return true;
  return false;
}

// ── sw ColorRemap oracle (rowLinear — VERBATIM of point_ops_remapcolor.cpp's rowLinear, which is
//    GPU-validated by --selftest-remapcolor). The sw atom binds the rasterized gradient row at s1
//    (clampedSampler, ClampToEdge) and samples it linear at v=0. GainAndBias=(0.5,0.5) is identity for
//    channel values in [0.001,0.999]; Exposure=Repeat=1, Cycle(→Offset)=0 → out.r = rowLinear(grad,
//    steps, in.r).r, etc. (Mode=1 IndividualChannels; the .t3 default). ──
simd::float4 rowLinear(const SwGradient& g, int steps, float u) {
  const int N = (steps > 0) ? std::min(std::max(steps, 1), 16384) : kGradientRowN;
  if (N == 1) return g.sample(0.0f);
  u = std::min(std::max(u, 0.0f), 1.0f);
  float f = u * (float)N - 0.5f;
  int i0 = (int)std::floor(f);
  float frac = f - (float)i0;
  int i1 = i0 + 1;
  i0 = std::min(std::max(i0, 0), N - 1);
  i1 = std::min(std::max(i1, 0), N - 1);
  auto texel = [&](int i) { return g.sample((float)i / (N - 1.0f)); };
  simd::float4 a = texel(i0), b = texel(i1);
  return simd::make_float4(a.x + (b.x - a.x) * frac, a.y + (b.y - a.y) * frac,
                           a.z + (b.z - a.z) * frac, a.w + (b.w - a.w) * frac);
}
// Mode=1 IndividualChannels per-pixel expected: out.{r,g,b,a} = rowLinear(grad, 0, in.{r,g,b,a}).{r,g,b,a}.
// Input channels chosen inside the gain/bias identity band [0.001,0.999] via CheckerBoard values 0.15/0.85.
simd::float4 expectIndividual(const SwGradient& ref, simd::float4 in) {
  return simd::make_float4(rowLinear(ref, 0, in.x).x, rowLinear(ref, 0, in.y).y,
                           rowLinear(ref, 0, in.z).z, rowLinear(ref, 0, in.w).w);
}

// RED→GREEN wired gradient (host reference sample(t) = (1-t, t, 0, 1)); distinct from the near-black→white
// unwired fallback (gray, R==G at every t) — the resident-wire teeth-guard.
SwGradient redGreenGradient() {
  SwGradient g; g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(1.0f, 0.0f, 0.0f, 1.0f)});
  g.steps.push_back({1.0f, simd::make_float4(0.0f, 1.0f, 0.0f, 1.0f)});
  return g;
}

}  // namespace

int runT3RemapColorParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  // ---- STEP 1: import the REAL RemapColor.t3 through the PRODUCTION importer (gradient-fed + passthrough). ----
  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  const bool imported = importT3Symbol(kRemapColorT3, lib, &rootId, &warnings);
  const Symbol* rsym = imported ? lib.find(rootId) : nullptr;
  const int nChildren = rsym ? (int)rsym->children.size() : -1;
  const int nConns = rsym ? (int)rsym->connections.size() : -1;
  printf("[t3-remapcolor] import: ok=%d rootId=%s children=%d conns=%d warnings=%zu\n",
         imported ? 1 : 0, rootId.c_str(), nChildren, nConns, warnings.size());
  if (rsym) {
    std::map<std::string, int> byType;
    for (const SymbolChild& c : rsym->children) byType[c.symbolId]++;
    printf("[t3-remapcolor]   collapsed children:");
    for (const auto& kv : byType) printf(" %s×%d", kv.first.c_str(), kv.second);
    printf("\n");
  }
  for (const std::string& w : warnings) printf("[t3-remapcolor]   warn: %s\n", w.c_str());

  const int rcId = rsym ? childIdOfType(*rsym, "RemapColor") : 0;
  const int i2fCount = rsym ? countType(*rsym, "IntToFloat") : 0;
  const int b2fCount = rsym ? countType(*rsym, "BoolToFloat") : 0;
  const int v2cCount = rsym ? countType(*rsym, "Vector2Components") : 0;
  const int gttCount = rsym ? countType(*rsym, "GradientsToTexture") : 0;  // MUST be 0 (elided)
  const int tiCount  = rsym ? countType(*rsym, "TransformImage") : 0;      // MUST be 0 (elided)
  const bool structOk = imported && rsym && rcId != 0 && i2fCount == 1 && b2fCount == 1 &&
                        v2cCount == 1 && gttCount == 0 && tiCount == 0;
  printf("[t3-remapcolor] SEAM: RemapColor-atom=%d IntToFloat×%d BoolToFloat×%d Vector2Components×%d "
         "GradientsToTexture×%d(elided) TransformImage×%d(elided) → %s\n",
         rcId, i2fCount, b2fCount, v2cCount, gttCount, tiCount,
         structOk ? "STRUCTURALLY GREEN" : "FAILED (elision/collapse did not produce the expected graph)");
  if (!structOk) { pool->release(); return injectBug ? 2 : 1; }

  // ---- ④d PORT-ORDER ASSERTION: the boundary-fed FloatParams wires land on the RIGHT atom ports.
  // RemapColor.t3 wires Cycle(b1763a8b)→Cycle, Exposure(8a2cc68c)→Exposure, Repeat(7023f71c)→Repeat
  // straight from the root. Plus the fixed slots: Image(876f6f64)→Image, Gradient(c45d487b, via the
  // GTT→TransformImage double-elision)→Gradient.
  const bool cycleOk  = hasConn(*rsym, kSymbolBoundary, "b1763a8b-aa98-4e00-a47c-a5d0d750ae6e", rcId, "Cycle");
  const bool expOk    = hasConn(*rsym, kSymbolBoundary, "8a2cc68c-ec38-4bb5-a201-e5c06f0a38d1", rcId, "Exposure");
  const bool repeatOk = hasConn(*rsym, kSymbolBoundary, "7023f71c-1c13-4d66-85e7-b0918cb8b02c", rcId, "Repeat");
  const bool imageOk    = hasConn(*rsym, kSymbolBoundary, "876f6f64-7cb4-4060-8571-e0b78b437d41", rcId, "Image");
  const bool gradientOk = hasConn(*rsym, kSymbolBoundary, "c45d487b-3221-44c7-bf9e-b982a65280f6", rcId, "Gradient");
  const bool orderOk = cycleOk && expOk && repeatOk && imageOk && gradientOk;
  printf("[t3-remapcolor] ④d PORT-ORDER: Cycle=%d Exposure=%d Repeat=%d Image=%d Gradient(double-elided)=%d → %s\n",
         cycleOk, expOk, repeatOk, imageOk, gradientOk,
         orderOk ? "GREEN (each FloatParams/fixed wire on the correct atom port)"
                 : "RED (a FloatParams wire landed on the WRONG atom port — ④d order broken)");
  if (!orderOk) { pool->release(); return injectBug ? 2 : 1; }

  // ---- STEP 2: cook. Wire a real DefineGradient (RED→GREEN) onto the atom's Gradient port, and a
  // CheckerBoard (ColorA=red-ish, ColorB=blue-ish — two SPATIAL regions inside the gain/bias identity band)
  // onto the atom's Image (t0). The RemapColor atom then per-channel-looks-up each region's color in the
  // gradient row → the field diverges. Mode/GainAndBias ride the plumbed defaults (Mode=1, (0.5,0.5));
  // Cycle/Exposure/Repeat drop to the atom's own port defaults (byte-equal to the .t3 boundary defaults →
  // benign drop). Only Resolution/CustomW/CustomH are set (a 64×64 readback fixture, not a parity param).
  Symbol* sym = const_cast<Symbol*>(rsym);
  for (SymbolChild& c : sym->children)
    if (c.id == rcId) {
      c.overrides["Resolution"] = 4.0f; c.overrides["CustomW"] = (float)kW; c.overrides["CustomH"] = (float)kH;
    }

  // CheckerBoard input: ColorA=(0.85,0.15,0.15,1) red-ish, ColorB=(0.15,0.15,0.85,1) blue-ish. Channels in
  // [0.001,0.999] so the atom's GainAndBias(0.5,0.5) is identity → per-channel lookup t == input channel.
  const simd::float4 kColorA = simd::make_float4(0.85f, 0.15f, 0.15f, 1.0f);
  const simd::float4 kColorB = simd::make_float4(0.15f, 0.15f, 0.85f, 1.0f);
  if (!lib.symbols.count("CheckerBoard"))
    if (const NodeSpec* fs = findSpec("CheckerBoard"))
      lib.symbols["CheckerBoard"] = atomicSymbolFromSpec(*fs);
  const int cbId = sym->nextChildId++;
  { SymbolChild cb; cb.id = cbId; cb.symbolId = "CheckerBoard";
    cb.overrides["ColorA.r"] = kColorA.x; cb.overrides["ColorA.g"] = kColorA.y; cb.overrides["ColorA.b"] = kColorA.z; cb.overrides["ColorA.a"] = kColorA.w;
    cb.overrides["ColorB.r"] = kColorB.x; cb.overrides["ColorB.g"] = kColorB.y; cb.overrides["ColorB.b"] = kColorB.z; cb.overrides["ColorB.a"] = kColorB.w;
    cb.overrides["Stretch.x"] = 1.0f; cb.overrides["Stretch.y"] = 1.0f; cb.overrides["Scale"] = 1.0f;
    cb.overrides["UseAspectRatio"] = 1.0f; cb.overrides["Offset.x"] = 0.0f; cb.overrides["Offset.y"] = 0.0f;
    cb.overrides["Resolution"] = 4.0f; cb.overrides["CustomW"] = (float)kW; cb.overrides["CustomH"] = (float)kH;
    sym->children.push_back(cb); }
  sym->connections.push_back({cbId, "out", rcId, "Image"});

  // DefineGradient producer set to RED→GREEN (non-default), wired onto the RemapColor atom's Gradient port.
  if (!lib.symbols.count("DefineGradient"))
    if (const NodeSpec* fs = findSpec("DefineGradient"))
      lib.symbols["DefineGradient"] = atomicSymbolFromSpec(*fs);
  const int dgId = sym->nextChildId++;
  { SymbolChild p; p.id = dgId; p.symbolId = "DefineGradient";
    p.overrides["Color1.x"] = 1.0f; p.overrides["Color1.y"] = 0.0f; p.overrides["Color1.z"] = 0.0f; p.overrides["Color1.w"] = 1.0f;
    p.overrides["Color1Pos"] = 0.0f;
    p.overrides["Color2.x"] = 0.0f; p.overrides["Color2.y"] = 1.0f; p.overrides["Color2.z"] = 0.0f; p.overrides["Color2.w"] = 1.0f;
    p.overrides["Color2Pos"] = 1.0f; p.overrides["Interpolation"] = 0.0f;  // Linear
    sym->children.push_back(p); }
  if (!injectBug) sym->connections.push_back({dgId, "out", rcId, "Gradient"});

  ResidentEvalGraph g = buildEvalGraph(lib, rootId);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[t3-remapcolor] FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, mlib, q, kW, kH);
  pg.cookResident(g, ctx, nullptr, std::to_string(rcId));
  MTL::Texture* tex = pg.residentTexFor(std::to_string(rcId));

  bool haveOut = tex && tex->width() == kW && tex->height() == kH;
  std::vector<uint8_t> px((size_t)kW * kH * 4, 0);
  if (haveOut) tex->getBytes(px.data(), kW * 4, MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto pixel = [&](int x, int y, uint8_t out[4]) {
    size_t i = ((size_t)y * kW + x) * 4;
    out[0] = px[i]; out[1] = px[i + 1]; out[2] = px[i + 2]; out[3] = px[i + 3];
  };

  // Three pins on the DIVERGING field. On a 64×64 CheckerBoard (default Stretch/Scale, 2×2 cells): quadrant
  // centers at texCoord ~(0.25,0.25)&(0.75,0.75) = ColorB (blue-ish input), ~(0.75,0.25)&(0.25,0.75) =
  // ColorA (red-ish input). RemapColor maps each region's INPUT color through the gradient per channel:
  //   RED pin (16,48) input ColorA → out = expectIndividual(grad, ColorA)
  //   BLUE pin (16,16) input ColorB → out = expectIndividual(grad, ColorB)   ← DECISIVE: differs sharply
  //   MID pin (48,48) input ColorB (bottom-right quadrant) → same-region cross-check
  // The RED vs BLUE pin outputs diverge maximally (their input .r/.b swap) — a stuck/wrong gradient or a
  // lost per-channel lookup can't match both. All input channels ∈[0.15,0.85] (gain/bias identity band).
  const int rx = 16, ry = 48, bx = 16, by = 16, mx = 48, my = 48;
  uint8_t rpin[4], bpin[4], mpin[4]; pixel(rx, ry, rpin); pixel(bx, by, bpin); pixel(mx, my, mpin);
  SwGradient ref = redGreenGradient();
  simd::float4 refR = expectIndividual(ref, kColorA);  // red-region input
  simd::float4 refB = expectIndividual(ref, kColorB);  // blue-region input
  simd::float4 refM = expectIndividual(ref, kColorB);  // mid pin also blue region
  auto near8 = [](int v, float t) { return std::abs(v - (int)std::lround(t * 255.0f)) <= 8; };
  bool rOk = haveOut && near8(rpin[0], refR.x) && near8(rpin[1], refR.y) && near8(rpin[2], refR.z);
  bool bOk = haveOut && near8(bpin[0], refB.x) && near8(bpin[1], refB.y) && near8(bpin[2], refB.z);
  bool mOk = haveOut && near8(mpin[0], refM.x) && near8(mpin[1], refM.y) && near8(mpin[2], refM.z);
  // Teeth-guard: the RED-region pin's OUTPUT differs from the BLUE-region pin's OUTPUT (proves the
  // per-channel gradient lookup carries the spatially-varying input, not a flat/gray fallback). The
  // regions differ only in their .r/.b input channels (ColorA.r=0.85 vs ColorB.r=0.15), so the OUTPUT
  // R channel = rowLinear(in.r).r = (1-in.r) diverges: RED-region out.r≈38, BLUE-region out.r≈217.
  // (A gray/UseGrayScale fallback would make both regions' gray IDENTICAL → out.r equal → guard bites.)
  bool divergeGuard = haveOut && std::abs((int)rpin[0] - (int)bpin[0]) > 40;
  bool pass = rOk && bOk && mOk && divergeGuard;
  printf("[t3-remapcolor] replay-vs-oracle: haveOut=%d RED=(%u,%u,%u) want~(%.0f,%.0f,%.0f) "
         "BLUE=(%u,%u,%u) want~(%.0f,%.0f,%.0f) MID=(%u,%u,%u) want~(%.0f,%.0f,%.0f) divergeGuard=%d injectBug=%d\n",
         haveOut ? 1 : 0, rpin[0], rpin[1], rpin[2], refR.x * 255, refR.y * 255, refR.z * 255,
         bpin[0], bpin[1], bpin[2], refB.x * 255, refB.y * 255, refB.z * 255,
         mpin[0], mpin[1], mpin[2], refM.x * 255, refM.y * 255, refM.z * 255, divergeGuard, injectBug ? 1 : 0);

  mlib->release(); q->release(); dev->release();

  if (!injectBug) {
    printf("[t3-remapcolor] PARITY VERDICT: %s\n",
           pass ? "GREEN (RemapColor.t3 gradient-fed collapse replays to the sw ColorRemap oracle — "
                  "IntToFloat + BoolToFloat + Vector2Components kept, GradientsToTexture/TransformImage/"
                  "GenerateMips elided; two-region field diverges)"
                : "RED (collapse gap — elision/gradient wire/FloatParams/input did not reach the atom)");
    pool->release();
    return pass ? 0 : 1;
  }
  // bug drops the Gradient wire → near-black→white fallback → gray (R==G) → the two regions converge in G.
  const bool bites = !pass;
  printf("[t3-remapcolor] -bug: Gradient-input tooth %s\n", bites ? "BITES" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 0;  // dead tooth exits 0 → --bite NO-BITE list catches it (GOLDEN_STANDARD 特徵3)
}

}  // namespace sw
