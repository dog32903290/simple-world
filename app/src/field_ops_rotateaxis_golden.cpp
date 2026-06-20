// field_ops_rotateaxis_golden — --selftest-field-rotateaxis. GPU DISTANCE-VALUE golden for the
// RotateAxis single-input MODIFIER (PRE-wrap; emits preShaderCode BEFORE the child recursion, like
// Translate / ReflectField). Builds RotateAxis(GoldenSphere@(0.3,0,0)) with Rotation=90, Axis=Z,
// assembles via the FROZEN base, compiles, renders, reads back R32Float (f.w distance), asserts each
// probe == sphereDistance(rotate(p) - center). Mirrors field_ops_reflectfield_golden.cpp's harness; ALSO
// exercises the pRotateAxis helper (addGlobals, CUT-94 by-value+return) and the Rotation scalar param
// under the modifier prefix.
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_reflectfield_golden.cpp).
//
// ROTATION-INVARIANT SHAPE -> OFF-CENTER CHILD (golden probe rule, Cut 71-72 / ReflectField precedent):
//   a sphere is rotation-invariant about its OWN center, so to make the rotation observable the child
//   sphere is placed OFF-CENTER at (0.3,0,0). The pre-wrap rotates the SAMPLING point p (about the world
//   origin), so the sphere appears to counter-rotate about the origin -> a probe at a fixed world point
//   reads a different distance depending on the rotation. The tooth = a probe whose rotated image lands
//   on the off-center sphere ONLY because of the rotation.
//
// AXIS MAPPING (backward-traced from RotateAxis.cs:51-52,56-61): _axisCodes0 = {"zy","zx","yx"} indexed
//   by AxisTypes{X=0,Y=1,Z=2}. Axis=Z -> index 2 -> swizzle "yx". The call is
//   `p.yx = pRotateAxis(p.yx, ang)`, helper `pRotateAxis(v,a){ return cos(a)*v + sin(a)*float2(v.y,-v.x);}`
//   with ang = Rotation/180*3.141578. Read p.yx as the float2 v=(p.y, p.x); assign back p.y=v'.x, p.x=v'.y.
//   At Rotation=90 -> ang=1.570789 -> cos≈7.3e-6 (NOT 0; keep the exact .cs constant), sin≈1.0:
//     v'=(cos*v.x+sin*v.y, cos*v.y-sin*v.x) ≈ (v.y, -v.x) = (p.x, -p.y).
//     p.y := v'.x ≈ p.x ;  p.x := v'.y ≈ -p.y.  (a rotation in the xy-plane: new x ≈ -old y, new y ≈ old x)
//   The host closed-form below uses the SAME constant so values match to 1e-5.
//
// CLOSED-FORM: child = GoldenSphere(center=(0.3,0,0), r=0.4) -> d_child(q) = |q - (0.3,0,0)| - 0.4.
//   Probes (x=0 column, vary y; child center off at +x=0.3):
//     p=(0,-0.3) rotates -> q≈( 0.3, 0) -> |q-(0.3,0)|-0.4 ≈ -0.400  (lands ON the sphere center: TOOTH)
//     p=(0, 0.3) rotates -> q≈(-0.3, 0) -> |(-0.6,0)|-0.4    ≈ +0.200  (rotated to the FAR side)
//     p=(0, 0.8) rotates -> q≈(-0.8, 0) -> |(-1.1,0)|-0.4    ≈ +0.700  (off-screen-safe, all q in plane)
//   The TOOTH is the rotated-vs-unrotated asymmetry: p=(0,-0.3) gives -0.4 ONLY because it rotates onto
//   the +x sphere; WITHOUT rotation it reads q=(0,-0.3) -> |(-0.3,-0.3)|-0.4 = 0.4243-0.4 = +0.024 (RED).
//   The p=(0,0.3) probe (rotates to +0.2) also goes RED without rotation (reads +0.024) so a partial /
//   wrong-angle mutation cannot silently pass.
//
// PARAM-PREFIX (BLOOD LESSON): the emitted token P.RotateAxis_<id>_Rotation MUST match sw's frozen prefix
//   convention ("<Type>_"+shortId+"_", accessed P.<prefix><Name>; backward-traced from
//   field_ops_combinesdf.cpp:288 / translate.cpp:46). A wrong prefix reads the wrong/0 Rotation -> angle 0
//   -> identity rotation -> the p=(0,-0.3) probe goes RED. NOT forward-assumed.
//
// injectBug: configureRotateAxis(node, 90, Z, injectBug>0) corrupts the OP'S REAL preShaderCode emit:
//   1 = drop the pRotateAxis call -> no rotation -> p=(0,-0.3) reads q=(0,-0.3) -> +0.024 != -0.4 -> RED.
//   The rotated probes catch it; the tooth bites the OP's emit, not the template (expected values are the
//   CORRECT rotated field, never altered for injectBug).
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

// Param-cook + test seam owned by field_ops_rotateaxis.cpp (leaf type TU-private). Forward-declared.
void configureRotateAxis(FieldNode& node, float rotation, int axis, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;
constexpr float kCx = 0.3f, kCy = 0.0f, kCz = 0.0f;  // child sphere center OFF-ORIGIN at x=0.3.
constexpr float kRotationDeg = 90.0f;                // Rotation = 90 degrees.
constexpr int kAxisZ = 2;                            // Axis = Z -> swizzle "yx" (xy-plane rotation).

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

// Host closed-form: rotate p about Z (swizzle "yx") by the EXACT .cs deg->rad constant, then child sphere.
//   v = (p.y, p.x); v' = cos(a)*v + sin(a)*float2(v.y, -v.x); p.y := v'.x, p.x := v'.y. (p.z = 0.)
float rotatedField(float px, float py) {
  const float a = kRotationDeg / 180.0f * 3.141578f;  // byte-exact .cs constant (fork (2)).
  const float c = std::cos(a), s = std::sin(a);
  const float vx = py, vy = px;             // v = p.yx = (p.y, p.x)
  const float vpx = c * vx + s * vy;        // v'.x = cos*v.x + sin*v.y
  const float vpy = c * vy + s * (-vx);     // v'.y = cos*v.y - sin*v.x
  const float qy = vpx;                     // p.y := v'.x
  const float qx = vpy;                     // p.x := v'.y
  const float dx = qx - kCx, dy = qy - kCy;  // q.z = 0, cz = 0
  return std::sqrt(dx * dx + dy * dy) - kSphR;
}

std::shared_ptr<FieldNode> buildTree(int injectBug) {
  std::shared_ptr<FieldNode> mod = makeFieldNode("RotateAxis", "golden0");
  if (!mod) return nullptr;
  configureRotateAxis(*mod, kRotationDeg, kAxisZ, injectBug);
  // child OFF-CENTER at (0.3,0,0) so the rotation is the discriminator (sphere is rotation-invariant).
  mod->inputs.push_back(std::make_shared<GoldenSphere>("a", kCx, kCy, kCz, kSphR));
  return mod;
}

struct Probe { const char* name; uint32_t px, py; };

}  // namespace

int runFieldRotateAxisGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-rotateaxis] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-rotateaxis] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  // injectBug=1 (drop the pRotateAxis call) lives in the OP's REAL preShaderCode emit; production passes 0.
  const int bugMode = injectBug ? 1 : 0;
  std::shared_ptr<FieldNode> tree = buildTree(bugMode);
  if (!tree) {
    std::printf("[selftest-field-rotateaxis] FAIL: RotateAxis factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  clearTexOpCache();
  MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-rotateaxis] FAIL: renderField2d null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  const float kTol = 1e-5f;
  int rc = 0;

  const uint32_t cx = (kW - 1) / 2;  // x=0 column (probes vary y at x≈0)
  auto pyFor = [](float target) -> uint32_t {
    // invert pY: target = 1 - (2*py+1)/kH -> py = ((1-target)*kH - 1)/2
    float f = ((1.0f - target) * kH - 1.0f) * 0.5f;
    int py = (int)std::lround(f);
    if (py < 0) py = 0;
    if (py >= (int)kH) py = kH - 1;
    return (uint32_t)py;
  };

  // Probes: the rotated-vs-unrotated asymmetry is the tooth (child center off at x=0.3, rotate about Z).
  //   rotatedOnto p≈(0,-0.3) -> q≈(0.3,0) -> -0.4  (RED if rotation dropped: q=(0,-0.3) -> +0.024)
  //   rotatedFar  p≈(0, 0.3) -> q≈(-0.3,0) -> +0.2  (RED if dropped: +0.024 — pins a partial-rot mutation)
  //   rotatedEdge p≈(0, 0.8) -> q≈(-0.8,0) -> +0.7  (off-screen-safe; all probes/q in [-1,1] x-y plane)
  Probe probes[] = {
      {"rotatedOnto", cx, pyFor(-0.3f)},
      {"rotatedFar", cx, pyFor(0.3f)},
      {"rotatedEdge", cx, pyFor(0.8f)},
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = rotatedField(px, py);  // CORRECT rotated field (never altered for injectBug)
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-rotateaxis] probe %-12s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-rotateaxis] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-rotateaxis] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-rotateaxis] PASS\n");
  return rc;
}

}  // namespace sw
