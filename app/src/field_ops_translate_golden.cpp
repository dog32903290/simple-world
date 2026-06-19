// field_ops_translate_golden — --selftest-field-translate. GPU DISTANCE-VALUE golden for the Translate
// single-input MODIFIER (the op that drives the OTHER half of the field_graph single-input wrap branch,
// field_graph.cpp:82-86: it emits preShaderCode BEFORE the child recursion, where Invert/Absolute emit
// postShaderCode). Builds Translate(GoldenSphere), assembles via the FROZEN base, compiles, renders,
// reads back R32Float, asserts each probe RED == sphereDistance(p - T) at the texel's p (z=0). Mirrors
// field_ops_combinesdf_golden.cpp's harness; ALSO exercises appendVec3Param packing under the modifier
// prefix (the Translation param read as P.Translate_<id>_Translation).
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_invertsdf_golden.cpp).
//
// CLOSED-FORM: child = GoldenSphere(center=0, r=0.4) -> d_child(q) = |q| - 0.4.
//   Translate pre `p.xyz -= P.<prefix>Translation;` -> child samples q = p - T.
//   field(p) = | p - T | - 0.4. With T=(0.3,0,0): on y=0, field = |p.x - 0.3| - 0.4.
//   The probes pick p so the SAMPLE q = p - T lands on a deterministic plateau in [-1,1]:
//     p=(0.3,0)  -> q=(0,0)   -> -0.4  (sphere center; the PARAM-READ discriminator: a 0/wrong
//                                       Translation reads q=p=(0.3,0) -> |0.3|-0.4 = -0.1, not -0.4)
//     p=(0.8,0)  -> q=(0.5,0) -> +0.1
//     p=(-0.2,0) -> q=(-0.5,0)-> +0.1
//
// PARAM-PREFIX (BLOOD LESSON): the emitted token P.Translate_<id>_Translation MUST match sw's frozen
//   prefix convention ("<Type>_"+shortId+"_", accessed P.<prefix><Name>; backward-traced from
//   field_ops_combinesdf.cpp:288 / boxsdf:56). A wrong prefix reads the wrong/0 struct member -> the
//   p=(0.3,0) probe (expects exactly -0.4 ONLY if the full T is read) goes RED. NOT forward-assumed.
//
// injectBug: configureTranslate(node, T, injectBug>0) corrupts the OP'S REAL preShaderCode emit:
//   1 = wrong sign (`+=`) -> child samples p+T -> p=(0.3,0) reads |0.6|-0.4 = +0.2 != -0.4 -> RED.
//   The golden runs injectBug=1 (wrong-sign) under the --bug entry; both wrong-sign and drop-pre (mode
//   2) miss the -0.4 plateau, so the value probes catch them. Tooth bites the OP's emit, not the
//   template (no expected-value tautology).
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

// Param-cook + test seam owned by field_ops_translate.cpp (leaf type TU-private). Forward-declared here.
void configureTranslate(FieldNode& node, float tx, float ty, float tz, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;
constexpr float kTx = 0.3f, kTy = 0.0f, kTz = 0.0f;  // Translation vector (x-only -> deterministic).

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

// Host closed-form: child sphere (origin, r) sampled at the TRANSLATED point q = p - T.
float translatedField(float px, float py) {
  const float qx = px - kTx, qy = py - kTy;  // qz = 0 - kTz = 0
  return std::sqrt(qx * qx + qy * qy) - kSphR;
}

std::shared_ptr<FieldNode> buildTree(int injectBug) {
  std::shared_ptr<FieldNode> mod = makeFieldNode("Translate", "golden0");
  if (!mod) return nullptr;
  configureTranslate(*mod, kTx, kTy, kTz, injectBug);
  mod->inputs.push_back(std::make_shared<GoldenSphere>("a", 0.f, 0.f, 0.f, kSphR));
  return mod;
}

struct Probe { const char* name; uint32_t px, py; };

}  // namespace

int runFieldTranslateGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-translate] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-translate] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  // injectBug=1 (wrong-sign) lives in the OP's REAL preShaderCode emit; production passes 0.
  const int bugMode = injectBug ? 1 : 0;
  std::shared_ptr<FieldNode> tree = buildTree(bugMode);
  if (!tree) {
    std::printf("[selftest-field-translate] FAIL: Translate factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  clearTexOpCache();
  MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-translate] FAIL: renderField2d null (compile/PSO failure)\n");
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

  // Probes: p chosen so q=p-T lands on a deterministic plateau. The p≈0.3 probe is the PARAM-READ
  // discriminator (only the full T read gives -0.4) AND the wrong-sign discriminator.
  Probe probes[] = {
      {"center", pxFor(0.3f), cy},  // q≈(0,0)   -> -0.4 (full-T read; wrong-sign reads |0.6|-0.4=+0.2)
      {"right", pxFor(0.8f), cy},   // q≈(0.5,0) -> +0.1
      {"left", pxFor(-0.2f), cy},   // q≈(-0.5,0)-> +0.1
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = translatedField(px, py);  // CORRECT field with q=p-T (never altered for injectBug)
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-translate] probe %-8s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-translate] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-translate] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-translate] PASS\n");
  return rc;
}

}  // namespace sw
