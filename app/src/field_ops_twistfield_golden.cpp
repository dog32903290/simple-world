// field_ops_twistfield_golden — --selftest-field-twistfield. GPU DISTANCE-VALUE golden for the TwistField
// single-input MODIFIER (drives BOTH halves of the field_graph single-input wrap branch,
// field_graph.cpp:82-86: preShaderCode twists the point BEFORE the child recursion, postShaderCode scales
// the distance AFTER). Builds TwistField(GoldenSphere), assembles via the FROZEN base, compiles, renders,
// reads back R32Float (f.w into RED), asserts the probe == childSphereDistance * StepFactor. Mirrors
// field_ops_bendfield_golden.cpp's harness (the same PRE+POST StepFactor-discriminator structure).
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_translate_golden.cpp).
//
// CLOSED-FORM: child = GoldenSphere(center=0, r=0.4) -> d_child(q) = |q| - 0.4.
//   Amount = 0 -> opTwist is IDENTITY: k = 0/(180/pi) = 0 -> cos(k*p.z)=1, sin(k*p.z)=0 -> the 2x2
//   rotation matrix m = float2x2(1,0,0,1) is the identity -> p = float3(m*p.xy, p.z) = p unchanged ->
//   child samples q = p. So field(p) BEFORE the post scale = |p| - 0.4.
//   The post `f.w *= StepFactor` then multiplies that distance:
//     field(p) = (|p| - 0.4) * StepFactor.
//   Probe p=(0.5,0) -> |p| = 0.5 -> child d = +0.1. Two StepFactor cases discriminate the post line:
//     StepFactor=1.0  -> +0.1   (identity twist + identity scale; the WHOLE-path baseline)
//     StepFactor=0.5  -> +0.05  (the f.w POST discriminator: only a real `*= StepFactor` gives 0.05)
//
// Why Amount=0 (identity twist): the twist rotates p.xy by an angle proportional to p.z; at Amount=0 the
// pre line is a VERIFIED no-op (q=p) so the StepFactor probe isolates the POST emit cleanly. (The pre
// opTwist STILL compiles & runs — a wrong swizzle / dropped opTwist would fail to compile or read wrong,
// so the pre path is exercised, just at its identity point.) The twist is rotation-about-z; at z=0 (the 2D
// render plane) the angle is k*p.z=0 even for non-zero Amount, so an origin-centered child is the clean
// choice here (the rotation-invariant OFF-CENTER trick is only needed when the transform is observable on
// the render plane — here the discriminator is the POST StepFactor, exactly as BendField).
//
// PARAM-PREFIX (BLOOD LESSON): the emitted token P.TwistField_<id>_StepFactor MUST match sw's frozen
//   prefix convention ("<Type>_"+shortId+"_", accessed P.<prefix><Name>; backward-traced from
//   field_ops_combinesdf.cpp:288). A wrong prefix reads the wrong/0 struct member -> the StepFactor=0.5
//   probe (expects exactly 0.05 ONLY if StepFactor is read) goes RED. NOT forward-assumed.
//
// injectBug: configureTwistField(node, ..., injectBug=1) DROPS the OP's REAL postShaderCode `*= StepFactor`
//   line. With the scale gone, the StepFactor=0.5 case reads the unscaled +0.1 instead of +0.05 -> RED.
//   Tooth bites the OP's emit, not the template (no expected-value tautology).
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

// Param-cook + test seam owned by field_ops_twistfield.cpp (leaf type TU-private). Forward-declared here.
void configureTwistField(FieldNode& node, float amount, float stepFactor, int axis, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;
constexpr float kAmount = 0.0f;  // identity twist (opTwist with k=0 -> identity rotation).
constexpr int kAxis = 0;         // X (yzx swizzle) — irrelevant at Amount=0, but exercises the pre path.

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

// Host closed-form: identity twist (Amount=0) -> child samples q=p -> (|p| - r) scaled by StepFactor.
float twistedField(float px, float py, float stepFactor) {
  const float d = std::sqrt(px * px + py * py) - kSphR;  // child sphere at q=p (identity twist)
  return d * stepFactor;
}

std::shared_ptr<FieldNode> buildTree(float stepFactor, int injectBug) {
  std::shared_ptr<FieldNode> mod = makeFieldNode("TwistField", "golden0");
  if (!mod) return nullptr;
  configureTwistField(*mod, kAmount, stepFactor, kAxis, injectBug);
  mod->inputs.push_back(std::make_shared<GoldenSphere>("a", 0.f, 0.f, 0.f, kSphR));
  return mod;
}

// Render one tree (a given StepFactor + injectBug) and check the p=(0.5,0) probe == expected.
// Returns 0 if the probe matches expected within tol, 1 otherwise. Prints one line.
int renderAndCheck(MTL::Device* dev, MTL::CommandQueue* q, const std::string& tmpl, const char* label,
                   float stepFactor, int injectBug) {
  clearTexOpCache();
  std::shared_ptr<FieldNode> tree = buildTree(stepFactor, injectBug);
  if (!tree) {
    std::printf("[selftest-field-twistfield] FAIL: TwistField factory not registered\n");
    return 1;
  }
  MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-twistfield] FAIL(%s): renderField2d null (compile/PSO failure)\n", label);
    return 1;
  }
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);

  const uint32_t cy = (kH - 1) / 2;
  auto pxFor = [](float target) -> uint32_t {
    float f = ((target + 1.0f) * kW - 1.0f) * 0.5f;
    int px = (int)std::lround(f);
    if (px < 0) px = 0;
    if (px >= (int)kW) px = kW - 1;
    return (uint32_t)px;
  };

  // p=(0.5,0): identity twist -> child d = +0.1; * StepFactor -> +0.1 (SF=1) / +0.05 (SF=0.5).
  const uint32_t qx = pxFor(0.5f);
  const float px = pX(qx), py = pY(cy);
  const float expected = twistedField(px, py, stepFactor);  // CORRECT field (never altered for injectBug)
  const float got = buf[(size_t)cy * kW + qx];
  const float diff = std::fabs(got - expected);
  const float kTol = 1e-5f;
  const bool ok = diff <= kTol;
  std::printf("[selftest-field-twistfield] probe %-10s p=(% .4f,% .4f) SF=%.2f got=% .6f expected=% .6f "
              "diff=%.2e %s\n",
              label, px, py, stepFactor, got, expected, diff, ok ? "OK" : "RED");
  tex->release();
  return ok ? 0 : 1;
}

}  // namespace

int runFieldTwistFieldGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-twistfield] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-twistfield] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  // injectBug lives in the OP's REAL postShaderCode emit (drops `*= StepFactor`); production passes 0.
  const int bugMode = injectBug ? 1 : 0;
  int rc = 0;
  // StepFactor=1.0 -> identity scale baseline (+0.1). With the bug dropped, *no scale* still reads +0.1
  // here, so this case alone can't catch the bug — but it confirms the identity-twist + identity-scale
  // whole path is correct in production.
  rc |= renderAndCheck(dev, q, tmpl, "stepfactor1", 1.0f, bugMode);
  // StepFactor=0.5 -> the POST discriminator (+0.05 with the scale, +0.1 without). injectBug drops the
  // `*= StepFactor` -> this probe reads +0.1 != +0.05 -> RED.
  rc |= renderAndCheck(dev, q, tmpl, "stepfactor05", 0.5f, bugMode);

  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-twistfield] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-twistfield] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-twistfield] PASS\n");
  return rc;
}

}  // namespace sw
