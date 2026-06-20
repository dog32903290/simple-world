// field_ops_repeatfield3_golden — --selftest-field-repeatfield3. GPU DISTANCE-VALUE golden for the
// RepeatField3 single-input MODIFIER (the op folds the sampling point into a repeating cell BEFORE the
// child is evaluated; same single-input wrap branch as Translate, field_graph.cpp:82-86 — preShaderCode
// runs BEFORE the child recursion). Builds RepeatField3(GoldenSphere), assembles via the FROZEN base,
// compiles, renders, reads back R32Float, asserts each probe RED == sphereDistance(pMod3(p)) at the
// texel's p (z=0). Mirrors field_ops_translate_golden.cpp's harness; ALSO exercises appendVec3Param
// packing under the modifier prefix (Size read as P.RepeatField3_<id>_Size) AND the addGlobals path
// (Common macro block + pMod3 helper).
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_translate_golden.cpp).
//
// CLOSED-FORM: child = GoldenSphere(center=0, r=0.4) -> d_child(q) = |q| - 0.4.
//   RepeatField3 pre `pMod3(p.xyz, P.<prefix>Size);` with Size=(1,1,1):
//     pMod3: p = mod(p + size*0.5, size) - size*0.5,  mod(x,y) = x - y*floor(x/y).
//   field(p) = | pMod3(p) | - 0.4.
//   The probes pick p so the FOLDED q lands on a deterministic plateau in [-1,1]:
//     p=(0,0)   -> q=(0,0)    -> -0.4  (cell center)
//     p=(0.5,0) -> q=(-0.5,0) -> +0.1  (cell edge: mod(1.0,1)-0.5 = -0.5)
//     p=(1,0)   -> q=(0,0)    -> -0.4  (TILING discriminator: the cell one period over folds back to the
//                                       center; a 0/wrong fold leaves q=(1,0) -> |1|-0.4 = +0.6, not -0.4)
//
// PARAM-PREFIX (BLOOD LESSON): the emitted token P.RepeatField3_<id>_Size MUST match sw's frozen prefix
//   convention ("<Type>_"+shortId+"_", accessed P.<prefix><Name>; backward-traced from
//   field_ops_combinesdf.cpp:288 / field_ops_translate.cpp:46). A wrong prefix reads a wrong/0 Size ->
//   division-by-zero / wrong cell -> the tiling probe goes RED. NOT forward-assumed.
//
// injectBug: configureRepeatField3(node, Size, injectBug>0) corrupts the OP'S REAL emit path:
//   1 = corrupt pMod3 helper body (drop the fold; `p = p;`) -> registered global is the bugged one ->
//       p=(1,0) reads |1|-0.4 = +0.6 != -0.4 -> RED.
//   2 = drop the pre line -> child samples raw p -> same +0.6 at p=(1,0).
//   The golden runs injectBug=1 (corrupt-fold) under the --bug entry; the tiling probe catches it.
//   Tooth bites the OP's emit (helper body / pre line), not the expected value (no tautology).
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

// Param-cook + test seam owned by field_ops_repeatfield3.cpp (leaf type TU-private). Forward-declared here.
void configureRepeatField3(FieldNode& node, float sx, float sy, float sz, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;
constexpr float kSx = 1.0f, kSy = 1.0f, kSz = 1.0f;  // Size vector (unit cell -> deterministic fold).

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

// Host mirror of the shader `mod(x,y) = x - y*floor(x/y)` (NOT fmod — the macro from Common).
float shaderMod(float x, float y) { return x - y * std::floor(x / y); }

// Host closed-form: child sphere (origin, r) sampled at the FOLDED point q = pMod3(p) with Size=(kSx..).
// pMod3 per-axis: q = mod(p + size*0.5, size) - size*0.5.  qz folds 0 -> stays 0 for the z=0 slice.
float repeatedField(float px, float py) {
  const float qx = shaderMod(px + kSx * 0.5f, kSx) - kSx * 0.5f;
  const float qy = shaderMod(py + kSy * 0.5f, kSy) - kSy * 0.5f;
  const float qz = shaderMod(0.0f + kSz * 0.5f, kSz) - kSz * 0.5f;  // = 0 for kSz=1
  return std::sqrt(qx * qx + qy * qy + qz * qz) - kSphR;
}

std::shared_ptr<FieldNode> buildTree(int injectBug) {
  std::shared_ptr<FieldNode> mod = makeFieldNode("RepeatField3", "golden0");
  if (!mod) return nullptr;
  configureRepeatField3(*mod, kSx, kSy, kSz, injectBug);
  mod->inputs.push_back(std::make_shared<GoldenSphere>("a", 0.f, 0.f, 0.f, kSphR));
  return mod;
}

struct Probe { const char* name; uint32_t px, py; };

}  // namespace

int runFieldRepeatField3GoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf(
        "[selftest-field-repeatfield3] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-repeatfield3] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  // injectBug=1 (corrupt-fold) lives in the OP's REAL addGlobals emit; production passes 0.
  const int bugMode = injectBug ? 1 : 0;
  std::shared_ptr<FieldNode> tree = buildTree(bugMode);
  if (!tree) {
    std::printf("[selftest-field-repeatfield3] FAIL: RepeatField3 factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  clearTexOpCache();
  MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-repeatfield3] FAIL: renderField2d null (compile/PSO failure)\n");
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

  // Probes: p chosen so the folded q lands on a deterministic plateau. The p≈1 probe is the TILING
  // discriminator (only the real fold maps it back to the center -> -0.4) AND the injectBug discriminator.
  Probe probes[] = {
      {"center", pxFor(0.0f), cy},  // q=(0,0)    -> -0.4 (cell center)
      {"edge", pxFor(0.5f), cy},    // q=(-0.5,0) -> +0.1 (cell edge)
      {"tile", pxFor(1.0f), cy},    // q=(0,0)    -> -0.4 (folds back; no-fold reads |1|-0.4 = +0.6)
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = repeatedField(px, py);  // CORRECT field with q=pMod3(p) (never altered for injectBug)
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-repeatfield3] probe %-7s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-repeatfield3] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-repeatfield3] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-repeatfield3] PASS\n");
  return rc;
}

}  // namespace sw
