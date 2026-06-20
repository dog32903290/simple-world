// field_ops_pushpullsdf_golden — --selftest-field-pushpullsdf. GPU DISTANCE-VALUE golden for the
// PushPullSDF custom-collect ADJUST op. PushPullSDF adds Amount to the wrapped SDF's distance (optionally
// modulated by an AmountField's red channel) and divides by (1 + StepScale). The golden exercises BOTH
// collect branches of tryBuildCustomCode — a blood-lesson discipline (static checks miss code that does
// not compile / mis-wires a subcontext):
//   Case A (no AmountField, 1 input):  f.w = (length(p) - r) + Amount.
//   Case B (with AmountField, 2 inputs): f.w = ((length(p) - r) + fAmount.r * Amount) / (1 + StepScale).
//     fAmount.r is the RED channel of the AmountField child's result — a KNOWN constant the child writes.
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_staircombinesdf_golden.cpp).
//
// WHY Case B matters: it proves the custom-collect pushes the "amount" subcontext correctly (the
// AmountField child writes into f<sub>, and `f{parent}.w += f{sub}.r * Amount` reads it back), AND that
// the `.r` swizzle + the `/ (1 + StepScale)` divide compile and compute. The PARENT context (SdfField)
// stays the accumulator — if the custom-collect wrongly pushed a subcontext for the SdfField too, the
// f{parent} write would target the wrong local and the probe would bite.
//
// injectBug: configurePushPullSdf(node, ..., injectBug=1) DROPS the push line (f.w += ...) -> f.w stays
//   the raw sphere distance (Case A) / the divide-only value (Case B) -> probe RED. Tooth bites the OP's
//   REAL emit (the appendCall is gated by injectBug), not the template.
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

// Param-cook + test seam owned by field_ops_pushpullsdf.cpp (leaf type TU-private). Forward-declared.
void configurePushPullSdf(FieldNode& node, float amount, float stepScale, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;
constexpr float kAmountR = 0.5f;  // the AmountField child's KNOWN f.r (red channel) for Case B.

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

// Golden-only sphere @ origin: f.w = length(p.xyz) - r.
struct GoldenSphere : FieldNode {
  float r;
  explicit GoldenSphere(float radius) : r(radius) { prefix = "GSphere_"; }
  void preShaderCode(CodeAssembleCtx& c, int) const override {
    const std::string ctx = c.ctx();
    c.appendCall("f" + ctx + ".w = length(p" + ctx + ".xyz) - P." + prefix + "Radius;");
  }
  void collectParams(std::vector<float>& fp, std::vector<std::string>& pf) const override {
    appendScalarParam(fp, pf, prefix + "Radius", r);
  }
};

// Golden-only AmountField child: writes a KNOWN f.xyz (so f.r == kAmountR) and a dummy f.w. No params.
struct AmountChild : FieldNode {
  AmountChild() { prefix = "GAmount_"; }
  void preShaderCode(CodeAssembleCtx& c, int) const override {
    const std::string ctx = c.ctx();
    char buf[128];
    std::snprintf(buf, sizeof(buf), "f%s.xyz = float3(%.6f, 0.0, 0.0);", ctx.c_str(), kAmountR);
    c.appendCall(buf);
    c.appendCall("f" + ctx + ".w = 0.0;");
  }
  void collectParams(std::vector<float>&, std::vector<std::string>&) const override {}
};

std::shared_ptr<FieldNode> buildTree(float amount, float stepScale, bool withAmountField, int injectBug) {
  std::shared_ptr<FieldNode> pp = makeFieldNode("PushPullSDF", "golden0");
  if (!pp) return nullptr;
  configurePushPullSdf(*pp, amount, stepScale, injectBug);
  pp->inputs.push_back(std::make_shared<GoldenSphere>(kSphR));  // inputs[0] = SdfField
  if (withAmountField) pp->inputs.push_back(std::make_shared<AmountChild>());  // inputs[1] = AmountField
  return pp;
}

struct Probe { const char* name; uint32_t px, py; float expected; };

}  // namespace

int runFieldPushPullSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-pushpullsdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-pushpullsdf] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  const int bugMode = injectBug ? 1 : 0;  // 1 = drop the push (production passes 0).
  const float kTol = 2e-4f;
  int rc = 0;
  const uint32_t cy = (kH - 1) / 2;
  auto pxFor = [](float target) -> uint32_t {
    float f = ((target + 1.0f) * kW - 1.0f) * 0.5f;
    int px = (int)std::lround(f);
    if (px < 0) px = 0;
    if (px >= (int)kW) px = kW - 1;
    return (uint32_t)px;
  };
  auto dSphere = [](float gx, float gy) { return std::sqrt(gx * gx + gy * gy) - kSphR; };

  auto runCase = [&](const char* caseName, float amount, float stepScale, bool withAmountField,
                     std::vector<Probe>& probes) {
    clearTexOpCache();  // distinct topology/source per case.
    std::shared_ptr<FieldNode> tree = buildTree(amount, stepScale, withAmountField, bugMode);
    if (!tree) {
      std::printf("[selftest-field-pushpullsdf] FAIL[%s]: factory not registered\n", caseName);
      rc = 1;
      return;
    }
    MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
    if (!tex) {
      std::printf("[selftest-field-pushpullsdf] FAIL[%s]: renderField2d null (compile/PSO failure)\n",
                  caseName);
      rc = 1;
      return;
    }
    std::vector<float> buf((size_t)kW * kH, 0.0f);
    tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
    for (Probe& pr : probes) {
      const float gx = pX(pr.px), gy = pY(pr.py);
      const float got = buf[(size_t)pr.py * kW + pr.px];
      const float diff = std::fabs(got - pr.expected);
      const bool ok = diff <= kTol;
      if (!ok) rc = 1;
      std::printf("[selftest-field-pushpullsdf] %-10s probe %-8s p=(% .4f,% .4f) got=% .6f "
                  "expected=% .6f diff=%.2e %s\n",
                  caseName, pr.name, gx, gy, got, pr.expected, diff, ok ? "OK" : "RED");
    }
    tex->release();
  };

  // ---- Case A: NO AmountField. f.w = dSphere + Amount. Amount=0.3 ----
  {
    const float amount = 0.3f;
    uint32_t insidex = pxFor(0.0f), edgex = pxFor(0.4f), outx = pxFor(-0.7f);
    std::vector<Probe> probes = {
        {"inside", insidex, cy, dSphere(pX(insidex), pY(cy)) + amount},
        {"edge", edgex, cy, dSphere(pX(edgex), pY(cy)) + amount},
        {"outside", outx, cy, dSphere(pX(outx), pY(cy)) + amount},
    };
    runCase("noAmount", amount, 1.0f, /*withAmountField=*/false, probes);
  }

  // ---- Case B: WITH AmountField. f.w = (dSphere + kAmountR*Amount) / (1 + StepScale). ----
  {
    const float amount = 0.4f, stepScale = 1.0f;  // divide by 2
    auto exp = [&](float gx, float gy) {
      return (dSphere(gx, gy) + kAmountR * amount) / (1.0f + stepScale);
    };
    uint32_t insidex = pxFor(0.0f), edgex = pxFor(0.4f), outx = pxFor(-0.7f);
    std::vector<Probe> probes = {
        {"inside", insidex, cy, exp(pX(insidex), pY(cy))},
        {"edge", edgex, cy, exp(pX(edgex), pY(cy))},
        {"outside", outx, cy, exp(pX(outx), pY(cy))},
    };
    runCase("withAmount", amount, stepScale, /*withAmountField=*/true, probes);
  }

  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-pushpullsdf] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-pushpullsdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-pushpullsdf] PASS\n");
  return rc;
}

}  // namespace sw
