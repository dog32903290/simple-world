// field_ops_noisedisplacesdf_golden — --selftest-field-noisedisplacesdf. GPU DISTANCE-VALUE golden for
// the NoiseDisplaceSDF single-input PRE+POST MODIFIER (adds 3D simplex noise to the DISTANCE, then scales
// by StepFactor). Builds NoiseDisplaceSDF(GoldenSphere @ origin), assembles via the FROZEN base,
// compiles, renders, reads back R32Float (f.w into RED). Mirrors field_ops_bendfield_golden.cpp.
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_combinesdf_golden.cpp).
//
// THREE-PRONGED tooth (Cut62/63/94 discipline; PRONG 3 added 2026-07-03 for the oracle-audit P4 fix
// #5 — before it the simplex body had NO value oracle, so a wrong constant/gradient table stayed
// green forever):
//
//   PRONG 1 (EXACT closed-form, Amount=0): with Amount=0 the noise add is mathematically ZERO, so
//     field(p) = (|p| - r) * StepFactor — an exact byte-parity check of (a) the child wrap, (b) the
//     `*= StepFactor` POST line, and (c) that the WHOLE noise body + the snapshot pre line still COMPILE
//     and RUN (a dropped global / mis-ordered helper / bad swizzle would fail to compile -> renderField2d
//     null -> FAIL). StepFactor=0.5 makes the post line a real discriminator (only a true `*= StepFactor`
//     gives half the child distance). This is the Cut94 "compiles AND runs at its identity point" tooth.
//
//   PRONG 2 (DISPLACEMENT-PRESENT, Amount=0.4): with Amount!=0 the simplex add SHIFTS f.w away from the
//     bare (|p|-r)*StepFactor. The golden asserts the rendered value DIFFERS from the no-noise baseline
//     by a meaningful margin at a noise-LIVE coord (Scale=0.7 keeps p/Scale off the integer lattice).
//     This bites injectBug=2 (drop the noise add): with the add gone, Amount=0.4 reads the SAME baseline
//     as Amount=0 -> the "displacement present" assertion FAILS -> RED. (It also confirms the noise is
//     deterministic: a second identical render must reproduce the value.)
//
//   PRONG 3 (NOISE-VALUE ORACLE, Amount=0.4, Scale=0.5, Offset!=0): expected SDF values are computed
//     ENTIRELY on the host as ((|p| - r) + fSimplexNoiseDisplace(p, Amount, Scale, -Offset)) * StepFactor,
//     where fSimplexNoiseDisplace is transcribed line-by-line from the TiXL HLSL global in
//     external/tixl Operators/Lib/field/adjust/NoiseDisplaceSDF.cs:41-127 (pinned TiXL SHA 395c4c55) —
//     NOT from the leaf's emitted MSL and NOT from any sw host helper (P5 discipline; cf. the
//     FractalSDF.cs transcription in field_ops_fractalsdf_golden.cpp). It bites the simplex constants,
//     the mod289/permute hash, the gradient decode, the 42.0 scale, the pos/scale+offset composition
//     AND the emit's `-Offset` negation (cs:158 — invisible at the other prongs' Offset=0).
//     Scale=0.5 keeps pos/scale EXACT in fp32 on both sides (power of two) even under Metal's default
//     fast-math reciprocal. Probe SELECTION is host-only (no GPU feedback): the Ashima gradient decode
//     has a genuine hash-driven kink at h==0 (~1/7 of corner lanes hit |x|+|y|==1 exactly) where the
//     GPU's fast-math division (~2.5 ulp; default MTLCompileOptions in platform::
//     compileLibraryFromSource) vs the host's correctly-rounded division can flip step(h, 0.0) onto a
//     DIFFERENT-but-legal gradient; candidates are filtered so every floor/step/max kink in the chain
//     has host margin >= 1e-3, making any diff > kNoiseTol a real formula divergence, not a rounding
//     branch flip. Tolerance kNoiseTol=1e-3: benign host-vs-GPU fp32 drift is ~1e-5 (fast-math ulps +
//     the template's p mapping; fractalsdf's precedent is 2e-4 on a longer fold chain — the noise
//     chain's hash core is exact-integer fp32, so 1e-3 is already generous), while the smallest
//     admitted noise term is kMinDisp=0.02 and a wrong constant/table entry shifts values by
//     O(0.05..1) — 1e-3 sits >=10x above drift and >=10x below the smallest real signal. Under
//     injectBug=2 PRONG 3 also diverges (got == bare base; diff >= kMinDisp*StepFactor = 10x tol).
//
// injectBug: configureNoiseDisplaceSdf(node, ..., injectBug=2) DROPS the OP's REAL noise-add post line.
//   Under the bug PRONG 2's "value moved" assertion fails (Amount=0.4 == baseline) -> RED. The tooth
//   bites the OP's emit, not a tautology.
#include "runtime/field_render.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/field_graph.h"
#include "runtime/field_node_registry.h"
#include "runtime/tex_op_cache.h"

#include "platform/metal_compile.h"

#include "tixl_simplex_oracle.h"  // PRONG 3 host reference (TiXL fSimplexNoiseDisplace transcription)

namespace sw {

// Param-cook + test seam owned by field_ops_noisedisplacesdf.cpp (leaf type TU-private). Forward-declared.
void configureNoiseDisplaceSdf(FieldNode& node, float amount, float scale, float ox, float oy, float oz,
                               float stepFactor, bool useLocalSpace, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;
constexpr float kScale = 0.7f;
constexpr float kStepFactor = 0.5f;

// PRONG 3 oracle params (see header): Scale=0.5 -> pos/scale exact in fp32 on both sides; nonzero
// Offset bites the emit's `-Offset` negation (NoiseDisplaceSDF.cs:158) which Offset=0 cannot see.
constexpr float kOracleAmount = 0.4f;
constexpr float kOracleScale = 0.5f;
constexpr float kOracleOx = -0.315f, kOracleOy = 0.427f, kOracleOz = -0.583f;

std::string loadTemplate() {
#ifdef SW_FIELD_TEMPLATE
  std::ifstream f(SW_FIELD_TEMPLATE);
  if (!f) return "";
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
#else
  return "";
#endif
}

float pX(uint32_t px) { return (2.0f * px + 1.0f) / kW - 1.0f; }
float pY(uint32_t py) { return 1.0f - (2.0f * py + 1.0f) / kH; }

struct GoldenSphere : FieldNode {
  float cx, cy, cz, r;
  GoldenSphere(const std::string& id, float x, float y, float z, float radius)
      : cx(x), cy(y), cz(z), r(radius) {
    prefix = "GSphere_" + id + "_";
  }
  void preShaderCode(CodeAssembleCtx& c, int) const override {
    const std::string ctx = c.ctx();
    c.appendCall("f" + ctx + ".w = length(p" + ctx + ".xyz - P." + prefix + "Center) - P." + prefix +
                 "Radius;");
  }
  void collectParams(std::vector<float>& fp, std::vector<std::string>& pf) const override {
    appendVec3Param(fp, pf, prefix + "Center", cx, cy, cz);
    appendScalarParam(fp, pf, prefix + "Radius", r);
  }
};

// Bare child distance scaled by StepFactor — the Amount=0 closed-form AND the no-noise baseline.
float baseField(float px, float py) {
  return (std::sqrt(px * px + py * py) - kSphR) * kStepFactor;
}

// PRONG 3 HOST REFERENCE lives in tixl_simplex_oracle.h (TiXL fSimplexNoiseDisplace transcription,
// NoiseDisplaceSDF.cs:41-127 @395c4c55; provenance + fp32 rationale in that header). Split out per
// ARCHITECTURE.md rule 4; the spatialdisplace lane reuses the same oracle.
using tixl_simplex::HV3;
using tixl_simplex::NoiseMargins;
using tixl_simplex::tixlFSimplexNoiseDisplace;

std::shared_ptr<FieldNode> buildTree(float amount, float scale, float ox, float oy, float oz,
                                     int injectBug) {
  std::shared_ptr<FieldNode> mod = makeFieldNode("NoiseDisplaceSDF", "golden0");
  if (!mod) return nullptr;
  configureNoiseDisplaceSdf(*mod, amount, scale, ox, oy, oz, kStepFactor,
                            /*useLocalSpace=*/false, injectBug);
  mod->inputs.push_back(std::make_shared<GoldenSphere>("a", 0.f, 0.f, 0.f, kSphR));
  return mod;
}

// Render one tree and return its R32Float buffer (empty on failure).
std::vector<float> render(MTL::Device* dev, MTL::CommandQueue* q, const std::string& tmpl, float amount,
                          float scale, float ox, float oy, float oz, int injectBug) {
  clearTexOpCache();
  std::shared_ptr<FieldNode> tree = buildTree(amount, scale, ox, oy, oz, injectBug);
  if (!tree) return {};
  MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
  if (!tex) return {};
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  tex->release();
  return buf;
}

}  // namespace

int runFieldNoiseDisplaceSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-noisedisplacesdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-noisedisplacesdf] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  const int bugMode = injectBug ? 2 : 0;  // 2 = drop the noise add (production passes 0).
  int rc = 0;
  const uint32_t cy = (kH - 1) / 2;
  auto pxFor = [](float target) -> uint32_t {
    float f = ((target + 1.0f) * kW - 1.0f) * 0.5f;
    int px = (int)std::lround(f);
    if (px < 0) px = 0;
    if (px >= (int)kW) px = kW - 1;
    return (uint32_t)px;
  };
  auto pyFor = [](float target) -> uint32_t {
    float f = ((1.0f - target) * kH - 1.0f) * 0.5f;
    int py = (int)std::lround(f);
    if (py < 0) py = 0;
    if (py >= (int)kH) py = kH - 1;
    return (uint32_t)py;
  };

  // ---- PRONG 1: Amount=0 exact closed-form (compiles+runs; *= StepFactor discriminator) ----
  std::vector<float> buf0 = render(dev, q, tmpl, 0.0f, kScale, 0.f, 0.f, 0.f, bugMode);
  if (buf0.empty()) {
    std::printf("[selftest-field-noisedisplacesdf] FAIL: Amount=0 render null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  struct P { const char* name; uint32_t px, py; };
  std::vector<P> probes = {{"a", pxFor(0.3f), cy}, {"b", pxFor(-0.2f), pyFor(0.25f)},
                           {"c", pxFor(0.45f), pyFor(-0.35f)}};
  const float kTol = 1e-5f;
  for (P& pr : probes) {
    const float px = pX(pr.px), py = pY(pr.py);
    const float expected = baseField(px, py);  // (|p|-r)*StepFactor (Amount=0)
    const float got = buf0[(size_t)pr.py * kW + pr.px];
    const float diff = std::fabs(got - expected);
    const bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-noisedisplacesdf] amount0 %-2s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  // ---- PRONG 2: Amount=0.4 displacement-present (must move off the Amount=0 baseline) ----
  std::vector<float> bufN = render(dev, q, tmpl, 0.4f, kScale, 0.f, 0.f, 0.f, bugMode);
  if (bufN.empty()) {
    std::printf("[selftest-field-noisedisplacesdf] FAIL: Amount=0.4 render null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  // Determinism: a second identical render must reproduce bufN at the probes.
  std::vector<float> bufN2 = render(dev, q, tmpl, 0.4f, kScale, 0.f, 0.f, 0.f, bugMode);
  // Sum the |displacement| at the probes; a present noise add moves the value by > kMinMove.
  const float kMinMove = 1e-3f;
  float totalMove = 0.0f;
  bool deterministic = !bufN2.empty();
  for (P& pr : probes) {
    const float px = pX(pr.px), py = pY(pr.py);
    const float baseline = baseField(px, py);             // Amount=0 value (= bufN under injectBug)
    const float gotN = bufN[(size_t)pr.py * kW + pr.px];
    totalMove += std::fabs(gotN - baseline);
    if (deterministic) {
      const float gotN2 = bufN2[(size_t)pr.py * kW + pr.px];
      if (std::fabs(gotN - gotN2) > 1e-6f) deterministic = false;
    }
    std::printf("[selftest-field-noisedisplacesdf] amount04 %-2s p=(% .4f,% .4f) got=% .6f baseline=% .6f "
                "move=%.4f\n",
                pr.name, px, py, gotN, baseline, std::fabs(gotN - baseline));
  }
  if (!deterministic) {
    std::printf("[selftest-field-noisedisplacesdf] FAIL: noise not deterministic across identical renders\n");
    rc = 1;
  }
  const bool moved = totalMove > kMinMove;
  if (!injectBug && !moved) {
    std::printf("[selftest-field-noisedisplacesdf] FAIL: Amount=0.4 did not displace f.w (total move "
                "%.5f <= %.5f) — noise add missing\n",
                totalMove, kMinMove);
    rc = 1;
  }
  std::printf("[selftest-field-noisedisplacesdf] displacement total move=%.5f (threshold %.5f) -> %s\n",
              totalMove, kMinMove, moved ? "PRESENT" : "ABSENT");

  // ---- PRONG 3: NOISE-VALUE ORACLE (oracle-audit P4 fix #5) — host-transcribed TiXL simplex expected
  // values (see header for source lines / tolerance / kink-margin rationale). Probe selection is
  // HOST-only and deterministic (no GPU feedback): fixed candidates, first 3 that clear every kink
  // margin AND carry a noise term >= kMinDisp. Under injectBug=2 these asserts also diverge (the RED
  // lines below on the bug leg are the tooth biting; the bug-leg verdict stays keyed on PRONG 2).
  {
    struct Cand { float tx, ty; };
    // Fixed mid-band candidates (off-lattice, away from the |p|=0 sqrt kink and the render border).
    const Cand cands[] = {{0.30f, 0.05f},   {-0.22f, 0.31f}, {0.47f, -0.36f}, {-0.41f, -0.18f},
                          {0.12f, 0.42f},   {-0.09f, -0.44f},{0.36f, 0.21f},  {-0.33f, 0.08f},
                          {0.19f, -0.27f},  {0.44f, 0.38f},  {-0.46f, 0.29f}, {0.07f, -0.11f},
                          {-0.15f, 0.45f},  {0.28f, -0.42f}, {-0.38f, -0.33f},{0.41f, 0.02f},
                          {-0.05f, 0.17f},  {0.23f, 0.33f},  {-0.27f, -0.06f},{0.09f, 0.26f},
                          {-0.19f, -0.39f}, {0.33f, -0.14f}, {-0.44f, 0.41f}, {0.15f, 0.06f}};
    const float kKinkMargin = 1e-3f;  // ~3000x the GPU fast-math div drift (~2.5 ulp ~ 3e-7 at O(1))
    const float kMinDisp = 0.02f;     // smallest admitted |noise add|; x StepFactor = 10x kNoiseTol
    const float kNoiseTol = 1e-3f;    // >=10x benign fp32 drift, >=10x under the smallest real signal
    struct OracleProbe { uint32_t px, py; float fx, fy, add, expected; };
    std::vector<OracleProbe> sel;
    for (const Cand& cd : cands) {
      const uint32_t cpx = pxFor(cd.tx), cpy = pyFor(cd.ty);
      const float fx = pX(cpx), fy = pY(cpy);
      NoiseMargins mg;
      // Mirrors the emit (NoiseDisplaceSDF.cs:157-159): sample point = the world-space snapshot `_t`
      // (== p here — GoldenSphere does not transform p; p.z == 0 in the 2D template slice, proven by
      // PRONG 1's closed-form at 1e-5), offset argument = -Offset.
      const float add =
          tixlFSimplexNoiseDisplace(HV3{fx, fy, 0.0f}, kOracleAmount, kOracleScale,
                                    HV3{-kOracleOx, -kOracleOy, -kOracleOz}, &mg);
      if (mg.outerFloor < kKinkMargin || mg.cornerTie < kKinkMargin || mg.shKink < kKinkMargin ||
          mg.mKink < kKinkMargin)
        continue;  // a GPU-vs-host branch flip is legal here — not a formula-parity point
      if (std::fabs(add) < kMinDisp) continue;  // noise-dead point: a dropped add would not diverge
      const float expected = (std::sqrt(fx * fx + fy * fy) - kSphR + add) * kStepFactor;
      sel.push_back({cpx, cpy, fx, fy, add, expected});
      if (sel.size() == 3) break;
    }
    if (sel.size() < 3) {
      // Host-only + deterministic: if this ever fires, the candidate list needs more points — it is a
      // loud config failure, never a flaky one.
      std::printf("[selftest-field-noisedisplacesdf] FAIL: oracle probe selection found %zu/3 usable "
                  "candidates (host kink margins)\n",
                  sel.size());
      rc = 1;
    } else {
      std::vector<float> bufO = render(dev, q, tmpl, kOracleAmount, kOracleScale, kOracleOx,
                                       kOracleOy, kOracleOz, bugMode);
      if (bufO.empty()) {
        std::printf("[selftest-field-noisedisplacesdf] FAIL: oracle render null (compile/PSO failure)\n");
        rc = 1;
      } else {
        for (size_t k = 0; k < sel.size(); ++k) {
          const OracleProbe& pr = sel[k];
          const float got = bufO[(size_t)pr.py * kW + pr.px];
          const float diff = std::fabs(got - pr.expected);
          const bool ok = diff <= kNoiseTol;
          if (!ok) rc = 1;
          std::printf("[selftest-field-noisedisplacesdf] oracle %zu p=(% .4f,% .4f) hostNoiseAdd=% .5f "
                      "got=% .6f expected=% .6f diff=%.2e %s\n",
                      k, pr.fx, pr.fy, pr.add, got, pr.expected, diff, ok ? "OK" : "RED");
        }
      }
    }
  }

  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    // Under injectBug the noise add is dropped -> Amount=0.4 == baseline -> NOT moved -> that is the RED.
    if (moved) {
      std::printf("[selftest-field-noisedisplacesdf] FAIL: injectBug did not trip (noise still present)\n");
      return 0;  // dead tooth -> exit 0 so --bite NO-BITE list catches it
    }
    std::printf("[selftest-field-noisedisplacesdf] injectBug correctly RED (noise add dropped)\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-noisedisplacesdf] PASS\n");
  return rc;
}

}  // namespace sw
