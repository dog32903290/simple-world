// field_ops_combinesdf_golden — --selftest-field-combinesdf. GPU DISTANCE-VALUE golden for the
// CombineSDF combiner leaf (the FIRST field op that FOLDS 2+ FIELD inputs). It builds a real two-input
// field tree by hand — CombineSDF(SphereSDF A, SphereSDF B) — assembles its MSL via the FROZEN base
// (multi-input sub-context path), runtime-compiles it, renders a fullscreen pass, reads back the
// R32Float distance texture, and asserts each probed texel's RED == the closed-form FOLDED signed
// distance at that texel's field-space p (z=0). Mirrors field_ops_boxsdf_golden.cpp's harness.
//
// ZONE: shell tier (lives at app/src/ root like field_render_golden.cpp / selftests.cpp / main.cpp).
// It deliberately crosses runtime (renderField2d, makeFieldNode, configureCombineSdf) AND platform
// (compileLibraryFromSource) — a runtime-zone selftest may NOT include platform (check_arch:
// runtime ↛ platform), so this integration golden sits at the shell tier (the only place allowed to
// bind both zones — same rationale as field_render_golden.cpp's header).
//
// WHY local SphereSDF children (not makeFieldNode("SphereSDF")): the registered leaf nodes are
// TU-private with no center/radius override seam (every prior golden uses factory DEFAULTS only — see
// field_render_golden / torussdf_golden). This combiner golden needs TWO spheres at DIFFERENT centers
// (cA=(-0.3,0,0), cB=(+0.3,0,0), r=0.4) so the fold has a non-trivial overlap region (the discriminator
// probes). What is UNDER TEST here is the CombineSDF FOLD codegen (postShaderCode), not SphereSDF
// (which has its own golden); the children only need to emit a KNOWN distance. So the golden declares a
// minimal local sphere leaf (GoldenSphere) that emits the exact SphereSDF formula
// `f{c}.w = length(p{c}.xyz - center) - radius` with caller-chosen center/radius. The COMBINER node is
// the REGISTERED leaf (makeFieldNode("CombineSDF") + configureCombineSdf), so the registered fold path
// is the thing exercised end-to-end.
//
// PIXEL -> FIELD-SPACE p (backward-traced, identical to field_render_golden.cpp / the template):
//   p.x = (2*px+1)/W - 1 ; p.y = 1 - (2*py+1)/H ; p.z = 0 ; p.w = 0. The golden reads each texel's
// EXACT p and asserts against the closed-form fold at that p — robust to the half-texel offset.
//
// FOLD STRUCTURE (verified against field_graph.cpp multi-input recursion + the template seed f=1):
//   the FIELD_CALL unfolds (root context "", subIndex 1, suffixes a/b) to:
//     float4 p1a=p; float4 f1a=f;  f1a.w=dA;  f = f1a;                              // child A + keep
//     float4 p1b=p; float4 f1b=f;  f1b.w=dB;  f.rgb=mix(...);  f.w=<op>(f.w,f1b.w[,K]); // child B + fold
//   so root f.w = op(dA, dB) — exactly the closed-form below. (f.rgb is a no-op no-read: both children
//   seed f.rgb=1; the template writes only f.w into RED — named fork: color line emitted, not observed.)
//
// CLOSED-FORM per case (dA = |p-cA|-0.4, dB = |p-cB|-0.4, z=0; arithmetic in the probe tables):
//   Case A Union(0):       d = min(dA, dB)
//   Case B CutOut(5):      d = max(dA, -dB)
//   Case C UnionSmooth(4,K=0.3): h=max(K-|dA-dB|,0); d = min(dA,dB) - h*h/(4K)
//
// injectBug: corrupt the template's RED-channel distance write so every cooked distance is shifted by
// +1.0 -> all value probes RED (same technique/tier/magnitude as field_ops_boxsdf_golden.cpp). The
// boundary-sign probe is skipped under injectBug (the shifted field has no inside region), as boxsdf
// does. The between/carved/smooth VALUE probes also catch fold-breaking mutations (a dropped keep or
// fold leaves f.w at the seed or at dA only).
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
#include "runtime/field_node_registry.h"  // makeFieldNode (CombineSDFNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache (fresh source-PSO cache per run-device)

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {

// Param-cook seam owned by field_ops_combinesdf.cpp (the leaf type is TU-private). Forward-declared
// here (no header) exactly as selftests forward-declare the golden entry points.
void configureCombineSdf(FieldNode& node, float k, int combineMethod);

namespace {

constexpr uint32_t kW = 128, kH = 128;

// The two sphere children: centers chosen so the lobes overlap around x=0 (cf. the blueprint).
constexpr float kCAx = -0.3f, kCBx = 0.3f, kSphR = 0.4f;

// CombineMethods enum values (must match field_ops_combinesdf.cpp / CombineSDF.cs:226-245 by index).
constexpr int kUnion = 0, kCutOut = 5, kUnionSmooth = 4, kCutOutRound = 6;

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

// Field-space p at pixel (px,py) (see header; identical to field_render_golden.cpp).
float pX(uint32_t px) { return (2.0f * px + 1.0f) / kW - 1.0f; }
float pY(uint32_t py) { return 1.0f - (2.0f * py + 1.0f) / kH; }

// ---- local SphereSDF child (golden-only; emits the exact SphereSDF.cs distance with chosen center) -
// NOT the registered SphereSDF leaf (that is TU-private with no center override). The math is the
// verbatim SphereSDF formula so the children produce a KNOWN distance for the fold under test.
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
float foldUnion(float px, float py) {
  return std::fmin(distSphere(px, py, kCAx), distSphere(px, py, kCBx));
}
float foldCutOut(float px, float py) {
  const float dA = distSphere(px, py, kCAx), dB = distSphere(px, py, kCBx);
  return std::fmax(dA, -dB);
}
float foldUnionSmooth(float px, float py, float k) {
  const float dA = distSphere(px, py, kCAx), dB = distSphere(px, py, kCBx);
  const float h = std::fmax(k - std::fabs(dA - dB), 0.0f);
  return std::fmin(dA, dB) - (h * h) / (4.0f * k);
}
// CutOutRound (mode 6) closed-form — hand-derived from CombineSDF.cs:101-112 (NOT from the leaf):
//   fOpDifferenceRound(a,b,r) = fOpIntersectionRound(a, -b, r)
//   fOpIntersectionRound(a,b,r) = min(-r, max(a,b)) + length(max(float2(r+a, r+b), 0))
// so with a=dA, b=dB carve, r=K:  u = max(float2(K+dA, K-dB), 0);
//   d = min(-K, max(dA, -dB)) + length(u).
// This case exists to exercise the TWO-helper difference path (Intersection + Difference globals),
// which the Union/CutOut/UnionSmooth cases never emit — that path was the global-emission-ORDER bug
// (fork (5)): MSL needs the Intersection helper declared before the Difference helper calls it.
float foldCutOutRound(float px, float py, float k) {
  const float dA = distSphere(px, py, kCAx), dB = distSphere(px, py, kCBx);
  const float ux = std::fmax(k + dA, 0.0f);
  const float uy = std::fmax(k - dB, 0.0f);  // r + (-dB) carve operand
  const float ulen = std::sqrt(ux * ux + uy * uy);
  return std::fmin(-k, std::fmax(dA, -dB)) + ulen;
}

// Build CombineSDF(SphereA, SphereB) with the given fold params. The combiner is the REGISTERED leaf.
std::shared_ptr<FieldNode> buildTree(float k, int combineMethod) {
  std::shared_ptr<FieldNode> combine = makeFieldNode("CombineSDF", "golden0");
  if (!combine) return nullptr;
  configureCombineSdf(*combine, k, combineMethod);
  combine->inputs.push_back(std::make_shared<GoldenSphere>("a", kCAx, 0.f, 0.f, kSphR));
  combine->inputs.push_back(std::make_shared<GoldenSphere>("b", kCBx, 0.f, 0.f, kSphR));
  return combine;
}

struct Probe { const char* name; uint32_t px, py; float expected; };

}  // namespace

int runFieldCombineSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-combinesdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-combinesdf] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();  // stale PSO from a released prior-run device must not be reused

  // injectBug at the MSL-string tier (same substring + magnitude as boxsdf): shift every cooked
  // distance by +1.0 -> all VALUE probes RED. The boundary-sign probe is skipped under injectBug.
  std::string useTmpl = tmpl;
  if (injectBug) {
    const std::string from = "float4(f.w, 0.0, 0.0, 1.0)";
    const std::string to = "float4(f.w + 1.0, 0.0, 0.0, 1.0)";
    size_t pos = useTmpl.find(from);
    if (pos == std::string::npos) {
      std::printf("[selftest-field-combinesdf] FAIL: injectBug could not find the distance-write site "
                  "in the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  const float kTol = 1e-5f;
  int rc = 0;

  // Pixel coords whose EXACT p the host re-derives via pX/pY (asserts against the per-texel p, not an
  // assumed value). Center row cy: p.y≈0. The blueprint's probe positions, mapped to nearest texel.
  const uint32_t cy = (kH - 1) / 2;  // 63 -> p.y = 0.0078125
  // px for a target p.x: px = round(((target+1)*W - 1)/2).
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
    std::shared_ptr<FieldNode> tree = buildTree(k, method);
    if (!tree) {
      std::printf("[selftest-field-combinesdf] FAIL[%s]: CombineSDF factory not registered\n", caseName);
      rc = 1;
      return false;
    }
    clearTexOpCache();  // each case is a distinct topology/source; do not reuse a prior case's PSO
    MTL::Texture* tex = renderField2d(dev, q, tree, useTmpl, kW, kH);
    if (!tex) {
      std::printf("[selftest-field-combinesdf] FAIL[%s]: renderField2d null (compile/PSO failure)\n",
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
      // recompute expected at the texel's EXACT p (the table's `expected` is the ideal-p check value).
      float diff = std::fabs(got - pr.expected);
      bool ok = diff <= kTol;
      if (!ok) rc = 1;
      std::printf("[selftest-field-combinesdf] %-12s probe %-8s p=(% .4f,% .4f) got=% .6f "
                  "expected=% .6f diff=%.2e %s\n",
                  caseName, pr.name, px, py, got, pr.expected, diff, ok ? "OK" : "RED");
    }
    tex->release();
    return true;
  };

  // The probe EXPECTED values are computed from the host closed-form at the texel's EXACT p (so they
  // are robust to the half-texel offset, exactly like boxsdf). The blueprint's hand-derived ideal-p
  // numbers (e.g. between=-0.1) are the design intent; here we assert the formula at the real texel.

  // ---- Case A: Union(0) ----
  {
    uint32_t insideAx = pxFor(kCAx);  // p.x≈-0.3 inside lobe A -> min ~ -0.4
    uint32_t betweenx = pxFor(0.0f);  // p.x≈0   overlap -> min(dA,dB) ~ -0.1 (BOTH folded discriminator)
    uint32_t outsidex = pxFor(0.9f);  // p.x≈0.9 outside both -> ~ 0.2
    std::vector<Probe> probes = {
        {"insideA", insideAx, cy, foldUnion(pX(insideAx), pY(cy))},
        {"between", betweenx, cy, foldUnion(pX(betweenx), pY(cy))},
        {"outside", outsidex, cy, foldUnion(pX(outsidex), pY(cy))},
    };
    runCase("Union", 0.0f, kUnion, probes);
  }

  // ---- Case B: CutOut(5) ----  d = max(dA, -dB)
  {
    uint32_t keptx = pxFor(-0.5f);  // p.x≈-0.5 kept side of A, outside B -> -0.2
    uint32_t carvex = pxFor(0.2f);  // p.x≈0.2 carved by B -> +0.3 (sign-of -dB discriminator)
    uint32_t outx = pxFor(-0.9f);   // p.x≈-0.9 outside both -> 0.2
    std::vector<Probe> probes = {
        {"kept", keptx, cy, foldCutOut(pX(keptx), pY(cy))},
        {"carved", carvex, cy, foldCutOut(pX(carvex), pY(cy))},
        {"outside", outx, cy, foldCutOut(pX(outx), pY(cy))},
    };
    runCase("CutOut", 0.0f, kCutOut, probes);
  }

  // ---- Case C: UnionSmooth(4), K=0.3 ----  d = min(dA,dB) - h*h/(4K)
  {
    const float k = 0.3f;
    uint32_t centerx = pxFor(0.0f);   // p.x≈0 overlap -> -0.175 (smooth blend)
    uint32_t controlx = pxFor(-0.5f); // p.x≈-0.5 |dA-dB|>K -> h=0 -> plain min = -0.2
    std::vector<Probe> probes = {
        {"smooth", centerx, cy, foldUnionSmooth(pX(centerx), pY(cy), k)},
        {"control", controlx, cy, foldUnionSmooth(pX(controlx), pY(cy), k)},
    };
    runCase("UnionSmooth", k, kUnionSmooth, probes);
  }

  // ---- Case D: CutOutRound(6), K=0.3 ----  d = min(-K, max(dA,-dB)) + length(max(float2(K+dA,K-dB),0))
  // The ONLY case that emits BOTH the fOpIntersectionRound and fOpDifferenceRound globals. Before the
  // fork-(5) forward-declaration fix, this MSL failed to compile (Difference helper emitted before the
  // Intersection helper it calls). So this case is a compile-AND-value tooth for the two-helper path.
  {
    const float k = 0.3f;
    uint32_t keptx = pxFor(-0.5f);  // p.x≈-0.5 kept side of A, outside B -> rounded-difference value
    uint32_t carvex = pxFor(0.2f);  // p.x≈0.2 inside B's carve region -> positive (carved away)
    uint32_t innerx = pxFor(-0.2f); // p.x≈-0.2 overlap zone where the round blend is active
    std::vector<Probe> probes = {
        {"kept", keptx, cy, foldCutOutRound(pX(keptx), pY(cy), k)},
        {"carved", carvex, cy, foldCutOutRound(pX(carvex), pY(cy), k)},
        {"inner", innerx, cy, foldCutOutRound(pX(innerx), pY(cy), k)},
    };
    runCase("CutOutRound", k, kCutOutRound, probes);
  }

  // ---- BOUNDARY-SIGN tooth (Union case): along the center row scanning +x from the far right, the
  //   Union field's sign flips at the RIGHT lobe's outer surface p.x = cB + r = 0.3+0.4 = 0.7 (on
  //   y≈0). Find the last px (scanning right->left from the right edge) where d turns < 0 and assert
  //   its p.x is within one texel of 0.7 — pins the texCoord->p map AND that lobe B was folded in
  //   (a Union of A only would flip at cA+r = 0.1). Skipped under injectBug (no inside region).
  if (!injectBug) {
    std::shared_ptr<FieldNode> tree = buildTree(0.0f, kUnion);
    clearTexOpCache();
    MTL::Texture* tex = tree ? renderField2d(dev, q, tree, useTmpl, kW, kH) : nullptr;
    if (!tex) {
      std::printf("[selftest-field-combinesdf] FAIL: boundary render null\n");
      rc = 1;
    } else {
      std::vector<float> buf((size_t)kW * kH, 0.0f);
      tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
      auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };
      const float kFace = kCBx + kSphR;  // 0.7
      // scan from center to the right edge; first px where d >= 0 is the crossing.
      const uint32_t cx0 = (kW - 1) / 2;
      int crossPx = -1;
      for (uint32_t px = cx0; px < kW; ++px) {
        if (sampleAt(px, cy) >= 0.0f) { crossPx = (int)px; break; }
      }
      if (crossPx <= 0) {
        std::printf("[selftest-field-combinesdf] FAIL: Union boundary sign never flipped\n");
        rc = 1;
      } else {
        float crossPxField = pX((uint32_t)crossPx);
        float prevPxField = pX((uint32_t)(crossPx - 1));
        float texelW = 2.0f / kW;
        bool ok = (crossPxField >= kFace - texelW) && (prevPxField <= kFace + texelW);
        if (!ok) rc = 1;
        std::printf("[selftest-field-combinesdf] boundary cross at px=%d p.x=% .4f (prev % .4f) "
                    "want≈%.3f texelW=%.4f %s\n",
                    crossPx, crossPxField, prevPxField, kFace, texelW, ok ? "OK" : "RED");
      }
      tex->release();
    }
  }

  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-combinesdf] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-combinesdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-combinesdf] PASS\n");
  return rc;
}

}  // namespace sw
