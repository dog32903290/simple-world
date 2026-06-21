// DefineIqGradient gradient op (gradient self-registration seam leaf — a PRODUCER, like DefineGradient).
//
// TiXL authority: external/tixl/Operators/Lib/numbers/floats/process/DefineIqGradient.cs (verbatim
// below) + its .t3 default mirror (DefineIqGradient.t3).
//
//   DefineIqGradient.cs Update() — Inigo Quilez cosine palette: color(t) = a + b*cos(TAU*(f*c + d)),
//   per channel, sampled at NumOfSteps positions → a Gradient. VERBATIM:
//
//     var a = A_Brightness; var b = B_Contrast; var c = C_Frequency; var d = D_Phase;   // 4 vec4 heads
//     var phase = Phase;                                                                  // scalar
//     var count = NumOfSteps.Clamp(1, 256);                                               // cs:31
//     if (count != gradient.Steps.Count) {                       // REBUILD branch when count changes
//         gradient.Steps.Clear();
//         for (i in 0..count) {
//             pos = count > 1 ? (float)i/(count-1) : 0;          // count==1 guard (no /0)  cs:38
//             Steps.Add({ NormalizedPosition=pos, Color=Vector4.One, Id=Guid.NewGuid() });
//         }
//     }
//     for (i in 0..count) {
//         f = count > 1 ? (float)i/(count-1) : 0;                // count==1 guard (no /0)  cs:50
//         f += phase;                                            // cs:51
//         color = ( (a.X + b.X*Cos(TAU*(f*c.X + d.X))).Clamp(0,1000),   // R  cs:53
//                   (a.Y + b.Y*Cos(TAU*(f*c.Y + d.Y))).Clamp(0,1000),   // G  cs:54
//                   (a.Z + b.Z*Cos(TAU*(f*c.Z + d.Z))).Clamp(0,1000),   // B  cs:55
//                   (a.W + b.W*Cos(TAU*(f*c.W + d.W))).Clamp(0,1) );     // A  cs:56  (alpha clamp 0..1!)
//         gradient.Steps[i].Color = color;                       // cs:60
//     }
//     gradient.Interpolation = (Interpolations)Interpolation;    // cs:63
//
// TRAPS handled (named forks below): NumOfSteps clamp(1,256) + the count!=Steps.Count rebuild branch;
// the count==1 divide-by-zero guard on both the rebuild AND the color loop; the ALPHA channel clamps to
// (0,1) while RGB clamp to (0,1000); 16 vec4-component Float ports + Phase scalar + NumOfSteps + the
// Interpolation enum = 19 input ports (18 Float inputs — under the 32 Float-input cap), out = Gradient.
//
// .t3 DEFAULT MIRROR (load-bearing — fresh-drop must match TiXL's fresh DefineIqGradient, per
// DefineIqGradient.t3):
//   C_Frequency = (1, 1, 1, 1)        A_Brightness = (0.5, 0.5, 0.5, 1.0)
//   B_Contrast  = (0.5, 0.5, 0.5, 0)  D_Phase      = (0.0, 0.333, 0.6666, 1.0)
//   Phase = 0.0   NumOfSteps = 64   Interpolation = 0 (Linear)
// (The .t3 stores per-component Vec defaults; each component below carries its own .t3 DefaultValue.)
//
// EVAL-SIDE LAYOUT: a pure PRODUCER — no Gradient input to gather. Every value comes from the RESOLVED
// Float params (each vec4's .x/.y/.z/.w components — boxsdf Center/Size precedent — plus the scalar
// Phase, NumOfSteps and the Interpolation enum). The host SwGradient self-sizes (no rebuild-branch state
// kept across cooks: a fresh cook always clears + (re)fills `count` steps, which is faithful to the .cs's
// observable output — the .cs only SKIPS the clear when count is unchanged, but the per-step Color loop
// overwrites every step's Color either way, so a fresh clear+fill yields the identical gradient).
//
// FORK (named): the host SwGradient::Step has no Guid (DefineIqGradient.cs sets Id=Guid.NewGuid() per
// rebuilt step). The Guid is UI-only stop identity (carved out in sw_gradient.h); Sample/sort parity is
// unaffected. Also: the .cs keeps `gradient.Steps` resident across frames and only rebuilds the position
// scaffold when `count` changes (premature opt). The host cook clears+refills every cook — the OBSERVABLE
// gradient (positions + colors + interpolation) is byte-identical, only the internal Guids would differ
// (which the host drops). Named so a reader knows the rebuild-branch is collapsed deliberately.
#include "runtime/gradient_op_registry.h"  // GradientOp / GradientCookCtx / gradientInjectBug / gradientParam
#include "runtime/graph.h"                  // NodeSpec, PortSpec, Widget

#include <cmath>  // std::cos

namespace sw {

namespace {

// MathUtils.Clamp (the .Clamp(min,max) the .cs uses on each channel): min(max(v,lo),hi).
inline float clampf(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }

// std::numbers / MathF.Tau = 2*pi (the .cs's MathF.Tau). Use the same double-then-cast constant TiXL
// uses (MathF.Tau is a float constant 6.2831855f); we keep it as a float to match the .cs's float math.
constexpr float kTau = 6.28318530717958647692f;  // MathF.Tau

// Read one vec4 head (.x/.y/.z/.w) from the resolved params, defaulting to the .t3 component values.
simd::float4 readVec4(const std::map<std::string, float>* p, const char* base, simd::float4 def) {
  std::string b(base);
  return simd::make_float4(gradientParam(p, (b + ".x").c_str(), def.x),
                           gradientParam(p, (b + ".y").c_str(), def.y),
                           gradientParam(p, (b + ".z").c_str(), def.z),
                           gradientParam(p, (b + ".w").c_str(), def.w));
}

void cookDefineIqGradient(GradientCookCtx& c) {
  if (!c.output) return;
  SwGradient& g = *c.output;

  // The four vec4 heads (.t3 defaults). DefineIqGradient.cs:23-26.
  const simd::float4 a = readVec4(c.params, "A_Brightness", simd::make_float4(0.5f, 0.5f, 0.5f, 1.0f));
  const simd::float4 b = readVec4(c.params, "B_Contrast", simd::make_float4(0.5f, 0.5f, 0.5f, 0.0f));
  const simd::float4 cc = readVec4(c.params, "C_Frequency", simd::make_float4(1.0f, 1.0f, 1.0f, 1.0f));
  const simd::float4 d = readVec4(c.params, "D_Phase", simd::make_float4(0.0f, 0.333f, 0.6666f, 1.0f));

  const float phase = gradientParam(c.params, "Phase", 0.0f);  // cs:28

  // TRAP (named): NumOfSteps.Clamp(1,256). The param rides as a Float; round to the nearest int first
  // (an int slot in TiXL), then clamp into [1,256]. cs:31.
  int count = (int)std::lround(gradientParam(c.params, "NumOfSteps", 64.0f));
  count = (int)clampf((float)count, 1.0f, 256.0f);

  // TRAP (named, fork): the .cs's `if (count != Steps.Count)` rebuild branch is COLLAPSED — the host
  // always rebuilds the position scaffold (clear + refill `count` stops). Faithful to observable output
  // (positions + colors overwritten every cook regardless of the branch); only the dropped Guids differ.
  g.steps.clear();
  g.steps.resize((size_t)count);  // Vector4.One placeholder is irrelevant — every Color is set below.

  for (int i = 0; i < count; ++i) {
    // TRAP (named): count==1 divide-by-zero guard. cs:50 `count > 1 ? (float)i/(count-1) : 0`.
    float f = count > 1 ? (float)i / (count - 1) : 0.0f;
    f += phase;  // cs:51

    g.steps[(size_t)i].pos = (count > 1 ? (float)i / (count - 1) : 0.0f);  // NormalizedPosition cs:38

    // color = a + b*cos(TAU*(f*c + d)) per channel. RGB clamp(0,1000); ALPHA clamp(0,1) (cs:53-56).
    g.steps[(size_t)i].color = simd::make_float4(
        clampf(a.x + b.x * std::cos(kTau * (f * cc.x + d.x)), 0.0f, 1000.0f),  // R cs:53
        clampf(a.y + b.y * std::cos(kTau * (f * cc.y + d.y)), 0.0f, 1000.0f),  // G cs:54
        clampf(a.z + b.z * std::cos(kTau * (f * cc.z + d.z)), 0.0f, 1000.0f),  // B cs:55
        clampf(a.w + b.w * std::cos(kTau * (f * cc.w + d.w)), 0.0f, 1.0f));    // A cs:56 (alpha 0..1!)
  }

  g.interpolation = (int)gradientParam(c.params, "Interpolation", 0.0f);  // cs:63 (enum int)

  // Test-only: corrupt the REAL output on the actual cook path (drop the last step) so the golden's RED
  // case bites HERE, not by flipping the expected value. Off in production. Mirrors DefineGradient leaf.
  if (gradientInjectBug() && !g.steps.empty())
    g.steps.pop_back();
}

}  // namespace

// ============================ FLAT + R-2 RESIDENT golden (in this leaf) ============================
// The Gradient seam has NO selftest sink of its own (gradient_op_registry.h carries only spec+cook
// sinks; the existing gradient goldens are hand-wired in the shared selftests.cpp). To stay write-leaf-
// ONLY (no shared-file edit), this leaf pushes its golden into valueOpSelfTests() — the live-consumed
// self-registration sink that selftests.cpp already iterates (selftests.cpp:447-451) — via a DIRECT
// push (NOT the ValueOp struct, whose ctor would also pollute valueOpSpecSink() with a dummy spec).
// DefineIqGradient lives in TiXL's numbers/floats/process (a value op), so this sink is the natural home.
//
// FLAT leg: hand-build a GradientCookCtx (no Metal) → cook → assert each step == the hand-computed IQ
// formula. RESIDENT leg (R-2 iron rule — flat-green alone is NOT enough): wire DefineIqGradient → the
// shipped GradientsToTexture consumer, cook FLAT then RESIDENT (libFromGraph → buildEvalGraph →
// cookResident), read back the RGBA32F texture and assert each texel == the analytic IQ gradient
// (resident == flat == TiXL). Mirrors gradient_golden.cpp's runGradientsToTextureSelfTest exactly,
// swapping the DefineGradient producer for DefineIqGradient.
}  // namespace sw

#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"        // SymbolLibrary (resident cook input)
#include "runtime/eval_context.h"          // EvaluationContext
#include "runtime/graph_bridge.h"          // libFromGraph (flat Graph -> SymbolLibrary, paths == node ids)
#include "runtime/point_graph.h"           // PointGraph::cook / cookResident / target()
#include "runtime/resident_eval_graph.h"   // ResidentEvalGraph / buildEvalGraph
#include "runtime/value_op_registry.h"     // valueOpSelfTests() — the live-consumed selftest sink
#include "runtime/sw_gradient.h"           // SwGradient

namespace sw {

namespace {

bool iqNearf(float a, float b, float eps = 2e-3f) { return std::fabs(a - b) < eps; }
bool iqNear4(simd::float4 a, simd::float4 b, float eps = 2e-3f) {
  return iqNearf(a.x, b.x, eps) && iqNearf(a.y, b.y, eps) && iqNearf(a.z, b.z, eps) &&
         iqNearf(a.w, b.w, eps);
}

// The .t3-default IQ palette, sampled at `count` steps, phase 0, Linear — the ANALYTIC reference.
// Computed straight from the TiXL formula (NOT from cookDefineIqGradient), so a bug in the cook diverges.
// HAND-COMPUTED golden anchor (count=4 ⇒ f = i/3; the GOLDEN coord is step i=1, f = 1/3):
//   a=(.5,.5,.5,1)  b=(.5,.5,.5,0)  c=(1,1,1,1)  d=(0,.333,.6666,1)
//   R = .5 + .5*cos(TAU*(1/3*1 + 0))      = .5 + .5*cos(2.0943951) = .5 + .5*(-0.5)      = 0.250000
//   G = .5 + .5*cos(TAU*(1/3*1 + .333))   = .5 + .5*cos(4.1866958) = .5 + .5*(-0.501813) = 0.249094
//   B = .5 + .5*cos(TAU*(1/3*1 + .6666))  = .5 + .5*cos(6.2827664) = .5 + .5*( 0.999999) = 0.999999
//   A = 1 + 0*cos(...) = 1.0   (b.w=0 ⇒ alpha is constant a.w=1; clamp(0,1) is a no-op here)
// (i=0 ⇒ (1, 0.250907, 0.249819, 1);  i=2 ⇒ (0.25, 0.999999, 0.250181, 1);  i=3 == i=0 by periodicity.)
SwGradient analyticIqGradient(int count) {
  SwGradient g;
  g.interpolation = kGradientLinear;
  const simd::float4 a = simd::make_float4(0.5f, 0.5f, 0.5f, 1.0f);
  const simd::float4 b = simd::make_float4(0.5f, 0.5f, 0.5f, 0.0f);
  const simd::float4 c = simd::make_float4(1.0f, 1.0f, 1.0f, 1.0f);
  const simd::float4 d = simd::make_float4(0.0f, 0.333f, 0.6666f, 1.0f);
  constexpr float TAU = 6.28318530717958647692f;
  auto cl = [](float v, float lo, float hi) { return std::min(std::max(v, lo), hi); };
  for (int i = 0; i < count; ++i) {
    float f = count > 1 ? (float)i / (count - 1) : 0.0f;  // phase 0
    SwGradientStep s;
    s.pos = count > 1 ? (float)i / (count - 1) : 0.0f;
    s.color = simd::make_float4(cl(a.x + b.x * std::cos(TAU * (f * c.x + d.x)), 0.0f, 1000.0f),
                                cl(a.y + b.y * std::cos(TAU * (f * c.y + d.y)), 0.0f, 1000.0f),
                                cl(a.z + b.z * std::cos(TAU * (f * c.z + d.z)), 0.0f, 1000.0f),
                                cl(a.w + b.w * std::cos(TAU * (f * c.w + d.w)), 0.0f, 1.0f));
    g.steps.push_back(s);
  }
  return g;
}

// Build the chain graph: node 10 = GradientsToTexture (Gradients=port0 MultiInput; Resolution/Direction);
// node 1 = DefineIqGradient (all params default → the .t3 IQ palette), but NumOfSteps overridden to 4 so
// the 4 texels (t = 0, 1/3, 2/3, 1) land EXACTLY on the 4 step positions → exact byte-match.
void buildIqChainGraph(Graph& g, int resolution, int numSteps) {
  Node gt; gt.id = 10; gt.type = "GradientsToTexture";
  gt.params["Resolution"] = (float)resolution;
  gt.params["Direction"] = 0.0f;  // Horizontal
  g.nodes.push_back(gt);
  Node dg; dg.id = 1; dg.type = "DefineIqGradient";
  dg.params["NumOfSteps"] = (float)numSteps;  // all other params default → .t3 IQ palette
  g.nodes.push_back(dg);
  // DefineIqGradient port layout: 4 vec4 heads × 4 comps = 16, + Phase(1) + NumOfSteps(1) + Interp(1) =
  // 19 input ports (indices 0..18); the "out" Gradient port = index 19. GradientsToTexture Gradients = 0.
  const int dgOutPort = 19;
  g.connections.push_back({500, pinId(1, dgOutPort), pinId(10, /*Gradients*/ 0)});
}

// Read back the 1×res RGBA32F texture (Horizontal) and assert each texel == analytic gradient.sample(t).
bool checkIqTexture(MTL::Texture* tex, const SwGradient& ref, int res, const char* tag) {
  const uint32_t wantW = (uint32_t)res, wantH = 1;
  uint32_t w = tex ? (uint32_t)tex->width() : 0;
  uint32_t h = tex ? (uint32_t)tex->height() : 0;
  if (!tex || w != wantW || h != wantH) {
    std::printf("[selftest-defineiqgradient] %s FAIL: dims=%ux%u want %ux%u\n", tag, w, h, wantW, wantH);
    return false;
  }
  std::vector<float> px((size_t)w * h * 4, -1.0f);  // RGBA32F: 4 floats/texel
  tex->getBytes(px.data(), w * 4 * sizeof(float), MTL::Region::Make2D(0, 0, w, h), 0);
  bool ok = true;
  for (uint32_t i = 0; i < w && ok; ++i) {
    float t = (float)i / (res - 1.0f);  // GradientsToTexture.cs:69
    simd::float4 want = ref.sample(t);
    simd::float4 got = simd::make_float4(px[i * 4 + 0], px[i * 4 + 1], px[i * 4 + 2], px[i * 4 + 3]);
    if (!iqNear4(got, want)) {
      std::printf("[selftest-defineiqgradient] %s texel %u t=%.3f got=(%.4f,%.4f,%.4f,%.4f) "
                  "want=(%.4f,%.4f,%.4f,%.4f) FAIL\n",
                  tag, i, t, got.x, got.y, got.z, got.w, want.x, want.y, want.z, want.w);
      ok = false;
    }
  }
  return ok;
}

int runDefineIqGradientGolden(bool injectBug) {
  bool ok = true;

  // ----------------------------- FLAT leg (pure cook, no Metal) -----------------------------
  // Hand-build a GradientCookCtx with NumOfSteps=4 (all else default) and cook directly. Assert each
  // step == analyticIqGradient (the hand-computed IQ formula). The GOLDEN coord is step i=1 (f=1/3):
  // R=0.25, G=0.249094, B=0.999999, A=1.0 (arithmetic in analyticIqGradient's comment).
  {
    std::map<std::string, float> params;
    params["NumOfSteps"] = 4.0f;
    // (All vec4 components + Phase + Interpolation absent ⇒ gradientParam returns the .t3 defaults.)
    SwGradient out;
    GradientCookCtx gc;
    gc.params = &params;
    gc.output = &out;
    gradientInjectBug() = injectBug;
    cookDefineIqGradient(gc);
    gradientInjectBug() = false;

    // SAME asserts run in BOTH modes (gradient_golden.cpp convention). In -bug the cook dropped the last
    // step (count 4 → 3), so the size assert (and thus the per-step + GOLDEN-coord asserts) diverge →
    // ok=false → exit 1 (the tooth bites on the REAL cook path, not by flipping the expected value).
    SwGradient ref = analyticIqGradient(4);
    if (out.steps.size() != 4) {
      std::printf("[selftest-defineiqgradient] flat step count=%zu want 4 FAIL\n", out.steps.size());
      ok = false;
    } else {
      for (int i = 0; i < 4; ++i) {
        if (!iqNearf(out.steps[i].pos, ref.steps[i].pos)) {
          std::printf("[selftest-defineiqgradient] flat step %d pos=%.4f want %.4f FAIL\n", i,
                      out.steps[i].pos, ref.steps[i].pos);
          ok = false;
        }
        if (!iqNear4(out.steps[i].color, ref.steps[i].color)) {
          std::printf("[selftest-defineiqgradient] flat step %d color=(%.4f,%.4f,%.4f,%.4f) "
                      "want=(%.4f,%.4f,%.4f,%.4f) FAIL\n",
                      i, out.steps[i].color.x, out.steps[i].color.y, out.steps[i].color.z,
                      out.steps[i].color.w, ref.steps[i].color.x, ref.steps[i].color.y,
                      ref.steps[i].color.z, ref.steps[i].color.w);
          ok = false;
        }
      }
      // Explicit GOLDEN-coord pin (step i=1, f=1/3): the load-bearing non-degenerate value.
      if (!iqNear4(out.steps[1].color, simd::make_float4(0.25f, 0.249094f, 0.999999f, 1.0f))) {
        std::printf("[selftest-defineiqgradient] flat GOLDEN i=1 color=(%.4f,%.4f,%.4f,%.4f) "
                    "want=(0.2500,0.2491,1.0000,1.0000) FAIL\n",
                    out.steps[1].color.x, out.steps[1].color.y, out.steps[1].color.z,
                    out.steps[1].color.w);
        ok = false;
      }
    }
  }

  // RED short-circuit: the flat tooth has already bitten in -bug mode (the cook dropped a step). The
  // resident leg builds a Metal device + chain; skip it in -bug to keep the RED path fast + deterministic
  // (mirrors gradient_golden.cpp, which skips its resident leg under injectBug).
  if (injectBug) {
    std::printf("[selftest-defineiqgradient] %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
  }

  // ------------------ R-2 RESIDENT leg (production cook → texture readback) ------------------
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  const int res = 4;       // texels at t = 0, 1/3, 2/3, 1 — exact knots of a 4-step gradient.
  const int numSteps = 4;  // gradient step positions = 0, 1/3, 2/3, 1 (align with the 4 texels).
  SwGradient ref = analyticIqGradient(numSteps);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  Graph g;
  buildIqChainGraph(g, res, numSteps);

  // FLAT cook of the CHAIN (DefineIqGradient → GradientsToTexture). Asserts the texture == analytic.
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/10);
  bool flatTexOk = checkIqTexture(pg.target(), ref, res, "chain-flat");
  ok = ok && flatTexOk;

  // RESIDENT (production) cook: libFromGraph (paths == flat node ids as strings) → buildEvalGraph →
  // cookResident with the GradientsToTexture terminal (path "10"). Proves DefineIqGradient is LIVE on the
  // production gradient walker (cookResidentGradient, point_graph_resident.cpp:308), not flat-only.
  SymbolLibrary lib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
  pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"10");
  bool resTexOk = checkIqTexture(pg.target(), ref, res, "chain-resident");
  ok = ok && resTexOk;

  if (ok)
    std::printf("[selftest-defineiqgradient] flat==resident==TiXL: %dx1 RGBA32F IQ-palette match\n", res);

  q->release();
  dev->release();
  pool->release();

  std::printf("[selftest-defineiqgradient] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace

// ================================= Self-registration =================================
// (1) The op, via GradientOp (spec → gradientSpecSink, cook → gradientCookFns) — mirrors the
//     DefineGradient leaf's registrar EXACTLY (gradient_ops_definegradient.cpp).
//     Ports APPEND (not insert): 4 vec4 heads (Widget::Vec vecArity=4, boxsdf precedent) = 16 Float
//     comps, Phase scalar, NumOfSteps (int slot as Float Slider 1..256), Interpolation enum (5 modes),
//     out = Gradient. .t3 DefaultValue mirrored per component (NOT the C# field defaults).
// (2) The golden, via a DIRECT push into valueOpSelfTests() (the live-consumed selftest sink) — NOT the
//     ValueOp struct, whose ctor would also push a dummy spec into valueOpSpecSink(). This keeps the
//     leaf zero-shared-file (selftests.cpp already iterates valueOpSelfTests() at :447-451).
static const GradientOp _reg_defineiqgradient{
    {"DefineIqGradient", "DefineIqGradient",
     {// A_Brightness (.t3 (0.5,0.5,0.5,1.0))
      {"A_Brightness.x", "A_Brightness", "Float", true, 0.5f, -100.0f, 100.0f, Widget::Vec, {}, false, 4},
      {"A_Brightness.y", "A_Brightness.y", "Float", true, 0.5f, -100.0f, 100.0f},
      {"A_Brightness.z", "A_Brightness.z", "Float", true, 0.5f, -100.0f, 100.0f},
      {"A_Brightness.w", "A_Brightness.w", "Float", true, 1.0f, -100.0f, 100.0f},
      // B_Contrast (.t3 (0.5,0.5,0.5,0.0))
      {"B_Contrast.x", "B_Contrast", "Float", true, 0.5f, -100.0f, 100.0f, Widget::Vec, {}, false, 4},
      {"B_Contrast.y", "B_Contrast.y", "Float", true, 0.5f, -100.0f, 100.0f},
      {"B_Contrast.z", "B_Contrast.z", "Float", true, 0.5f, -100.0f, 100.0f},
      {"B_Contrast.w", "B_Contrast.w", "Float", true, 0.0f, -100.0f, 100.0f},
      // C_Frequency (.t3 (1,1,1,1))
      {"C_Frequency.x", "C_Frequency", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 4},
      {"C_Frequency.y", "C_Frequency.y", "Float", true, 1.0f, -100.0f, 100.0f},
      {"C_Frequency.z", "C_Frequency.z", "Float", true, 1.0f, -100.0f, 100.0f},
      {"C_Frequency.w", "C_Frequency.w", "Float", true, 1.0f, -100.0f, 100.0f},
      // D_Phase (.t3 (0.0, 0.333, 0.6666, 1.0))
      {"D_Phase.x", "D_Phase", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 4},
      {"D_Phase.y", "D_Phase.y", "Float", true, 0.333f, -100.0f, 100.0f},
      {"D_Phase.z", "D_Phase.z", "Float", true, 0.6666f, -100.0f, 100.0f},
      {"D_Phase.w", "D_Phase.w", "Float", true, 1.0f, -100.0f, 100.0f},
      // Phase scalar (.t3 0.0)
      {"Phase", "Phase", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Slider},
      // NumOfSteps (int slot → Float; .t3 64; clamp(1,256) happens in the cook)
      {"NumOfSteps", "NumOfSteps", "Float", true, 64.0f, 1.0f, 256.0f, Widget::Slider},
      // Interpolation enum (Gradient.Interpolations; .t3 0 = Linear)
      {"Interpolation", "Interpolation", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"Linear", "Hold", "Smooth", "OkLab", "Spline"}},
      // Output: the host Gradient (the 8th flow's currency). out = port index 19.
      {"out", "out", "Gradient", false}},
     /*evaluate=*/nullptr},  // Gradient output cannot ride NodeSpec::evaluate (returns ONE float)
    cookDefineIqGradient};

// Golden registrar: DIRECT push into the live-consumed value-op selftest sink (no shared-file edit).
namespace {
struct IqGoldenRegistrar {
  IqGoldenRegistrar() { valueOpSelfTests().push_back({"defineiqgradient", runDefineIqGradientGolden}); }
};
static const IqGoldenRegistrar _reg_defineiqgradient_golden;
}  // namespace

}  // namespace sw
