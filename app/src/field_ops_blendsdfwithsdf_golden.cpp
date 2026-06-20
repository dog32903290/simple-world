// field_ops_blendsdfwithsdf_golden — --selftest-field-blendsdfwithsdf. GPU golden for the BlendSDFWithSDF
// 3-input custom-collect combiner. It blends FieldA/FieldB by a WeightField mask: f.w via the
// sdfBlendByMask helper, f.xyz via a smoothstep mix. The golden proves THREE things (blood-lesson
// discipline — static checks miss code that does not compile / mis-wires a subcontext):
//
//   Case A (f.w, smooth blend, Range=0.5):  f.w = sdfBlendByMask(dA, dB, dM - Offset, Range), the w>0
//     SMOOTH branch (smin/smax of the mask). Children: SphereA@origin, SphereB@(0.6,0,0), Mask@(0.3,0,0).
//   Case B (f.w, hard blend, Range=0):  the w<=0 EXACT branch (max/min). Same children.
//   Case C (f.xyz color blend):  colored FieldA/FieldB + a mask; a Readback wrapper surfaces f.xyz.x into
//     f.w so the standard R32Float readback exposes the mixed color. Proves the lerp->mix f.xyz fork.
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_staircombinesdf_golden.cpp).
//
// ★SHARED-KEY mixed-graph: BlendSDFWithSDF registers Globals["Common"] + Globals["sdfBlendByMask"]. The
//   golden tree is a single Blend node with sphere children (no other "Common" registrant) — so the
//   mixed-graph cross-op "Common" de-dup is proven SEPARATELY by the standalone xcrun metal compile the
//   agent ran (Blend + StairCombineSDF in one graph, both registering "Common" byte-identically -> ONE
//   copy, EXIT 0). Here the golden proves the helper trio compiles + computes; that compile already
//   exercises the de-dup machinery (map keyed by "Common"/"sdfBlendByMask").
//
// injectBug: configureBlendSdfWithSdf(node, ..., injectBug=1) corrupts the f.w blend (uses FieldB
//   directly, ignoring the mask) -> probe RED. Tooth bites the OP's REAL emit (the appendCall branch).
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

// Param-cook + test seam owned by field_ops_blendsdfwithsdf.cpp (leaf type TU-private). Forward-declared.
void configureBlendSdfWithSdf(FieldNode& node, float range, float offset, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;
constexpr float kAx = 0.0f, kBx = 0.6f, kMx = 0.3f;  // sphere centers (z=0)

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

// Golden-only sphere @ (cx,0,0): f.w = length(p - center) - r. Optional KNOWN f.xyz color (for Case C).
struct GoldenSphere : FieldNode {
  float cx, r;
  bool writeColor;
  float colR, colG, colB;
  GoldenSphere(const std::string& id, float centerX, float radius, bool wc = false, float cr = 0.f,
               float cg = 0.f, float cb = 0.f)
      : cx(centerX), r(radius), writeColor(wc), colR(cr), colG(cg), colB(cb) {
    prefix = "GSphere_" + id + "_";
  }
  void preShaderCode(CodeAssembleCtx& c, int) const override {
    const std::string ctx = c.ctx();
    c.appendCall("f" + ctx + ".w = length(p" + ctx + ".xyz - P." + prefix + "Center) - P." + prefix +
                 "Radius;");
    if (writeColor) {
      char buf[160];
      std::snprintf(buf, sizeof(buf), "f%s.xyz = float3(%.6f, %.6f, %.6f);", ctx.c_str(), colR, colG,
                    colB);
      c.appendCall(buf);
    }
  }
  void collectParams(std::vector<float>& fp, std::vector<std::string>& pf) const override {
    appendVec3Param(fp, pf, prefix + "Center", cx, 0.f, 0.f);
    appendScalarParam(fp, pf, prefix + "Radius", r);
  }
};

// Golden-only Readback wrapper (Case C): surfaces the blended f.xyz.x into f.w for the R32Float readback.
struct Readback : FieldNode {
  Readback() { prefix = "GReadback_"; }
  void preShaderCode(CodeAssembleCtx&, int) const override {}
  void postShaderCode(CodeAssembleCtx& c, int) const override {
    const std::string ctx = c.ctx();
    c.appendCall("f" + ctx + ".w = f" + ctx + ".xyz.x;");
  }
  void collectParams(std::vector<float>&, std::vector<std::string>&) const override {}
};

float dSphere(float gx, float gy, float cx) {
  const float dx = gx - cx, dy = gy;
  return std::sqrt(dx * dx + dy * dy) - kSphR;
}

// Host sdfBlendByMask (verbatim math; mix==lerp).
float hMix(float a, float b, float h) { return a * (1.0f - h) + b * h; }
float hSmin(float a, float b, float k) {
  float h = std::fmin(std::fmax(0.5f + 0.5f * (b - a) / k, 0.0f), 1.0f);
  return hMix(b, a, h) - k * h * (1.0f - h);
}
float hSMax(float a, float b, float k) { return -hSmin(-a, -b, k); }
float hBlend(float dA, float dB, float dM, float w) {
  if (w <= 0.0f) {
    float da = std::fmax(dA, dM);
    float db = std::fmax(dB, -dM);
    return std::fmin(da, db);
  }
  float da = hSMax(dA, dM, w);
  float db = hSMax(dB, -dM, w);
  return hSmin(da, db, w);
}
float hSmoothstep(float e0, float e1, float x) {
  float t = std::fmin(std::fmax((x - e0) / (e1 - e0), 0.0f), 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

std::shared_ptr<FieldNode> buildFwTree(float range, float offset, int injectBug) {
  std::shared_ptr<FieldNode> blend = makeFieldNode("BlendSDFWithSDF", "golden0");
  if (!blend) return nullptr;
  configureBlendSdfWithSdf(*blend, range, offset, injectBug);
  blend->inputs.push_back(std::make_shared<GoldenSphere>("a", kAx, kSphR));  // FieldA
  blend->inputs.push_back(std::make_shared<GoldenSphere>("b", kBx, kSphR));  // FieldB
  blend->inputs.push_back(std::make_shared<GoldenSphere>("m", kMx, kSphR));  // WeightField (mask)
  return blend;
}

// Case C: colored A/B + mask, wrapped in Readback to surface f.xyz.x.
constexpr float kColAr = 0.2f, kColBr = 0.9f;  // FieldA.r, FieldB.r (the channels that blend)
std::shared_ptr<FieldNode> buildColorTree(float range, float offset, int injectBug) {
  std::shared_ptr<FieldNode> blend = makeFieldNode("BlendSDFWithSDF", "golden0");
  if (!blend) return nullptr;
  configureBlendSdfWithSdf(*blend, range, offset, injectBug);
  blend->inputs.push_back(std::make_shared<GoldenSphere>("a", kAx, kSphR, true, kColAr, 0.f, 0.f));
  blend->inputs.push_back(std::make_shared<GoldenSphere>("b", kBx, kSphR, true, kColBr, 0.f, 0.f));
  blend->inputs.push_back(std::make_shared<GoldenSphere>("m", kMx, kSphR));
  auto readback = std::make_shared<Readback>();
  readback->inputs.push_back(blend);
  return readback;
}

struct Probe { const char* name; uint32_t px, py; float expected; };

}  // namespace

int runFieldBlendSdfWithSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-blendsdfwithsdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-blendsdfwithsdf] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  const int bugMode = injectBug ? 1 : 0;  // 1 = corrupt the f.w blend (production passes 0).
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

  auto runFwCase = [&](const char* caseName, float range, float offset, std::vector<Probe>& probes) {
    clearTexOpCache();
    std::shared_ptr<FieldNode> tree = buildFwTree(range, offset, bugMode);
    if (!tree) {
      std::printf("[selftest-field-blendsdfwithsdf] FAIL[%s]: factory not registered\n", caseName);
      rc = 1; return;
    }
    MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
    if (!tex) {
      std::printf("[selftest-field-blendsdfwithsdf] FAIL[%s]: renderField2d null (compile/PSO failure)\n",
                  caseName);
      rc = 1; return;
    }
    std::vector<float> buf((size_t)kW * kH, 0.0f);
    tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
    for (Probe& pr : probes) {
      const float gx = pX(pr.px), gy = pY(pr.py);
      const float got = buf[(size_t)pr.py * kW + pr.px];
      const float diff = std::fabs(got - pr.expected);
      const bool ok = diff <= kTol;
      if (!ok) rc = 1;
      std::printf("[selftest-field-blendsdfwithsdf] %-10s probe %-8s p=(% .4f,% .4f) got=% .6f "
                  "expected=% .6f diff=%.2e %s\n",
                  caseName, pr.name, gx, gy, got, pr.expected, diff, ok ? "OK" : "RED");
    }
    tex->release();
  };

  // ---- Case A: SMOOTH blend, Range=0.5, Offset=0 (w>0 branch) ----
  {
    const float range = 0.5f, offset = 0.0f;
    auto exp = [&](float gx, float gy) {
      const float dA = dSphere(gx, gy, kAx), dB = dSphere(gx, gy, kBx), dM = dSphere(gx, gy, kMx);
      return hBlend(dA, dB, dM - offset, range);
    };
    uint32_t midx = pxFor(0.3f), ax = pxFor(0.0f), bx = pxFor(0.6f);
    std::vector<Probe> probes = {
        {"mid", midx, cy, exp(pX(midx), pY(cy))},
        {"sideA", ax, cy, exp(pX(ax), pY(cy))},
        {"sideB", bx, cy, exp(pX(bx), pY(cy))},
    };
    runFwCase("smooth", range, offset, probes);
  }

  // ---- Case B: HARD blend, Range=0, Offset=0 (w<=0 exact branch) ----
  {
    const float range = 0.0f, offset = 0.0f;
    auto exp = [&](float gx, float gy) {
      const float dA = dSphere(gx, gy, kAx), dB = dSphere(gx, gy, kBx), dM = dSphere(gx, gy, kMx);
      return hBlend(dA, dB, dM - offset, range);
    };
    uint32_t midx = pxFor(0.3f), ax = pxFor(0.0f), bx = pxFor(0.6f);
    std::vector<Probe> probes = {
        {"mid", midx, cy, exp(pX(midx), pY(cy))},
        {"sideA", ax, cy, exp(pX(ax), pY(cy))},
        {"sideB", bx, cy, exp(pX(bx), pY(cy))},
    };
    runFwCase("hard", range, offset, probes);
  }

  // ---- Case C: f.xyz color blend (Readback surfaces f.xyz.x). Range=0.5, Offset=0 ----
  {
    const float range = 0.5f, offset = 0.0f;
    clearTexOpCache();
    std::shared_ptr<FieldNode> tree = buildColorTree(range, offset, bugMode);
    if (!tree) {
      std::printf("[selftest-field-blendsdfwithsdf] FAIL[color]: factory not registered\n");
      rc = 1;
    } else {
      MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
      if (!tex) {
        std::printf("[selftest-field-blendsdfwithsdf] FAIL[color]: renderField2d null\n");
        rc = 1;
      } else {
        std::vector<float> buf((size_t)kW * kH, 0.0f);
        tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
        // f.xyz.x = mix(colAr, colBr, smoothstep(0,1,(dM - offset)/range)). injectBug==1 (corrupt f.w)
        // does NOT touch the f.xyz line, so the color case is the cross-check on the f.w bug. We assert
        // the color blend only in the production (no-bug) path.
        auto expColor = [&](float gx, float gy) {
          const float dM = dSphere(gx, gy, kMx);
          const float t = hSmoothstep(0.f, 1.f, (dM - offset) / range);
          return hMix(kColAr, kColBr, t);
        };
        uint32_t midx = pxFor(0.3f), ax = pxFor(-0.2f), bx = pxFor(0.8f);
        std::vector<Probe> probes = {
            {"mid", midx, cy, expColor(pX(midx), pY(cy))},
            {"farA", ax, cy, expColor(pX(ax), pY(cy))},
            {"farB", bx, cy, expColor(pX(bx), pY(cy))},
        };
        for (Probe& pr : probes) {
          const float gx = pX(pr.px), gy = pY(pr.py);
          const float got = buf[(size_t)pr.py * kW + pr.px];
          const float diff = std::fabs(got - pr.expected);
          // In injectBug mode the f.w corruption doesn't change f.xyz, so this case stays green — it is
          // NOT the bug discriminator (Cases A/B are). We still print it; only fail in production mode.
          const bool ok = diff <= kTol;
          if (!ok && !injectBug) rc = 1;
          std::printf("[selftest-field-blendsdfwithsdf] %-10s probe %-8s p=(% .4f,% .4f) got=% .6f "
                      "expected=% .6f diff=%.2e %s\n",
                      "color", pr.name, gx, gy, got, pr.expected, diff, ok ? "OK" : "RED");
        }
        tex->release();
      }
    }
  }

  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-blendsdfwithsdf] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-blendsdfwithsdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-blendsdfwithsdf] PASS\n");
  return rc;
}

}  // namespace sw
