// runtime/t3import_radialgradient_golden (--selftest-t3-radialgradient) — GRADIENT-FED image GENERATOR
// collapse proof. Same seam as BubbleZoom (GradientsToTexture ELIDED onto the atom's Gradient port), but
// the wrapper is an image/generate/basic op with THREE decompose helpers (Center/Stretch/BiasAndGain →
// Vector2Components) + BoolToFloat×3 + IntToFloat feeding the ④d FloatParams rail.
//
// ── THE SEAM THIS PROVES ─────────────────────────────────────────────────────────────────────────────
// RadialGradient.t3 (byte-embedded below) has 9 children: the fx-setup framework child
// (_multiImageFxSetupStatic, cc34a183), a GradientsToTexture child (2c53eee7) rendering the root's Gradient
// boundary → ImageB(t1), THREE Vector2Components (Center/Stretch/BiasAndGain vec2 → X/Y), THREE BoolToFloat
// (PingPong/Repeat/PolarOrientation) and one IntToFloat (BlendMode). sw's RadialGradient atom
// (point_ops_radialgradient.cpp) takes the Gradient on a "Gradient" PORT and rasterizes it to a 1×512 row
// ITSELF, so the SEPARATE GradientsToTexture is redundant → the collapse ELIDES it (same
// gradientstotexture-elided-to-gradient-port fork as BubbleZoom). The fx-setup child collapses onto the flat
// sw "RadialGradient" tex atom; the 6 decompose helpers are KEPT.
//
// ── WHAT THIS MEASURES ───────────────────────────────────────────────────────────────────────────────
// (STRUCTURE) import → collapsed graph = 6 helpers (3×Vector2Components + 3×BoolToFloat + 1×IntToFloat) + 1
//   RadialGradient atom, NO GradientsToTexture (elided).
// (④d PORT-ORDER) the boundary-fed FloatParams wires (Width, Offset, Noise) + the Image / Gradient fixed
//   slots are asserted wire-by-wire onto the RIGHT atom ports (a positional misconfig would swap them).
// (COOK) wire a real DefineGradient producer (NON-default RED→GREEN) onto the atom's Gradient port; Image
//   UNWIRED → generator mode. The radial params reach the GPU on the UNAUTHORED production path: Center /
//   Stretch / BiasAndGain via the plumbed vec-default collapse (all helpers keep their boundary vec defaults
//   Center=(0,0), Stretch=(1,1), BiasAndGain=(0.5,0.5)); Width / Offset / PingPong / Repeat / PolarOrientation
//   / BlendMode / Noise drop to the sw atom's OWN port defaults (byte-equal to RadialGradient.t3's boundary
//   defaults → benign drop). Nothing carrying parity semantics is hand-authored. Cook resident, read back
//   THREE pins (center / off-center-edge / MID-FIELD) and assert they match the CLOSED-FORM radial oracle
//   (identical math to --selftest-radialgradient's shaderT, already GPU-validated). -bug OMITS the Gradient
//   wire → the near-transparent white→black fallback → the G channel goes gray → green-dominance collapses.
//
// ZONE: runtime golden (shell tier — runtime import/collapse + resident tex cook + closed-form radial oracle).
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

static const char* kRadialGradientT3 =
#include "runtime/radialgradient_t3_embed.inc"
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

// ── CLOSED-FORM RadialGradient.hlsl oracle (verbatim of point_ops_radialgradient.cpp's shaderT, which is
//    GPU-validated by --selftest-radialgradient). Defaults: PolarOrientation=0, Width=1, Offset=0,
//    PingPong=Repeat=0, Stretch=(1,1), Center=(0,0), BiasAndGain=(0.5,0.5), Noise=0. ──
float rgPingPongRepeat(float x, bool pingPong, bool repeat) {
  float baseValue = x + 0.5f;
  float repeatValue = x + 0.5f - std::floor(x + 0.5f);
  float pingPongValue = 1.0f - std::fabs((x * 0.5f - std::floor(x * 0.5f)) * 2.0f - 1.0f);
  float singlePingPong = std::fabs(x);
  float sR = repeat ? 1.0f : 0.0f, sP = pingPong ? 1.0f : 0.0f;
  float pingPongOutput = singlePingPong + (pingPongValue - singlePingPong) * sR;
  float value = baseValue + (repeatValue - baseValue) * sR;
  value = value + (pingPongOutput - value) * sP;
  float sat = std::min(std::max(value, 0.0f), 1.0f);
  value = sat + (value - sat) * sR;
  return value;
}
float rgGetBias(float bias, float x) { return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f); }
float rgGetSchlickBias(float g, float x) {
  if (x < 0.5f) { x *= 2.0f; x = 0.5f * rgGetBias(g, x); }
  else { x = 2.0f * x - 1.0f; x = 0.5f * rgGetBias(1.0f - g, x) + 0.5f; }
  return x;
}
float rgApplyGainAndBias(float value, float gx, float gy) {
  float g = std::min(std::max(gx, 0.0f), 1.0f), b = std::min(std::max(gy, 0.0f), 1.0f);
  if (value > 0.9999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) { value = rgGetBias(b, value); value = rgGetSchlickBias(g, value); }
  else { value = rgGetSchlickBias(g, value); value = rgGetBias(b, value); }
  return value;
}
float shaderT(int px, int py, int W, int H) {
  float uvx = (px + 0.5f) / W, uvy = (py + 0.5f) / H;
  float aspect = (float)W / (float)H;
  float pxc = (uvx - 0.5f) * aspect, pyc = (uvy - 0.5f);
  float r = std::sqrt(pxc * pxc + pyc * pyc);
  float w = std::max(std::fabs(1.0f), 1e-6f);
  float c = r * 2.0f / w - (0.5f + (1.0f - 0.5f) * 0.0f);
  c -= 0.0f;
  c = rgPingPongRepeat(c, false, false);
  c = std::min(std::max(c, 0.001f), 0.999f);
  return rgApplyGainAndBias(c, 0.5f, 0.5f);
}

// RED→GREEN wired gradient (host reference sample(t) = (1-t, t, 0, 1)); distinct from the near-transparent
// white→black unwired fallback (gray at every t) in the G channel — the resident-wire teeth-guard.
SwGradient redGreenGradient() {
  SwGradient g; g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(1.0f, 0.0f, 0.0f, 1.0f)});
  g.steps.push_back({1.0f, simd::make_float4(0.0f, 1.0f, 0.0f, 1.0f)});
  return g;
}

}  // namespace

int runT3RadialGradientParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  // ---- STEP 1: import the REAL RadialGradient.t3 through the PRODUCTION importer (gradient-fed collapse). ----
  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  const bool imported = importT3Symbol(kRadialGradientT3, lib, &rootId, &warnings);
  const Symbol* rsym = imported ? lib.find(rootId) : nullptr;
  const int nChildren = rsym ? (int)rsym->children.size() : -1;
  const int nConns = rsym ? (int)rsym->connections.size() : -1;
  printf("[t3-radialgradient] import: ok=%d rootId=%s children=%d conns=%d warnings=%zu\n",
         imported ? 1 : 0, rootId.c_str(), nChildren, nConns, warnings.size());
  if (rsym) {
    std::map<std::string, int> byType;
    for (const SymbolChild& c : rsym->children) byType[c.symbolId]++;
    printf("[t3-radialgradient]   collapsed children:");
    for (const auto& kv : byType) printf(" %s×%d", kv.first.c_str(), kv.second);
    printf("\n");
  }
  for (const std::string& w : warnings) printf("[t3-radialgradient]   warn: %s\n", w.c_str());

  const int rgId = rsym ? childIdOfType(*rsym, "RadialGradient") : 0;
  const int v2cCount = rsym ? countType(*rsym, "Vector2Components") : 0;
  const int b2fCount = rsym ? countType(*rsym, "BoolToFloat") : 0;
  const int i2fCount = rsym ? countType(*rsym, "IntToFloat") : 0;
  const int gttCount = rsym ? countType(*rsym, "GradientsToTexture") : 0;  // MUST be 0 (elided)
  const bool structOk = imported && rsym && rgId != 0 && v2cCount == 3 && b2fCount == 3 &&
                        i2fCount == 1 && gttCount == 0;
  printf("[t3-radialgradient] SEAM: RadialGradient-atom=%d Vector2Components×%d BoolToFloat×%d IntToFloat×%d "
         "GradientsToTexture×%d(elided) → %s\n",
         rgId, v2cCount, b2fCount, i2fCount, gttCount,
         structOk ? "STRUCTURALLY GREEN" : "FAILED (elision/collapse did not produce the expected graph)");
  if (!structOk) { pool->release(); return injectBug ? 2 : 1; }

  // ---- ④d PORT-ORDER ASSERTION (refuter finding B guard): the boundary-fed FloatParams wires land on the
  // RIGHT atom ports. RadialGradient.t3 wires Width(bfdcfed4)→Width, Offset(98314ae6)→Offset,
  // Noise(f22859b1)→Noise straight from the root. Plus the fixed slots: Image(54bca43c)→Image,
  // Gradient(3f5a284b, GradientsToTexture-elided)→Gradient.
  const bool widthOk  = hasConn(*rsym, kSymbolBoundary, "bfdcfed4-263f-4115-a1a8-291088e34c0a", rgId, "Width");
  const bool offsetOk = hasConn(*rsym, kSymbolBoundary, "98314ae6-b2a9-433b-90e9-931b059ae62e", rgId, "Offset");
  const bool noiseOk  = hasConn(*rsym, kSymbolBoundary, "f22859b1-044b-44c7-8953-ea0f9cee36e8", rgId, "Noise");
  const bool imageOk    = hasConn(*rsym, kSymbolBoundary, "54bca43c-fc2b-4a40-b991-8b76e35eee01", rgId, "Image");
  const bool gradientOk = hasConn(*rsym, kSymbolBoundary, "3f5a284b-e2f0-47e2-bf79-2a7fe8949519", rgId, "Gradient");
  const bool orderOk = widthOk && offsetOk && noiseOk && imageOk && gradientOk;
  printf("[t3-radialgradient] ④d PORT-ORDER: Width=%d Offset=%d Noise=%d Image=%d Gradient(elided)=%d → %s\n",
         widthOk, offsetOk, noiseOk, imageOk, gradientOk,
         orderOk ? "GREEN (each FloatParams/fixed wire on the correct atom port)"
                 : "RED (a FloatParams wire landed on the WRONG atom port — ④d order broken)");
  if (!orderOk) { pool->release(); return injectBug ? 2 : 1; }

  // ---- STEP 2: cook. Wire a real DefineGradient (RED→GREEN) onto the atom's Gradient port; Image UNWIRED
  // (generator mode, IsTextureValid=0 → the shader returns the gradient directly). Nothing carrying radial
  // parity semantics is hand-authored: Center/Stretch/BiasAndGain ride the plumbed vec defaults ((0,0)/(1,1)/
  // (0.5,0.5)); Width/Offset/PingPong/Repeat/PolarOrientation/BlendMode/Noise drop to the sw atom's OWN port
  // defaults (byte-equal to the .t3 boundary defaults → benign drop). Only Resolution/CustomW/CustomH are set
  // (a test-fixture choice for the 64×64 readback, not a parity param).
  Symbol* sym = const_cast<Symbol*>(rsym);
  for (SymbolChild& c : sym->children)
    if (c.id == rgId) {
      c.overrides["Resolution"] = 4.0f; c.overrides["CustomW"] = (float)kW; c.overrides["CustomH"] = (float)kH;
    }

  // DefineGradient producer set to RED→GREEN (non-default), wired onto the RadialGradient atom's Gradient port.
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
  if (!injectBug) sym->connections.push_back({dgId, "out", rgId, "Gradient"});

  ResidentEvalGraph g = buildEvalGraph(lib, rootId);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[t3-radialgradient] FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, mlib, q, kW, kH);
  pg.cookResident(g, ctx, nullptr, std::to_string(rgId));
  MTL::Texture* tex = pg.residentTexFor(std::to_string(rgId));

  bool haveOut = tex && tex->width() == kW && tex->height() == kH;
  std::vector<uint8_t> px((size_t)kW * kH * 4, 0);
  if (haveOut) tex->getBytes(px.data(), kW * 4, MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto pixel = [&](int x, int y, uint8_t out[4]) {
    size_t i = ((size_t)y * kW + x) * 4;
    out[0] = px[i]; out[1] = px[i + 1]; out[2] = px[i + 2]; out[3] = px[i + 3];
  };

  // THREE pins on the horizontal axis (y=32, where the shader's Y-flip is radially symmetric → oracle==GPU):
  //   CENTER (32,32): r≈0 → t≈0.022 → near-RED. Coincidence-safe: near an endpoint.
  //   EDGE (56,32):   r large → t≈0.766 → GREEN-dominant. The teeth-guard pin.
  //   MID-FIELD (48,32): r≈0.246 → t≈0.494 → R≈130,G≈126. The DECISIVE pin — mid radial ramp, where a
  //     stuck/plumb-lost field or a wrong gradient diverges maximally from the smooth oracle.
  const int cx = kW / 2, cy = kH / 2, ex = 56, ey = 32, mx = 48, my = 32;
  uint8_t cpin[4], epin[4], mpin[4]; pixel(cx, cy, cpin); pixel(ex, ey, epin); pixel(mx, my, mpin);
  SwGradient ref = redGreenGradient();
  simd::float4 refC = ref.sample(shaderT(cx, cy, kW, kH));
  simd::float4 refE = ref.sample(shaderT(ex, ey, kW, kH));
  simd::float4 refM = ref.sample(shaderT(mx, my, kW, kH));
  auto near8 = [](int v, float t) { return std::abs(v - (int)std::lround(t * 255.0f)) <= 8; };
  bool cOk = haveOut && near8(cpin[0], refC.x) && near8(cpin[1], refC.y) && near8(cpin[2], refC.z);
  bool eOk = haveOut && near8(epin[0], refE.x) && near8(epin[1], refE.y) && near8(epin[2], refE.z);
  bool mOk = haveOut && near8(mpin[0], refM.x) && near8(mpin[1], refM.y) && near8(mpin[2], refM.z);
  bool greenGuard = haveOut && epin[1] > epin[0] + 40;  // edge G clearly exceeds R → NOT the gray fallback
  bool pass = cOk && eOk && mOk && greenGuard;
  printf("[t3-radialgradient] replay-vs-oracle: haveOut=%d center=(%u,%u,%u) want~(%.0f,%.0f,%.0f) "
         "edge=(%u,%u,%u) want~(%.0f,%.0f,%.0f) MID=(%u,%u,%u) want~(%.0f,%.0f,%.0f) greenGuard=%d injectBug=%d\n",
         haveOut ? 1 : 0, cpin[0], cpin[1], cpin[2], refC.x * 255, refC.y * 255, refC.z * 255,
         epin[0], epin[1], epin[2], refE.x * 255, refE.y * 255, refE.z * 255,
         mpin[0], mpin[1], mpin[2], refM.x * 255, refM.y * 255, refM.z * 255, greenGuard, injectBug ? 1 : 0);

  mlib->release(); q->release(); dev->release();

  if (!injectBug) {
    printf("[t3-radialgradient] PARITY VERDICT: %s\n",
           pass ? "GREEN (RadialGradient.t3 gradient-fed collapse replays to the closed-form radial oracle — "
                  "3×Vector2Components + 3×BoolToFloat + IntToFloat kept, GradientsToTexture elided)"
                : "RED (gradient-fed collapse gap — elision/gradient wire/FloatParams did not reach the atom)");
    pool->release();
    return pass ? 0 : 1;
  }
  const bool bites = !pass;  // bug drops the Gradient wire → white→black fallback → gray → green-guard bites
  printf("[t3-radialgradient] -bug: Gradient-input tooth %s\n", bites ? "BITES" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 2;
}

}  // namespace sw
