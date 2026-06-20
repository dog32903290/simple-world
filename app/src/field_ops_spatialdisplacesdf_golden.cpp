// field_ops_spatialdisplacesdf_golden — --selftest-field-spatialdisplacesdf. GPU DISTANCE-VALUE golden
// for the SpatialDisplaceSDF single-input PRE-wrap MODIFIER (warps the SAMPLE POINT p by a per-axis 3D
// simplex-noise vector BEFORE the child is evaluated). Builds SpatialDisplaceSDF(GoldenSphere @ origin),
// assembles via the FROZEN base, compiles, renders, reads back R32Float (f.w into RED). Mirrors
// field_ops_bendfield_golden.cpp.
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_combinesdf_golden.cpp).
//
// TWO-PRONGED tooth (same rationale as field_ops_noisedisplacesdf_golden.cpp — avoid a fragile host
// re-implementation of the simplex body × the vNoise 3-axis wrapper, while biting the OP's real emit):
//
//   PRONG 1 (EXACT closed-form, Amount=0): with Amount=0 the warp vector is ZERO, so the child samples
//     the unwarped p and field(p) = |p| - r exactly. This is a byte-parity check that ALSO proves the two
//     globals (fSimplexNoiseDisplace + fSimplexNoiseDisplace3D) compile in the favourable std::map KEY
//     order (vNoise calls fSimplexNoiseDisplace, which must be emitted first — a forward-ref failure or a
//     bad swizzle would null renderField2d -> FAIL). The Cut94 "compiles AND runs at its identity point".
//
//   PRONG 2 (WARP-PRESENT, Amount=0.3): with Amount!=0 the warp shifts p, so the rendered f.w differs
//     from the bare sphere distance at noise-LIVE coords (Scale=0.7 off the integer lattice). The golden
//     asserts the value MOVED off the no-warp baseline by a meaningful margin AND is deterministic across
//     identical renders. This bites injectBug=1 (drop the pre warp line): with the warp gone, Amount=0.3
//     reads the SAME bare distance -> the "warp present" assertion FAILS -> RED.
//
// injectBug: configureSpatialDisplaceSdf(node, ..., injectBug=1) DROPS the OP's REAL pre warp line. Under
//   the bug PRONG 2's "value moved" assertion fails -> RED. Tooth bites the OP's emit, not a tautology.
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

// Param-cook + test seam owned by field_ops_spatialdisplacesdf.cpp (leaf type TU-private). Forward-decl.
void configureSpatialDisplaceSdf(FieldNode& node, float amount, float scale, float vsx, float vsy,
                                 float vsz, float ox, float oy, float oz, float spx, float spy,
                                 float spz, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;
constexpr float kScale = 0.7f;

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

// Bare child distance — the Amount=0 closed-form AND the no-warp baseline.
float baseField(float px, float py) { return std::sqrt(px * px + py * py) - kSphR; }

std::shared_ptr<FieldNode> buildTree(float amount, int injectBug) {
  std::shared_ptr<FieldNode> mod = makeFieldNode("SpatialDisplaceSDF", "golden0");
  if (!mod) return nullptr;
  // vScale=(1,1,1), Offset=0, SamplePos=(0.3,0.5,0.7) so each axis samples a distinct noise location
  // (a zero SamplePos would make all three axes sample the same noise -> still valid, but a non-zero
  // SamplePos exercises the float3(spos.x,0,0) etc. offsets inside vNoise).
  configureSpatialDisplaceSdf(*mod, amount, kScale, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 0.3f, 0.5f, 0.7f,
                              injectBug);
  mod->inputs.push_back(std::make_shared<GoldenSphere>("a", 0.f, 0.f, 0.f, kSphR));
  return mod;
}

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

int runFieldSpatialDisplaceSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-spatialdisplacesdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-spatialdisplacesdf] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  const int bugMode = injectBug ? 1 : 0;  // 1 = drop the pre warp line (production passes 0).
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

  // ---- PRONG 1: Amount=0 exact closed-form (compiles+runs; warp == identity) ----
  std::vector<float> buf0 = render(dev, q, tmpl, 0.0f, bugMode);
  if (buf0.empty()) {
    std::printf("[selftest-field-spatialdisplacesdf] FAIL: Amount=0 render null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  struct P { const char* name; uint32_t px, py; };
  std::vector<P> probes = {{"a", pxFor(0.3f), cy}, {"b", pxFor(-0.2f), pyFor(0.25f)},
                           {"c", pxFor(0.45f), pyFor(-0.35f)}};
  const float kTol = 1e-5f;
  for (P& pr : probes) {
    const float px = pX(pr.px), py = pY(pr.py);
    const float expected = baseField(px, py);  // |p|-r (Amount=0 -> warp is zero)
    const float got = buf0[(size_t)pr.py * kW + pr.px];
    const float diff = std::fabs(got - expected);
    const bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-spatialdisplacesdf] amount0 %-2s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  // ---- PRONG 2: Amount=0.3 warp-present (must move off the Amount=0 baseline) ----
  std::vector<float> bufN = render(dev, q, tmpl, 0.3f, bugMode);
  if (bufN.empty()) {
    std::printf("[selftest-field-spatialdisplacesdf] FAIL: Amount=0.3 render null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  std::vector<float> bufN2 = render(dev, q, tmpl, 0.3f, bugMode);
  const float kMinMove = 1e-3f;
  float totalMove = 0.0f;
  bool deterministic = !bufN2.empty();
  for (P& pr : probes) {
    const float px = pX(pr.px), py = pY(pr.py);
    const float baseline = baseField(px, py);  // |p|-r (= bufN under injectBug)
    const float gotN = bufN[(size_t)pr.py * kW + pr.px];
    totalMove += std::fabs(gotN - baseline);
    if (deterministic) {
      const float gotN2 = bufN2[(size_t)pr.py * kW + pr.px];
      if (std::fabs(gotN - gotN2) > 1e-6f) deterministic = false;
    }
    std::printf("[selftest-field-spatialdisplacesdf] amount03 %-2s p=(% .4f,% .4f) got=% .6f baseline=% .6f "
                "move=%.4f\n",
                pr.name, px, py, gotN, baseline, std::fabs(gotN - baseline));
  }
  if (!deterministic) {
    std::printf("[selftest-field-spatialdisplacesdf] FAIL: warp not deterministic across identical renders\n");
    rc = 1;
  }
  const bool moved = totalMove > kMinMove;
  if (!injectBug && !moved) {
    std::printf("[selftest-field-spatialdisplacesdf] FAIL: Amount=0.3 did not warp p (total move %.5f <= "
                "%.5f) — pre warp line missing\n",
                totalMove, kMinMove);
    rc = 1;
  }
  std::printf("[selftest-field-spatialdisplacesdf] warp total move=%.5f (threshold %.5f) -> %s\n",
              totalMove, kMinMove, moved ? "PRESENT" : "ABSENT");

  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (moved) {
      std::printf("[selftest-field-spatialdisplacesdf] FAIL: injectBug did not trip (warp still present)\n");
      return 1;
    }
    std::printf("[selftest-field-spatialdisplacesdf] injectBug correctly RED (pre warp dropped)\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-spatialdisplacesdf] PASS\n");
  return rc;
}

}  // namespace sw
