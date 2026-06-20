// field_ops_rotatefield_golden — --selftest-field-rotatefield. GPU DISTANCE-VALUE golden for the
// RotateField single-input MODIFIER (PRE-wrap; emits preShaderCode BEFORE the child recursion, like
// Translate / ReflectField). Builds RotateField(GoldenSphere@(0.3,0,0)) with Rotation=(0,0,90°) so ONLY
// the third pre line (p.yx, RotateRad.z) is non-identity, assembles via the FROZEN base, compiles,
// renders, reads back R32Float, asserts each probe RED == sphereDistance(rotate(p) - center). Mirrors
// field_ops_reflectfield_golden.cpp's harness; ALSO exercises the pRotateAxis helper (addGlobals,
// by-value+return ★Cut-94 swizzle fix) and the RotateRad param (packed_float3) under the modifier prefix.
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_translate_golden.cpp).
//
// CLOSED-FORM: child = GoldenSphere(center=(0.3,0,0), r=0.4) -> d_child(q) = |q - (0.3,0,0)| - 0.4.
//   RotateField pre lines with Rotation=(0,0,90°) -> RotateRad=(0,0,PI/2) (host deg->rad fork):
//     line1 pRotateAxis(p.zy, 0)  -> a=0 -> cos=1,sin=0 -> IDENTITY.
//     line2 pRotateAxis(p.zx, 0)  -> IDENTITY.
//     line3 pRotateAxis(p.yx, PI/2): pRotateAxis((u,v),a) = (cos a*u+sin a*v, cos a*v - sin a*u).
//       p.yx = (p.y, p.x); a=PI/2 -> cos=0,sin=1 -> returns (p.x, -p.y); assigned back to (p.y, p.x):
//       new p.y =  p.x ;  new p.x = -p.y.  (simultaneous via by-value copy — the inout copy-in/out.)
//   The render samples z=0, so lines 1&2 (which touch z) are identity AND leave z=0. With z=0 the
//   rotated point is q = (qx,qy) = (-p.y, p.x). The CHILD IS OFF-CENTER at x=0.3 (not origin) so the
//   rotation is the discriminator, not a no-op at the symmetric origin.
//   Probes (child center (0.3,0)):
//     p=( 0.0,-0.3) -> q=(0.3, 0.0) -> |q-(0.3,0)|-0.4 = -0.4   (rotation lands child center here)
//     p=( 0.0,-0.6) -> q=(0.6, 0.0) -> |0.3|-0.4 = -0.1
//     p=( 0.3, 0.0) -> q=(0.0, 0.3) -> |(-0.3,0.3)|-0.4 = +0.024264  (the +x point rotates AWAY)
//   The TOOTH is the rotated-vs-unrotated asymmetry: p=(0,-0.3) gives -0.4 ONLY because it rotates onto
//   the off-center child; WITHOUT the z rotation q=(0,-0.3) -> |(-0.3,-0.3)|-0.4 = +0.024 (RED). The
//   p=(0.3,0) probe pins the +x side (unrotated read there would be -0.4) so a dropped/swapped rotation
//   flips it the other way too — both directions catch the mutation.
//
// PARAM-PREFIX (BLOOD LESSON): the emitted token P.RotateField_<id>_RotateRad MUST match sw's frozen
//   prefix convention ("<Type>_"+shortId+"_", accessed P.<prefix><Name>; backward-traced from
//   field_ops_combinesdf.cpp:288 / translate.cpp:46). A wrong prefix reads a zeroed RotateRad ->
//   identity rotation -> the p=(0,-0.3) probe goes RED. NOT forward-assumed.
//
// DEG->RAD fork: the golden passes Rotation in DEGREES (0,0,90) to configureRotateField, which converts
//   to radians (PI/2) before packing — exactly TiXL's host Update() `Rotation * MathUtils.ToRad`. The
//   host closed-form below rotates with PI/2 radians to match.
//
// injectBug: configureRotateField(node, deg, injectBug>0) corrupts the OP'S REAL preShaderCode emit:
//   1 = drop the z rotation line -> q=p -> p=(0,-0.3) reads |(-0.3,-0.3)|-0.4 = +0.024 != -0.4 -> RED.
//   (mode 2 = wrong-axis swizzle p.xy also defeats the intended rotation.) The rotated probes catch them;
//   the tooth bites the OP's emit, not the template (expected values are the CORRECT rotated field).
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

// Param-cook + test seam owned by field_ops_rotatefield.cpp (leaf type TU-private). Forward-declared.
// Takes Rotation in DEGREES (converted to radians inside) + injectBug (0 none / 1 drop-z / 2 wrong-axis).
void configureRotateField(FieldNode& node, float degX, float degY, float degZ, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;
constexpr float kCx = 0.3f, kCy = 0.0f, kCz = 0.0f;  // child sphere center OFF-ORIGIN at x=0.3.
constexpr float kDegX = 0.0f, kDegY = 0.0f, kDegZ = 90.0f;  // Rotation in DEGREES; only z is active.

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

// Host closed-form replaying the THREE pre lines (RotateRad in RADIANS) at z=0, then child sphere @
// (0.3,0,0). pRotateAxis((u,v),a) = (cos a*u + sin a*v, cos a*v - sin a*u) — verbatim helper math.
float rotatedField(float px, float py) {
  constexpr float kToRad = 3.14159265358979323846f / 180.0f;
  const float ax = kDegX * kToRad, ay = kDegY * kToRad, az = kDegZ * kToRad;  // host deg->rad fork.
  float x = px, y = py, z = 0.0f;
  auto rot = [](float u, float v, float a, float& ou, float& ov) {
    ou = std::cos(a) * u + std::sin(a) * v;
    ov = std::cos(a) * v - std::sin(a) * u;
  };
  // line1: p.zy = pRotateAxis(p.zy, ax)  -> (u,v)=(z,y), assign back z,y.
  { float ou, ov; rot(z, y, ax, ou, ov); z = ou; y = ov; }
  // line2: p.zx = pRotateAxis(p.zx, ay)  -> (u,v)=(z,x), assign back z,x.
  { float ou, ov; rot(z, x, ay, ou, ov); z = ou; x = ov; }
  // line3: p.yx = pRotateAxis(p.yx, az)  -> (u,v)=(y,x), assign back y,x.
  { float ou, ov; rot(y, x, az, ou, ov); y = ou; x = ov; }
  const float dx = x - kCx, dy = y - kCy, dz = z - kCz;
  return std::sqrt(dx * dx + dy * dy + dz * dz) - kSphR;
}

std::shared_ptr<FieldNode> buildTree(int injectBug) {
  std::shared_ptr<FieldNode> mod = makeFieldNode("RotateField", "golden0");
  if (!mod) return nullptr;
  configureRotateField(*mod, kDegX, kDegY, kDegZ, injectBug);
  // child OFF-CENTER at (0.3,0,0) so the rotation is the discriminator (not a no-op at the origin).
  mod->inputs.push_back(std::make_shared<GoldenSphere>("a", kCx, kCy, kCz, kSphR));
  return mod;
}

struct Probe { const char* name; uint32_t px, py; };

}  // namespace

int runFieldRotateFieldGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-rotatefield] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-rotatefield] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  // injectBug=1 (drop the z rotation line) lives in the OP's REAL preShaderCode emit; production passes 0.
  const int bugMode = injectBug ? 1 : 0;
  std::shared_ptr<FieldNode> tree = buildTree(bugMode);
  if (!tree) {
    std::printf("[selftest-field-rotatefield] FAIL: RotateField factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  clearTexOpCache();
  MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-rotatefield] FAIL: renderField2d null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  const float kTol = 1e-5f;
  int rc = 0;

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

  // Probes: the rotated-vs-unrotated asymmetry is the tooth (child center off at x=0.3).
  //   onCenter p≈(0,-0.3) -> q=(0.3,0)  -> -0.4   (RED if z rotation dropped: q=(0,-0.3) -> +0.024)
  //   near     p≈(0,-0.6) -> q=(0.6,0)  -> -0.1   (RED if dropped: q=(0,-0.6) -> +0.271)
  //   unrot_x  p≈(0.3,0)  -> q=(0,0.3)  -> +0.024 (RED if dropped: q=(0.3,0) -> -0.4 — opposite flip)
  Probe probes[] = {
      {"onCenter", pxFor(0.0f), pyFor(-0.3f)},
      {"near", pxFor(0.0f), pyFor(-0.6f)},
      {"unrot_x", pxFor(0.3f), pyFor(0.0f)},
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = rotatedField(px, py);  // CORRECT rotated field (never altered for injectBug)
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-rotatefield] probe %-10s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-rotatefield] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-rotatefield] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-rotatefield] PASS\n");
  return rc;
}

}  // namespace sw
