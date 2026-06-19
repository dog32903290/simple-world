// field_ops_absolutesdf_golden — --selftest-field-absolutesdf. GPU DISTANCE-VALUE golden for the
// AbsoluteSDF single-input MODIFIER (drives the field_graph single-input post-wrap branch,
// field_graph.cpp:82-86). Builds AbsoluteSDF(GoldenSphere), assembles via the FROZEN base, compiles,
// renders, reads back R32Float, asserts each probe RED == |sphere distance| at the texel's p (z=0).
// Mirrors field_ops_combinesdf_golden.cpp's harness.
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_invertsdf_golden.cpp).
//
// CLOSED-FORM: child = GoldenSphere(center=0, r=0.4) -> d_child = |p| - 0.4.
//   AbsoluteSDF post `f.w = abs(f.w);` -> field = | |p| - 0.4 |.
//   THE TOOTH is the INTERIOR probe: inside the sphere d_child < 0, abs flips it to +|d|. A dropped
//   abs() would leave it negative -> the interior probe is the discriminator for the abs().
//   Probes (z=0): p=(0,0) INTERIOR d_child=-0.4 -> |.| = +0.4 (sign-flips, the tooth) ;
//   p=(0.9,0) EXTERIOR d_child=+0.5 -> |.| = +0.5 (unchanged) ; p=(0.4,0) surface -> ~0.
//
// injectBug: configureAbsoluteSdf(node, true) drops the abs() IN THE OP'S REAL postShaderCode emit
// (`f.w = (f.w);`) -> the interior probe stays negative -> RED. Tooth bites the OP's emit, not the
// template (no expected-value tautology).
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

void configureAbsoluteSdf(FieldNode& node, bool injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;

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

float distSphere(float px, float py) { return std::sqrt(px * px + py * py) - kSphR; }
float absField(float px, float py) { return std::fabs(distSphere(px, py)); }

std::shared_ptr<FieldNode> buildTree(bool injectBug) {
  std::shared_ptr<FieldNode> mod = makeFieldNode("AbsoluteSDF", "golden0");
  if (!mod) return nullptr;
  configureAbsoluteSdf(*mod, injectBug);
  mod->inputs.push_back(std::make_shared<GoldenSphere>("a", 0.f, 0.f, 0.f, kSphR));
  return mod;
}

struct Probe { const char* name; uint32_t px, py; };

}  // namespace

int runFieldAbsoluteSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-absolutesdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-absolutesdf] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  std::shared_ptr<FieldNode> tree = buildTree(injectBug);
  if (!tree) {
    std::printf("[selftest-field-absolutesdf] FAIL: AbsoluteSDF factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  clearTexOpCache();
  MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-absolutesdf] FAIL: renderField2d null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  const float kTol = 1e-5f;
  int rc = 0;

  const uint32_t cy = (kH - 1) / 2;
  auto pxFor = [](float target) -> uint32_t {
    float f = ((target + 1.0f) * kW - 1.0f) * 0.5f;
    int px = (int)std::lround(f);
    if (px < 0) px = 0;
    if (px >= (int)kW) px = kW - 1;
    return (uint32_t)px;
  };

  // The INTERIOR probe is the tooth (abs flips the sign); exterior is unchanged.
  Probe probes[] = {
      {"interior", pxFor(0.0f), cy},  // d_child=-0.4 -> abs = +0.4 (THE tooth — RED if abs dropped)
      {"exterior", pxFor(0.9f), cy},  // d_child=+0.5 -> abs = +0.5 (unchanged)
      {"surface", pxFor(0.4f), cy},   // d_child≈0    -> abs ≈ 0
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = absField(px, py);  // CORRECT abs value (never altered for injectBug)
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-absolutesdf] probe %-8s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-absolutesdf] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-absolutesdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-absolutesdf] PASS\n");
  return rc;
}

}  // namespace sw
