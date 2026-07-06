// runtime/t3import_lineargradient_golden (--selftest-t3-lineargradient) — GRADIENT-FED image GENERATOR
// collapse proof + the OFFSET-ROUTING-SUBGRAPH ELISION first证. Same GradientsToTexture-elided-to-Gradient-
// port seam as the gradient trio, but LinearGradient.t3 also carries an Offset-routing subgraph: Multiply
// (Width×Offset) + PickFloat (OffsetMode-select [Offset, Width×Offset]) feed the fx FloatParams[8]=Offset.
// sw's LinearGradient atom (point_ops_lineargradient.cpp:129-133) REIMPLEMENTS that selection internally
// from its own Offset + OffsetMode ports, so the subgraph is redundant → ELIDED (fork
// offset-routing-subgraph-elided-atom-reimplements): PickFloat.out re-anchors to its raw-Offset FIRST
// FloatValues source (→ atom.Offset via the FloatParams rail), and the OffsetMode boundary is re-anchored
// onto the atom's OWN OffsetMode port. (Keeping the subgraph would double-route AND the dropped
// boundary→Multiply wires would poison it — Multiply reads a=b=1 default → Offset=1 → the ramp saturates.)
//
// ── THE SEAM THIS PROVES ─────────────────────────────────────────────────────────────────────────────
// LinearGradient.t3 (byte-embedded) has 9 children: the fx-setup framework child (_multiImageFxSetupStatic,
// cc34a183), a GradientsToTexture (2c53eee7) rendering the Gradient boundary → ImageB(t1), TWO
// Vector2Components (Center/GainAndBias vec2 → X/Y), TWO BoolToFloat (PingPong/Repeat), TWO IntToFloat
// (SizeMode/BlendMode), a Multiply (Width×Offset) and a PickFloat (OffsetMode select). The collapse ELIDES
// the GTT + the Multiply/PickFloat offset subgraph; the fx child collapses onto the flat sw "LinearGradient"
// tex atom; the 6 decompose helper value ops (2×V2C + 2×B2F + 2×I2F) are KEPT.
//
// ── WHAT THIS MEASURES ───────────────────────────────────────────────────────────────────────────────
// (STRUCTURE) import → collapsed graph = 8 helpers (2×Vector2Components + 2×BoolToFloat + 2×IntToFloat +
//   Multiply + PickFloat) + 1 LinearGradient atom, NO GradientsToTexture (elided).
// (④d PORT-ORDER) the boundary-fed FloatParams wires (Width, Rotate) + the Image / Gradient fixed slots are
//   asserted wire-by-wire onto the RIGHT atom ports (a positional misconfig would swap them).
// (COOK) wire a real DefineGradient producer (NON-default RED→GREEN) onto the atom's Gradient port; Image
//   UNWIRED → generator mode. Center/GainAndBias ride the plumbed vec defaults ((0,0)/(0.5,0.5)); Width /
//   Rotate / PingPong / Repeat / Offset / SizeMode / BlendMode drop to the sw atom's OWN port defaults
//   (byte-equal to LinearGradient.t3's boundary defaults → benign drop). Cook resident, read back a LEFT
//   pin, a RIGHT pin, and a MID-FIELD pin on the Rotate=90 horizontal ramp, and assert each matches the
//   CLOSED-FORM LinearGradient oracle (identical math to --selftest-lineargradient's shaderT, GPU-validated).
//   -bug OMITS the Gradient wire → near-black→white fallback → the G channel goes gray → green-dominance collapses.
//
// ZONE: runtime golden (shell tier — runtime import/collapse + resident tex cook + closed-form LinearGradient oracle).
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

static const char* kLinearGradientT3 =
#include "runtime/lineargradient_t3_embed.inc"
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

// ── CLOSED-FORM LinearGradient.hlsl oracle (verbatim of gradient_golden.cpp's shaderT, which is GPU-
//    validated by --selftest-lineargradient). Defaults: Rotate=90, Center=0, Offset=0, Width=1, SizeMode=0,
//    GainAndBias=(0.5,0.5), PingPong=Repeat=false. Returns the gradient-sample t (= clamped dBiased). ──
float lgPingPongRepeat(float x, bool pingPong, bool repeat) {
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
float lgGetBias(float bias, float x) { return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f); }
float lgGetSchlickBias(float g, float x) {
  if (x < 0.5f) { x *= 2.0f; x = 0.5f * lgGetBias(g, x); }
  else { x = 2.0f * x - 1.0f; x = 0.5f * lgGetBias(1.0f - g, x) + 0.5f; }
  return x;
}
float lgApplyGainAndBias(float value, float gx, float gy) {
  float g = std::min(std::max(gx, 0.0f), 1.0f), b = std::min(std::max(gy, 0.0f), 1.0f);
  if (value > 0.9999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) { value = lgGetBias(b, value); value = lgGetSchlickBias(g, value); }
  else { value = lgGetSchlickBias(g, value); value = lgGetBias(b, value); }
  return value;
}
float shaderT(int px, int py, int W, int H) {
  float uvx = (px + 0.5f) / W, uvy = (py + 0.5f) / H;
  float aspect = (float)W / (float)H;       // square → 1
  float pxc = uvx - 0.5f, pyc = uvy - 0.5f;
  pxc *= aspect;                            // SizeMode=0 → p.x *= aspect
  float radians = 90.0f / 180.0f * 3.141578f;
  float ax = std::sin(radians), ay = std::cos(radians);
  float c = pxc * ax + pyc * ay;           // dot(p - Center(0), angle)
  c += 0.0f;                               // Offset=0 (PickFloat routing → 0 at defaults)
  c = lgPingPongRepeat(c / 1.0f, false, false);  // Width=1
  float sat = std::min(std::max(c, 0.0f), 1.0f);
  float dBiased = lgApplyGainAndBias(sat, 0.5f, 0.5f);
  return std::min(std::max(dBiased, 0.000001f), 0.99999f);
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

int runT3LinearGradientParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  // ---- STEP 1: import the REAL LinearGradient.t3 through the PRODUCTION importer (gradient-fed collapse). ----
  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  const bool imported = importT3Symbol(kLinearGradientT3, lib, &rootId, &warnings);
  const Symbol* rsym = imported ? lib.find(rootId) : nullptr;
  const int nChildren = rsym ? (int)rsym->children.size() : -1;
  const int nConns = rsym ? (int)rsym->connections.size() : -1;
  printf("[t3-lineargradient] import: ok=%d rootId=%s children=%d conns=%d warnings=%zu\n",
         imported ? 1 : 0, rootId.c_str(), nChildren, nConns, warnings.size());
  if (rsym) {
    std::map<std::string, int> byType;
    for (const SymbolChild& c : rsym->children) byType[c.symbolId]++;
    printf("[t3-lineargradient]   collapsed children:");
    for (const auto& kv : byType) printf(" %s×%d", kv.first.c_str(), kv.second);
    printf("\n");
  }
  for (const std::string& w : warnings) printf("[t3-lineargradient]   warn: %s\n", w.c_str());

  const int lgId = rsym ? childIdOfType(*rsym, "LinearGradient") : 0;
  const int v2cCount = rsym ? countType(*rsym, "Vector2Components") : 0;
  const int b2fCount = rsym ? countType(*rsym, "BoolToFloat") : 0;
  const int i2fCount = rsym ? countType(*rsym, "IntToFloat") : 0;
  const int mulCount = rsym ? countType(*rsym, "Multiply") : 0;    // MUST be 0 (offset-routing elided)
  const int pickCount = rsym ? countType(*rsym, "PickFloat") : 0;  // MUST be 0 (offset-routing elided)
  const int gttCount = rsym ? countType(*rsym, "GradientsToTexture") : 0;  // MUST be 0 (elided)
  const bool structOk = imported && rsym && lgId != 0 && v2cCount == 2 && b2fCount == 2 &&
                        i2fCount == 2 && mulCount == 0 && pickCount == 0 && gttCount == 0;
  printf("[t3-lineargradient] SEAM: LinearGradient-atom=%d Vector2Components×%d BoolToFloat×%d IntToFloat×%d "
         "Multiply×%d(elided) PickFloat×%d(elided) GradientsToTexture×%d(elided) → %s\n",
         lgId, v2cCount, b2fCount, i2fCount, mulCount, pickCount, gttCount,
         structOk ? "STRUCTURALLY GREEN" : "FAILED (elision/collapse did not produce the expected graph)");
  if (!structOk) { pool->release(); return injectBug ? 2 : 1; }

  // ---- ④d PORT-ORDER ASSERTION: the boundary-fed FloatParams wires land on the RIGHT atom ports.
  // LinearGradient.t3 wires Width(10d59d0f)→Width, Rotate(8169be8f)→Rotate straight from the root. Plus the
  // fixed slots: Image(d6e157fb)→Image, Gradient(e47e9e63, GradientsToTexture-elided)→Gradient.
  const bool widthOk  = hasConn(*rsym, kSymbolBoundary, "10d59d0f-a5a3-42e6-b874-345ab028978e", lgId, "Width");
  const bool rotateOk = hasConn(*rsym, kSymbolBoundary, "8169be8f-cb35-4900-b462-f2139b412d59", lgId, "Rotate");
  const bool imageOk    = hasConn(*rsym, kSymbolBoundary, "d6e157fb-5300-4a9a-aea8-8b0ea0104ea3", lgId, "Image");
  const bool gradientOk = hasConn(*rsym, kSymbolBoundary, "e47e9e63-9c94-4c29-9555-2452fa498d57", lgId, "Gradient");
  // OFFSET-ROUTING re-anchor asserts: raw Offset(c38647f6, the PickFloat FIRST-Values source) → atom.Offset
  // via FloatParams[8]; OffsetMode(29587763, the PickFloat.Index source) → atom.OffsetMode (added by preConns).
  const bool offsetOk     = hasConn(*rsym, kSymbolBoundary, "c38647f6-c6ea-40a0-b872-0df6d4168c05", lgId, "Offset");
  const bool offsetModeOk = hasConn(*rsym, kSymbolBoundary, "29587763-5456-4d33-bfd4-5d47b133f1cd", lgId, "OffsetMode");
  const bool orderOk = widthOk && rotateOk && imageOk && gradientOk && offsetOk && offsetModeOk;
  printf("[t3-lineargradient] ④d PORT-ORDER: Width=%d Rotate=%d Image=%d Gradient(elided)=%d "
         "Offset(pickfloat-elided)=%d OffsetMode(re-anchored)=%d → %s\n",
         widthOk, rotateOk, imageOk, gradientOk, offsetOk, offsetModeOk,
         orderOk ? "GREEN (each FloatParams/fixed wire on the correct atom port; offset subgraph elided cleanly)"
                 : "RED (a FloatParams wire landed on the WRONG atom port — ④d order broken)");
  if (!orderOk) { pool->release(); return injectBug ? 2 : 1; }

  // ---- STEP 2: cook. Wire a real DefineGradient (RED→GREEN) onto the atom's Gradient port; Image UNWIRED
  // (generator mode → the shader returns the gradient directly). Nothing carrying gradient parity semantics
  // is hand-authored: Center/GainAndBias ride the plumbed vec defaults ((0,0)/(0.5,0.5)); Width/Rotate/
  // PingPong/Repeat/Offset/SizeMode/BlendMode drop to the sw atom's OWN port defaults (byte-equal to the .t3
  // boundary defaults → benign drop). Only Resolution/CustomW/CustomH are set (a 64×64 readback fixture).
  Symbol* sym = const_cast<Symbol*>(rsym);
  for (SymbolChild& c : sym->children)
    if (c.id == lgId) {
      c.overrides["Resolution"] = 4.0f; c.overrides["CustomW"] = (float)kW; c.overrides["CustomH"] = (float)kH;
    }

  // DefineGradient producer set to RED→GREEN (non-default), wired onto the LinearGradient atom's Gradient port.
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
  if (!injectBug) sym->connections.push_back({dgId, "out", lgId, "Gradient"});

  ResidentEvalGraph g = buildEvalGraph(lib, rootId);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[t3-lineargradient] FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, mlib, q, kW, kH);
  pg.cookResident(g, ctx, nullptr, std::to_string(lgId));
  MTL::Texture* tex = pg.residentTexFor(std::to_string(lgId));

  bool haveOut = tex && tex->width() == kW && tex->height() == kH;
  std::vector<uint8_t> px((size_t)kW * kH * 4, 0);
  if (haveOut) tex->getBytes(px.data(), kW * 4, MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto pixel = [&](int x, int y, uint8_t out[4]) {
    size_t i = ((size_t)y * kW + x) * 4;
    out[0] = px[i]; out[1] = px[i + 1]; out[2] = px[i + 2]; out[3] = px[i + 3];
  };

  // Rotate=90 → the ramp runs HORIZONTALLY (c = uvx-0.5). Three pins on the y=32 row:
  //   LEFT (8,32):  uvx≈0.133 → c≈-0.367 → t≈0.13 → near-RED.
  //   RIGHT (56,32): uvx≈0.883 → c≈0.383 → t≈0.88 → GREEN-dominant. The teeth-guard pin.
  //   MID-FIELD (32,32): uvx≈0.508 → c≈0.008 → t≈0.51 → R≈125,G≈130. The DECISIVE pin — mid ramp, where a
  //     stuck/plumb-lost field or a wrong gradient diverges maximally from the smooth oracle.
  const int lx = 8, ly = 32, rx = 56, ry = 32, mx = 32, my = 32;
  uint8_t lpin[4], rpin[4], mpin[4]; pixel(lx, ly, lpin); pixel(rx, ry, rpin); pixel(mx, my, mpin);
  SwGradient ref = redGreenGradient();
  simd::float4 refL = ref.sample(shaderT(lx, ly, kW, kH));
  simd::float4 refR = ref.sample(shaderT(rx, ry, kW, kH));
  simd::float4 refM = ref.sample(shaderT(mx, my, kW, kH));
  auto near8 = [](int v, float t) { return std::abs(v - (int)std::lround(t * 255.0f)) <= 8; };
  bool lOk = haveOut && near8(lpin[0], refL.x) && near8(lpin[1], refL.y) && near8(lpin[2], refL.z);
  bool rOk = haveOut && near8(rpin[0], refR.x) && near8(rpin[1], refR.y) && near8(rpin[2], refR.z);
  bool mOk = haveOut && near8(mpin[0], refM.x) && near8(mpin[1], refM.y) && near8(mpin[2], refM.z);
  bool greenGuard = haveOut && rpin[1] > rpin[0] + 40;  // right-edge G clearly exceeds R → NOT the gray fallback
  bool pass = lOk && rOk && mOk && greenGuard;
  printf("[t3-lineargradient] replay-vs-oracle: haveOut=%d left=(%u,%u,%u) want~(%.0f,%.0f,%.0f) "
         "right=(%u,%u,%u) want~(%.0f,%.0f,%.0f) MID=(%u,%u,%u) want~(%.0f,%.0f,%.0f) greenGuard=%d injectBug=%d\n",
         haveOut ? 1 : 0, lpin[0], lpin[1], lpin[2], refL.x * 255, refL.y * 255, refL.z * 255,
         rpin[0], rpin[1], rpin[2], refR.x * 255, refR.y * 255, refR.z * 255,
         mpin[0], mpin[1], mpin[2], refM.x * 255, refM.y * 255, refM.z * 255, greenGuard, injectBug ? 1 : 0);

  mlib->release(); q->release(); dev->release();

  if (!injectBug) {
    printf("[t3-lineargradient] PARITY VERDICT: %s\n",
           pass ? "GREEN (LinearGradient.t3 gradient-fed collapse replays to the closed-form linear oracle — "
                  "2×Vector2Components + 2×BoolToFloat + 2×IntToFloat kept; GradientsToTexture + the "
                  "Multiply/PickFloat offset subgraph elided; OffsetMode re-anchored onto the atom)"
                : "RED (gradient-fed collapse gap — elision/gradient wire/FloatParams did not reach the atom)");
    pool->release();
    return pass ? 0 : 1;
  }
  const bool bites = !pass;  // bug drops the Gradient wire → black→white fallback → gray → green-guard bites
  printf("[t3-lineargradient] -bug: Gradient-input tooth %s\n", bites ? "BITES" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 0;  // dead tooth exits 0 → --bite NO-BITE list catches it (GOLDEN_STANDARD 特徵3)
}

}  // namespace sw
