// field_ops_noisedisplacesdf_golden — --selftest-field-noisedisplacesdf. GPU DISTANCE-VALUE golden for
// the NoiseDisplaceSDF single-input PRE+POST MODIFIER (adds 3D simplex noise to the DISTANCE, then scales
// by StepFactor). Builds NoiseDisplaceSDF(GoldenSphere @ origin), assembles via the FROZEN base,
// compiles, renders, reads back R32Float (f.w into RED). Mirrors field_ops_bendfield_golden.cpp.
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_combinesdf_golden.cpp).
//
// TWO-PRONGED tooth (avoids a fragile host re-implementation of the 90-line simplex body, while still
// biting the OP's real emit at SDF-active coords — Cut62/63/94 discipline):
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

namespace sw {

// Param-cook + test seam owned by field_ops_noisedisplacesdf.cpp (leaf type TU-private). Forward-declared.
void configureNoiseDisplaceSdf(FieldNode& node, float amount, float scale, float ox, float oy, float oz,
                               float stepFactor, bool useLocalSpace, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;
constexpr float kScale = 0.7f;
constexpr float kStepFactor = 0.5f;

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

std::shared_ptr<FieldNode> buildTree(float amount, int injectBug) {
  std::shared_ptr<FieldNode> mod = makeFieldNode("NoiseDisplaceSDF", "golden0");
  if (!mod) return nullptr;
  configureNoiseDisplaceSdf(*mod, amount, kScale, 0.f, 0.f, 0.f, kStepFactor,
                            /*useLocalSpace=*/false, injectBug);
  mod->inputs.push_back(std::make_shared<GoldenSphere>("a", 0.f, 0.f, 0.f, kSphR));
  return mod;
}

// Render one tree and return its R32Float buffer (empty on failure).
std::vector<float> render(MTL::Device* dev, MTL::CommandQueue* q, const std::string& tmpl, float amount,
                          int injectBug) {
  clearTexOpCache();
  std::shared_ptr<FieldNode> tree = buildTree(amount, injectBug);
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
  std::vector<float> buf0 = render(dev, q, tmpl, 0.0f, bugMode);
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
  std::vector<float> bufN = render(dev, q, tmpl, 0.4f, bugMode);
  if (bufN.empty()) {
    std::printf("[selftest-field-noisedisplacesdf] FAIL: Amount=0.4 render null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  // Determinism: a second identical render must reproduce bufN at the probes.
  std::vector<float> bufN2 = render(dev, q, tmpl, 0.4f, bugMode);
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

  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    // Under injectBug the noise add is dropped -> Amount=0.4 == baseline -> NOT moved -> that is the RED.
    if (moved) {
      std::printf("[selftest-field-noisedisplacesdf] FAIL: injectBug did not trip (noise still present)\n");
      return 1;
    }
    std::printf("[selftest-field-noisedisplacesdf] injectBug correctly RED (noise add dropped)\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-noisedisplacesdf] PASS\n");
  return rc;
}

}  // namespace sw
