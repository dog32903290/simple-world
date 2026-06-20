// field_ops_transformfield_golden — --selftest-field-transformfield. GPU DISTANCE-VALUE golden for the
// TransformField single-input PRE+POST modifier. TransformField transforms the SAMPLE POINT by a matrix
// (pre), then rescales the returned distance by UniformScale (post). The golden builds
// TransformField(GoldenSphere), assembles via the FROZEN base (single-input wrap path), compiles,
// renders R32Float (f.w into RED), and asserts each probe == the closed-form transformed sphere distance.
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_staircombinesdf_golden.cpp).
//
// ★MATRIX ORIENTATION (the load-bearing thing this golden PROVES on hardware): TiXL's HLSL pre-code is
//   `mul(float4(p.xyz,1), Transform)` (row-vector × row-major-cbuffer matrix). The leaf emits MSL
//   `Transform * float4(p.xyz,1)` (matrix × column-vector). For the SAME 16 floats uploaded into an MSL
//   float4x4 STRUCT MEMBER (filled column-major), these compute the IDENTICAL result (derivation in
//   field_ops_transformfield.cpp fork (2)). We pick a TRANSLATE matrix: M * (p,1) = (p + T). With the
//   column-major float layout below, c3 = (T.x,T.y,T.z,1) is the translation column, so the sample point
//   is shifted by +T before the sphere distance — equivalently the sphere's effective center moves by -T.
//   If the orientation were wrong (e.g. T landed in the bottom ROW instead of the last column, or the
//   wrong multiply), the probe would not match the host's `length((p+T) - center) - r` and bite RED.
//
// CASE: Transform = translate by T=(0.3, -0.2, 0), UniformScale = 0.5. Child GoldenSphere @ origin, r=0.4.
//   pre:  p' = p + T
//   sphere: d = length(p'.xyz - 0) - 0.4 = length(p + T) - 0.4   (z=0 plane)
//   post: f.w = d * 0.5
//   So expected(p) = (length((p.x+0.3, p.y-0.2)) - 0.4) * 0.5.
//
// injectBug: configureTransformField(node, ..., injectBug=1) DROPS the pre transform (p not shifted) ->
//   d = length(p) - 0.4, scaled -> != expected at a probe where T matters -> RED. (injectBug=2 would drop
//   the post scale; we use the pre-drop here, and a probe chosen so the shift is the discriminator.)
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

// Param-cook + test seam owned by field_ops_transformfield.cpp (leaf type TU-private). Forward-declared.
void configureTransformField(FieldNode& node, const float transform[16], float uniformScale,
                             bool rotateFieldVecs, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;
constexpr float kTx = 0.3f, kTy = -0.2f, kTz = 0.0f;  // translation column
constexpr float kScale = 0.5f;                         // UniformScale

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

// Golden-only sphere @ origin: f.w = length(p.xyz - center) - r. (Verbatim SphereSDF distance.)
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

// Column-major float layout for MSL float4x4 struct member: c0[0..3], c1[0..3], c2[0..3], c3[0..3].
// Translation matrix: c3 = (T.x,T.y,T.z,1); identity elsewhere -> M * (p,1) = (p + T).
std::shared_ptr<FieldNode> buildTree(int injectBug) {
  std::shared_ptr<FieldNode> xf = makeFieldNode("TransformField", "golden0");
  if (!xf) return nullptr;
  const float m[16] = {
      1.f, 0.f, 0.f, 0.f,   // column 0
      0.f, 1.f, 0.f, 0.f,   // column 1
      0.f, 0.f, 1.f, 0.f,   // column 2
      kTx, kTy, kTz, 1.f};  // column 3 = translation
  configureTransformField(*xf, m, kScale, /*rotateFieldVecs=*/false, injectBug);
  xf->inputs.push_back(std::make_shared<GoldenSphere>("a", 0.f, 0.f, 0.f, kSphR));
  return xf;
}

float expected(float px, float py) {
  // p' = p + T; d = length(p'.xy) - r (z=0); f.w = d * scale.
  const float dx = px + kTx, dy = py + kTy;
  const float d = std::sqrt(dx * dx + dy * dy) - kSphR;
  return d * kScale;
}

struct Probe { const char* name; uint32_t px, py; };

}  // namespace

int runFieldTransformFieldGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-transformfield] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-transformfield] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  const int bugMode = injectBug ? 1 : 0;  // 1 = drop the pre transform (production passes 0).
  std::shared_ptr<FieldNode> tree = buildTree(bugMode);
  if (!tree) {
    std::printf("[selftest-field-transformfield] FAIL: TransformField factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-transformfield] FAIL: renderField2d null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);

  // Probes in SDF-active region [-1,1], non-degenerate (the shift matters at each).
  const uint32_t cy = (kH - 1) / 2;
  auto pxFor = [](float target) -> uint32_t {
    float f = ((target + 1.0f) * kW - 1.0f) * 0.5f;
    int px = (int)std::lround(f);
    if (px < 0) px = 0;
    if (px >= (int)kW) px = kW - 1;
    return (uint32_t)px;
  };
  std::vector<Probe> probes = {
      {"center", pxFor(0.0f), cy},     // p=(0,0) -> p'=(0.3,-0.2) -> inside-ish, shift load-bearing
      {"left", pxFor(-0.5f), cy},      // p=(-0.5,0) -> p'=(-0.2,-0.2)
      {"right", pxFor(0.5f), cy},      // p=(0.5,0) -> p'=(0.8,-0.2)
  };

  const float kTol = 2e-4f;
  int rc = 0;
  for (Probe& pr : probes) {
    const float gx = pX(pr.px), gy = pY(pr.py);
    const float got = buf[(size_t)pr.py * kW + pr.px];
    const float exp = expected(gx, gy);
    const float diff = std::fabs(got - exp);
    const bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-transformfield] probe %-8s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, gx, gy, got, exp, diff, ok ? "OK" : "RED");
  }
  tex->release();

  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-transformfield] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-transformfield] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-transformfield] PASS\n");
  return rc;
}

}  // namespace sw
