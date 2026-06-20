// field_ops_repeataxis_golden — --selftest-field-repeataxis. GPU DISTANCE-VALUE golden for the RepeatAxis
// single-input MODIFIER (PRE-wrap; same wrap half as Translate, field_graph.cpp:82-86: emits
// preShaderCode BEFORE the child recursion). Builds RepeatAxis(GoldenSphere), assembles via the FROZEN
// base, compiles, renders, reads back R32Float, asserts each probe RED == sphereDistance(fold(p)) at the
// texel's p (z=0). Mirrors field_ops_translate_golden.cpp's harness; exercises the compile-time Axis/
// Mirror selectors (Size=1, Axis=X, Mirror=Off) and the packed Size param (P.RepeatAxis_<id>_Size).
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_translate_golden.cpp).
//
// CLOSED-FORM: child = GoldenSphere(center=0, r=0.4) -> d_child(q) = |q| - 0.4.
//   RepeatAxis pre (no-mirror, axis X): `pMod1(p.x, P.<prefix>Size);` folds p.x in place:
//     halfsize = size*0.5;  q.x = mod(p.x + halfsize, size) - halfsize;  (mod = (a)-(b)*floor((a)/(b)))
//   child then samples q = (q.x, p.y, p.z) -> field(p) = sqrt(q.x^2 + p.y^2) - 0.4 on z=0.
//   With Size=1.0 on y=0:
//     p=(0,0)   -> q.x = mod(0.5,1)-0.5 = 0   -> -0.4  (cell center; ALSO the wrong-axis/drop-fold
//                                                       discriminator — see injectBug)
//     p=(0.5,0) -> q.x = mod(1.0,1)-0.5 = -0.5 -> length 0.5 -> +0.1
//   (Probes use texel-center p from pxFor, so the host closed-form recomputes the SAME pMod1 fold per
//    texel — never a hard-coded -0.4/+0.1 — and the GPU must match to kTol.)
//
// PARAM-PREFIX (BLOOD LESSON): the emitted token P.RepeatAxis_<id>_Size MUST match sw's frozen prefix
//   convention ("<Type>_"+shortId+"_", accessed P.<prefix><Name>; backward-traced from
//   field_ops_combinesdf.cpp:288 / translate.cpp:46). A wrong prefix reads the wrong/0 Size -> the fold
//   width changes -> the p=(0.5,0) probe (expects exactly +0.1 ONLY if Size=1 is read) goes RED. NOT
//   forward-assumed.
//
// injectBug: configureRepeatAxis(node, Size, axis, mirror, injectBug>0) corrupts the OP'S REAL
//   preShaderCode emit:
//   1 = wrong axis (folds p.y instead of p.x) -> on y=0, p.y folds to ~0 (no effect there) but p.x is
//       NOT folded -> at p=(0.5,0) the child samples q=(0.5,0) -> |0.5|-0.4 = +0.1 (coincidentally same)
//       BUT at p=(0,0)... also -0.4. The DISCRIMINATING probe is one OUTSIDE the first cell: p≈(1.0,0)
//       correct folds q.x->0 -> -0.4; wrong-axis leaves q.x=1.0 -> +0.6. The golden runs mode 1 and the
//       "wrapAround" probe at p≈1.0 reddens.
//   2 = drop the pre line (no fold) -> p≈(1.0,0) child samples q=(1.0,0) -> +0.6 != -0.4 -> RED.
//   The golden runs injectBug=1 (wrong-axis) under the --bug entry; the wrapAround probe at p≈1.0 (where
//   a correct X-fold maps back to the cell center) catches both wrong-axis and drop-fold. Tooth bites the
//   OP's emit, not the template (no expected-value tautology: host always computes the CORRECT X-fold).
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

// Param-cook + test seam owned by field_ops_repeataxis.cpp (leaf type TU-private). Forward-declared here.
void configureRepeatAxis(FieldNode& node, float size, int axis, bool mirror, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;
constexpr float kSize = 1.0f;  // cell width -> deterministic fold.
constexpr int kAxisX = 0;      // fold along x.

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

// Host pMod1 fold — matches the MSL `mod` macro `(a) - (b)*floor((a)/(b))` exactly (NOT std::fmod, which
// differs in sign for negatives — pMod1's input p.x+halfsize stays >=0 here, but use the macro form for
// faithfulness).
float hgMod(float a, float b) { return a - b * std::floor(a / b); }
float pMod1Host(float p, float size) {
  const float halfsize = size * 0.5f;
  return hgMod(p + halfsize, size) - halfsize;
}

// Host closed-form: child sphere (origin, r) sampled at the X-FOLDED point q = (pMod1(p.x), p.y, 0).
float repeatField(float px, float py) {
  const float qx = pMod1Host(px, kSize);
  return std::sqrt(qx * qx + py * py) - kSphR;
}

std::shared_ptr<FieldNode> buildTree(int injectBug) {
  std::shared_ptr<FieldNode> mod = makeFieldNode("RepeatAxis", "golden0");
  if (!mod) return nullptr;
  configureRepeatAxis(*mod, kSize, kAxisX, /*mirror=*/false, injectBug);
  mod->inputs.push_back(std::make_shared<GoldenSphere>("a", 0.f, 0.f, 0.f, kSphR));
  return mod;
}

struct Probe { const char* name; uint32_t px, py; };

}  // namespace

int runFieldRepeatAxisGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-repeataxis] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-repeataxis] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  // injectBug=1 (wrong-axis) lives in the OP's REAL preShaderCode emit; production passes 0.
  const int bugMode = injectBug ? 1 : 0;
  std::shared_ptr<FieldNode> tree = buildTree(bugMode);
  if (!tree) {
    std::printf("[selftest-field-repeataxis] FAIL: RepeatAxis factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  clearTexOpCache();
  MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-repeataxis] FAIL: renderField2d null (compile/PSO failure)\n");
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

  // Probes: p chosen so the X-fold lands on a deterministic point.
  //   center   p≈0   -> q.x≈0   -> -0.4 (cell center)
  //   edge     p≈0.5 -> q.x≈-0.5-> +0.1 (cell boundary; the PARAM-READ Size discriminator)
  //   wrapAround p≈1.0 -> q.x≈0 -> -0.4 (NEXT cell folds back to center; the wrong-axis/drop-fold tooth:
  //              an unfolded x=1.0 reads |1.0|-0.4=+0.6, far from -0.4)
  Probe probes[] = {
      {"center", pxFor(0.0f), cy},     // q.x≈0    -> -0.4
      {"edge", pxFor(0.5f), cy},       // q.x≈-0.5 -> +0.1
      {"wrapAround", pxFor(1.0f), cy}, // q.x≈0    -> -0.4  (fold wraps; unfolded would be +0.6)
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = repeatField(px, py);  // CORRECT X-fold (never altered for injectBug)
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-repeataxis] probe %-10s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-repeataxis] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-repeataxis] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-repeataxis] PASS\n");
  return rc;
}

}  // namespace sw
