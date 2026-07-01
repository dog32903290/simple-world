// runtime/t3import_bubblezoom_golden (--selftest-t3-bubblezoom) — the GRADIENT-FED image-fx collapse
// proof: it unlocks the whole class of image-fx compounds whose ImageB (t1) is a GradientsToTexture
// render of a Gradient boundary input (BubbleZoom, RemapColor, …).
//
// ── THE SEAM THIS PROVES ─────────────────────────────────────────────────────────────────────────────
// BubbleZoom.t3 (byte-embedded below) has 4 children: the fx-setup framework child (_multiImageFxSetupStatic,
// cc34a183), TWO Vector2Components helpers (Center / GainAndBias vec2 → X/Y into the FloatParams rail), and
// a GradientsToTexture child (2c53eee7) that renders the root's FeatherGradient boundary Gradient into a 1D
// texture wired to the fx-setup child's ImageB (t1). sw's BubbleZoom atom (point_ops_bubblezoom.cpp) takes
// the Gradient on a "Gradient" PORT and rasterizes it to a 1×512 texture row ITSELF (rasterizeGradientRow,
// :161) — the equivalence is "both raster-then-sample", NOT sw-samples-continuously-in-shader. So the SEPARATE
// GradientsToTexture render is redundant (the atom does the equivalent raster internally): the collapse ELIDES
// it (gradientstotexture-elided-to-gradient-port, t3_import_collapse.cpp), re-anchoring the atom's Gradient
// port onto GradientsToTexture's Gradients SOURCE. Row format/sampler diff = [fork-grad-row-format-32f] /
// [fork-gradient-row-sampler] (sub-perceptual, named in gradient_raster.h + t3_import_collapse.cpp). The two
// Vector2Components helpers are KEPT (they have sw atoms + map rows); the fx-setup child collapses onto the
// flat sw "BubbleZoom" tex atom.
//
// ── WHAT THIS MEASURES ───────────────────────────────────────────────────────────────────────────────
// (STRUCTURE) import BubbleZoom.t3 → the collapsed graph = 3 children (2×Vector2Components + 1 BubbleZoom
//   atom), NO GradientsToTexture (elided), and the FloatParams wires re-anchored onto the atom's scalar
//   ports IN ④d ORDER. To catch a port-order misconfig (refuter finding B: a neutral-default golden can't),
//   the collapsed connection list is asserted wire-by-wire against the EXPECTED (source → atom.<port>) map.
// (COOK) then wire a real DefineGradient producer (NON-default RED→GREEN) onto the atom's Gradient port +
//   an unwired Image (transparent-black dummy). The parity params reach the GPU on the UNAUTHORED production
//   path: GainAndBias/Center via the plumbed vec-default collapse; Feather/Magnify/FlipEffect via the sw
//   atom's OWN port defaults (their boundary FloatParams wires drop, and the port defaults are byte-equal to
//   BubbleZoom.t3's boundary defaults → [fork-bubblezoom-scalar-drop-benign], the drop is proven harmless).
//   The ONLY hand-authored value is Radius=1, a deliberate non-default test choice (0.5 saturates flat).
//   Cook resident, read back three pins (center / corner / MID-FIELD), and assert they match the CLOSED-FORM
//   BubbleZoom.hlsl gradient-param oracle (identical math to --selftest-bubblezoom). -bug OMITS the Gradient
//   wire → the cook falls to the near-transparent white→blue fallback → pins collapse to ~black → tooth bites.
//
// ZONE: runtime golden (shell tier — runtime import/collapse + resident tex cook + closed-form gradient oracle).
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

static const char* kBubbleZoomT3 =
#include "runtime/bubblezoom_t3_embed.inc"
;

constexpr uint32_t kW = 64, kH = 64;

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}
int countType(const Symbol& s, const std::string& type) {
  int n = 0; for (const SymbolChild& c : s.children) if (c.symbolId == type) n++; return n;
}

// ── CLOSED-FORM BubbleZoom.hlsl gradient-param oracle (Center=0, Feather=1, Radius=1, GainAndBias=(0.5,0.5)
//    identity, FlipEffect=0). Verbatim of point_ops_bubblezoom.cpp's shaderT — dBiased = saturate(2*|p|). ──
float bzGetBias(float bias, float x) { return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f); }
float bzGetSchlickBias(float g, float x) {
  if (x < 0.5f) { x *= 2.0f; x = 0.5f * bzGetBias(g, x); }
  else { x = 2.0f * x - 1.0f; x = 0.5f * bzGetBias(1.0f - g, x) + 0.5f; }
  return x;
}
float bzApplyGainAndBias(float value, float gx, float gy) {
  float g = std::min(std::max(gx, 0.0f), 1.0f), b = std::min(std::max(gy, 0.0f), 1.0f);
  if (value > 0.9999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) { value = bzGetBias(b, value); value = bzGetSchlickBias(g, value); }
  else { value = bzGetSchlickBias(g, value); value = bzGetBias(b, value); }
  return value;
}
float shaderT(int px, int py, int W, int H) {
  float uvx = (px + 0.5f) / W, uvy = (py + 0.5f) / H;
  float aspect = (float)W / (float)H;  // square → 1
  float pxc = (uvx - 0.5f) * aspect, pyc = (uvy - 0.5f);
  float r = std::sqrt(pxc * pxc + pyc * pyc);
  float adjustedRadius = 2.0f * 1.0f * aspect;
  float c = r * 2.0f - adjustedRadius + 2.0f * std::fabs(1.0f) / aspect;
  c = std::min(std::max(c / 1.0f, 0.0f), 1.0f);
  return bzApplyGainAndBias(c, 0.5f, 0.5f);
}

// RED→GREEN wired gradient (host reference sample(t) = (1-t, t, 0, 1)); distinct from the near-transparent
// white→blue unwired fallback in the G channel — the resident-wire teeth-guard.
SwGradient redGreenGradient() {
  SwGradient g; g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(1.0f, 0.0f, 0.0f, 1.0f)});
  g.steps.push_back({1.0f, simd::make_float4(0.0f, 1.0f, 0.0f, 1.0f)});
  return g;
}

// A wire in the collapsed graph, as (srcChild, srcSlot, dstChild, dstSlot) — for the ④d order assertion.
bool hasConn(const Symbol& s, int sc, const std::string& ss, int dc, const std::string& ds) {
  for (const SymbolConnection& c : s.connections)
    if (c.srcChild == sc && c.srcSlot == ss && c.dstChild == dc && c.dstSlot == ds) return true;
  return false;
}

// Image producer: a 1×1-equivalent transparent-black fill (orgColor=(0,0,0,0)) so the output rgb =
// gradient.rgb (per BubbleZoom.hlsl :63, lerp(orgColor.rgb, gradient.rgb, gradient.a=1)) and a = 0.
void blackImage(TexCookCtx& c) {
  if (!c.output) return;
  const uint32_t w = (uint32_t)c.output->width(), h = (uint32_t)c.output->height();
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  c.output->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

}  // namespace

// injectBug: OMIT the Gradient wire → the cook falls to the near-transparent white→blue fallback → the
// output rgb ≈ 0 (black) everywhere → the off-center green-dominance pin collapses → tooth bites.
int runT3BubbleZoomParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  // ---- STEP 1: import the REAL BubbleZoom.t3 through the PRODUCTION importer (gradient-fed collapse). ----
  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  const bool imported = importT3Symbol(kBubbleZoomT3, lib, &rootId, &warnings);
  const Symbol* rsym = imported ? lib.find(rootId) : nullptr;
  const int nChildren = rsym ? (int)rsym->children.size() : -1;
  const int nConns = rsym ? (int)rsym->connections.size() : -1;
  printf("[t3-bubblezoom] import: ok=%d rootId=%s children=%d conns=%d warnings=%zu\n",
         imported ? 1 : 0, rootId.c_str(), nChildren, nConns, warnings.size());
  if (rsym) {
    std::map<std::string, int> byType;
    for (const SymbolChild& c : rsym->children) byType[c.symbolId]++;
    printf("[t3-bubblezoom]   collapsed children:");
    for (const auto& kv : byType) printf(" %s×%d", kv.first.c_str(), kv.second);
    printf("\n");
  }
  for (const std::string& w : warnings) printf("[t3-bubblezoom]   warn: %s\n", w.c_str());

  const int bzId = rsym ? childIdOfType(*rsym, "BubbleZoom") : 0;
  const int v2cCount = rsym ? countType(*rsym, "Vector2Components") : 0;
  const int gttCount = rsym ? countType(*rsym, "GradientsToTexture") : 0;  // MUST be 0 (elided)
  const bool structOk = imported && rsym && bzId != 0 && v2cCount == 2 && gttCount == 0;
  printf("[t3-bubblezoom] SEAM: BubbleZoom-atom=%d Vector2Components×%d GradientsToTexture×%d(elided) → %s\n",
         bzId, v2cCount, gttCount,
         structOk ? "STRUCTURALLY GREEN" : "FAILED (elision/collapse did not produce the expected graph)");
  if (!structOk) { pool->release(); return injectBug ? 2 : 1; }

  // ---- ④d PORT-ORDER ASSERTION (refuter finding B guard): the FloatParams rail must land wire-by-wire on
  // the RIGHT atom scalar ports. The BubbleZoom.t3 FloatParams wire order (into 2929c4c9) is:
  //   V2C[Center].X→Center.x, V2C[Center].Y→Center.y, root.Magnify→Magnify, root.Feather→Feather,
  //   root.Radius→Radius, V2C[GainAndBias].X→GainAndBias.x, V2C[GainAndBias].Y→GainAndBias.y,
  //   root.FlipEffect→FlipEffect. The 3 boundary scalars (Magnify/Feather/Radius/FlipEffect) come from the
  //   root; assert each boundary FloatParams wire lands on the correct atom port (a swap would be caught).
  const bool magnifyOk = hasConn(*rsym, kSymbolBoundary, "5bb5934d-28a8-4bd3-a146-f0ecb804170a", bzId, "Magnify");
  const bool featherOk = hasConn(*rsym, kSymbolBoundary, "1e93d2b1-d21c-44bd-a96c-9844b9a89c8f", bzId, "Feather");
  const bool radiusOk  = hasConn(*rsym, kSymbolBoundary, "064b98dc-60f8-4019-a40c-21cb3bed9f4a", bzId, "Radius");
  const bool flipOk    = hasConn(*rsym, kSymbolBoundary, "244d64c7-5f45-44a1-9644-b0a8b9e04261", bzId, "FlipEffect");
  // Image (t0) fixed slot + Gradient (t1, elided-from-GTT) fixed slot: root.Image→atom.Image; the Gradient
  // port's source is GradientsToTexture's Gradients source (root.FeatherGradient boundary) via the elision.
  const bool imageOk    = hasConn(*rsym, kSymbolBoundary, "019b7a1a-0f86-48e2-b5e9-9a04480a3b07", bzId, "Image");
  const bool gradientOk = hasConn(*rsym, kSymbolBoundary, "0ce5b753-bcc0-4102-b011-64e603a50567", bzId, "Gradient");
  const bool orderOk = magnifyOk && featherOk && radiusOk && flipOk && imageOk && gradientOk;
  printf("[t3-bubblezoom] ④d PORT-ORDER: Magnify=%d Feather=%d Radius=%d Flip=%d Image=%d Gradient(elided)=%d → %s\n",
         magnifyOk, featherOk, radiusOk, flipOk, imageOk, gradientOk,
         orderOk ? "GREEN (each FloatParams/fixed wire on the correct atom port)"
                 : "RED (a FloatParams wire landed on the WRONG atom port — ④d order broken)");
  if (!orderOk) { pool->release(); return injectBug ? 2 : 1; }

  // ---- STEP 2: cook. Wire a real DefineGradient (RED→GREEN) onto the atom's Gradient port + Image dummy.
  // NOTHING that carries BubbleZoom parity semantics is hand-authored on the atom anymore — the golden now
  // tests the UNAUTHORED production path end-to-end:
  //   * GainAndBias (0.5,0.5): PLUMBED — the collapse authors the boundary vec2 default through the kept
  //     V2C[GainAndBias] helper onto atom.GainAndBias.x/y (collapse-boundary-typed-default-plumbed-through-
  //     kept-helper). Center (0,0) rides the same plumb (V2C[Center]).
  //   * Feather=1 / Magnify=1.25 / FlipEffect=0 SCALARS: their boundary→atom FloatParams wires DROP at cook
  //     (no top-level boundary injection), so production reads the sw atom's OWN PORT DEFAULTS. Those port
  //     defaults were ported from the SAME TiXL op and are BYTE-EQUAL to BubbleZoom.t3's boundary defaults
  //     (Feather 1.0==1.0, Magnify 1.25==1.25, FlipEffect 0.0==0.0, Radius 0.5==0.5 — verified against
  //     point_ops_bubblezoom.cpp ports vs the .t3 Inputs[]). So the scalar drop is BENIGN: unauthored cook ==
  //     .t3 default == oracle. [fork-bubblezoom-scalar-drop-benign] (was -scalar-authored-on-atom; the
  //     authoring is retired now that the drop is proven benign). NOT authored here — the golden relies on
  //     the atom port defaults, so a future port-default drift would turn this golden RED (it now GUARDS them).
  // The ONLY override is Radius=1 — a DELIBERATE non-default test choice (the .t3/atom default 0.5 saturates
  // the field flat → no spatial ramp to measure); the oracle's shaderT hardcodes the matching Radius=1.
  Symbol* sym = const_cast<Symbol*>(rsym);
  for (SymbolChild& c : sym->children)
    if (c.id == bzId) {
      c.overrides["Radius"] = 1.0f;   // NON-default TEST CHOICE (0.5 saturates flat → no spatial variation)
      // Feather/Magnify/FlipEffect/Center/GainAndBias INTENTIONALLY NOT authored — plumbed vec defaults +
      // benign scalar drop to the (byte-equal) atom port defaults carry the production values.
      c.overrides["Resolution"] = 4.0f; c.overrides["CustomW"] = (float)kW; c.overrides["CustomH"] = (float)kH;
    }

  // DefineGradient producer set to RED→GREEN (non-default), wired onto the BubbleZoom atom's Gradient port.
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

  // Image producer (transparent black) wired onto the atom's Image port. Mirror the HSE/Blend goldens:
  // override the "RenderTarget" texOp (symbolId with a real NodeSpec) to paint transparent black.
  registerTexOp("RenderTarget", blackImage);
  if (!lib.symbols.count("RenderTarget"))
    if (const NodeSpec* fs = findSpec("RenderTarget"))
      lib.symbols["RenderTarget"] = atomicSymbolFromSpec(*fs);
  const int imgId = sym->nextChildId++;
  { SymbolChild p; p.id = imgId; p.symbolId = "RenderTarget"; sym->children.push_back(p); }
  sym->connections.push_back({imgId, "out", bzId, "Image"});
  if (!injectBug) sym->connections.push_back({dgId, "out", bzId, "Gradient"});

  ResidentEvalGraph g = buildEvalGraph(lib, rootId);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[t3-bubblezoom] FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, mlib, q, kW, kH);
  pg.cookResident(g, ctx, nullptr, std::to_string(bzId));
  MTL::Texture* tex = pg.residentTexFor(std::to_string(bzId));

  bool haveOut = tex && tex->width() == kW && tex->height() == kH;
  std::vector<uint8_t> px((size_t)kW * kH * 4, 0);
  if (haveOut) tex->getBytes(px.data(), kW * 4, MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto pixel = [&](int x, int y, uint8_t out[4]) {
    size_t i = ((size_t)y * kW + x) * 4;
    out[0] = px[i]; out[1] = px[i + 1]; out[2] = px[i + 2]; out[3] = px[i + 3];
  };

  // THREE pins across the field, chosen to CATCH the hollow-green hole the refuter found:
  //   CENTER (32,32): dBiased≈0.02 → near-RED (249,6). Coincidence point #1 — GPU==oracle even under the bug.
  //   CORNER (4,4):   dBiased=1.0 → full GREEN (0,255). Coincidence point #2 + the resident-wire teeth-guard.
  //   MID-FIELD (48,32): dBiased≈0.516 → R≈123,G≈132. The DECISIVE pin — it sits in the SMOOTH radial ramp
  //     where the near-binary bug field (GainAndBias=(0,0)) diverges MAXIMALLY (~±130) from the oracle. It is
  //     GREEN only if GainAndBias=(0.5,0.5) truly reached the GPU through the plumbed collapse. (Before the
  //     machine fix this pin was RED — the old two pins straddled it without ever testing the ramp.)
  const int cx = kW / 2, cy = kH / 2, ox = 4, oy = 4, mx = 48, my = 32;
  uint8_t cpin[4], opin[4], mpin[4]; pixel(cx, cy, cpin); pixel(ox, oy, opin); pixel(mx, my, mpin);
  SwGradient ref = redGreenGradient();
  simd::float4 refO = ref.sample(shaderT(ox, oy, kW, kH));  // expected corner rgb
  simd::float4 refC = ref.sample(shaderT(cx, cy, kW, kH));  // expected center rgb
  simd::float4 refM = ref.sample(shaderT(mx, my, kW, kH));  // expected MID-FIELD rgb (the decisive pin)
  auto near8 = [](int v, float t) { return std::abs(v - (int)std::lround(t * 255.0f)) <= 8; };
  bool oOk = haveOut && near8(opin[0], refO.x) && near8(opin[1], refO.y) && near8(opin[2], refO.z);
  bool cOk = haveOut && near8(cpin[0], refC.x) && near8(cpin[1], refC.y) && near8(cpin[2], refC.z);
  bool mOk = haveOut && near8(mpin[0], refM.x) && near8(mpin[1], refM.y) && near8(mpin[2], refM.z);
  bool greenGuard = haveOut && opin[1] > 40;  // corner G clearly > 0 (impossible under transparent fallback)
  bool pass = oOk && cOk && mOk && greenGuard;
  printf("[t3-bubblezoom] replay-vs-oracle: haveOut=%d corner=(%u,%u,%u) want~(%.0f,%.0f,%.0f) "
         "center=(%u,%u,%u) want~(%.0f,%.0f,%.0f) MID=(%u,%u,%u) want~(%.0f,%.0f,%.0f) greenGuard=%d injectBug=%d\n",
         haveOut ? 1 : 0, opin[0], opin[1], opin[2], refO.x * 255, refO.y * 255, refO.z * 255,
         cpin[0], cpin[1], cpin[2], refC.x * 255, refC.y * 255, refC.z * 255,
         mpin[0], mpin[1], mpin[2], refM.x * 255, refM.y * 255, refM.z * 255, greenGuard, injectBug ? 1 : 0);

  mlib->release(); q->release(); dev->release();

  if (!injectBug) {
    printf("[t3-bubblezoom] PARITY VERDICT: %s\n",
           pass ? "GREEN (BubbleZoom.t3 gradient-fed collapse replays to the closed-form gradient oracle — "
                  "the GradientsToTexture-elided seam unlocks gradient-fed image-fx)"
                : "RED (gradient-fed collapse gap — elision/gradient wire/FloatParams did not reach the atom)");
    pool->release();
    return pass ? 0 : 1;
  }
  const bool bites = !pass;  // bug drops the Gradient wire → transparent fallback → black → tooth bites
  printf("[t3-bubblezoom] -bug: Gradient-input tooth %s\n", bites ? "BITES" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 2;
}

}  // namespace sw
