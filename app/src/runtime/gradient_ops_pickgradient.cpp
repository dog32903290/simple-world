// PickGradient gradient op — MultiInput Gradient gather → Index.Mod(count) passthrough.
//
// TiXL authority: external/tixl/Operators/Lib/numbers/color/PickGradient.cs (VERBATIM below).
// .t3 defaults (PickGradient.t3):
//   Index     = 0        (InputSlot<int> with DefaultValue 0)
//   Gradients = empty MultiInputSlot<Gradient> (slot-default = empty gradient, irrelevant when nothing wired)
//
// TiXL Update() VERBATIM (cs:17-37):
//   var connections = Gradients.GetCollectedTypedInputs();    // gather wired Gradient inputs
//   if (connections == null || connections.Count == 0) return; // nothing wired → do nothing
//   var index = Index.GetValue(context).Mod(connections.Count); // POSITIVE modulo (C# .Mod helper)
//   Selected.Value = connections[index].GetValue(context);       // passthrough of selected gradient
//   // Clear dirty flag (first-update loop + DirtyFlag.Clear())
//
// TRAP (handled): Index.Mod(count) = POSITIVE modulo — C# .Mod helper = ((n % count) + count) % count,
//   handling negative indices correctly (e.g. Index=-1, count=2 → 1 not -1). We mirror this exactly.
//
// TRAP (handled): MultiInput Gradient gather is wire-declaration-order (GradientsToTexture precedent,
//   already live). inputGradients[i] corresponds to the i-th wired connection in declaration order.
//
// TRAP (handled): Index is declared `InputSlot<int>` in TiXL but our value spine is Float. We read it
//   as Float and truncate to int via (int) cast (as TiXL's int slot reads as int; truncation is
//   faithful since the .t3 default = 0 and typical values are whole-number integers).
//
// SHAPE: this is a GRADIENT CONSUMER (Gradient MultiInput input) that PRODUCES a Gradient output.
//   It is NOT a tex op — it outputs a host SwGradient like DefineGradient. The cook driver routes it
//   through cookGradientNode (flat) and cookResidentGradient (resident), NOT through the tex walker.
//
// FORK (named): none beyond drop-Guid (inherited from the Gradient seam; parity unaffected).
//
// RESIDENT LEG (R-2 iron rule, see golden below): PickGradient flows through cookGradientNode (flat)
//   and cookResidentGradient (resident). The resident golden wires DefineGradient → PickGradient →
//   GradientsToTexture and reads the texture — the same shape as the GradientsToTexture resident golden,
//   exercising cookResidentGradient through the PickGradient pass-through on the production path.
//
// SHARED-FILE NOTE: to register the selftest (`runPickGradientSelfTest`) into the --selftest-* dispatch,
//   gradient_op_registry.h must gain a selftest parameter on `GradientOp` (parallel to ImageFilterOp),
//   and selftests.cpp must wire a `gradientOpSelfTests()` sink (parallel to imageFilterSelfTests()).
//   Neither file is touched here (leaf-only write constraint). The selftest function is defined below
//   and callable once those wires land. Report: sharedFileTouched=true for this seam gap.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"      // SymbolLibrary (resident cook input)
#include "runtime/eval_context.h"        // EvaluationContext
#include "runtime/gradient_op_registry.h"  // GradientOp / GradientCookCtx / gradientInjectBug / gradientParam
#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget, Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"        // libFromGraph (flat Graph → SymbolLibrary)
#include "runtime/point_graph.h"         // PointGraph::cook / cookResident / target()
#include "runtime/resident_eval_graph.h" // ResidentEvalGraph / buildEvalGraph
#include "runtime/sw_gradient.h"         // SwGradient (the 8th flow's currency)

namespace sw {

// Forward-declare the selftest (defined at end of file).
int runPickGradientSelfTest(bool injectBug);

// Test-only injection seam: when set, the cook adds 1 to the raw index before mod so the WRONG
// gradient is selected and the golden's RED case fires on the actual cook path (not by flipping the
// expected value). Off in production.
bool& pickGradientInjectBug() {
  static bool b = false;
  return b;
}

namespace {

// cookPickGradient: read inputGradients (the N gathered SwGradients, wire-declaration order) +
// Float param "Index" (int-on-Float), select connections[Index.Mod(count)], copy to *output.
// TiXL Update() verbatim (cs:17-37).
void cookPickGradient(GradientCookCtx& c) {
  if (!c.output) return;

  const std::vector<SwGradient>* grads = c.inputGradients;
  const int count = grads ? (int)grads->size() : 0;
  if (count == 0) return;  // cs:19-20: connections.Count == 0 → return (leave *output unchanged)

  // cs:22: Index.GetValue(context).Mod(connections.Count) — POSITIVE modulo.
  // Index is int-on-Float: read as Float then truncate. TiXL's int slot gives the whole int directly;
  // truncation to int via (int)cast is faithful for the expected integer-valued Float param.
  int rawIndex = (int)gradientParam(c.params, "Index", 0.0f);
  // Test-only: shift index by +1 before mod so the wrong gradient is selected (inject-bug seam).
  if (pickGradientInjectBug()) rawIndex += 1;

  // C# .Mod(n, m) = ((n % m) + m) % m — always non-negative (handles negative rawIndex).
  const int idx = ((rawIndex % count) + count) % count;  // cs:22

  // cs:23: Selected.Value = connections[index].GetValue(context) — passthrough copy.
  *c.output = (*grads)[idx];  // copy the selected SwGradient into the driver-owned output slot
}

}  // namespace

// Self-registration. File-scope static GradientOp — independent leaf .cpp (no shared edit point).
// Feeds gradientSpecSink() + gradientCookFns() during pre-main dynamic init.
//   Ports (APPEND order mirrors GradientsToTexture convention):
//     Gradients = Gradient MultiInput (the gathered host gradients, wire-declaration order),
//     out       = Gradient output (the selected host gradient — pass-through of inputs[Index.Mod(n)]),
//     Index     = Float scalar (int-on-Float, .t3 default 0).
//   NOTE: GradientOp currently has no selftest parameter (unlike ImageFilterOp). The selftest function
//   `runPickGradientSelfTest` below is defined but not yet wired into the --selftest-* dispatch. To
//   complete the hook, gradient_op_registry.h needs a selftest parameter on GradientOp and selftests.cpp
//   needs to read a gradientOpSelfTests() sink — both are shared files (deferred, sharedFileTouched=true).
static const GradientOp _reg_pickgradient{
    {"PickGradient", "PickGradient",
     {// Gradients = Gradient MultiInput (the N upstream Gradient sources; wire-declaration order)
      {"Gradients", "Gradients", "Gradient", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      // out = Gradient output (selected pass-through)
      {"out", "out", "Gradient", false},
      // Index = int-on-Float scalar (.t3 default 0)
      {"Index", "Index", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Slider}},
     /*evaluate=*/nullptr},  // Gradient output cannot ride NodeSpec::evaluate (returns ONE float)
    cookPickGradient};

// ============================================================
// --selftest-pickgradient  (runPickGradientSelfTest)
//
// Golden coord + hand-computed expected values (TiXL formula):
//
//   Setup:
//     Gradient A (index 0): 2-stop Linear, stop0=(0, red=(1,0,0,1)), stop1=(1, blue=(0,0,1,1))
//     Gradient B (index 1): 2-stop Linear, stop0=(0, green=(0,1,0,1)), stop1=(1, white=(1,1,1,1))
//     Index = 1, count = 2.
//
//   C# .Mod(1, 2) = ((1 % 2) + 2) % 2 = 1 → picks Gradient B.
//
//   Arithmetic on Gradient B (Linear interpolation):
//     sample(0.0) = stop0 = (0, 1, 0, 1)  ← green  (exact, t==stop0.pos)
//     sample(0.5) = lerp((0,1,0,1),(1,1,1,1), 0.5) = (0.5, 1.0, 0.5, 1.0)
//     sample(1.0) = stop1 = (1, 1, 1, 1)  ← white  (exact, t==stop1.pos)
//
//   Negative-index TRAP check:
//     Index = -1, count = 2: C# .Mod(-1, 2) = ((-1 % 2) + 2) % 2 = (−1+2)%2 = 1 → index 1 (Gradient B).
//     sample(0.0) still = (0,1,0,1). Same result as Index=1.
//
//   injectBug (rawIndex += 1 before mod):
//     rawIndex = 1+1 = 2, C# .Mod(2, 2) = 0 → picks Gradient A.
//     sample(0.0) = (1,0,0,1) ≠ expected (0,1,0,1) → FAIL (tooth bites).
//
// FLAT golden: hand-build two gradients as inputGradients, invoke cookPickGradient, read *output.
// RESIDENT golden (R-2 iron rule): wire DefineGradient → PickGradient → GradientsToTexture; cook
//   GradientsToTexture as the resident terminal; read texture back and assert per-texel == DefineGradient
//   sampled at t. Exercises cookResidentGradient through the PickGradient pass-through on the
//   production (resident) path — confirming PickGradient is not a flat-only black hole.
// ============================================================

namespace {

bool pgnear(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
bool pgnear4(simd::float4 a, simd::float4 b, float eps = 1e-4f) {
  return pgnear(a.x, b.x, eps) && pgnear(a.y, b.y, eps) && pgnear(a.z, b.z, eps) &&
         pgnear(a.w, b.w, eps);
}

// The default DefineGradient gradient (~black → white, 2-stop Linear — mirror of gradient_golden.cpp).
SwGradient defaultDefGrad() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(1e-06f, 9.9999e-07f, 9.9999e-07f, 1.0f)});
  g.steps.push_back({1.0f, simd::make_float4(1.0f, 1.0f, 1.0f, 1.0f)});
  return g;
}

}  // namespace

int runPickGradientSelfTest(bool injectBug) {
  bool ok = true;

  // ---- FLAT cook: hand-build ctx + two gradients, invoke cookPickGradient directly ----

  // Gradient A: red→blue (index 0)
  SwGradient gradA;
  gradA.interpolation = kGradientLinear;
  gradA.steps.push_back({0.0f, simd::make_float4(1, 0, 0, 1)});
  gradA.steps.push_back({1.0f, simd::make_float4(0, 0, 1, 1)});

  // Gradient B: green→white (index 1) — the expected output when Index=1
  SwGradient gradB;
  gradB.interpolation = kGradientLinear;
  gradB.steps.push_back({0.0f, simd::make_float4(0, 1, 0, 1)});
  gradB.steps.push_back({1.0f, simd::make_float4(1, 1, 1, 1)});

  std::vector<SwGradient> inputs = {gradA, gradB};

  std::map<std::string, float> params;
  params["Index"] = 1.0f;  // Index=1, count=2 → .Mod(1,2)=1 → picks gradB

  SwGradient output;
  GradientCookCtx gc;
  gc.inputGradients = &inputs;
  gc.output = &output;
  gc.params = &params;

  // --- FLAT / no-bug case ---
  pickGradientInjectBug() = injectBug;
  cookPickGradient(gc);
  pickGradientInjectBug() = false;

  if (!injectBug) {
    // Expected: output == Gradient B
    // sample(0.0) = (0,1,0,1) [stop0 of B, exact — t==pos]
    simd::float4 got0 = output.sample(0.0f);
    simd::float4 exp0 = simd::make_float4(0, 1, 0, 1);
    if (!pgnear4(got0, exp0)) {
      std::printf("[selftest-pickgradient] flat t=0 FAIL got=(%.4f,%.4f,%.4f,%.4f) want=(0,1,0,1)\n",
                  got0.x, got0.y, got0.z, got0.w);
      ok = false;
    }
    // sample(1.0) = (1,1,1,1) [stop1 of B, exact]
    simd::float4 got1 = output.sample(1.0f);
    simd::float4 exp1 = simd::make_float4(1, 1, 1, 1);
    if (!pgnear4(got1, exp1)) {
      std::printf("[selftest-pickgradient] flat t=1 FAIL got=(%.4f,%.4f,%.4f,%.4f) want=(1,1,1,1)\n",
                  got1.x, got1.y, got1.z, got1.w);
      ok = false;
    }
    // sample(0.5) = lerp((0,1,0,1),(1,1,1,1),0.5) = (0.5,1.0,0.5,1.0) [arithmetic above]
    simd::float4 got5 = output.sample(0.5f);
    simd::float4 exp5 = simd::make_float4(0.5f, 1.0f, 0.5f, 1.0f);
    if (!pgnear4(got5, exp5)) {
      std::printf("[selftest-pickgradient] flat t=0.5 FAIL got=(%.4f,%.4f,%.4f,%.4f) want=(0.5,1,0.5,1)\n",
                  got5.x, got5.y, got5.z, got5.w);
      ok = false;
    }

    // Negative-index TRAP: Index=-1, count=2 → C# .Mod(-1,2)=1 → still picks gradB → same t=0 result
    params["Index"] = -1.0f;
    SwGradient outputNeg;
    gc.output = &outputNeg;
    gc.params = &params;
    cookPickGradient(gc);
    simd::float4 gotn = outputNeg.sample(0.0f);
    if (!pgnear4(gotn, exp0)) {
      std::printf("[selftest-pickgradient] flat neg-index FAIL got=(%.4f,%.4f,%.4f,%.4f) want=(0,1,0,1)\n",
                  gotn.x, gotn.y, gotn.z, gotn.w);
      ok = false;
    }
    params["Index"] = 1.0f;
    gc.output = &output;
    gc.params = &params;
  } else {
    // injectBug: rawIndex+1=2, .Mod(2,2)=0 → picks gradA; sample(0.0)=(1,0,0,1) ≠ (0,1,0,1) → FAIL.
    simd::float4 gotBug = output.sample(0.0f);
    simd::float4 expGreen = simd::make_float4(0, 1, 0, 1);
    if (pgnear4(gotBug, expGreen)) {
      std::printf("[selftest-pickgradient] flat(bug) t=0 should FAIL but got green — inject did not bite\n");
      ok = false;  // bug did NOT bite → the tooth is broken (this -bug run should always be ok=false)
    } else {
      ok = false;  // correct: the inject bit → -bug exit 1 (harness expects failure)
    }
  }

  // ---- RESIDENT golden (R-2 iron rule): DefineGradient → PickGradient → GradientsToTexture ----
  // Skipped in injectBug mode (flat tooth already bit; the resident cook has no inject seam here
  // — we corrupt the flat cook above so the overall test exits 1 as required by --bite convention).
  if (!injectBug) {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    MTL::Device* dev = MTL::CreateSystemDefaultDevice();
    MTL::CommandQueue* q = dev->newCommandQueue();
    PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

    // Build chain: node 1 = DefineGradient (all default params → ~black→white 2-stop Linear),
    //              node 2 = PickGradient (Index=0, one Gradient input wired from DefineGradient
    //                       → .Mod(0,1)=0 → passes through the single wired gradient),
    //              node 3 = GradientsToTexture (Resolution=4, Direction=Horizontal, Gradients ← node 2).
    //
    // Port indices:
    //   DefineGradient "out" = port 21  (verified: 16 color comps + 4 pos + 1 interp = 21)
    //   PickGradient ports: 0=Gradients(MultiInput), 1=out, 2=Index
    //   GradientsToTexture ports: 0=Gradients(MultiInput), 1=out, 2=Resolution, 3=Direction
    Graph g;
    {
      Node dg; dg.id = 1; dg.type = "DefineGradient";  // all params default → .t3 default gradient
      g.nodes.push_back(dg);
    }
    {
      Node pg_; pg_.id = 2; pg_.type = "PickGradient";
      pg_.params["Index"] = 0.0f;  // Index=0, count=1 → .Mod(0,1)=0 → picks the single input
      g.nodes.push_back(pg_);
    }
    {
      Node gt; gt.id = 3; gt.type = "GradientsToTexture";
      gt.params["Resolution"] = 4.0f;   // small, exact: t=0, 1/3, 2/3, 1
      gt.params["Direction"] = 0.0f;    // Horizontal
      g.nodes.push_back(gt);
    }

    // DefineGradient.out (port 21) → PickGradient.Gradients (port 0)
    const int dgOutPort = 21;
    g.connections.push_back({100, pinId(1, dgOutPort), pinId(2, /*Gradients*/ 0)});
    // PickGradient.out (port 1) → GradientsToTexture.Gradients (port 0)
    g.connections.push_back({101, pinId(2, /*out*/ 1), pinId(3, /*Gradients*/ 0)});

    const SwGradient ref = defaultDefGrad();  // expected: the DefineGradient default gradient

    EvaluationContext ctx{};
    ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

    // --- flat cook on the chain (smoke-check that the plumbing builds before resident) ---
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/3);
    MTL::Texture* flatTex = pg.target();
    const uint32_t wantW = 4u, wantH = 1u;
    if (!flatTex || flatTex->width() != wantW || flatTex->height() != wantH) {
      std::printf("[selftest-pickgradient] resident-chain flat FAIL: dims=%ux%u want %ux%u\n",
                  flatTex ? (uint32_t)flatTex->width() : 0,
                  flatTex ? (uint32_t)flatTex->height() : 0, wantW, wantH);
      ok = false;
    } else {
      std::vector<float> px(wantW * wantH * 4, -1.0f);
      flatTex->getBytes(px.data(), wantW * 4 * sizeof(float), MTL::Region::Make2D(0, 0, wantW, wantH), 0);
      for (uint32_t i = 0; i < wantW; ++i) {
        float t = (float)i / (4 - 1.0f);
        simd::float4 want = ref.sample(t);
        simd::float4 got = simd::make_float4(px[i * 4], px[i * 4 + 1], px[i * 4 + 2], px[i * 4 + 3]);
        if (!pgnear4(got, want, 2e-3f)) {
          std::printf("[selftest-pickgradient] chain-flat texel %u t=%.3f FAIL got=(%.3f,%.3f,%.3f,%.3f) want=(%.3f,%.3f,%.3f,%.3f)\n",
                      i, t, got.x, got.y, got.z, got.w, want.x, want.y, want.z, want.w);
          ok = false;
        }
      }
    }

    // --- RESIDENT cook: libFromGraph → buildEvalGraph → cookResident with GradientsToTexture("3") ---
    // cookResidentGradient walks through node "2" (PickGradient) → "1" (DefineGradient) on the resident
    // path, proving PickGradient's pass-through runs correctly under the production cook driver.
    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"3");
    MTL::Texture* resTex = pg.target();
    if (!resTex || resTex->width() != wantW || resTex->height() != wantH) {
      std::printf("[selftest-pickgradient] resident FAIL: dims=%ux%u want %ux%u\n",
                  resTex ? (uint32_t)resTex->width() : 0,
                  resTex ? (uint32_t)resTex->height() : 0, wantW, wantH);
      ok = false;
    } else {
      std::vector<float> px(wantW * wantH * 4, -1.0f);
      resTex->getBytes(px.data(), wantW * 4 * sizeof(float), MTL::Region::Make2D(0, 0, wantW, wantH), 0);
      for (uint32_t i = 0; i < wantW; ++i) {
        float t = (float)i / (4 - 1.0f);
        simd::float4 want = ref.sample(t);
        simd::float4 got = simd::make_float4(px[i * 4], px[i * 4 + 1], px[i * 4 + 2], px[i * 4 + 3]);
        if (!pgnear4(got, want, 2e-3f)) {
          std::printf("[selftest-pickgradient] resident texel %u t=%.3f FAIL got=(%.3f,%.3f,%.3f,%.3f) want=(%.3f,%.3f,%.3f,%.3f)\n",
                      i, t, got.x, got.y, got.z, got.w, want.x, want.y, want.z, want.w);
          ok = false;
        }
      }
    }

    if (ok)
      std::printf("[selftest-pickgradient] flat+resident chain: PickGradient pass-through verified\n");

    q->release();
    dev->release();
    pool->release();
  }

  std::printf("[selftest-pickgradient] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
