// BlendGradients gradient op (gradient seam CONSUMER — 2 Gradient inputs A,B → cross-sample merge →
// new Gradient output). The 8th cook flow's first TWO-input consumer (DefineGradient = producer,
// GradientsToTexture = consumer-into-tex; this one stays inside the Gradient currency: Gradient→Gradient).
//
// TiXL authority (transcribed VERBATIM below — every line ref is to that file):
//   external/tixl/Operators/Lib/numbers/color/BlendGradients.cs + its .t3 default mirror
//   (BlendGradients.t3). Math reproduced exactly incl. quirks; NOT "fixed".
//
//   BlendGradients.cs Update():
//     blendMode = (BlendModes)BlendMode.GetValue();          // enum int
//     mixFactor = MixFactor.GetValue().Clamp(0,1);
//     _steps.Clear();
//     foreach stepA in gradientA.Steps:                       // cs:31
//       posA = stepA.NormalizedPosition; colorA = stepA.Color;
//       colorB = gradientB.Sample(posA);                      // CROSS-SAMPLE B at A's position
//       _steps[posA] = BlendColors(colorA, colorB, ...);      // a=colorA(from A), b=colorB(B-sampled)
//     foreach stepB in gradientB.Steps:                       // cs:40
//       posB = stepB.NormalizedPosition; colorB = stepB.Color;
//       colorA = gradientA.Sample(posB);                      // CROSS-SAMPLE A at B's position
//       _steps[posB] = BlendColors(colorA, colorB, ...);      // a=colorA(A-sampled), b=colorB(from B)
//     positions = _steps.Keys; Array.Sort(positions);         // cs:49-51 — sort ASCENDING
//     result.Steps = [ {pos=p, color=_steps[p]} for p in positions ]; Interpolation = Linear;  // cs:62-67
//
//   _steps is a Dictionary<float,Vector4> (cs:71): position-KEYED. The two foreach loops can write the
//   SAME key (a position present in BOTH gradients) — the SECOND write (the B-loop) WINS (dict OVERWRITE,
//   cs:38 then cs:47). The output interpolation is ALWAYS Linear (cs:65), regardless of A/B's modes.
//
//   BlendColors(a, b, mode, mixFactor) (cs:75-116) — a/b ORDER is load-bearing (asymmetric for Normal):
//     Normal:   alpha = a.W + b.W - a.W*b.W;  rgb = (1-b.W)*a + b.W*b   (B-OVER-A alpha composite, cs:81-89)
//     Multiply: r = a*b (per channel incl W); r.W = a.W + b.W - a.W*b.W  (cs:93-96)
//     Screen:   r = One - (One-a)*(One-b); r.W = a.W + b.W - a.W*b.W     (cs:100-103)
//     Mix:      Vector4.Lerp(a, b, mixFactor)  = a + (b-a)*mixFactor      (cs:107)
//     (unreachable default): return Vector4.One                          (cs:114)
//
// ★FORK 1 (named) — .t3 default DIVERGES from the C# field default. The C# enum BlendModes orders
//   {Normal=0, Multiply=1, Screen=2, Mix=3}; the C# InputSlot<int> field default would be 0. BUT the
//   .t3 sets BlendMode DefaultValue = 3 (Mix) and MixFactor DefaultValue = 0.0. We mirror the .t3 (a
//   fresh-drop BlendGradients must match TiXL's fresh drop): BlendMode default 3.0f, MixFactor default
//   0.0f. With Mix + mixFactor=0 the blend is Lerp(a,b,0)=a → a fresh drop passes gradientA through
//   (cross-sampled at B's extra positions). NOT a taste call — forced by the .t3.
//
// ★FORK 2 (named) — output is always Gradient.Interpolations.Linear (cs:65), even if A or B used
//   OkLab/Spline. The merge bakes A/B's interpolation INTO the sampled stop colors (via Sample), then
//   re-emits a Linear gradient over those baked stops. Transcribed faithfully.
//
// ★FORK 3 (named) — dict iteration / float-key identity. .NET Dictionary<float,Vector4> keys on float
//   bit-identity; two stops at the SAME float position collapse to one key (last-write-wins). The host
//   uses a std::map<float,…> below (ordered, so the explicit Array.Sort is implicit) with the same
//   exact-float-key collapse. A position present in BOTH gradients ends with the B-loop's value (parity).
//
// EVAL-SIDE LAYOUT: a CONSUMER — GradientA = inputGradients->at(0), GradientB = at(1) (spec port order;
// both single-input, NOT MultiInput → at most one wire each, in spec order). BlendMode/MixFactor come
// from the RESOLVED Float params (the cook driver resolves every Float input port; the enum rides an int
// cast of the Float, exactly like DefineGradient.Interpolation).
#include <algorithm>
#include <cstdio>
#include <map>
#include <vector>

#include "runtime/gradient_op_registry.h"  // GradientOp / GradientCookCtx / gradientInjectBug / gradientParam
#include "runtime/graph.h"                  // NodeSpec, PortSpec, Widget
#include "runtime/sw_gradient.h"            // SwGradient / SwGradient::sample (Gradient.cs verbatim)

namespace sw {

namespace {

// BlendModes (BlendGradients.cs:120-126) — enum ordinals; BlendMode int casts to these.
enum BlendModes { kBlendNormal = 0, kBlendMultiply = 1, kBlendScreen = 2, kBlendMix = 3 };

// BlendColors (BlendGradients.cs:75-116) VERBATIM. a/b order matters (see header). For unknown mode the
// .cs returns Vector4.One (cs:114) — transcribed faithfully (unreachable for the 4 valid enum values).
simd::float4 blendColors(simd::float4 a, simd::float4 b, int mode, float mixFactor) {
  using namespace gradient_detail;
  switch (mode) {
    case kBlendNormal: {  // cs:79-90 — B-over-A alpha composite
      float alpha = a.w + b.w - a.w * b.w;                          // cs:81
      return simd::make_float4((1.0f - b.w) * a.x + b.w * b.x,      // cs:84
                               (1.0f - b.w) * a.y + b.w * b.y,      // cs:85
                               (1.0f - b.w) * a.z + b.w * b.z,      // cs:86
                               alpha);                              // cs:87
    }
    case kBlendMultiply: {  // cs:92-97
      simd::float4 r = simd::make_float4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w);  // cs:94 (a*b)
      r.w = a.w + b.w - a.w * b.w;                                  // cs:95 (overwrite W)
      return r;
    }
    case kBlendScreen: {  // cs:99-104
      simd::float4 r = simd::make_float4(1.0f - (1.0f - a.x) * (1.0f - b.x),   // cs:101 One-(One-a)*(One-b)
                                         1.0f - (1.0f - a.y) * (1.0f - b.y),
                                         1.0f - (1.0f - a.z) * (1.0f - b.z),
                                         1.0f - (1.0f - a.w) * (1.0f - b.w));
      r.w = a.w + b.w - a.w * b.w;                                  // cs:102 (overwrite W)
      return r;
    }
    case kBlendMix:  // cs:106-108
      return lerp4(a, b, mixFactor);                                // cs:107 Vector4.Lerp(a,b,mixFactor)
  }
  return simd::make_float4(1, 1, 1, 1);                             // cs:114 (unreachable default = One)
}

void cookBlendGradients(GradientCookCtx& c) {
  if (!c.output) return;

  // Inputs in spec port order (both single-input). The driver gathers wired Gradient sources; an
  // unwired input contributes no entry, so size() may be 0..2. Missing inputs fall back to an EMPTY
  // SwGradient (Steps empty → Sample returns white (1,1,1,1) per Gradient.cs:168, faithful to a null/
  // empty slot). This mirrors GradientsToTexture's borrow-by-index contract.
  const std::vector<SwGradient>* in = c.inputGradients;
  SwGradient gradientA = (in && in->size() >= 1) ? (*in)[0] : SwGradient{};
  SwGradient gradientB = (in && in->size() >= 2) ? (*in)[1] : SwGradient{};

  const int blendMode = (int)gradientParam(c.params, "BlendMode", 3.0f);   // .t3 default 3 (Mix) — FORK 1
  // MixFactor.GetValue().Clamp(0,1) (cs:24). .t3 default 0.0 (FORK 1).
  float mixFactor = gradient_detail::clampf(gradientParam(c.params, "MixFactor", 0.0f), 0.0f, 1.0f);

  // _steps: a position-KEYED ordered map. std::map<float,…> gives ascending key order for free, so the
  // explicit Array.Sort(positions) (cs:51) is implicit. Same exact-float-key collapse as the .NET dict
  // (FORK 3): a position present in both gradients ends with the B-loop's value (last write wins).
  std::map<float, simd::float4> steps;

  // A-loop (cs:31-39): for each A stop, cross-sample B at A's position; blend a=colorA, b=colorB(sampled).
  for (const SwGradientStep& stepA : gradientA.steps) {
    float positionA = stepA.pos;                          // cs:33
    simd::float4 colorA = stepA.color;                    // cs:34
    simd::float4 colorB = gradientB.sample(positionA);    // cs:35 (cross-sample B)
    steps[positionA] = blendColors(colorA, colorB, blendMode, mixFactor);  // cs:36-37 (a=A, b=B)
  }
  // B-loop (cs:40-48): for each B stop, cross-sample A at B's position; blend a=colorA(sampled), b=colorB.
  // OVERWRITES any key shared with the A-loop (FORK 3 — last write wins).
  for (const SwGradientStep& stepB : gradientB.steps) {
    float positionB = stepB.pos;                          // cs:42
    simd::float4 colorB = stepB.color;                    // cs:43
    simd::float4 colorA = gradientA.sample(positionB);    // cs:44 (cross-sample A)
    steps[positionB] = blendColors(colorA, colorB, blendMode, mixFactor);  // cs:45-46 (a=A-sampled, b=B)
  }

  SwGradient& g = *c.output;
  g.steps.clear();
  for (const auto& kv : steps) {                          // ascending key order (== Array.Sort, cs:51-60)
    SwGradientStep st;
    st.pos = kv.first;                                    // cs:57 NormalizedPosition
    st.color = kv.second;                                 // cs:56 Color
    g.steps.push_back(st);
  }
  g.interpolation = kGradientLinear;                      // cs:65 — ALWAYS Linear (FORK 2)

  // Test-only: corrupt the REAL output on the actual cook path (drop the last step) so the golden's RED
  // case bites HERE, not by flipping the expected value. Off in production. (Same seam as DefineGradient.)
  if (gradientInjectBug() && !g.steps.empty()) g.steps.pop_back();
}

}  // namespace

// Self-registration. File-scope static GradientOp — independent leaf .cpp (feeds gradientSpecSink() +
// gradientCookFns() at pre-main dynamic init). Ports APPEND (not insert), mirroring DefineGradient's
// registrar shape. Port ORDER (load-bearing for the resident-leg wiring index):
//   0: GradientA  (Gradient input, single)
//   1: GradientB  (Gradient input, single)
//   2: out        (Gradient output)
//   3: BlendMode  (Float Widget::Enum {Normal,Multiply,Screen,Mix}, .t3 default 3)
//   4: MixFactor  (Float Widget::Slider, .t3 default 0.0)
// Both Gradient inputs are SINGLE (NOT MultiInput) — TiXL InputSlot<Gradient>, one wire each.
static const GradientOp _reg_blendgradients{
    {"BlendGradients", "BlendGradients",
     {{"GradientA", "GradientA", "Gradient", true},   // port 0 — single Gradient input
      {"GradientB", "GradientB", "Gradient", true},   // port 1 — single Gradient input
      {"out", "out", "Gradient", false},              // port 2 — Gradient output
      // BlendMode enum (.t3 DefaultValue=3=Mix — FORK 1; C# field default would be 0). Labels in
      // enum-ordinal order {Normal,Multiply,Screen,Mix} so the index == the int the cook casts.
      {"BlendMode", "BlendMode", "Float", true, 3.0f, 0.0f, 3.0f, Widget::Enum,
       {"Normal", "Multiply", "Screen", "Mix"}},
      // MixFactor (.t3 DefaultValue=0.0 — FORK 1). Clamp(0,1) happens in the cook (cs:24).
      {"MixFactor", "MixFactor", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider}},
     /*evaluate=*/nullptr},  // Gradient output cannot ride NodeSpec::evaluate (returns ONE float)
    cookBlendGradients};

// ============================ --selftest-blendgradients (in-file golden) ============================
// FLAT golden + ★R-2 RESIDENT-PATH leg. References HAND-COMPUTED from the TiXL formula (arithmetic in
// comments), independent of the impl. The op flows Gradient→Gradient; to exercise the RESIDENT production
// cook (the lane's iron rule: flat-green alone is NOT enough) we wire BlendGradients → GradientsToTexture
// (the Gradient consumer-into-tex, already shipped + R-2-proven) and read its op-owned RGBA32F texture
// back on BOTH the flat cook and cookResident — mirroring gradient_golden.cpp's gradientstotexture leg.
//
// REGISTRATION NOTE (seam): there is NO gradient-specific self-test sink (the GradientOp registrar feeds
// only gradientSpecSink() + gradientCookFns()). The lane convention (gradient_ops_defineiqgradient.cpp)
// is to push the golden DIRECTLY into valueOpSelfTests() — the live-consumed selftest sink that
// selftests.cpp:447-451 already iterates — so the --selftest-blendgradients flag needs NO selftests.cpp
// edit. That push is done in a file-scope static registrar at the bottom of this file (zero shared file).
//
// The ONLY remaining central wiring is CMake: gradient leaf .cpp files are EXPLICITLY listed in
// app/CMakeLists.txt (NOT globbed like point_ops*/value_op*), so this TU must be added there to compile:
//   app/CMakeLists.txt: add `src/runtime/gradient_ops_blendgradients.cpp` (next to the other
//   gradient_ops_*.cpp). This is the same one-line central collation the orchestrator does for ALL the
//   new gradient leaves this batch (defineiqgradient/pickgradient/blendgradients are all untracked +
//   un-CMake'd). Reported via sharedFileTouched — NOT edited here (parallel lanes own CMakeLists).

}  // namespace sw

// Headers for the resident-leg golden (kept below the op so the production registrar above stays the
// file's load-bearing top). These are READ-ONLY includes (no shared file edited).
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"       // SymbolLibrary
#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/graph_bridge.h"         // libFromGraph
#include "runtime/point_graph.h"          // PointGraph::cook / cookResident / target()
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / buildEvalGraph
#include "runtime/value_op_registry.h"    // valueOpSelfTests() — the live-consumed selftest sink

namespace sw {

namespace {

bool bgNearf(float a, float b, float eps = 2e-3f) { return std::fabs(a - b) < eps; }
bool bgNear4(simd::float4 a, simd::float4 b, float eps = 2e-3f) {
  return bgNearf(a.x, b.x, eps) && bgNearf(a.y, b.y, eps) && bgNearf(a.z, b.z, eps) &&
         bgNearf(a.w, b.w, eps);
}

// Build the two source gradients DIRECTLY as DefineGradient-equivalent SwGradients. We CANNOT feed two
// distinct gradients through DefineGradient nodes with non-default stops here cleanly (DefineGradient
// only has 4 fixed (Color,Pos) slots — sufficient, but the analytic ref is clearer if we hand-build the
// SwGradient and also drive the cook via DefineGradient slot params). For the cook we use DefineGradient
// producers; the ANALYTIC reference replicates BlendGradients::Update over these same stops.
//
//   GradientA (Linear): stop@0=(0.8,0.4,0.2,1)  stop@1=(0.2,0.6,1.0,1)
//   GradientB (Linear): stop@0=(0.5,0.5,0.5,1)  stop@0.5=(0.0,1.0,0.0,1)  stop@1=(1.0,1.0,1.0,1)
// Mode = Multiply (1) — purely closed-form (no OkLab matrices), exercises the W-overwrite term.
SwGradient srcA() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(0.8f, 0.4f, 0.2f, 1.0f)});
  g.steps.push_back({1.0f, simd::make_float4(0.2f, 0.6f, 1.0f, 1.0f)});
  return g;
}
SwGradient srcB() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(0.5f, 0.5f, 0.5f, 1.0f)});
  g.steps.push_back({0.5f, simd::make_float4(0.0f, 1.0f, 0.0f, 1.0f)});
  g.steps.push_back({1.0f, simd::make_float4(1.0f, 1.0f, 1.0f, 1.0f)});
  return g;
}

// The analytic BlendGradients result (Multiply) over srcA/srcB — the SAME math the cook runs, replicated
// here independently (so a bug in the cook diverges). HAND-COMPUTED merged stops (see arithmetic block in
// runBlendGradientsSelfTest). Returned as a SwGradient so we can Sample at the texel t's.
SwGradient analyticBlend() {
  SwGradient A = srcA(), B = srcB();
  SwGradient g;
  g.interpolation = kGradientLinear;
  std::map<float, simd::float4> steps;
  auto mul = [](simd::float4 a, simd::float4 b) {
    simd::float4 r = simd::make_float4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w);
    r.w = a.w + b.w - a.w * b.w;
    return r;
  };
  for (const auto& s : A.steps) steps[s.pos] = mul(s.color, B.sample(s.pos));
  for (const auto& s : B.steps) steps[s.pos] = mul(A.sample(s.pos), s.color);
  for (const auto& kv : steps) g.steps.push_back({kv.first, kv.second});
  return g;
}

// Build a DefineGradient node whose 4 (Color,Pos) slots reproduce `g`'s stops (g has <=3 stops here, so
// slots 4 stays disabled with Pos=-1). DefineGradient port indices: 4 colors ×4 comps = 16, +4 Pos = 20,
// +1 Interpolation = 21; "out" = port 21. (Mirrors gradient_golden.cpp's dgOutPort comment.)
void setDefineGradientStops(Node& n, const SwGradient& g) {
  const char* colIds[4] = {"Color1", "Color2", "Color3", "Color4"};
  const char* posIds[4] = {"Color1Pos", "Color2Pos", "Color3Pos", "Color4Pos"};
  for (int i = 0; i < 4; ++i) {
    if (i < (int)g.steps.size()) {
      const SwGradientStep& s = g.steps[i];
      n.params[std::string(colIds[i]) + ".x"] = s.color.x;
      n.params[std::string(colIds[i]) + ".y"] = s.color.y;
      n.params[std::string(colIds[i]) + ".z"] = s.color.z;
      n.params[std::string(colIds[i]) + ".w"] = s.color.w;
      n.params[posIds[i]] = s.pos;
    } else {
      n.params[posIds[i]] = -1.0f;  // disabled slot (pos<0 → skipped, DefineGradient.cs:29-30)
    }
  }
  n.params["Interpolation"] = 0.0f;  // Linear
}

// Assert the readback RGBA32F texture (1 row × res cols, Horizontal) == the analytic blend sampled at t.
bool bgCheckTexture(MTL::Texture* tex, const SwGradient& ref, int res, const char* tag) {
  const uint32_t wantW = (uint32_t)res, wantH = 1;
  uint32_t w = tex ? (uint32_t)tex->width() : 0;
  uint32_t h = tex ? (uint32_t)tex->height() : 0;
  if (!tex || w != wantW || h != wantH) {
    std::printf("[selftest-blendgradients] %s FAIL: dims=%ux%u want %ux%u\n", tag, w, h, wantW, wantH);
    return false;
  }
  std::vector<float> px((size_t)w * h * 4, -1.0f);
  tex->getBytes(px.data(), w * 4 * sizeof(float), MTL::Region::Make2D(0, 0, w, h), 0);
  bool ok = true;
  for (uint32_t i = 0; i < w && ok; ++i) {
    float t = (float)i / (res - 1.0f);  // GradientsToTexture.cs:69
    simd::float4 want = ref.sample(t);
    simd::float4 got = simd::make_float4(px[i * 4 + 0], px[i * 4 + 1], px[i * 4 + 2], px[i * 4 + 3]);
    if (!bgNear4(got, want)) {
      std::printf(
          "[selftest-blendgradients] %s texel %u t=%.3f got=(%.3f,%.3f,%.3f,%.3f) want=(%.3f,%.3f,%.3f,%.3f) FAIL\n",
          tag, i, t, got.x, got.y, got.z, got.w, want.x, want.y, want.z, want.w);
      ok = false;
    }
  }
  return ok;
}

}  // namespace

// HAND-COMPUTED golden (TiXL BlendGradients.cs, Multiply mode, over srcA/srcB):
//
//   merged positions = {0.0, 0.5, 1.0}  (A has stops at {0,1}; B adds 0.5 → 3 keys. The 0.0 and 1.0 keys
//   are written by BOTH loops but to the SAME value, so the overwrite is harmless there; the 0.5 key
//   comes ONLY from B → exercises colorA = A.Sample(0.5) cross-sample.)
//
//   pos 0.0:  colorA = A@0 = (0.8,0.4,0.2,1);  colorB = B.Sample(0)=B@0=(0.5,0.5,0.5,1)
//             Multiply rgb = (0.8*0.5, 0.4*0.5, 0.2*0.5) = (0.40, 0.20, 0.10); W = 1+1-1*1 = 1
//             → (0.40, 0.20, 0.10, 1.0)
//   pos 0.5:  colorB = B@0.5 = (0.0,1.0,0.0,1);  colorA = A.Sample(0.5) = lerp(A@0,A@1,0.5)
//             = (0.8+(0.2-0.8)*0.5, 0.4+(0.6-0.4)*0.5, 0.2+(1.0-0.2)*0.5) = (0.50,0.50,0.60)
//             Multiply rgb = (0.50*0.0, 0.50*1.0, 0.60*0.0) = (0.00, 0.50, 0.00); W = 1
//             → (0.00, 0.50, 0.00, 1.0)
//   pos 1.0:  colorA = A@1 = (0.2,0.6,1.0,1);  colorB = B.Sample(1)=B@1=(1,1,1,1)
//             Multiply rgb = (0.2*1, 0.6*1, 1.0*1) = (0.20, 0.60, 1.00); W = 1
//             → (0.20, 0.60, 1.0, 1.0)
//
//   Output gradient (Linear) over those 3 stops. res=4 texels at t = 0, 1/3, 2/3, 1:
//     t=0    : (0.40, 0.20, 0.10, 1.0)                     [== stop@0]
//     t=1/3  : lerp(stop@0, stop@0.5, frac=(1/3)/0.5=2/3)  = (0.40+(0-0.40)*2/3, 0.20+(0.50-0.20)*2/3,
//              0.10+(0-0.10)*2/3) = (0.13333, 0.40000, 0.03333, 1.0)
//     t=2/3  : lerp(stop@0.5, stop@1, frac=(2/3-0.5)/0.5=1/3) = (0+(0.20-0)*1/3, 0.50+(0.60-0.50)*1/3,
//              0+(1.0-0)*1/3) = (0.06667, 0.53333, 0.33333, 1.0)
//     t=1    : (0.20, 0.60, 1.0, 1.0)                       [== stop@1]
int runBlendGradientsSelfTest(bool injectBug) {
  bool ok = true;
  const int res = 4;
  SwGradient ref = analyticBlend();

  // The four texel samples (Linear over the merged stops) — HAND-COMPUTED above; reused by both legs.
  const simd::float4 texWant[4] = {
      simd::make_float4(0.40000f, 0.20000f, 0.10000f, 1.0f),
      simd::make_float4(0.13333f, 0.40000f, 0.03333f, 1.0f),
      simd::make_float4(0.06667f, 0.53333f, 0.33333f, 1.0f),
      simd::make_float4(0.20000f, 0.60000f, 1.00000f, 1.0f)};

  // ----------------------------- FLAT leg (pure cook, no Metal) -----------------------------
  // Hand-build a GradientCookCtx feeding srcA/srcB DIRECTLY as inputGradients[0]/[1] and call
  // cookBlendGradients(gc) — single-op isolation so gradientInjectBug() corrupts ONLY BlendGradients'
  // output (NOT upstream DefineGradient producers, which the shared global would also hit in a chain
  // cook). Mirrors gradient_ops_defineiqgradient.cpp's hand-built flat leg. Assert the cooked stops +
  // texel samples == analyticBlend()/the hand arithmetic. injectBug drops BlendGradients' last step →
  // the merged gradient loses stop@1 → t>0.5 texels diverge (texel2/texel3, see python verification).
  {
    std::vector<SwGradient> inputs = {srcA(), srcB()};
    std::map<std::string, float> params;
    params["BlendMode"] = (float)kBlendMultiply;  // 1 — closed-form (override the .t3 Mix default)
    params["MixFactor"] = 0.0f;
    SwGradient out;
    GradientCookCtx gc;
    gc.inputGradients = &inputs;
    gc.output = &out;
    gc.params = &params;
    gradientInjectBug() = injectBug;
    cookBlendGradients(gc);
    gradientInjectBug() = false;

    // Pin the three merged stops + four texel samples == hand arithmetic (analyticBlend() is itself
    // byte-checked vs the cook, not trusted). STANDARD --bite shape (no inverted "expected divergence"
    // logic): in -bug mode cookBlendGradients dropped a step (3→2), so out.steps.size()!=3 and the texel
    // samples diverge → ok=false → exit 1 (the tooth bites). Same shape as defineiqgradient's bug leg.
    if (out.steps.size() != 3) {
      std::printf("[selftest-blendgradients] flat merge produced %zu stops, want 3 %s\n",
                  out.steps.size(), injectBug ? "(injectBug step-drop → tooth bites)" : "FAIL");
      ok = false;
    } else {
      if (!bgNearf(out.steps[0].pos, 0.0f) || !bgNear4(out.steps[0].color, simd::make_float4(0.40f, 0.20f, 0.10f, 1.0f))) {
        std::printf("[selftest-blendgradients] flat stop@0 FAIL\n"); ok = false;
      }
      if (!bgNearf(out.steps[1].pos, 0.5f) || !bgNear4(out.steps[1].color, simd::make_float4(0.00f, 0.50f, 0.00f, 1.0f))) {
        std::printf("[selftest-blendgradients] flat stop@0.5 (GOLDEN cross-sample) FAIL\n"); ok = false;
      }
      if (!bgNearf(out.steps[2].pos, 1.0f) || !bgNear4(out.steps[2].color, simd::make_float4(0.20f, 0.60f, 1.0f, 1.0f))) {
        std::printf("[selftest-blendgradients] flat stop@1 FAIL\n"); ok = false;
      }
    }
    // Pin the four texel samples (Linear over the merged stops) — flat cook == hand arithmetic.
    for (int i = 0; i < res; ++i) {
      float t = (float)i / (res - 1.0f);
      if (!bgNear4(out.sample(t), texWant[i])) {
        std::printf("[selftest-blendgradients] flat texel %d t=%.3f FAIL\n", i, t); ok = false;
      }
    }
    // analyticBlend() helper itself == hand arithmetic (independent cross-check of the reference; this
    // leg uses the unbugged ref so it passes in both modes — it validates the reference, not the cook).
    for (int i = 0; i < res; ++i) {
      float t = (float)i / (res - 1.0f);
      if (!bgNear4(ref.sample(t), texWant[i])) {
        std::printf("[selftest-blendgradients] analytic texel %d t=%.3f FAIL\n", i, t); ok = false;
      }
    }
  }

  // RED short-circuit: the flat tooth has already bitten in -bug mode. The resident leg builds a Metal
  // device + chain; skip it under injectBug (the resident path shares the SAME cook fn — gradientInjectBug
  // would also hit upstream DefineGradient producers in a chain cook, muddying attribution). Mirrors
  // gradient_golden.cpp / gradient_ops_defineiqgradient.cpp, which skip their resident leg under injectBug.
  if (injectBug) {
    std::printf("[selftest-blendgradients] %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;  // -bug: dropped stop made the sample diverge → exit 1 (the tooth bites)
  }

  // -------- ★R-2 RESIDENT leg (production cook → GradientsToTexture readback) --------
  // Build the chain: node 1 = DefineGradient(A), node 2 = DefineGradient(B), node 3 = BlendGradients
  // (GradientA←1, GradientB←2, BlendMode=Multiply), node 10 = GradientsToTexture (Gradients←3.out).
  // Cook BOTH the flat chain AND cookResident (production), asserting each texture == analyticBlend().
  // The resident tex cook recurses into cookResidentGradient to gather BlendGradients' output (path "3"),
  // which itself gathers DefineGradient A/B (paths "1"/"2") — proving the 8th flow's TWO-input consumer is
  // LIVE on the production walker (point_graph_resident.cpp:308), not flat-only. resident==flat==TiXL.
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  Graph g;
  Node dgA; dgA.id = 1; dgA.type = "DefineGradient"; setDefineGradientStops(dgA, srcA()); g.nodes.push_back(dgA);
  Node dgB; dgB.id = 2; dgB.type = "DefineGradient"; setDefineGradientStops(dgB, srcB()); g.nodes.push_back(dgB);
  Node bg; bg.id = 3; bg.type = "BlendGradients";
  bg.params["BlendMode"] = (float)kBlendMultiply;  // 1 — override the .t3 Mix default for a closed-form golden
  bg.params["MixFactor"] = 0.0f;
  g.nodes.push_back(bg);
  Node gt; gt.id = 10; gt.type = "GradientsToTexture";
  gt.params["Resolution"] = (float)res; gt.params["Direction"] = 0.0f;  // Horizontal
  g.nodes.push_back(gt);

  // Wire: DefineGradient.out (port 21) → BlendGradients.GradientA (port 0) / .GradientB (port 1);
  // BlendGradients.out (port 2) → GradientsToTexture.Gradients (port 0).
  const int dgOutPort = 21;  // DefineGradient: 16 color comps + 4 pos + 1 interp = ports 0..20; out=21
  const int bgOutPort = 2;   // BlendGradients: GradientA=0, GradientB=1, out=2
  g.connections.push_back({500, pinId(1, dgOutPort), pinId(3, /*GradientA*/ 0)});
  g.connections.push_back({501, pinId(2, dgOutPort), pinId(3, /*GradientB*/ 1)});
  g.connections.push_back({502, pinId(3, bgOutPort), pinId(10, /*Gradients*/ 0)});

  // FLAT cook of the CHAIN (no inject) — texture == analytic blend.
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/10);
  bool flatTexOk = bgCheckTexture(pg.target(), ref, res, "chain-flat");
  ok = ok && flatTexOk;

  // RESIDENT (production) cook: libFromGraph → buildEvalGraph → cookResident terminal "10".
  SymbolLibrary lib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
  pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"10");
  bool resTexOk = bgCheckTexture(pg.target(), ref, res, "chain-resident");
  ok = ok && resTexOk;

  if (ok)
    std::printf("[selftest-blendgradients] flat==resident==TiXL: %dx1 RGBA32F blended (Multiply) gradient match\n", res);

  q->release();
  dev->release();
  pool->release();
  std::printf("[selftest-blendgradients] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// Golden registrar: DIRECT push into the live-consumed value-op selftest sink (no shared-file edit —
// selftests.cpp:447-451 iterates valueOpSelfTests()). Mirrors gradient_ops_defineiqgradient.cpp.
namespace {
struct BlendGradientsGoldenRegistrar {
  BlendGradientsGoldenRegistrar() {
    valueOpSelfTests().push_back({"blendgradients", runBlendGradientsSelfTest});
  }
};
static const BlendGradientsGoldenRegistrar _reg_blendgradients_golden;
}  // namespace

}  // namespace sw
