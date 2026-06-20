// field_ops_staircombinesdf_golden — --selftest-field-staircombinesdf. GPU DISTANCE-VALUE golden for the
// StairCombineSDF multi-input combiner (carpenter-joinery folds: stairs / columns / groove / tongue).
// Builds StairCombineSDF(GoldenSphere A, GoldenSphere B), assembles via the FROZEN base (multi-input
// sub-context path), compiles, renders, reads back R32Float (f.w into RED), and asserts each probe ==
// the closed-form fold of (dA, dB) at the texel's EXACT p. Mirrors field_ops_combinesdf_golden.cpp.
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_combinesdf_golden.cpp).
//
// TWO cases (the second exercises the ★CUT-94 by-value pMod1 swizzle COMPILE path — a blood-lesson:
// static checks miss code that does not compile, so the Columns mode is rendered, not just inspected):
//   Case A UnionStairs(3) [DEFAULT], K=0.5, Steps=3:
//     fOpUnionStairs(a,b,r,n) = min(min(a,b), 0.5*(u + a + abs(mod(u-a+s, 2*s) - s))), s=r/n, u=b-r.
//   Case B UnionColumns(0), K=0.5, Steps=3:
//     the Columns helper calls pR45(local float2) + pMod1(p.y swizzle) — the latter NEEDS the by-value
//     pMod1 to compile. The host replays the full helper (pR45 + pMod1 + the column-circle math).
//
// CLOSED-FORM children: GoldenSphere A @ origin, B @ (0.6,0,0), both r=0.4 (cf. combinesdf golden).
//   dA = |p| - 0.4 ; dB = |p - (0.6,0,0)| - 0.4 (z=0).
//
// injectBug: the MSL-string tier shift (same technique as field_ops_combinesdf_golden.cpp): shift every
// cooked distance by +1.0 -> all VALUE probes RED. (StairCombineSDF's fold lives in postShaderCode via a
// data-table fn name; the template-tier shift after the fold reddens the observed f.w uniformly. The
// per-mode fold correctness is established by the two distinct closed forms above — a dropped/mis-named
// fold would leave f.w at dA (the kept accumulator) or the seed, which the Steps/K-sensitive expected
// values do not equal.)
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

// Param-cook seam owned by field_ops_staircombinesdf.cpp (leaf type TU-private). Forward-declared here.
void configureStairCombineSdf(FieldNode& node, float k, float steps, int combineMethod);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kCAx = 0.0f, kCBx = 0.6f, kSphR = 0.4f;
constexpr int kUnionStairs = 3, kUnionColumns = 0;

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

float distSphere(float px, float py, float cx) {
  const float dx = px - cx, dy = py;
  return std::sqrt(dx * dx + dy * dy) - kSphR;
}

float hmod(float x, float y) { return x - y * std::floor(x / y); }

// Host fOpUnionStairs (verbatim math).
float foldUnionStairs(float a, float b, float r, float n) {
  const float s = r / n;
  const float u = b - r;
  return std::fmin(std::fmin(a, b), 0.5f * (u + a + std::fabs(hmod(u - a + s, 2.0f * s) - s)));
}

// Host pR45 / pMod1 (by-value forms matching the registered CommonHgSdf).
void hPR45(float& x, float& y) {
  const float k = std::sqrt(0.5f);
  const float nx = (x + y) * k;
  const float ny = (y + (-x)) * k;  // (p + float2(p.y,-p.x)) -> y' = p.y + (-p.x)
  x = nx;
  y = ny;
}
float hPMod1(float p, float size) {
  const float hs = size * 0.5f;
  return hmod(p + hs, size) - hs;
}

// Host fOpUnionColumns (verbatim).
float foldUnionColumns(float a, float b, float r, float n) {
  if ((a < r) && (b < r)) {
    float px = a, py = b;
    const float cr = r * std::sqrt(2.0f) / ((n - 1.0f) * 2.0f + std::sqrt(2.0f));
    hPR45(px, py);
    px -= std::sqrt(2.0f) / 2.0f * r;
    px += cr * std::sqrt(2.0f);
    if (hmod(n, 2.0f) == 1.0f) py += cr;
    py = hPMod1(py, cr * 2.0f);
    float result = std::sqrt(px * px + py * py) - cr;
    result = std::fmin(result, px);
    result = std::fmin(result, a);
    return std::fmin(result, b);
  }
  return std::fmin(a, b);
}

std::shared_ptr<FieldNode> buildTree(float k, float steps, int method) {
  std::shared_ptr<FieldNode> combine = makeFieldNode("StairCombineSDF", "golden0");
  if (!combine) return nullptr;
  configureStairCombineSdf(*combine, k, steps, method);
  combine->inputs.push_back(std::make_shared<GoldenSphere>("a", kCAx, 0.f, 0.f, kSphR));
  combine->inputs.push_back(std::make_shared<GoldenSphere>("b", kCBx, 0.f, 0.f, kSphR));
  return combine;
}

struct Probe { const char* name; uint32_t px, py; float expected; };

}  // namespace

int runFieldStairCombineSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-staircombinesdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-staircombinesdf] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  std::string useTmpl = tmpl;
  if (injectBug) {
    const std::string from = "float4(f.w, 0.0, 0.0, 1.0)";
    const std::string to = "float4(f.w + 1.0, 0.0, 0.0, 1.0)";
    size_t pos = useTmpl.find(from);
    if (pos == std::string::npos) {
      std::printf("[selftest-field-staircombinesdf] FAIL: injectBug could not find the distance-write "
                  "site in the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

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

  auto runCase = [&](const char* caseName, float k, float steps, int method,
                     std::vector<Probe>& probes) {
    clearTexOpCache();  // distinct topology/source per case.
    std::shared_ptr<FieldNode> tree = buildTree(k, steps, method);
    if (!tree) {
      std::printf("[selftest-field-staircombinesdf] FAIL[%s]: factory not registered\n", caseName);
      rc = 1;
      return;
    }
    MTL::Texture* tex = renderField2d(dev, q, tree, useTmpl, kW, kH);
    if (!tex) {
      std::printf("[selftest-field-staircombinesdf] FAIL[%s]: renderField2d null (compile/PSO failure)\n",
                  caseName);
      rc = 1;
      return;
    }
    std::vector<float> buf((size_t)kW * kH, 0.0f);
    tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
    for (Probe& pr : probes) {
      const float px = pX(pr.px), py = pY(pr.py);
      const float got = buf[(size_t)pr.py * kW + pr.px];
      const float diff = std::fabs(got - pr.expected);
      const bool ok = diff <= kTol;
      if (!ok) rc = 1;
      std::printf("[selftest-field-staircombinesdf] %-12s probe %-8s p=(% .4f,% .4f) got=% .6f "
                  "expected=% .6f diff=%.2e %s\n",
                  caseName, pr.name, px, py, got, pr.expected, diff, ok ? "OK" : "RED");
    }
    tex->release();
  };

  // ---- Case A: UnionStairs(3), K=0.5, Steps=3 (DEFAULT mode) ----
  {
    const float k = 0.5f, n = 3.0f;
    uint32_t betweenx = pxFor(0.3f);  // overlap zone where the stair fold is active
    uint32_t insideAx = pxFor(0.0f);  // inside A
    uint32_t outsidex = pxFor(-0.6f); // outside both on A's side
    std::vector<Probe> probes = {
        {"between", betweenx, cy,
         foldUnionStairs(distSphere(pX(betweenx), pY(cy), kCAx),
                         distSphere(pX(betweenx), pY(cy), kCBx), k, n)},
        {"insideA", insideAx, cy,
         foldUnionStairs(distSphere(pX(insideAx), pY(cy), kCAx),
                         distSphere(pX(insideAx), pY(cy), kCBx), k, n)},
        {"outside", outsidex, cy,
         foldUnionStairs(distSphere(pX(outsidex), pY(cy), kCAx),
                         distSphere(pX(outsidex), pY(cy), kCBx), k, n)},
    };
    runCase("UnionStairs", k, n, kUnionStairs, probes);
  }

  // ---- Case B: UnionColumns(0), K=0.5, Steps=3 (the CUT-94 by-value pMod1 swizzle COMPILE+value path) -
  {
    const float k = 0.5f, n = 3.0f;
    uint32_t betweenx = pxFor(0.3f);  // overlap zone (both dA,dB < r -> the column branch is taken)
    uint32_t insideAx = pxFor(0.0f);  // inside A
    std::vector<Probe> probes = {
        {"between", betweenx, cy,
         foldUnionColumns(distSphere(pX(betweenx), pY(cy), kCAx),
                          distSphere(pX(betweenx), pY(cy), kCBx), k, n)},
        {"insideA", insideAx, cy,
         foldUnionColumns(distSphere(pX(insideAx), pY(cy), kCAx),
                          distSphere(pX(insideAx), pY(cy), kCBx), k, n)},
    };
    runCase("UnionColumns", k, n, kUnionColumns, probes);
  }

  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-staircombinesdf] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-staircombinesdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-staircombinesdf] PASS\n");
  return rc;
}

}  // namespace sw
