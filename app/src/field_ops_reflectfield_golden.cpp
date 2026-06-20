// field_ops_reflectfield_golden — --selftest-field-reflectfield. GPU DISTANCE-VALUE golden for the
// ReflectField single-input MODIFIER (PRE-wrap; emits preShaderCode BEFORE the child recursion, like
// Translate). Builds ReflectField(GoldenSphere@(0.3,0,0)), assembles via the FROZEN base, compiles,
// renders, reads back R32Float, asserts each probe RED == sphereDistance(reflect(p) - center). Mirrors
// field_ops_translate_golden.cpp's harness; ALSO exercises the pReflect hg_sdf helper (addGlobals) and
// the PlaneNormal/Offset params (packed_float3 + scalar) under the modifier prefix.
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_translate_golden.cpp).
//
// CLOSED-FORM: child = GoldenSphere(center=(0.3,0,0), r=0.4) -> d_child(q) = |q - (0.3,0,0)| - 0.4.
//   ReflectField pre `pReflect(p.xyz, normalize(PlaneNormal), Offset);` with PlaneNormal=(1,0,0),
//   Offset=0 -> t = dot(p,(1,0,0))+0 = p.x. If t<0 (p.x<0): p = p - 2t*(1,0,0) -> p.x := -p.x.
//   So the child samples q where q.x = |p.x| (mirror of the negative half-space onto +x), q.y = p.y.
//   The CHILD IS OFF-CENTER at x=0.3 (not origin) — the reflection is the discriminator, not a no-op.
//   Probes (y=0, child center (0.3,0)):
//     p=(-0.3,0)  reflects -> q=(0.3,0)  -> |q-(0.3,0)|-0.4 = -0.4  (reflected onto the sphere center)
//     p=( 0.3,0)  UNreflected (p.x>=0) -> q=(0.3,0) -> -0.4         (same value, NO reflection path)
//     p=(-0.8,0)  reflects -> q=(0.8,0)  -> |0.5|-0.4 = +0.1
//   The TOOTH is the reflected-vs-unreflected asymmetry: p=(-0.3) gives -0.4 ONLY because it reflects;
//   without reflection it reads q=(-0.3,0) -> |(-0.6,0)|-0.4 = +0.2 (RED). The p=(0.3) probe pins the
//   unreflected side (also -0.4) so a "reflect everything" mutation would not silently pass.
//
// PARAM-PREFIX (BLOOD LESSON): the emitted tokens P.ReflectField_<id>_PlaneNormal / _Offset MUST match
//   sw's frozen prefix convention ("<Type>_"+shortId+"_", accessed P.<prefix><Name>; backward-traced
//   from field_ops_combinesdf.cpp:288 / translate.cpp:46). A wrong prefix reads the wrong/0 normal ->
//   no reflection -> the p=(-0.3) probe goes RED. NOT forward-assumed.
//
// injectBug: configureReflectField(node, N, off, injectBug>0) corrupts the OP'S REAL preShaderCode emit:
//   1 = drop the pReflect call -> no reflection -> p=(-0.3) reads q=(-0.3,0) -> +0.2 != -0.4 -> RED.
//   (mode 2 = zeroed-normal also defeats reflection.) The reflected probes catch them; the tooth bites
//   the OP's emit, not the template (expected values are the CORRECT reflected field, never altered).
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

// Param-cook + test seam owned by field_ops_reflectfield.cpp (leaf type TU-private). Forward-declared.
void configureReflectField(FieldNode& node, float nx, float ny, float nz, float offset, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;
constexpr float kCx = 0.3f, kCy = 0.0f, kCz = 0.0f;  // child sphere center OFF-ORIGIN at x=0.3.
constexpr float kNx = 1.0f, kNy = 0.0f, kNz = 0.0f;  // PlaneNormal = (1,0,0) -> reflect across x=0.
constexpr float kOffset = 0.0f;

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

// Host closed-form: reflect p across plane (normal=(1,0,0), offset=0), then child sphere @ (0.3,0,0).
//   t = p.x + offset; if t<0 -> p.x := p.x - 2*t = -p.x. (n is unit; normalize is a no-op host-side.)
float reflectedField(float px, float py) {
  float qx = px, qy = py;  // qz = 0
  const float t = qx * kNx + qy * kNy + 0.0f * kNz + kOffset;  // dot(p, n) + offset, p.z=0
  if (t < 0.0f) {
    qx = qx - 2.0f * t * kNx;
    qy = qy - 2.0f * t * kNy;  // ny=0 -> qy unchanged; kept for fidelity to the helper math
  }
  const float dx = qx - kCx, dy = qy - kCy;  // cz=0, q.z=0
  return std::sqrt(dx * dx + dy * dy) - kSphR;
}

std::shared_ptr<FieldNode> buildTree(int injectBug) {
  std::shared_ptr<FieldNode> mod = makeFieldNode("ReflectField", "golden0");
  if (!mod) return nullptr;
  configureReflectField(*mod, kNx, kNy, kNz, kOffset, injectBug);
  // child OFF-CENTER at (0.3,0,0) so the reflection is the discriminator (not a no-op at the origin).
  mod->inputs.push_back(std::make_shared<GoldenSphere>("a", kCx, kCy, kCz, kSphR));
  return mod;
}

struct Probe { const char* name; uint32_t px, py; };

}  // namespace

int runFieldReflectFieldGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-reflectfield] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-reflectfield] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  // injectBug=1 (drop the pReflect call) lives in the OP's REAL preShaderCode emit; production passes 0.
  const int bugMode = injectBug ? 1 : 0;
  std::shared_ptr<FieldNode> tree = buildTree(bugMode);
  if (!tree) {
    std::printf("[selftest-field-reflectfield] FAIL: ReflectField factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  clearTexOpCache();
  MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-reflectfield] FAIL: renderField2d null (compile/PSO failure)\n");
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

  // Probes: the reflected-vs-unreflected asymmetry is the tooth (child center off at x=0.3).
  //   reflected   p≈-0.3 -> q=(0.3,0) -> -0.4  (RED if reflection dropped: q=(-0.3,0) -> +0.2)
  //   unreflected p≈ 0.3 -> q=(0.3,0) -> -0.4  (pins the +x side; no reflection path taken)
  //   reflectedFar p≈-0.8 -> q=(0.8,0) -> +0.1 (the off-screen-safe reflected value, all in [-1,1])
  Probe probes[] = {
      {"reflected", pxFor(-0.3f), cy},
      {"unreflected", pxFor(0.3f), cy},
      {"reflectedFar", pxFor(-0.8f), cy},
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = reflectedField(px, py);  // CORRECT reflected field (never altered for injectBug)
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-reflectfield] probe %-12s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-reflectfield] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-reflectfield] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-reflectfield] PASS\n");
  return rc;
}

}  // namespace sw
