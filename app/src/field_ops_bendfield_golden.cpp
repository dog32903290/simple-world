// field_ops_bendfield_golden — --selftest-field-bendfield. GPU DISTANCE-VALUE golden for the BendField
// single-input MODIFIER (the first op to drive BOTH halves of the field_graph single-input wrap branch,
// field_graph.cpp:82-86: preShaderCode bends the point BEFORE the child recursion, postShaderCode scales
// the distance AFTER). Builds BendField(GoldenSphere), assembles via the FROZEN base, compiles, renders,
// reads back R32Float (f.w into RED), asserts the probe == childSphereDistance * StepFactor. Mirrors
// field_ops_translate_golden.cpp's harness.
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_translate_golden.cpp).
//
// CLOSED-FORM: child = GoldenSphere(center=0, r=0.4) -> d_child(q) = |q| - 0.4.
//   Amount = 0 -> opBend is IDENTITY: k = 0/(180/pi) = 0 -> cos=1,sin=0 -> rotation matrix = identity ->
//   p unchanged -> child samples q = p. So field(p) BEFORE the post scale = |p| - 0.4.
//   The post `f.w *= StepFactor` then multiplies that distance:
//     field(p) = (|p| - 0.4) * StepFactor.
//   Probe p=(0.5,0) -> |p| = 0.5 -> child d = +0.1. Two StepFactor cases discriminate the post line:
//     StepFactor=1.0  -> +0.1   (identity bend + identity scale; the WHOLE-path baseline)
//     StepFactor=0.5  -> +0.05  (the f.w POST discriminator: only a real `*= StepFactor` gives 0.05)
//
// Why the two Amount=0 probes stay: at Amount=0 the pre line is a verified no-op (q=p) so the StepFactor
// probes isolate the POST emit cleanly (they also pin the pixel->p mapping the mid probe reuses). But an
// identity point can NEVER bite the bend math itself (P2, GOLDEN_ORACLE_AUDIT.md) — and an ORIGIN-CENTERED
// child can't either (any rotation of a coordinate pair preserves |p|, so |bend(p)| == |p| for EVERY
// Amount). Hence the third probe below: Amount!=0 AND an off-center child.
//
// MID PROBE "bend45mid" (P2 fix — the bend formula finally bites). Hand-derived from BendField.cs ONLY
// (never from sw output). Axis=Z -> swizzle "xyz" (BendField.cs:59-64) -> t=p, the rotation stays in the
// render plane (z=0). TiXL math (BendField.cs:38-45, HLSL):
//     k  = Amount / (180 / 3.14157892)          (.cs:40 — TiXL's literal pi, kept exact)
//     th = k * t.x                              (.cs:41-42 — angle proportional to swizzled .x)
//     m  = float2x2(c,-s,s,c)  -- HLSL fills ROW-major: row0=(c,-s), row1=(s,c)
//     mul(m, t.xy)             -- t.xy as COLUMN vector -> (c*x - s*y, s*x + c*y) = rotation by +th
// Probe pixel (96,63) -> p=(0.5078125, 0.0078125, 0); Amount=90 -> th = 0.797666523 rad (45.7029 deg):
//     cos=0.698378745 sin=0.715728390
//     q = (x*c - y*s, x*s + y*c, 0) = (0.349053828, 0.368911907, 0)
// Child sphere center (-0.2,-0.3,0) r=0.4 (OFF-CENTER — see above; in-plane since Axis=Z keeps z=0):
//     d = |q - C| - 0.4 = 0.465391961;  expected = d * StepFactor(0.5) = 0.232695980
// Discrimination (all >> kTolMid=1e-4, python-verified):
//     transposed rotation R(-th)      -> 0.081616  (dev 0.151)   [see HLSL-vs-MSL note below]
//     pre line dropped (q=p)          -> 0.185923  (dev 0.047)
//     deg-not-rad (k = Amount)        -> 0.204675  (dev 0.028)
//     wrong swizzle (yzx applied)     -> 0.185923  (dev 0.047)
//     post *= StepFactor dropped      -> 0.465392  (dev 0.233)   [so injectBug trips this probe too]
//
// SUSPECTED PARITY FORK (audit prey — implementation NOT touched, per P2 workorder): TiXL HLSL
// `float2x2(c,-s,s,c)` fills ROW-major (row0=(c,-s)) and `mul(m,v)` treats v as a column -> rotation by
// +th. sw's MSL port (field_ops_bendfield.cpp:58-59) emits `float2x2(c,-s,s,c)` + `m * p.xy` — MSL fills
// scalars COLUMN-major (col0=(c,-s)), so `m*v` computes the TRANSPOSE = rotation by -th. If this probe
// goes RED with got ~= +0.081616 (the R(-th) prediction above), that is the frozen "mul(m,v) -> m*v" fork
// flipping rotation direction — invisible at Amount=0 and for origin-centered children, which is exactly
// why no golden ever caught it. Expected value stays TiXL-derived (the authority); do not backfill.
//
// PARAM-PREFIX (BLOOD LESSON): the emitted token P.BendField_<id>_StepFactor MUST match sw's frozen
//   prefix convention ("<Type>_"+shortId+"_", accessed P.<prefix><Name>; backward-traced from
//   field_ops_combinesdf.cpp:288 / translate.cpp:46). A wrong prefix reads the wrong/0 struct member ->
//   the StepFactor=0.5 probe (expects exactly 0.05 ONLY if StepFactor is read) goes RED. NOT
//   forward-assumed.
//
// injectBug: configureBendField(node, ..., injectBug=1) DROPS the OP's REAL postShaderCode `*= StepFactor`
//   line. With the scale gone, the StepFactor=0.5 case reads the unscaled +0.1 instead of +0.05 -> RED.
//   Tooth bites the OP's emit, not the template (no expected-value tautology).
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

// Param-cook + test seam owned by field_ops_bendfield.cpp (leaf type TU-private). Forward-declared here.
void configureBendField(FieldNode& node, float amount, float stepFactor, int axis, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;

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

// Host oracle — the FULL TiXL bend formula, hand-ported from BendField.cs (HLSL semantics; NEVER from
// sw's MSL emit — that would be a self-consistent fake anchor, P5). Double precision. The render plane
// samples p=(px,py,0). Steps + citations:
//   swizzle copy-in  t = p.<perm>          — BendField.cs:50-51 call `opBend(p{c}.{axi}, ...)`; HLSL
//                                            inout on a swizzle = copy-in/copy-out. perm table .cs:59-64.
//   k  = Amount/(180/3.14157892)           — BendField.cs:40 (TiXL's literal pi — kept exact).
//   th = k * t.x                           — BendField.cs:41-42 (angle proportional to swizzled .x).
//   rot: HLSL float2x2(c,-s,s,c) fills ROW-major (row0=(c,-s)); mul(m, t.xy) treats t.xy as a COLUMN
//        vector -> (c*x - s*y, s*x + c*y) = rotation by +th.  — BendField.cs:43-44.
//   swizzle copy-out p.<perm> = rotated t  — the inout write-back; then child d = |q - C| - r and the
//   post scale d * StepFactor              — BendField.cs:56.
// At Amount=0 this reduces exactly to the old identity closed-form (|p| - r) * StepFactor.
float bentFieldTiXL(float px, float py, float amount, int axis, float cx, float cy, float cz,
                    float stepFactor) {
  static const int kPerm[3][3] = {{1, 2, 0}, {0, 2, 1}, {0, 1, 2}};  // "yzx","xzy","xyz" (.cs:59-64)
  const int* perm = kPerm[axis];
  const double p[3] = {px, py, 0.0};
  const double t[3] = {p[perm[0]], p[perm[1]], p[perm[2]]};  // copy-in: t = p.<perm>
  const double k = amount / (180.0 / 3.14157892);            // .cs:40
  const double th = k * t[0];                                // .cs:41-42
  const double c = std::cos(th), s = std::sin(th);
  const double rot[3] = {c * t[0] - s * t[1], s * t[0] + c * t[1], t[2]};  // .cs:43-44 (+th, see above)
  double q[3] = {p[0], p[1], p[2]};
  q[perm[0]] = rot[0]; q[perm[1]] = rot[1]; q[perm[2]] = rot[2];  // copy-out: p.<perm> = t
  const double dx = q[0] - cx, dy = q[1] - cy, dz = q[2] - cz;
  const double d = std::sqrt(dx * dx + dy * dy + dz * dz) - kSphR;  // child sphere
  return (float)(d * stepFactor);                                  // .cs:56 post scale
}

std::shared_ptr<FieldNode> buildTree(float amount, float stepFactor, int axis, float cx, float cy,
                                     float cz, int injectBug) {
  std::shared_ptr<FieldNode> mod = makeFieldNode("BendField", "golden0");
  if (!mod) return nullptr;
  configureBendField(*mod, amount, stepFactor, axis, injectBug);
  mod->inputs.push_back(std::make_shared<GoldenSphere>("a", cx, cy, cz, kSphR));
  return mod;
}

// Render one tree (Amount/StepFactor/Axis/child-center/injectBug) and check the probe pixel (targetX on
// row `rowPy`) against the host TiXL oracle. Returns 0 if within tol, 1 otherwise. Prints one line.
int renderAndCheck(MTL::Device* dev, MTL::CommandQueue* q, const std::string& tmpl, const char* label,
                   float amount, float stepFactor, int axis, float cx, float cy, float cz, float targetX,
                   uint32_t rowPy, float tol, int injectBug) {
  clearTexOpCache();
  std::shared_ptr<FieldNode> tree = buildTree(amount, stepFactor, axis, cx, cy, cz, injectBug);
  if (!tree) {
    std::printf("[selftest-field-bendfield] FAIL: BendField factory not registered\n");
    return 1;
  }
  MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-bendfield] FAIL(%s): renderField2d null (compile/PSO failure)\n", label);
    return 1;
  }
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);

  auto pxFor = [](float target) -> uint32_t {
    float f = ((target + 1.0f) * kW - 1.0f) * 0.5f;
    int px = (int)std::lround(f);
    if (px < 0) px = 0;
    if (px >= (int)kW) px = kW - 1;
    return (uint32_t)px;
  };

  const uint32_t qx = pxFor(targetX);
  const float px = pX(qx), py = pY(rowPy);
  // CORRECT field per the TiXL formula (never altered for injectBug, never backfilled from sw output).
  const float expected = bentFieldTiXL(px, py, amount, axis, cx, cy, cz, stepFactor);
  const float got = buf[(size_t)rowPy * kW + qx];
  const float diff = std::fabs(got - expected);
  const bool ok = diff <= tol;
  std::printf("[selftest-field-bendfield] probe %-11s p=(% .4f,% .4f) A=%.1f SF=%.2f axis=%d got=% .6f "
              "expected=% .6f diff=%.2e %s\n",
              label, px, py, amount, stepFactor, axis, got, expected, diff, ok ? "OK" : "RED");
  tex->release();
  return ok ? 0 : 1;
}

}  // namespace

int runFieldBendFieldGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-bendfield] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-bendfield] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  // injectBug lives in the OP's REAL postShaderCode emit (drops `*= StepFactor`); production passes 0.
  const int bugMode = injectBug ? 1 : 0;
  const uint32_t kMidRow = (kH - 1) / 2;  // row 63 -> p.y = 0.0078125
  int rc = 0;
  // Identity probes (Amount=0, axis X, origin child): p=(0.5078,0.0078) -> child d = |p| - 0.4 ~ +0.1.
  // StepFactor=1.0 -> identity scale baseline. With the bug dropped, *no scale* still reads the same
  // here, so this case alone can't catch the bug — but it confirms the identity-bend + identity-scale
  // whole path is correct in production.
  rc |= renderAndCheck(dev, q, tmpl, "stepfactor1", 0.0f, 1.0f, 0, 0.f, 0.f, 0.f, 0.5f, kMidRow,
                       1e-5f, bugMode);
  // StepFactor=0.5 -> the POST discriminator (+0.05-ish with the scale, unscaled without). injectBug
  // drops the `*= StepFactor` -> this probe goes RED.
  rc |= renderAndCheck(dev, q, tmpl, "stepfactor05", 0.0f, 0.5f, 0, 0.f, 0.f, 0.f, 0.5f, kMidRow,
                       1e-5f, bugMode);
  // MID PROBE (P2 fix): Amount=90, Axis=Z ("xyz" — rotation stays in the z=0 render plane), OFF-CENTER
  // child (-0.2,-0.3,0) r=0.4, StepFactor=0.5. Hand-derived expected (full derivation in the header):
  //   th = 0.797666523 rad; q = (0.349053828, 0.368911907, 0); expected = 0.232695980.
  // Transposed-rotation (MSL column-major float2x2 fill) would read ~+0.081616 — see the SUSPECTED
  // PARITY FORK note in the header. Tol 1e-4 (float32 GPU trig; every modeled formula error deviates
  // >= 2.8e-2, i.e. >= 280x tol). SF=0.5 also makes this probe bite injectBug (unscaled +0.465392).
  rc |= renderAndCheck(dev, q, tmpl, "bend45mid", 90.0f, 0.5f, 2, -0.2f, -0.3f, 0.f, 0.5f, kMidRow,
                       1e-4f, bugMode);

  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-bendfield] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 0;  // dead tooth -> exit 0 so --bite NO-BITE list catches it
    }
    std::printf("[selftest-field-bendfield] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-bendfield] PASS\n");
  return rc;
}

}  // namespace sw
