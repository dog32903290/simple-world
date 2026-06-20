// field_ops_repeatpolar_golden — --selftest-field-repeatpolar. GPU DISTANCE-VALUE golden for the
// RepeatPolar single-input PRE-wrap MODIFIER (polar/radial domain repetition). Builds
// RepeatPolar(GoldenSphere off-axis), assembles via the FROZEN base, compiles, renders, reads back
// R32Float (f.w into RED), and asserts each probe == the closed-form distance to the polar-FOLDED
// sphere at the texel's EXACT p. Mirrors field_ops_repeataxis_golden.cpp's harness.
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_combinesdf_golden.cpp).
//
// WHAT IT TESTS: pModPolar folds the (perpendicular-plane) 2-swizzle of p into one wedge of
// 2*PI/Repetitions BEFORE the child sphere is sampled. So a single off-CENTER sphere appears N times
// around the fold axis. The golden uses Axis=X (swizzle "zy" -> the (z,y) plane is folded; p.x passes
// through). The child sphere is placed OFF the y-axis so the fold is non-trivial, and the host replays
// the EXACT pModPolar math (CUT-94 by-value port == HLSL inout copy-out) then the sphere distance at the
// folded point.
//
// HOST CLOSED-FORM (Axis=X, swizzle zy; child sphere center=(cx,cy,0), r):
//   in:  q = float2(p.z, p.y) = (0, p.y)   (p.z=0 in this template)
//   pModPolar(q, reps, off): angle=2PI/reps; a=atan2(q.y,q.x)+angle/2+off/(180*PI); r=length(q);
//                            a=mod(a,angle)-angle/2; q'=float2(cos a, sin a)*r;
//   fold writes (p.z, p.y) = q'  -> p_folded = (p.x, q'.y, q'.x)
//   d = length(p_folded - center) - r_sphere
// At Offset=0 the host replays this verbatim. probe points chosen where the folded sphere distance is a
// clean non-degenerate value (probe ∈ [-1,1]; values asserted at the texel's EXACT p, robust to the
// half-texel offset — Cut62-63 golden discipline).
//
// injectBug: configureRepeatPolar(node, ..., injectBug=2) DROPS the OP's REAL preShaderCode (no polar
// fold) -> the child sees the raw p -> the folded probe reads the UNFOLDED sphere distance != expected
// -> RED. Tooth bites the OP's emit, not the template (no expected-value tautology).
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

// Param-cook + test seam owned by field_ops_repeatpolar.cpp (leaf type TU-private). Forward-declared here.
void configureRepeatPolar(FieldNode& node, float repetitions, float offset, int axis, bool mirror,
                          int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.2f;
constexpr float kCx = 0.0f, kCy = 0.5f, kCz = 0.0f;  // child sphere OFF the y-axis (folded plane = z,y)
constexpr float kReps = 6.0f;     // 6 wedges
constexpr float kOffset = 0.0f;   // no angular offset (keeps the host replay simple & exact)
constexpr int kAxis = 0;          // X -> swizzle "zy"
constexpr float kPI = 3.14159265f;

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

// host `mod` matching the Common macro: (x) - (y)*floor((x)/(y)).
float hmod(float x, float y) { return x - y * std::floor(x / y); }

// Host replay of pModPolar(float2 q, reps, offset) — byte-faithful to the helper math.
void hPModPolar(float& qx, float& qy, float reps, float offset) {
  const float angle = 2.0f * kPI / reps;
  float a = std::atan2(qy, qx) + angle / 2.0f + offset / (180.0f * kPI);
  const float r = std::sqrt(qx * qx + qy * qy);
  a = hmod(a, angle) - angle / 2.0f;
  qx = std::cos(a) * r;
  qy = std::sin(a) * r;
}

// Host closed-form: fold (p.z, p.y) via pModPolar, then sphere distance at the folded point.
float foldedSphere(float px, float py) {
  // swizzle "zy": q = (p.z, p.y) = (0, py).
  float qx = 0.0f, qy = py;
  hPModPolar(qx, qy, kReps, kOffset);
  // write back: p.z = qx, p.y = qy ; p.x unchanged.
  const float fx = px, fy = qy, fz = qx;
  const float dx = fx - kCx, dy = fy - kCy, dz = fz - kCz;
  return std::sqrt(dx * dx + dy * dy + dz * dz) - kSphR;
}

std::shared_ptr<FieldNode> buildTree(int injectBug) {
  std::shared_ptr<FieldNode> mod = makeFieldNode("RepeatPolar", "golden0");
  if (!mod) return nullptr;
  configureRepeatPolar(*mod, kReps, kOffset, kAxis, /*mirror=*/false, injectBug);
  mod->inputs.push_back(std::make_shared<GoldenSphere>("a", kCx, kCy, kCz, kSphR));
  return mod;
}

struct Probe { const char* name; uint32_t px, py; };

}  // namespace

int runFieldRepeatPolarGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-repeatpolar] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-repeatpolar] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  const int bugMode = injectBug ? 2 : 0;  // 2 = drop the pre fold line (production passes 0).
  std::shared_ptr<FieldNode> tree = buildTree(bugMode);
  if (!tree) {
    std::printf("[selftest-field-repeatpolar] FAIL: RepeatPolar factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-repeatpolar] FAIL: renderField2d null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  const uint32_t cy = (kH - 1) / 2;  // p.y ≈ 0
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

  // Probes: pick texels where the folded sphere distance is non-degenerate (probe in [-1,1]). The fold
  // is about X (z,y plane), so vary p.y to exercise the angular wedge mapping; p.x is a passthrough.
  std::vector<Probe> probes = {
      {"onAxisY", pxFor(0.0f), pyFor(0.5f)},     // near the original lobe (p.y≈0.5, the sphere center copy)
      {"wedge1", pxFor(0.0f), cy},               // p.y≈0 -> folded into the wedge boundary region
      {"wedge2", pxFor(0.3f), pyFor(0.3f)},      // off both axes -> non-trivial fold
      {"wedge3", pxFor(-0.3f), pyFor(-0.4f)},    // negative quadrant -> different wedge
  };

  const float kTol = 2e-4f;  // simplex-free but trig-heavy fold: small float drift host vs GPU.
  int rc = 0;
  for (Probe& pr : probes) {
    const float px = pX(pr.px), py = pY(pr.py);
    const float expected = foldedSphere(px, py);
    const float got = sampleAt(pr.px, pr.py);
    const float diff = std::fabs(got - expected);
    const bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-repeatpolar] probe %-8s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }
  tex->release();

  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-repeatpolar] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-repeatpolar] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-repeatpolar] PASS\n");
  return rc;
}

}  // namespace sw
