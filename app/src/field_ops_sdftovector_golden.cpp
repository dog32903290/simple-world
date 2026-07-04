// field_ops_sdftovector_golden — --selftest-field-sdftovector. GPU VECTOR-FIELD golden for the
// SdfToVector custom-collect op. SdfToVector differentiates a wrapped SDF into a surface-NORMAL vector
// (f.xyz = normalize of a 4-sample tetrahedral finite-difference gradient) and carries the center
// distance in f.w. The op's PRIMARY output is the VECTOR (f.xyz) — so this golden must READ f.xyz, not
// f.w. The production field_render template hard-codes the fragment to return float4(f.w,0,0,1) (only
// f.w reaches the R32Float readback). This golden therefore renders through a GOLDEN-ONLY template
// VARIANT (production template loaded, its return line rewritten to output a chosen f.xyz CHANNEL) so
// the normal is observable. That variant edit is string-only and golden-local — it touches NO production
// file (the production template is loaded read-only and copied).
//
// EXPECTED VALUE (independent-of-impl, closed-form): for a sphere d(p)=length(p)-R, SdfToVector emits
//   grad = Σ_{i=0..3} s_i * d(pStart + s_i * h),  with the four TETRAHEDRAL signs
//     s = {(1,-1,-1),(-1,1,-1),(-1,-1,1),(1,1,1)}  (SdfToVector.cs:66-70; sw fork (2): sign == offset/h).
//   f.xyz = normalize(grad).  The golden recomputes grad on the CPU from the sphere formula + these EXACT
//   offsets (NOT from sw's output), normalizes, and asserts the rendered channel == the CPU normal
//   component. This is a genuine finite-difference oracle at the op's REAL step h (0.05), sampled at
//   off-axis probes so the normal has non-trivial x/y — NOT an identity point.
//
// injectBug: configureSdfToVector(node, h, injectBug=1) DROPS the normalize (f.xyz = raw grad, length
//   != 1) -> the rendered component differs from the normalized expected -> probe RED. Tooth bites the
//   OP's REAL vector emit (the normalize call is gated by injectBug), not the template.
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_pushpullsdf_golden.cpp).
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

// Param-cook + test seam owned by field_ops_sdftovector.cpp (leaf type TU-private). Forward-declared.
void configureSdfToVector(FieldNode& node, float lookUpDistance, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;
constexpr float kStep = 0.05f;  // the op's REAL finite-difference step h. Large enough for a robust probe.

// Load the production field template and rewrite its fragment return so the golden reads an f.xyz CHANNEL
// (0=x,1=y,2=z) instead of f.w. String-only, golden-local (production template untouched on disk).
std::string loadTemplateChannel(int channel) {
#ifdef SW_FIELD_TEMPLATE
  std::ifstream f(SW_FIELD_TEMPLATE);
  if (!f) return "";
  std::ostringstream ss;
  ss << f.rdbuf();
  std::string t = ss.str();
  // Production return line (field_render_template.metal:91): `return float4(f.w, 0.0, 0.0, 1.0);`
  const std::string prod = "return float4(f.w, 0.0, 0.0, 1.0);";
  const char comp = channel == 0 ? 'x' : (channel == 1 ? 'y' : 'z');
  std::string repl = std::string("return float4(f.") + comp + ", 0.0, 0.0, 1.0);";
  size_t pos = t.find(prod);
  if (pos == std::string::npos) return "";  // template drifted -> fail loud (caller checks empty).
  t.replace(pos, prod.size(), repl);
  return t;
#else
  return "";
#endif
}

float pX(uint32_t px) { return (2.0f * px + 1.0f) / kW - 1.0f; }
float pY(uint32_t py) { return 1.0f - (2.0f * py + 1.0f) / kH; }

// Golden-only sphere @ origin: f.w = length(p.xyz) - r. The SDF SdfToVector differentiates.
struct GoldenSphere : FieldNode {
  float r;
  explicit GoldenSphere(float radius) : r(radius) { prefix = "GSphere_"; }
  void preShaderCode(CodeAssembleCtx& c, int) const override {
    const std::string ctx = c.ctx();
    c.appendCall("f" + ctx + ".w = length(p" + ctx + ".xyz) - P." + prefix + "Radius;");
  }
  void collectParams(std::vector<float>& fp, std::vector<std::string>& pf) const override {
    appendScalarParam(fp, pf, prefix + "Radius", r);
  }
};

std::shared_ptr<FieldNode> buildTree(float h, int injectBug) {
  std::shared_ptr<FieldNode> s2v = makeFieldNode("SdfToVector", "golden0");
  if (!s2v) return nullptr;
  configureSdfToVector(*s2v, h, injectBug);
  s2v->inputs.push_back(std::make_shared<GoldenSphere>(kSphR));  // inputs[0] = InputField
  return s2v;
}

// CPU closed-form: the SdfToVector tetrahedral gradient normal for a sphere d=length(p)-R at world (gx,gy,0).
// EXACTLY mirrors the op's math (the four signs + step h + normalize) — but computed from the sphere
// FORMULA, not sw's output (independent-of-impl). Returns the 3 components of normalize(grad).
void expectedNormal(float gx, float gy, float h, float out[3]) {
  const float sign[4][3] = {{1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, 1, 1}};
  const float base[3] = {gx, gy, 0.0f};
  float grad[3] = {0, 0, 0};
  for (int i = 0; i < 4; ++i) {
    float sp[3] = {base[0] + sign[i][0] * h, base[1] + sign[i][1] * h, base[2] + sign[i][2] * h};
    float d = std::sqrt(sp[0] * sp[0] + sp[1] * sp[1] + sp[2] * sp[2]) - kSphR;
    for (int k = 0; k < 3; ++k) grad[k] += sign[i][k] * d;
  }
  float len = std::sqrt(grad[0] * grad[0] + grad[1] * grad[1] + grad[2] * grad[2]);
  if (len < 1e-12f) len = 1.0f;
  for (int k = 0; k < 3; ++k) out[k] = grad[k] / len;
}

struct Probe { const char* name; uint32_t px, py; int channel; };

}  // namespace

int runFieldSdfToVectorGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-sdftovector] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });

  const int bugMode = injectBug ? 1 : 0;  // 1 = drop the normalize (production passes 0).
  const float kTol = 3e-3f;               // finite-difference + f16-free R32 render; loose but real.
  int rc = 0;

  // Off-axis probes: pick pixels whose world (gx,gy) give a normal with meaningful x AND y (NOT on an
  // axis, NOT at the origin/singularity) so a wrong sign/step/dropped-normalize bites.
  struct P { const char* name; float gx, gy; };
  std::vector<P> targets = {
      {"upperRight", 0.30f, 0.30f},
      {"lowerLeft", -0.35f, -0.20f},
      {"right", 0.55f, 0.05f},
  };

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

  // Render the field once per CHANNEL (x and y) through the channel-variant template, then probe each
  // target's snapped-pixel value against the CPU closed-form normal component.
  for (int channel = 0; channel <= 1; ++channel) {  // 0 = x, 1 = y
    clearTexOpCache();
    const std::string tmpl = loadTemplateChannel(channel);
    if (tmpl.empty()) {
      std::printf("[selftest-field-sdftovector] FAIL: template load/rewrite failed (channel %d)\n",
                  channel);
      rc = 1;
      break;
    }
    std::shared_ptr<FieldNode> tree = buildTree(kStep, bugMode);
    if (!tree) {
      std::printf("[selftest-field-sdftovector] FAIL: factory not registered\n");
      rc = 1;
      break;
    }
    MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
    if (!tex) {
      std::printf("[selftest-field-sdftovector] FAIL: renderField2d null (compile/PSO failure)\n");
      rc = 1;
      break;
    }
    std::vector<float> buf((size_t)kW * kH, 0.0f);
    tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);

    for (const P& t : targets) {
      uint32_t px = pxFor(t.gx), py = pyFor(t.gy);
      const float gx = pX(px), gy = pY(py);  // the ACTUAL snapped world coord the shader sampled.
      float en[3];
      expectedNormal(gx, gy, kStep, en);
      const float expected = en[channel];
      const float got = buf[(size_t)py * kW + px];
      const float diff = std::fabs(got - expected);
      const bool ok = diff <= kTol;
      if (!ok) rc = 1;
      std::printf("[selftest-field-sdftovector] %-10s .%c p=(% .4f,% .4f) got=% .6f expected=% .6f "
                  "diff=%.2e %s\n",
                  t.name, channel == 0 ? 'x' : 'y', gx, gy, got, expected, diff, ok ? "OK" : "RED");
    }
    tex->release();
  }

  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-sdftovector] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 0;  // did-not-trip -> return 0 so --bite's NO-BITE list catches a dead tooth (GOLDEN_STANDARD).
    }
    std::printf("[selftest-field-sdftovector] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-sdftovector] PASS\n");
  return rc;
}

}  // namespace sw
