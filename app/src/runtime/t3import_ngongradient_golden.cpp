// runtime/t3import_ngongradient_golden (--selftest-t3-ngongradient) — GRADIENT-FED image GENERATOR
// collapse proof. Same seam as BubbleZoom/RadialGradient (GradientsToTexture ELIDED onto the atom's
// Gradient port). NGonGradient's ④d rail is the LONGEST boundary-scalar run of the three (8 boundary
// FloatParams scalars: Sides/Radius/Curvature/Blades/Roundness/Rotate/Width/Offset), so this golden is the
// strongest positional-order stress of the family.
//
// ── THE SEAM ─────────────────────────────────────────────────────────────────────────────────────────
// NGonGradient.t3 has 7 children: the fx-setup child (cc34a183), a GradientsToTexture (2c53eee7) → ImageB,
// TWO Vector2Components (Position/BiasAndGain), TWO BoolToFloat (PingPong/Repeat), one IntToFloat (BlendMode).
// The fx-setup child collapses onto the flat sw "NGonGradient" tex atom; helpers kept; GTT elided onto the
// atom's Gradient port.
//
// ── WHAT THIS MEASURES ───────────────────────────────────────────────────────────────────────────────
// (STRUCTURE) collapsed graph = 2×Vector2Components + 2×BoolToFloat + 1×IntToFloat + 1 NGonGradient atom,
//   NO GradientsToTexture (elided).
// (④d PORT-ORDER) the 8 boundary-fed FloatParams wires (Sides/Radius/Curvature/Blades/Roundness/Rotate/
//   Width/Offset) + Image / Gradient fixed slots asserted wire-by-wire onto the RIGHT atom ports.
// (COOK) wire a real DefineGradient (RED→GREEN) onto the Gradient port; Image UNWIRED → generator mode.
//   Nothing parity-bearing is hand-authored: Position/BiasAndGain ride the plumbed vec defaults; the 8
//   NGon scalars drop to the sw atom's OWN port defaults (byte-equal to NGonGradient.t3's boundary defaults
//   Sides=5, Radius=0.33, Curvature=0, Blades=0, Roundness=1, Rotate=180, Width=0.14, Offset=0 → benign).
//   Cook resident, read THREE pins (center / corner / MID-FIELD-on-edge-ramp) vs the CLOSED-FORM NGon-SDF
//   oracle (verbatim of --selftest-ngongradient's ngonGradientT, already GPU-validated). -bug OMITS the
//   Gradient wire → white→black fallback → gray → green-dominance collapses.
//
// ZONE: runtime golden (shell tier — runtime import/collapse + resident tex cook + closed-form NGon oracle).
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

static const char* kNGonGradientT3 =
#include "runtime/ngongradient_t3_embed.inc"
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

// ── CLOSED-FORM NGonGradient.hlsl oracle (verbatim of point_ops_ngongradient.cpp's ngonGradientT, which is
//    GPU-validated by --selftest-ngongradient). Defaults: Rotate=180, Radius=0.33, Sides=5, Blades=0,
//    Curvature=0, Roundness=1, Width=0.14, Position=(0,0), BiasAndGain=(0.5,0.5), PingPong=Repeat=0. ──
float ngFmod(float x, float y) { return x - y * std::floor(x / y); }
float ngGetBias(float bias, float x) { return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f); }
float ngGetSchlickBias(float g, float x) {
  if (x < 0.5f) { x *= 2.0f; x = 0.5f * ngGetBias(g, x); }
  else { x = 2.0f * x - 1.0f; x = 0.5f * ngGetBias(1.0f - g, x) + 0.5f; }
  return x;
}
float ngApplyGainAndBias(float value, float gx, float gy) {
  float g = std::min(std::max(gx, 0.0f), 1.0f), b = std::min(std::max(gy, 0.0f), 1.0f);
  if (value > 0.9999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) { value = ngGetBias(b, value); value = ngGetSchlickBias(g, value); }
  else { value = ngGetSchlickBias(g, value); value = ngGetBias(b, value); }
  return value;
}
float ngPingPongRepeat(float x, bool pingPong, bool repeat) {
  float baseValue = x;
  float repeatValue = x - std::floor(x);
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
float ngSdRegularPolygon(float px, float py, float r, float n,
                         float blades, float curvature, float roundness) {
  float an = 3.141593f / n;
  float acsx = std::cos(an), acsy = std::sin(an);
  float originalLen = std::sqrt(px * px + py * py);
  float bn = ngFmod(std::atan2(px, py), 2.0f * an) - an;
  bn *= bn > 0.0f ? (1.0f - std::min(std::max(blades, 0.0f), 1.0f)) : 1.0f;
  float len = std::sqrt(px * px + py * py);
  px = len * std::cos(bn);
  py = len * std::fabs(std::sin(bn));
  px -= r * acsx;
  py -= r * acsy;
  py += std::min(std::max(-py, 0.0f), r * acsy);
  py *= py > 0.0f ? (std::min(std::max(roundness, 0.0f), 1.0f)) : 1.0f;
  float dist = std::sqrt(px * px + py * py) * (px > 0.0f ? 1.0f : (px < 0.0f ? -1.0f : 0.0f));
  dist += (r - originalLen) * curvature;
  return dist;
}
float shaderT(int px, int py, int W, int H) {
  float uvx = (px + 0.5f) / W, uvy = (py + 0.5f) / H;
  float aspect = (float)W / (float)H;
  float pcx = uvx - 0.5f, pcy = uvy - 0.5f;
  pcx *= aspect;
  float ang = 180.0f * (3.14159265358979323846f / 180.0f);
  float ca = std::cos(ang), sa = std::sin(ang);
  float rx = pcx * ca + pcy * sa;
  float ry = pcx * sa - pcy * ca;
  float c = ngSdRegularPolygon(rx, ry, 0.33f, 5.0f, 0.0f, 0.0f, 1.0f) * 2.0f - 0.0f * 0.14f;
  c = ngPingPongRepeat(c / 0.14f, false, false);
  float dBiased = ngApplyGainAndBias(c, 0.5f, 0.5f);
  return std::min(std::max(dBiased, 0.001f), 0.999f);
}

SwGradient redGreenGradient() {
  SwGradient g; g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(1.0f, 0.0f, 0.0f, 1.0f)});
  g.steps.push_back({1.0f, simd::make_float4(0.0f, 1.0f, 0.0f, 1.0f)});
  return g;
}

}  // namespace

int runT3NGonGradientParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  const bool imported = importT3Symbol(kNGonGradientT3, lib, &rootId, &warnings);
  const Symbol* rsym = imported ? lib.find(rootId) : nullptr;
  printf("[t3-ngongradient] import: ok=%d rootId=%s children=%d conns=%d warnings=%zu\n",
         imported ? 1 : 0, rootId.c_str(), rsym ? (int)rsym->children.size() : -1,
         rsym ? (int)rsym->connections.size() : -1, warnings.size());
  if (rsym) {
    std::map<std::string, int> byType;
    for (const SymbolChild& c : rsym->children) byType[c.symbolId]++;
    printf("[t3-ngongradient]   collapsed children:");
    for (const auto& kv : byType) printf(" %s×%d", kv.first.c_str(), kv.second);
    printf("\n");
  }
  for (const std::string& w : warnings) printf("[t3-ngongradient]   warn: %s\n", w.c_str());

  const int ngId = rsym ? childIdOfType(*rsym, "NGonGradient") : 0;
  const int v2cCount = rsym ? countType(*rsym, "Vector2Components") : 0;
  const int b2fCount = rsym ? countType(*rsym, "BoolToFloat") : 0;
  const int i2fCount = rsym ? countType(*rsym, "IntToFloat") : 0;
  const int gttCount = rsym ? countType(*rsym, "GradientsToTexture") : 0;  // MUST be 0 (elided)
  const bool structOk = imported && rsym && ngId != 0 && v2cCount == 2 && b2fCount == 2 &&
                        i2fCount == 1 && gttCount == 0;
  printf("[t3-ngongradient] SEAM: NGonGradient-atom=%d Vector2Components×%d BoolToFloat×%d IntToFloat×%d "
         "GradientsToTexture×%d(elided) → %s\n",
         ngId, v2cCount, b2fCount, i2fCount, gttCount,
         structOk ? "STRUCTURALLY GREEN" : "FAILED (elision/collapse did not produce the expected graph)");
  if (!structOk) { pool->release(); return injectBug ? 2 : 1; }

  // ---- ④d PORT-ORDER: the 8 boundary-fed FloatParams scalars land on the RIGHT atom ports. NGonGradient.t3
  // wires each straight from the root boundary. (Position/BiasAndGain arrive via Vector2Components — those
  // wires' source is the helper child, not the boundary, so only the 8 boundary scalars are asserted here.)
  const bool sidesOk   = hasConn(*rsym, kSymbolBoundary, "15a72217-4a3e-4e03-a767-0494ea1943f7", ngId, "Sides");
  const bool radiusOk  = hasConn(*rsym, kSymbolBoundary, "b96527a6-0392-4e50-aa17-564976141290", ngId, "Radius");
  const bool curvOk    = hasConn(*rsym, kSymbolBoundary, "1264ebb1-f953-46f9-9915-d0fca5a72aa8", ngId, "Curvature");
  const bool bladesOk  = hasConn(*rsym, kSymbolBoundary, "d3cf9f75-ec08-4162-bad6-514173ef974c", ngId, "Blades");
  const bool roundOk   = hasConn(*rsym, kSymbolBoundary, "3493fb54-55a1-497d-8eb0-4a8fa5b92a9c", ngId, "Roundness");
  const bool rotateOk  = hasConn(*rsym, kSymbolBoundary, "49066387-92a6-46b2-a471-be0104e70651", ngId, "Rotate");
  const bool widthOk   = hasConn(*rsym, kSymbolBoundary, "134a9879-54f7-4b9c-8494-195a159d6428", ngId, "Width");
  const bool offsetOk  = hasConn(*rsym, kSymbolBoundary, "d926fc8b-c42d-4880-8cfc-f940f7dafb03", ngId, "Offset");
  const bool imageOk    = hasConn(*rsym, kSymbolBoundary, "3bc236ab-c5f8-4dee-b933-84cf627118ef", ngId, "Image");
  const bool gradientOk = hasConn(*rsym, kSymbolBoundary, "08937f41-a722-4d5b-8cf6-0b7d48323af4", ngId, "Gradient");
  const bool orderOk = sidesOk && radiusOk && curvOk && bladesOk && roundOk && rotateOk && widthOk &&
                       offsetOk && imageOk && gradientOk;
  printf("[t3-ngongradient] ④d PORT-ORDER: Sides=%d Radius=%d Curvature=%d Blades=%d Roundness=%d Rotate=%d "
         "Width=%d Offset=%d Image=%d Gradient(elided)=%d → %s\n",
         sidesOk, radiusOk, curvOk, bladesOk, roundOk, rotateOk, widthOk, offsetOk, imageOk, gradientOk,
         orderOk ? "GREEN (each FloatParams/fixed wire on the correct atom port)"
                 : "RED (a FloatParams wire landed on the WRONG atom port — ④d order broken)");
  if (!orderOk) { pool->release(); return injectBug ? 2 : 1; }

  // ---- STEP 2: cook. Wire RED→GREEN DefineGradient onto the Gradient port; Image UNWIRED (generator mode).
  Symbol* sym = const_cast<Symbol*>(rsym);
  for (SymbolChild& c : sym->children)
    if (c.id == ngId) {
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
  if (!injectBug) sym->connections.push_back({dgId, "out", ngId, "Gradient"});

  ResidentEvalGraph g = buildEvalGraph(lib, rootId);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[t3-ngongradient] FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, mlib, q, kW, kH);
  pg.cookResident(g, ctx, nullptr, std::to_string(ngId));
  MTL::Texture* tex = pg.residentTexFor(std::to_string(ngId));

  bool haveOut = tex && tex->width() == kW && tex->height() == kH;
  std::vector<uint8_t> px((size_t)kW * kH * 4, 0);
  if (haveOut) tex->getBytes(px.data(), kW * 4, MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto pixel = [&](int x, int y, uint8_t out[4]) {
    size_t i = ((size_t)y * kW + x) * 4;
    out[0] = px[i]; out[1] = px[i + 1]; out[2] = px[i + 2]; out[3] = px[i + 3];
  };

  // THREE pins:
  //   CENTER (32,32): inside the pentagon → t≈0.001 → near-RED.
  //   CORNER (2,2):   far outside → t≈0.999 → full GREEN (the teeth-guard pin, off-axis).
  //   MID-FIELD (48,18): on the polygon edge ramp → t≈0.62 → R≈97,G≈158. The DECISIVE pin — a wrong
  //     gradient or a plumb-lost field diverges maximally here (not clamped at either end).
  const int cx = 32, cy = 32, ox = 2, oy = 2, mx = 48, my = 18;
  uint8_t cpin[4], opin[4], mpin[4]; pixel(cx, cy, cpin); pixel(ox, oy, opin); pixel(mx, my, mpin);
  SwGradient ref = redGreenGradient();
  simd::float4 refC = ref.sample(shaderT(cx, cy, kW, kH));
  simd::float4 refO = ref.sample(shaderT(ox, oy, kW, kH));
  simd::float4 refM = ref.sample(shaderT(mx, my, kW, kH));
  auto near8 = [](int v, float t) { return std::abs(v - (int)std::lround(t * 255.0f)) <= 8; };
  bool cOk = haveOut && near8(cpin[0], refC.x) && near8(cpin[1], refC.y) && near8(cpin[2], refC.z);
  bool oOk = haveOut && near8(opin[0], refO.x) && near8(opin[1], refO.y) && near8(opin[2], refO.z);
  bool mOk = haveOut && near8(mpin[0], refM.x) && near8(mpin[1], refM.y) && near8(mpin[2], refM.z);
  bool greenGuard = haveOut && opin[1] > opin[0] + 40;  // corner G clearly exceeds R → NOT the gray fallback
  bool pass = cOk && oOk && mOk && greenGuard;
  printf("[t3-ngongradient] replay-vs-oracle: haveOut=%d center=(%u,%u,%u) want~(%.0f,%.0f,%.0f) "
         "corner=(%u,%u,%u) want~(%.0f,%.0f,%.0f) MID=(%u,%u,%u) want~(%.0f,%.0f,%.0f) greenGuard=%d injectBug=%d\n",
         haveOut ? 1 : 0, cpin[0], cpin[1], cpin[2], refC.x * 255, refC.y * 255, refC.z * 255,
         opin[0], opin[1], opin[2], refO.x * 255, refO.y * 255, refO.z * 255,
         mpin[0], mpin[1], mpin[2], refM.x * 255, refM.y * 255, refM.z * 255, greenGuard, injectBug ? 1 : 0);

  mlib->release(); q->release(); dev->release();

  if (!injectBug) {
    printf("[t3-ngongradient] PARITY VERDICT: %s\n",
           pass ? "GREEN (NGonGradient.t3 gradient-fed collapse replays to the closed-form NGon-SDF oracle — "
                  "2×Vector2Components + 2×BoolToFloat + IntToFloat kept, GradientsToTexture elided)"
                : "RED (gradient-fed collapse gap — elision/gradient wire/FloatParams did not reach the atom)");
    pool->release();
    return pass ? 0 : 1;
  }
  const bool bites = !pass;
  printf("[t3-ngongradient] -bug: Gradient-input tooth %s\n", bites ? "BITES" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 0;  // dead tooth exits 0 → --bite NO-BITE list catches it (GOLDEN_STANDARD 特徵3)
}

}  // namespace sw
