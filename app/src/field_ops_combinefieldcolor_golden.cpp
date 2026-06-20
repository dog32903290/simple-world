// field_ops_combinefieldcolor_golden — --selftest-field-combinefieldcolor. GPU VALUE golden for the
// CombineFieldColor multi-input FOLD combiner. Unlike CombineSDF (which folds only .w and color-blends
// .rgb separately), CombineFieldColor folds the WHOLE float4 by {Mix,Add,Multiply} — so the fold is
// directly observable in the template's f.w-only RED readback.
//
// Builds CombineFieldColor(GoldenSphere A @ origin, GoldenSphere B @ (0.6,0,0), both r=0.4), assembles
// via the FROZEN base (multi-input sub-context path), compiles, renders, reads back R32Float, and
// asserts each probe's RED == the closed-form fold of (dA, dB) at the texel's EXACT p (z=0). Mirrors
// field_ops_combinesdf_golden.cpp's harness.
//
// ZONE: shell tier (app/src/ root) — crosses runtime (renderField2d/makeFieldNode/configure) + platform
// (compileLibraryFromSource); a runtime selftest may not include platform (check_arch). Same rationale
// as field_ops_combinesdf_golden.cpp / field_render_golden.cpp.
//
// WHY local GoldenSphere children: the registered SphereSDF leaf is TU-private with no center override;
// this fold needs TWO spheres at DIFFERENT centers so dA != dB at the probes. GoldenSphere emits the
// verbatim SphereSDF distance with a caller-chosen center. The COMBINER is the REGISTERED leaf
// (makeFieldNode("CombineFieldColor") + configureCombineFieldColor), so the registered fold path is
// what is exercised end-to-end.
//
// PIXEL -> FIELD-SPACE p (identical to field_render_golden.cpp / the template):
//   p.x = (2*px+1)/W - 1 ; p.y = 1 - (2*py+1)/H ; p.z = 0 ; p.w = 0.
//
// FOLD STRUCTURE (multi-input recursion, root context "", subIndex 1, suffixes a/b; CombineFieldColor
// keeps i==0 then folds i==1 over the WHOLE float4):
//     f1a.w = dA;  f = f1a;                  // child A + keep (inputIndex 0)
//     f1b.w = dB;  f = <method>(f, f1b);     // child B fold (inputIndex 1)  -> root f.w = fold(dA, dB)
//
// CLOSED-FORM (dA = |p - cA| - 0.4 with cA=origin, dB = |p - cB| - 0.4 with cB=(0.6,0,0), z=0):
//   Add (1):       f.w = dA + dB
//   Mix (0,K=0.5): f.w = mix(dA, dB, 0.5) = dA + 0.5*(dB - dA)
// Hand-check at p=(0,0): dA = -0.4, dB = |0.6|-0.4 = +0.2.
//   Add -> -0.4 + 0.2 = -0.2.   Mix(K=0.5) -> mix(-0.4, +0.2, 0.5) = -0.1.  (task design intent)
//
// injectBug: configureCombineFieldColorBug(node, K, method, 1) corrupts the OP'S REAL postShaderCode
// emit (method() forces the Multiply fold instead of the selected one). The golden's primary case is
// Add; under injectBug the fold becomes dA*dB, so at p=(0,0) got = (-0.4)*(+0.2) = -0.08 != -0.2 -> RED.
// The tooth bites the OP's REAL emit (the fold builder), NOT the expected value (no tautology).
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
#include "runtime/field_node_registry.h"  // makeFieldNode (CombineFieldColorNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource

namespace sw {

// Param-cook + test seams owned by field_ops_combinefieldcolor.cpp (leaf type TU-private). Forward-
// declared here (no header), as selftests forward-declare golden entry points.
void configureCombineFieldColor(FieldNode& node, float k, int combineMethod);
void configureCombineFieldColorBug(FieldNode& node, float k, int combineMethod, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;

// The two sphere children: A at origin, B at (0.6,0,0) — so at a probe their distances differ.
constexpr float kCAx = 0.0f, kCBx = 0.6f, kSphR = 0.4f;

// CombineMethods enum values (must match field_ops_combinefieldcolor.cpp / CombineFieldColor.cs:82-87).
constexpr int kMix = 0, kAdd = 1;

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

// ---- local SphereSDF child (golden-only; verbatim SphereSDF distance with chosen center) -----------
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

// Host closed-form distances (z=0).
float distSphere(float px, float py, float cx) {
  const float dx = px - cx, dy = py;  // cz=0, p.z=0
  return std::sqrt(dx * dx + dy * dy) - kSphR;
}
float foldAdd(float px, float py) {
  return distSphere(px, py, kCAx) + distSphere(px, py, kCBx);
}
float foldMix(float px, float py, float k) {
  const float dA = distSphere(px, py, kCAx), dB = distSphere(px, py, kCBx);
  return dA + k * (dB - dA);  // mix(dA, dB, k)
}

// Build CombineFieldColor(SphereA@origin, SphereB@(0.6,0,0)). The combiner is the REGISTERED leaf.
// injectBug>0 routes through the test-only configure overload that corrupts the REAL fold emit.
std::shared_ptr<FieldNode> buildTree(float k, int combineMethod, int injectBug) {
  std::shared_ptr<FieldNode> combine = makeFieldNode("CombineFieldColor", "golden0");
  if (!combine) return nullptr;
  if (injectBug)
    configureCombineFieldColorBug(*combine, k, combineMethod, injectBug);
  else
    configureCombineFieldColor(*combine, k, combineMethod);
  combine->inputs.push_back(std::make_shared<GoldenSphere>("a", kCAx, 0.f, 0.f, kSphR));
  combine->inputs.push_back(std::make_shared<GoldenSphere>("b", kCBx, 0.f, 0.f, kSphR));
  return combine;
}

struct Probe { const char* name; uint32_t px, py; float expected; };

}  // namespace

int runFieldCombineFieldColorGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf(
        "[selftest-field-combinefieldcolor] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-combinefieldcolor] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  const int bugMode = injectBug ? 1 : 0;  // 1 = force Multiply fold in the REAL emit.

  const float kTol = 1e-5f;
  int rc = 0;

  const uint32_t cy = (kH - 1) / 2;  // 63 -> p.y ≈ 0.0078125
  auto pxFor = [](float target) -> uint32_t {
    float f = ((target + 1.0f) * kW - 1.0f) * 0.5f;
    int px = (int)std::lround(f);
    if (px < 0) px = 0;
    if (px >= (int)kW) px = kW - 1;
    return (uint32_t)px;
  };

  // ---- run one case: build the tree, render, probe ----
  auto runCase = [&](const char* caseName, float k, int method,
                     std::vector<Probe>& probes) -> bool {
    std::shared_ptr<FieldNode> tree = buildTree(k, method, bugMode);
    if (!tree) {
      std::printf("[selftest-field-combinefieldcolor] FAIL[%s]: CombineFieldColor factory not "
                  "registered\n",
                  caseName);
      rc = 1;
      return false;
    }
    clearTexOpCache();  // distinct topology/source per case
    MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
    if (!tex) {
      std::printf("[selftest-field-combinefieldcolor] FAIL[%s]: renderField2d null (compile/PSO "
                  "failure)\n",
                  caseName);
      rc = 1;
      return false;
    }
    std::vector<float> buf((size_t)kW * kH, 0.0f);
    tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
    auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

    for (Probe& pr : probes) {
      float px = pX(pr.px), py = pY(pr.py);
      float got = sampleAt(pr.px, pr.py);
      float diff = std::fabs(got - pr.expected);
      bool ok = diff <= kTol;
      if (!ok) rc = 1;
      std::printf("[selftest-field-combinefieldcolor] %-4s probe %-8s p=(% .4f,% .4f) got=% .6f "
                  "expected=% .6f diff=%.2e %s\n",
                  caseName, pr.name, px, py, got, pr.expected, diff, ok ? "OK" : "RED");
    }
    tex->release();
    return true;
  };

  // The probe EXPECTED values are computed from the host closed-form at the texel's EXACT p (robust to
  // the half-texel offset). The task's hand-derived ideal-p numbers (Add@origin=-0.2, Mix@origin=-0.1)
  // are the design intent; here we assert the formula at the real texel.

  // ---- Case A (PRIMARY, the injectBug target): Add(1) ----  f.w = dA + dB
  // Under injectBug the fold becomes dA*dB; at p≈0 that is (-0.4)(+0.2)=-0.08 != -0.2 -> RED.
  {
    uint32_t originx = pxFor(0.0f);  // p.x≈0   -> dA=-0.4, dB=+0.2 -> -0.2 (BOTH folded discriminator)
    uint32_t midx = pxFor(0.3f);     // p.x≈0.3 midpoint -> dA=-0.1, dB=-0.1 -> -0.2
    uint32_t farx = pxFor(0.9f);     // p.x≈0.9 -> dA=+0.5, dB=-0.1 -> +0.4
    std::vector<Probe> probes = {
        {"origin", originx, cy, foldAdd(pX(originx), pY(cy))},
        {"mid", midx, cy, foldAdd(pX(midx), pY(cy))},
        {"far", farx, cy, foldAdd(pX(farx), pY(cy))},
    };
    runCase("Add", 0.0f, kAdd, probes);
  }

  // ---- Case B: Mix(0), K=0.5 ----  f.w = mix(dA, dB, 0.5)
  // Under injectBug the fold becomes dA*dB regardless of method -> also RED at origin.
  {
    const float k = 0.5f;
    uint32_t originx = pxFor(0.0f);  // p.x≈0   -> mix(-0.4, +0.2, 0.5) = -0.1 (task design intent)
    uint32_t bcenx = pxFor(0.6f);    // p.x≈0.6 -> mix(+0.2, -0.4, 0.5) = -0.1 (B-side discriminator)
    std::vector<Probe> probes = {
        {"origin", originx, cy, foldMix(pX(originx), pY(cy), k)},
        {"bcenter", bcenx, cy, foldMix(pX(bcenx), pY(cy), k)},
    };
    runCase("Mix", k, kMix, probes);
  }

  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-combinefieldcolor] FAIL: injectBug did not trip any probe (tooth "
                  "has no bite)\n");
      return 1;
    }
    std::printf("[selftest-field-combinefieldcolor] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-combinefieldcolor] PASS\n");
  return rc;
}

}  // namespace sw
