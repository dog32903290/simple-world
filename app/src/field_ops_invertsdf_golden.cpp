// field_ops_invertsdf_golden — --selftest-field-invertsdf. GPU DISTANCE-VALUE golden for the InvertSDF
// single-input MODIFIER (the FIRST op to drive the field_graph single-input post-wrap branch,
// field_graph.cpp:82-86). It builds InvertSDF(GoldenSphere) by hand, assembles its MSL via the FROZEN
// base (single-input wrap path), runtime-compiles it, renders a fullscreen pass, reads back the R32Float
// distance texture, and asserts each probed texel's RED == -(sphere signed distance) at that texel's
// field-space p (z=0). Mirrors field_ops_combinesdf_golden.cpp's harness (local GoldenSphere child +
// registered modifier + configure* downcast).
//
// ZONE: shell tier (app/src/ root) — crosses runtime (renderField2d/makeFieldNode/configureInvertSdf)
// AND platform (compileLibraryFromSource); a runtime-zone selftest may NOT include platform
// (check_arch: runtime ↛ platform), so this integration golden sits at the shell tier (same rationale
// as field_render_golden.cpp / field_ops_combinesdf_golden.cpp).
//
// WHY a local GoldenSphere child (not makeFieldNode("SphereSDF")): the registered SphereSDF leaf is
// TU-private with no center/radius override seam. The modifier under test only needs a child that emits
// a KNOWN distance, so the golden declares a minimal sphere leaf emitting the verbatim SphereSDF formula
// with caller-chosen center/radius. The MODIFIER is the REGISTERED leaf (makeFieldNode("InvertSDF") +
// configureInvertSdf) so the registered post-wrap emit path is exercised end-to-end.
//
// PIXEL -> FIELD-SPACE p (identical to field_render_golden.cpp / the template):
//   p.x = (2*px+1)/W - 1 ; p.y = 1 - (2*py+1)/H ; p.z = 0 ; p.w = 0.
//
// CLOSED-FORM: child = GoldenSphere(center=0, r=0.4) -> d_child = |p| - 0.4.
//   InvertSDF post `f.w *= -1;` -> field = -(|p| - 0.4) = 0.4 - |p|.
//   Probes (z=0): p=(0,0) inside -> -(-0.4) = +0.4 ; p=(0.4,0) surface -> -0 = 0 ; p=(0.9,0) outside
//   -> -(0.5) = -0.5. The sign is INVERTED everywhere — the discriminator for the *-1.
//
// injectBug: configureInvertSdf(node, true) drops the negation IN THE OP'S REAL postShaderCode emit
// (`f.w *= 1;` instead of `*= -1;`) -> every probe reads +d (un-inverted) -> all VALUE probes RED. The
// tooth bites the OP's emit string, not the template (no tautological expected-value flip).
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

#include "runtime/field_graph.h"          // setFieldSourceCompiler, FieldNode, CodeAssembleCtx
#include "runtime/field_node_registry.h"  // makeFieldNode (InvertSDFNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache (fresh source-PSO cache per run-device)

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {

// Test seam owned by field_ops_invertsdf.cpp (the leaf type is TU-private). Forward-declared here.
void configureInvertSdf(FieldNode& node, bool injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;  // child sphere radius (center at origin).

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

// Local SphereSDF child (golden-only): emits the verbatim SphereSDF distance with chosen center/radius.
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

// Host closed-form: sphere distance at origin, then INVERTED.
float distSphere(float px, float py) { return std::sqrt(px * px + py * py) - kSphR; }
float invertedField(float px, float py) { return -distSphere(px, py); }

// Build InvertSDF(GoldenSphere). The modifier is the REGISTERED leaf (single-input wrap path).
std::shared_ptr<FieldNode> buildTree(bool injectBug) {
  std::shared_ptr<FieldNode> mod = makeFieldNode("InvertSDF", "golden0");
  if (!mod) return nullptr;
  configureInvertSdf(*mod, injectBug);
  mod->inputs.push_back(std::make_shared<GoldenSphere>("a", 0.f, 0.f, 0.f, kSphR));
  return mod;
}

struct Probe { const char* name; uint32_t px, py; };

}  // namespace

int runFieldInvertSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-invertsdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-invertsdf] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  // injectBug lives in the OP's REAL emit (configureInvertSdf drops the *-1), NOT the template.
  std::shared_ptr<FieldNode> tree = buildTree(injectBug);
  if (!tree) {
    std::printf("[selftest-field-invertsdf] FAIL: InvertSDF factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  clearTexOpCache();
  MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-invertsdf] FAIL: renderField2d null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  const float kTol = 1e-5f;
  int rc = 0;

  const uint32_t cy = (kH - 1) / 2;  // 63 -> p.y ≈ 0
  auto pxFor = [](float target) -> uint32_t {
    float f = ((target + 1.0f) * kW - 1.0f) * 0.5f;
    int px = (int)std::lround(f);
    if (px < 0) px = 0;
    if (px >= (int)kW) px = kW - 1;
    return (uint32_t)px;
  };

  // Value probes: inside (inverted -> +0.4), surface (~0), outside (inverted -> negative).
  Probe probes[] = {
      {"inside", pxFor(0.0f), cy},   // p≈(0,0)  -> -( -0.4) = +0.4
      {"surface", pxFor(0.4f), cy},  // p≈(0.4,0)-> -(0)     =  0
      {"outside", pxFor(0.9f), cy},  // p≈(0.9,0)-> -(0.5)   = -0.5
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = invertedField(px, py);  // the CORRECT inverted value (never flipped for injectBug)
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-invertsdf] probe %-8s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-invertsdf] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-invertsdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-invertsdf] PASS\n");
  return rc;
}

}  // namespace sw
