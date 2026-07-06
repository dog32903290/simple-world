// runtime/t3import_boxgradient_golden (--selftest-t3-boxgradient) — GRADIENT-FED image GENERATOR collapse
// proof, and the FOUR-COMPONENT-corner proof of the family: a Vector4Components helper decomposes the
// CornersRadius vec4 boundary into 4 scalars on the ④d FloatParams rail (the other two gradients only use
// Vector2Components). Same GradientsToTexture-elided seam as BubbleZoom/RadialGradient/NGon.
//
// ── THE SEAM ─────────────────────────────────────────────────────────────────────────────────────────
// BoxGradient.t3 has 9 children: the fx-setup child (cc34a183), a GradientsToTexture (2c53eee7) → ImageB,
// THREE Vector2Components (Center/Size/GainAndBias), ONE Vector4Components (CornersRadius), TWO BoolToFloat
// (PingPong/Repeat), one IntToFloat (BlendMode). fx-setup collapses onto the flat sw "BoxGradient" atom;
// helpers kept; GTT elided onto the atom's Gradient port.
//
// ── WHAT THIS MEASURES ───────────────────────────────────────────────────────────────────────────────
// (STRUCTURE) collapsed graph = 3×Vector2Components + 1×Vector4Components + 2×BoolToFloat + 1×IntToFloat +
//   1 BoxGradient atom, NO GradientsToTexture (elided).
// (④d PORT-ORDER) the boundary-fed scalar wires (Rotation/UniformScale/GradientWidth/Offset) + Image /
//   Gradient fixed slots asserted wire-by-wire onto the RIGHT atom ports.
// (COOK) wire RED→GREEN DefineGradient onto the Gradient port; Image UNWIRED → generator mode. Nothing
//   parity-bearing is hand-authored: Center/Size/GainAndBias (V2C) + CornersRadius (V4C) ride the plumbed
//   vec defaults; Rotation/UniformScale/GradientWidth/Offset/PingPong/Repeat/BlendMode drop to the sw atom's
//   OWN port defaults (byte-equal to BoxGradient.t3's boundary defaults — incl. PingPong=TRUE, Size=(.25,.25)
//   → benign). Cook resident, read THREE pins vs the CLOSED-FORM box-SDF oracle (verbatim of
//   --selftest-boxgradient's shaderT, already GPU-validated). -bug OMITS the Gradient wire → white→black
//   fallback → GRAY (R=G=B) → the B≈0 teeth-guard collapses (the box field is PingPong-compressed to
//   t∈[~0.08,~0.65], so a green-DOMINANCE guard is too weak; the blue channel is the clean discriminator —
//   the wired red→green has B=0 everywhere, the gray fallback has B==R==G).
//
// ZONE: runtime golden (shell tier — runtime import/collapse + resident tex cook + closed-form box oracle).
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

static const char* kBoxGradientT3 =
#include "runtime/boxgradient_t3_embed.inc"
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

// ── CLOSED-FORM BoxGradient.hlsl oracle (verbatim of point_ops_boxgradient.cpp's shaderT, GPU-validated by
//    --selftest-boxgradient). Defaults: Center=0, Size=(0.25,0.25), CornersRadius=0, Rotation=0,
//    UniformScale=1, Width=1, Offset=0, PingPong=1(TRUE), Repeat=0, GainAndBias=(0.5,0.5). aspect=1. ──
float bgPingPongRepeat(float x, float pingPong, float repeat) {
  float baseValue = x;
  float repeatValue = x - std::floor(x);
  float pingPongValue = 1.0f - std::fabs((x * 0.5f - std::floor(x * 0.5f)) * 2.0f - 1.0f);
  float singlePingPong = std::fabs(x);
  float sR = (repeat >= 0.5f) ? 1.0f : 0.0f, sP = (pingPong >= 0.5f) ? 1.0f : 0.0f;
  float pingPongOutput = singlePingPong + (pingPongValue - singlePingPong) * sR;
  float value = baseValue + (repeatValue - baseValue) * sR;
  value = value + (pingPongOutput - value) * sP;
  float sat = std::min(std::max(value, 0.0f), 1.0f);
  value = sat + (value - sat) * sR;
  return value;
}
float bgGetBias(float bias, float x) { return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f); }
float bgGetSchlickBias(float g, float x) {
  if (x < 0.5f) { x *= 2.0f; x = 0.5f * bgGetBias(g, x); }
  else { x = 2.0f * x - 1.0f; x = 0.5f * bgGetBias(1.0f - g, x) + 0.5f; }
  return x;
}
float bgApplyGainAndBias(float value, float gx, float gy) {
  float g = std::min(std::max(gx, 0.0f), 1.0f), b = std::min(std::max(gy, 0.0f), 1.0f);
  if (value > 0.9999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) { value = bgGetBias(b, value); value = bgGetSchlickBias(g, value); }
  else { value = bgGetSchlickBias(g, value); value = bgGetBias(b, value); }
  return value;
}
float bgSdRoundedBox(float px, float py, float bx, float by) {
  float qx = std::fabs(px) - bx, qy = std::fabs(py) - by;
  float outside = std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) +
                            std::max(qy, 0.0f) * std::max(qy, 0.0f));
  return std::min(std::max(qx, qy), 0.0f) + outside;
}
float shaderT(int px, int py, int W, int H) {
  float uvx = (px + 0.5f) / W, uvy = (py + 0.5f) / H;
  float aspect = (float)W / (float)H;
  float pxc = uvx - 0.5f, pyc = uvy - 0.5f;
  pxc *= aspect;
  float rpx = pxc, rpy = -pyc;  // rotatePoint(p, 0) → (p.x, -p.y)
  float c = bgSdRoundedBox(rpx, rpy, 0.25f, 0.25f) * 2.0f - 0.0f;
  c = bgPingPongRepeat(c / 1.0f, 1.0f, 0.0f);  // Width=1, PingPong=1, Repeat=0
  float dBiased = bgApplyGainAndBias(c, 0.5f, 0.5f);
  return std::min(std::max(dBiased, 0.001f), 0.999f);
}

SwGradient redGreenGradient() {
  SwGradient g; g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(1.0f, 0.0f, 0.0f, 1.0f)});
  g.steps.push_back({1.0f, simd::make_float4(0.0f, 1.0f, 0.0f, 1.0f)});
  return g;
}

}  // namespace

int runT3BoxGradientParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  const bool imported = importT3Symbol(kBoxGradientT3, lib, &rootId, &warnings);
  const Symbol* rsym = imported ? lib.find(rootId) : nullptr;
  printf("[t3-boxgradient] import: ok=%d rootId=%s children=%d conns=%d warnings=%zu\n",
         imported ? 1 : 0, rootId.c_str(), rsym ? (int)rsym->children.size() : -1,
         rsym ? (int)rsym->connections.size() : -1, warnings.size());
  if (rsym) {
    std::map<std::string, int> byType;
    for (const SymbolChild& c : rsym->children) byType[c.symbolId]++;
    printf("[t3-boxgradient]   collapsed children:");
    for (const auto& kv : byType) printf(" %s×%d", kv.first.c_str(), kv.second);
    printf("\n");
  }
  for (const std::string& w : warnings) printf("[t3-boxgradient]   warn: %s\n", w.c_str());

  const int bxId = rsym ? childIdOfType(*rsym, "BoxGradient") : 0;
  const int v2cCount = rsym ? countType(*rsym, "Vector2Components") : 0;
  const int v4cCount = rsym ? countType(*rsym, "Vector4Components") : 0;
  const int b2fCount = rsym ? countType(*rsym, "BoolToFloat") : 0;
  const int i2fCount = rsym ? countType(*rsym, "IntToFloat") : 0;
  const int gttCount = rsym ? countType(*rsym, "GradientsToTexture") : 0;  // MUST be 0 (elided)
  const bool structOk = imported && rsym && bxId != 0 && v2cCount == 3 && v4cCount == 1 &&
                        b2fCount == 2 && i2fCount == 1 && gttCount == 0;
  printf("[t3-boxgradient] SEAM: BoxGradient-atom=%d Vector2Components×%d Vector4Components×%d BoolToFloat×%d "
         "IntToFloat×%d GradientsToTexture×%d(elided) → %s\n",
         bxId, v2cCount, v4cCount, b2fCount, i2fCount, gttCount,
         structOk ? "STRUCTURALLY GREEN" : "FAILED (elision/collapse did not produce the expected graph)");
  if (!structOk) { pool->release(); return injectBug ? 2 : 1; }

  // ---- ④d PORT-ORDER: the boundary-fed scalar wires land on the RIGHT atom ports. BoxGradient.t3 wires
  // Rotation(1dd6cf1e)→Rotation, UniformScale(5a164383)→UniformScale, GradientWidth(94d46ad8)→GradientWidth,
  // Offset(c3d48bd0)→Offset straight from the root. (Center/Size/CornersRadius/GainAndBias arrive via the
  // V2C/V4C helpers.) Plus fixed slots Image(71cd9153)→Image, Gradient(5e7cd523, GTT-elided)→Gradient.
  const bool rotOk    = hasConn(*rsym, kSymbolBoundary, "1dd6cf1e-d374-40d5-b51f-21b91deb3802", bxId, "Rotation");
  const bool uscaleOk = hasConn(*rsym, kSymbolBoundary, "5a164383-4f9d-4978-bfcb-a1c48b9b8f34", bxId, "UniformScale");
  const bool gwidthOk = hasConn(*rsym, kSymbolBoundary, "94d46ad8-7dd6-490c-ade4-a527c7ee9d05", bxId, "GradientWidth");
  const bool offsetOk = hasConn(*rsym, kSymbolBoundary, "c3d48bd0-5153-4631-ae6c-a7ded46ce952", bxId, "Offset");
  const bool imageOk    = hasConn(*rsym, kSymbolBoundary, "71cd9153-da17-47f0-9251-80bdef8906b3", bxId, "Image");
  const bool gradientOk = hasConn(*rsym, kSymbolBoundary, "5e7cd523-0c39-42e4-a4e9-05cc20477296", bxId, "Gradient");
  const bool orderOk = rotOk && uscaleOk && gwidthOk && offsetOk && imageOk && gradientOk;
  printf("[t3-boxgradient] ④d PORT-ORDER: Rotation=%d UniformScale=%d GradientWidth=%d Offset=%d Image=%d "
         "Gradient(elided)=%d → %s\n",
         rotOk, uscaleOk, gwidthOk, offsetOk, imageOk, gradientOk,
         orderOk ? "GREEN (each FloatParams/fixed wire on the correct atom port)"
                 : "RED (a FloatParams wire landed on the WRONG atom port — ④d order broken)");
  if (!orderOk) { pool->release(); return injectBug ? 2 : 1; }

  // ---- STEP 2: cook. Wire RED→GREEN DefineGradient onto the Gradient port; Image UNWIRED (generator mode).
  Symbol* sym = const_cast<Symbol*>(rsym);
  for (SymbolChild& c : sym->children)
    if (c.id == bxId) {
      c.overrides["Resolution"] = 4.0f; c.overrides["CustomW"] = (float)kW; c.overrides["CustomH"] = (float)kH;
    }
  if (!lib.symbols.count("DefineGradient"))
    if (const NodeSpec* fs = findSpec("DefineGradient"))
      lib.symbols["DefineGradient"] = atomicSymbolFromSpec(*fs);
  const int dgId = sym->nextChildId++;
  { SymbolChild p; p.id = dgId; p.symbolId = "DefineGradient";
    p.overrides["Color1.x"] = 1.0f; p.overrides["Color1.y"] = 0.0f; p.overrides["Color1.z"] = 0.0f; p.overrides["Color1.w"] = 1.0f;
    p.overrides["Color1Pos"] = 0.0f;
    p.overrides["Color2.x"] = 0.0f; p.overrides["Color2.y"] = 1.0f; p.overrides["Color2.z"] = 0.0f; p.overrides["Color2.w"] = 1.0f;
    p.overrides["Color2Pos"] = 1.0f; p.overrides["Interpolation"] = 0.0f;
    sym->children.push_back(p); }
  if (!injectBug) sym->connections.push_back({dgId, "out", bxId, "Gradient"});

  ResidentEvalGraph g = buildEvalGraph(lib, rootId);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[t3-boxgradient] FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, mlib, q, kW, kH);
  pg.cookResident(g, ctx, nullptr, std::to_string(bxId));
  MTL::Texture* tex = pg.residentTexFor(std::to_string(bxId));

  bool haveOut = tex && tex->width() == kW && tex->height() == kH;
  std::vector<uint8_t> px((size_t)kW * kH * 4, 0);
  if (haveOut) tex->getBytes(px.data(), kW * 4, MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto pixel = [&](int x, int y, uint8_t out[4]) {
    size_t i = ((size_t)y * kW + x) * 4;
    out[0] = px[i]; out[1] = px[i + 1]; out[2] = px[i + 2]; out[3] = px[i + 3];
  };

  // THREE pins spanning the PingPong-compressed box field (t∈[~0.08,~0.65]):
  //   RED (15,13):    just inside the box wall → t≈0.08 → strong RED (235,20).
  //   MID (32,32):    box center → t≈0.484 → R≈132,G≈124. The decisive equilibrium pin.
  //   GREEN (2,0):    corner → t≈0.642 → green-leaning (91,164). Distinct from the other two.
  // All three have B≈0 (the wired red→green has zero blue) — the teeth-guard, since the white→black
  // fallback is gray (B==R==G≠0).
  const int rx = 15, ry = 13, mx = 32, my = 32, gx = 2, gy = 0;
  uint8_t rpin[4], mpin[4], gpin[4]; pixel(rx, ry, rpin); pixel(mx, my, mpin); pixel(gx, gy, gpin);
  SwGradient ref = redGreenGradient();
  simd::float4 refR = ref.sample(shaderT(rx, ry, kW, kH));
  simd::float4 refM = ref.sample(shaderT(mx, my, kW, kH));
  simd::float4 refG = ref.sample(shaderT(gx, gy, kW, kH));
  auto near8 = [](int v, float t) { return std::abs(v - (int)std::lround(t * 255.0f)) <= 8; };
  bool rOk = haveOut && near8(rpin[0], refR.x) && near8(rpin[1], refR.y) && near8(rpin[2], refR.z);
  bool mOk = haveOut && near8(mpin[0], refM.x) && near8(mpin[1], refM.y) && near8(mpin[2], refM.z);
  bool gOk = haveOut && near8(gpin[0], refG.x) && near8(gpin[1], refG.y) && near8(gpin[2], refG.z);
  // B≈0 teeth-guard: the wired red→green gradient has zero blue everywhere; the white→black fallback is
  // gray (B==R==G, clearly nonzero at every pin). All three pins' blue must be low.
  bool blueGuard = haveOut && rpin[2] < 20 && mpin[2] < 20 && gpin[2] < 20;
  bool pass = rOk && mOk && gOk && blueGuard;
  printf("[t3-boxgradient] replay-vs-oracle: haveOut=%d red=(%u,%u,%u) want~(%.0f,%.0f,%.0f) "
         "mid=(%u,%u,%u) want~(%.0f,%.0f,%.0f) green=(%u,%u,%u) want~(%.0f,%.0f,%.0f) blueGuard=%d injectBug=%d\n",
         haveOut ? 1 : 0, rpin[0], rpin[1], rpin[2], refR.x * 255, refR.y * 255, refR.z * 255,
         mpin[0], mpin[1], mpin[2], refM.x * 255, refM.y * 255, refM.z * 255,
         gpin[0], gpin[1], gpin[2], refG.x * 255, refG.y * 255, refG.z * 255, blueGuard, injectBug ? 1 : 0);

  mlib->release(); q->release(); dev->release();

  if (!injectBug) {
    printf("[t3-boxgradient] PARITY VERDICT: %s\n",
           pass ? "GREEN (BoxGradient.t3 gradient-fed collapse replays to the closed-form box-SDF oracle — "
                  "3×Vector2Components + Vector4Components(CornersRadius) + 2×BoolToFloat + IntToFloat kept, "
                  "GradientsToTexture elided)"
                : "RED (gradient-fed collapse gap — elision/gradient wire/FloatParams did not reach the atom)");
    pool->release();
    return pass ? 0 : 1;
  }
  const bool bites = !pass;
  printf("[t3-boxgradient] -bug: Gradient-input tooth %s\n", bites ? "BITES" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 0;  // dead tooth exits 0 → --bite NO-BITE list catches it (GOLDEN_STANDARD 特徵3)
}

}  // namespace sw
