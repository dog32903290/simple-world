// field_ops_spatialdisplacesdf_golden — --selftest-field-spatialdisplacesdf. GPU DISTANCE-VALUE golden
// for the SpatialDisplaceSDF single-input PRE-wrap MODIFIER (warps the SAMPLE POINT p by a per-axis 3D
// simplex-noise vector BEFORE the child is evaluated). Builds SpatialDisplaceSDF(GoldenSphere @ origin),
// assembles via the FROZEN base, compiles, renders, reads back R32Float (f.w into RED). Mirrors
// field_ops_bendfield_golden.cpp.
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_combinesdf_golden.cpp).
//
// TWO-PRONGED tooth (same rationale as field_ops_noisedisplacesdf_golden.cpp — avoid a fragile host
// re-implementation of the simplex body × the vNoise 3-axis wrapper, while biting the OP's real emit):
//
//   PRONG 1 (EXACT closed-form, Amount=0): with Amount=0 the warp vector is ZERO, so the child samples
//     the unwarped p and field(p) = |p| - r exactly. This is a byte-parity check that ALSO proves the two
//     globals (fSimplexNoiseDisplace + fSimplexNoiseDisplace3D) compile in the favourable std::map KEY
//     order (vNoise calls fSimplexNoiseDisplace, which must be emitted first — a forward-ref failure or a
//     bad swizzle would null renderField2d -> FAIL). The Cut94 "compiles AND runs at its identity point".
//
//   PRONG 2 (WARP-PRESENT, Amount=0.3): with Amount!=0 the warp shifts p, so the rendered f.w differs
//     from the bare sphere distance at noise-LIVE coords (Scale=0.7 off the integer lattice). The golden
//     asserts the value MOVED off the no-warp baseline by a meaningful margin AND is deterministic across
//     identical renders. This bites injectBug=1 (drop the pre warp line): with the warp gone, Amount=0.3
//     reads the SAME bare distance -> the "warp present" assertion FAILS -> RED.
//
// MID-BAND STRUCTURAL TEETH (P2 fix — PRONG 1 is the identity point, PRONG 2 only "moved+deterministic").
// The displacement contains simplex noise, so per GOLDEN_STANDARD we do NOT hand-push noise values;
// instead we assert EXACT algebraic laws the TiXL formula mandates at Amount!=0
// (SpatialDisplaceSDF.cs @ repo tixl3d/tixl, locked SHA 395c4c55):
//   TOOTH 3 (AMOUNT-QUADRATIC + DIRECTION LAW; .cs:114-116/:122-128/:133-137): the helper is
//     simplexNoise3D(pos/scale + offset) * amount -> Amount is an OUTER LINEAR multiplier, so the warp is
//     p' = p + A*N(p) with N fixed per pixel; with SamplePos=0, vScale=(1,1,1) the three axis calls are
//     IDENTICAL (.cs:124-126) -> N = n(p)*(1,1,1). Child = sphere:
//       q(A) := (got(A)+r)^2 = |p|^2 + 2A*(p.N) + A^2*|N|^2  — EXACTLY quadratic in A, q(0)=|p|^2 closed
//       form; and with p=(px,py,0), S:=px+py:  3*(p.N)^2 == |N|^2 * S^2  (the (1,1,1)-direction law).
//     Renders at A=0.15/0.30/0.45: third finite difference ~0 (nonlinear Amount plumbing -> RED); the
//     direction law reds a dropped/duplicated warp axis (off by exactly n^2*S^2).
//   TOOTH 4 (SCALE*VSCALE EQUIVALENCE; .cs:124-126): per-axis noise scale = scale*vscale.axis, so
//     (Scale=0.35, vScale=1) vs (Scale=0.7, vScale=0.5) — 0.7f*0.5f==0.35f bit-exact, rest identical —
//     must render EQUAL. vScale additive / ignored / applied to pos instead of scale -> RED.
//   TOOTH 5 (SCALE-POLARITY PIN; .cs:114-116 arg = pos/scale + offset, .cs:136 passes -Offset): at a
//     probe pixel with exact field-space p0, (Scale=1, Offset=0) and (Scale=0.5, Offset=p0) sample the
//     SAME arg there: pos/0.5 - p0 = 2*p0 - p0 = p0 (exact fp) -> equal values. Pins what equivalences
//     can't: pos*scale polarity, doubled scale, offset scaled by scale, offset sign -> all RED.
//   KNOWN LIMIT (needs-noise-oracle): a CONSISTENT axis permutation (vscale.yzx feeding xN/yN/zN, or the
//     warp vector permuted) is invisible to every metamorphic tooth above (both sides permute together;
//     TOOTH 3's config is axis-symmetric). Pinning axis identity needs the host simplexNoise3D
//     transcription oracle — owned by the noisedisplace lane; extend when it lands.
//
// injectBug: configureSpatialDisplaceSdf(node, ..., injectBug=1) DROPS the OP's REAL pre warp line. Under
//   the bug PRONG 2's "value moved" assertion fails -> RED. Tooth bites the OP's emit, not a tautology.
//   TEETH 3-5 are skipped under injectBug (trivially "pass" on an unwarped field — the bite contract
//   stays exactly PRONG 2's moved-check, same as before this P2 fix).
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

// Param-cook + test seam owned by field_ops_spatialdisplacesdf.cpp (leaf type TU-private). Forward-decl.
void configureSpatialDisplaceSdf(FieldNode& node, float amount, float scale, float vsx, float vsy,
                                 float vsz, float ox, float oy, float oz, float spx, float spy,
                                 float spz, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;
constexpr float kScale = 0.7f;

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

// Bare child distance — the Amount=0 closed-form AND the no-warp baseline.
float baseField(float px, float py) { return std::sqrt(px * px + py * py) - kSphR; }

std::shared_ptr<FieldNode> buildTree(float amount, int injectBug) {
  std::shared_ptr<FieldNode> mod = makeFieldNode("SpatialDisplaceSDF", "golden0");
  if (!mod) return nullptr;
  // vScale=(1,1,1), Offset=0, SamplePos=(0.3,0.5,0.7) so each axis samples a distinct noise location
  // (a zero SamplePos would make all three axes sample the same noise -> still valid, but a non-zero
  // SamplePos exercises the float3(spos.x,0,0) etc. offsets inside vNoise).
  configureSpatialDisplaceSdf(*mod, amount, kScale, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 0.3f, 0.5f, 0.7f,
                              injectBug);
  mod->inputs.push_back(std::make_shared<GoldenSphere>("a", 0.f, 0.f, 0.f, kSphR));
  return mod;
}

std::vector<float> render(MTL::Device* dev, MTL::CommandQueue* q, const std::string& tmpl, float amount,
                          int injectBug) {
  clearTexOpCache();
  std::shared_ptr<FieldNode> tree = buildTree(amount, injectBug);
  if (!tree) return {};
  MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
  if (!tex) return {};
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  tex->release();
  return buf;
}

// Full-config variant for the mid-band structural teeth (TOOTH 3-5) — the fixed-config buildTree above
// stays untouched (PRONG 1/2 preserved verbatim). Production path only (injectBug=0).
struct Cfg {
  float amount, scale;
  float vsx, vsy, vsz, ox, oy, oz, spx, spy, spz;
};

// NO clearTexOpCache inside: same device + same assembled MSL across configs (params live in the
// buffer, not the source) -> the cached PSO stays valid; render() already cleared on this device.
std::vector<float> renderCfg(MTL::Device* dev, MTL::CommandQueue* q, const std::string& tmpl,
                             const Cfg& c) {
  std::shared_ptr<FieldNode> mod = makeFieldNode("SpatialDisplaceSDF", "golden0");
  if (!mod) return {};
  configureSpatialDisplaceSdf(*mod, c.amount, c.scale, c.vsx, c.vsy, c.vsz, c.ox, c.oy, c.oz, c.spx,
                              c.spy, c.spz, /*injectBug=*/0);
  mod->inputs.push_back(std::make_shared<GoldenSphere>("a", 0.f, 0.f, 0.f, kSphR));
  MTL::Texture* tex = renderField2d(dev, q, mod, tmpl, kW, kH);
  if (!tex) return {};
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  tex->release();
  return buf;
}

}  // namespace

int runFieldSpatialDisplaceSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-spatialdisplacesdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-spatialdisplacesdf] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  const int bugMode = injectBug ? 1 : 0;  // 1 = drop the pre warp line (production passes 0).
  int rc = 0;
  const uint32_t cy = (kH - 1) / 2;
  auto pxFor = [](float target) -> uint32_t {
    float f = ((target + 1.0f) * kW - 1.0f) * 0.5f;
    int px = (int)std::lround(f);
    if (px < 0) px = 0;
    if (px >= (int)kW) px = kW - 1;
    return (uint32_t)px;
  };
  auto pyFor = [](float target) -> uint32_t {
    float f = ((1.0f - target) * kH - 1.0f) * 0.5f;
    int py = (int)std::lround(f);
    if (py < 0) py = 0;
    if (py >= (int)kH) py = kH - 1;
    return (uint32_t)py;
  };

  // ---- PRONG 1: Amount=0 exact closed-form (compiles+runs; warp == identity) ----
  std::vector<float> buf0 = render(dev, q, tmpl, 0.0f, bugMode);
  if (buf0.empty()) {
    std::printf("[selftest-field-spatialdisplacesdf] FAIL: Amount=0 render null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  struct P { const char* name; uint32_t px, py; };
  std::vector<P> probes = {{"a", pxFor(0.3f), cy}, {"b", pxFor(-0.2f), pyFor(0.25f)},
                           {"c", pxFor(0.45f), pyFor(-0.35f)}};
  const float kTol = 1e-5f;
  for (P& pr : probes) {
    const float px = pX(pr.px), py = pY(pr.py);
    const float expected = baseField(px, py);  // |p|-r (Amount=0 -> warp is zero)
    const float got = buf0[(size_t)pr.py * kW + pr.px];
    const float diff = std::fabs(got - expected);
    const bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-spatialdisplacesdf] amount0 %-2s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  // ---- PRONG 2: Amount=0.3 warp-present (must move off the Amount=0 baseline) ----
  std::vector<float> bufN = render(dev, q, tmpl, 0.3f, bugMode);
  if (bufN.empty()) {
    std::printf("[selftest-field-spatialdisplacesdf] FAIL: Amount=0.3 render null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  std::vector<float> bufN2 = render(dev, q, tmpl, 0.3f, bugMode);
  const float kMinMove = 1e-3f;
  float totalMove = 0.0f;
  bool deterministic = !bufN2.empty();
  for (P& pr : probes) {
    const float px = pX(pr.px), py = pY(pr.py);
    const float baseline = baseField(px, py);  // |p|-r (= bufN under injectBug)
    const float gotN = bufN[(size_t)pr.py * kW + pr.px];
    totalMove += std::fabs(gotN - baseline);
    if (deterministic) {
      const float gotN2 = bufN2[(size_t)pr.py * kW + pr.px];
      if (std::fabs(gotN - gotN2) > 1e-6f) deterministic = false;
    }
    std::printf("[selftest-field-spatialdisplacesdf] amount03 %-2s p=(% .4f,% .4f) got=% .6f baseline=% .6f "
                "move=%.4f\n",
                pr.name, px, py, gotN, baseline, std::fabs(gotN - baseline));
  }
  if (!deterministic) {
    std::printf("[selftest-field-spatialdisplacesdf] FAIL: warp not deterministic across identical renders\n");
    rc = 1;
  }
  const bool moved = totalMove > kMinMove;
  if (!injectBug && !moved) {
    std::printf("[selftest-field-spatialdisplacesdf] FAIL: Amount=0.3 did not warp p (total move %.5f <= "
                "%.5f) — pre warp line missing\n",
                totalMove, kMinMove);
    rc = 1;
  }
  std::printf("[selftest-field-spatialdisplacesdf] warp total move=%.5f (threshold %.5f) -> %s\n",
              totalMove, kMinMove, moved ? "PRESENT" : "ABSENT");

  // ---- MID-BAND STRUCTURAL TEETH (P2 fix) — production path only; see header for the TiXL derivation
  // (SpatialDisplaceSDF.cs:114-116 / :122-128 / :133-137 @ SHA 395c4c55). Skipped under injectBug: the
  // bite contract stays PRONG 2's moved-check.
  if (!injectBug) {
    // -- TOOTH 3: Amount-quadratic law + (1,1,1)-direction law --------------------------------------
    // Config: SamplePos=0 + vScale=(1,1,1) -> the three vNoise axis calls are identical (.cs:124-126)
    // -> warp = A * n(p) * (1,1,1). Scale=0.7 keeps the noise arg off the integer lattice.
    const Cfg t3base = {0.0f, kScale, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    const float kA = 0.15f;  // renders at A, 2A, 3A; q(0)=|p|^2 is closed form (no render)
    Cfg t3a = t3base, t3b = t3base, t3c = t3base;
    t3a.amount = kA; t3b.amount = 2 * kA; t3c.amount = 3 * kA;
    std::vector<float> bA = renderCfg(dev, q, tmpl, t3a);
    std::vector<float> bB = renderCfg(dev, q, tmpl, t3b);
    std::vector<float> bC = renderCfg(dev, q, tmpl, t3c);
    if (bA.empty() || bB.empty() || bC.empty()) {
      std::printf("[selftest-field-spatialdisplacesdf] FAIL: TOOTH3 render null (compile/PSO failure)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    // Error budget: got carries ~1e-5 GPU float noise -> dq ~ 3e-5; the third difference sums 8 q's
    // -> ~1e-4 worst case. kQTol=1e-3 = 10x headroom; an Amount-nonlinearity signal is O(1e-2..1e-1).
    const float kQTol = 1e-3f;
    int dirGated = 0;
    // TOOTH-3-specific probes with LARGE |S|=|px+py| (direction-law signal = n^2*S^2; probes near the
    // S=0 antidiagonal have no teeth). S^2: 0.72/0.72/0.49 -> a dropped axis reds out from |n| ~ 0.09.
    std::vector<P> t3probes = {{"a", pxFor(0.5f), pyFor(0.35f)},
                               {"b", pxFor(-0.55f), pyFor(-0.3f)},
                               {"c", pxFor(0.25f), pyFor(0.45f)}};
    for (P& pr : t3probes) {
      const float px = pX(pr.px), py = pY(pr.py);
      const float q0 = px * px + py * py;  // q(0) = |p|^2, closed form (warp vanishes at A=0)
      auto qOf = [&](const std::vector<float>& b) {
        const float g = b[(size_t)pr.py * kW + pr.px] + kSphR;
        return g * g;
      };
      const float q1 = qOf(bA), q2 = qOf(bB), q3 = qOf(bC);
      // Law 1 — q(A) exactly quadratic in A <=> third finite difference vanishes.
      const float d3 = q3 - 3.f * q2 + 3.f * q1 - q0;
      const bool okQ = std::fabs(d3) <= kQTol;
      if (!okQ) rc = 1;
      // Law 2 — extract u=p.N and w=|N|^2 from q(A),q(2A) and check 3u^2 == w*S^2 (N || (1,1,1)).
      const float u = (4.f * (q1 - q0) - (q2 - q0)) / (4.f * kA);
      const float w = ((q2 - q0) - 2.f * (q1 - q0)) / (2.f * kA * kA);
      bool okDir = true;
      const float S = px + py;  // p.z = 0
      if (w > 0.005f) {  // gate: noise must be meaningfully alive at this probe for the ratio test
        ++dirGated;
        const float lhs = 3.f * u * u, rhs = w * S * S;
        const float tol = std::fmax(5e-3f, 0.10f * std::fmax(lhs, rhs));
        okDir = std::fabs(lhs - rhs) <= tol;
        if (!okDir) rc = 1;
      }
      std::printf("[selftest-field-spatialdisplacesdf] tooth3 %-2s q3rdDiff=% .2e %s  u=% .4f w=% .4f "
                  "dir(3u^2=%.4f wS^2=%.4f) %s\n",
                  pr.name, d3, okQ ? "OK" : "RED", u, w, 3.f * u * u, w * S * S,
                  w > 0.005f ? (okDir ? "OK" : "RED") : "SKIP(w small)");
    }
    if (dirGated == 0) {  // |N|^2 <= 0.005 at ALL probes -> noise dead -> tooth hollow
      std::printf("[selftest-field-spatialdisplacesdf] FAIL: tooth3 direction law gated out everywhere\n");
      rc = 1;
    }

    // -- TOOTH 4: scale*vscale equivalence (.cs:124-126) --------------------------------------------
    // (Scale=0.35, vScale=1) vs (Scale=0.7, vScale=0.5): per-axis noise scale 0.7f*0.5f == 0.35f is
    // bit-exact -> identical noise args -> the two renders must agree at every probe.
    const Cfg t4a = {0.3f, 0.35f, 1.f, 1.f, 1.f, 0.1f, -0.2f, 0.15f, 0.3f, 0.5f, 0.7f};
    const Cfg t4b = {0.3f, 0.7f, 0.5f, 0.5f, 0.5f, 0.1f, -0.2f, 0.15f, 0.3f, 0.5f, 0.7f};
    std::vector<float> b4a = renderCfg(dev, q, tmpl, t4a);
    std::vector<float> b4b = renderCfg(dev, q, tmpl, t4b);
    if (b4a.empty() || b4b.empty()) {
      std::printf("[selftest-field-spatialdisplacesdf] FAIL: TOOTH4 render null (compile/PSO failure)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    for (P& pr : probes) {
      const float ga = b4a[(size_t)pr.py * kW + pr.px], gb = b4b[(size_t)pr.py * kW + pr.px];
      const float diff = std::fabs(ga - gb);
      const bool ok = diff <= 1e-5f;
      if (!ok) rc = 1;
      std::printf("[selftest-field-spatialdisplacesdf] tooth4 %-2s s*vs equiv got=(% .6f,% .6f) "
                  "diff=%.2e %s\n",
                  pr.name, ga, gb, diff, ok ? "OK" : "RED");
    }

    // -- TOOTH 5: absolute scale-polarity pin (.cs:114-116 arg = pos/scale + offset; .cs:136 -Offset):
    // (Scale=1, Offset=0) and (Scale=0.5, Offset=p0) sample the same arg at the p0 pixel (see header).
    const Cfg t5ref = {0.3f, 1.0f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    std::vector<float> b5r = renderCfg(dev, q, tmpl, t5ref);
    struct T5 { const char* name; uint32_t px, py; };
    T5 t5probes[] = {{"p", pxFor(0.4f), pyFor(0.3f)}, {"q", pxFor(-0.35f), pyFor(-0.2f)}};
    for (const T5& pr : t5probes) {
      const float p0x = pX(pr.px), p0y = pY(pr.py);  // exact texel p (p0.z = 0)
      Cfg t5c = t5ref;
      t5c.scale = 0.5f;
      t5c.ox = p0x; t5c.oy = p0y; t5c.oz = 0.0f;
      std::vector<float> b5c = renderCfg(dev, q, tmpl, t5c);
      if (b5r.empty() || b5c.empty()) {
        std::printf("[selftest-field-spatialdisplacesdf] FAIL: TOOTH5 render null (compile/PSO failure)\n");
        q->release(); dev->release(); pool->release();
        return 1;
      }
      const float gr = b5r[(size_t)pr.py * kW + pr.px], gc = b5c[(size_t)pr.py * kW + pr.px];
      const float diff = std::fabs(gr - gc);
      const bool ok = diff <= 1e-4f;  // 1-ulp host/shader p mismatch propagates ~1e-7 through the noise
      if (!ok) rc = 1;
      std::printf("[selftest-field-spatialdisplacesdf] tooth5 %-2s p0=(% .4f,% .4f) got=(% .6f,% .6f) "
                  "diff=%.2e %s\n",
                  pr.name, p0x, p0y, gr, gc, diff, ok ? "OK" : "RED");
    }
  }

  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (moved) {
      std::printf("[selftest-field-spatialdisplacesdf] FAIL: injectBug did not trip (warp still present)\n");
      return 0;  // dead tooth -> exit 0 so --bite NO-BITE list catches it
    }
    std::printf("[selftest-field-spatialdisplacesdf] injectBug correctly RED (pre warp dropped)\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-spatialdisplacesdf] PASS\n");
  return rc;
}

}  // namespace sw
