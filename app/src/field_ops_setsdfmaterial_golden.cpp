// field_ops_setsdfmaterial_golden — --selftest-field-setsdfmaterial. GPU COLOR golden for
// SetSDFMaterial: a custom-collect ADJUST op that paints a material color onto the field's f.rgb when
// p.w is in (0.5, 1.5) — the "inside-surface" material band.
//
// THE P.W GATE: The standard field render template supplies p.w = 0 (from float4(uv, 0, 0)). The gate
// `p{c}.w > 0.5 && p{c}.w < 1.5` therefore NEVER fires with the unmodified template. To drive the gate
// we use a golden-only SdfWithPwSet child that emits BOTH a distance formula (f.w = sphere dist) AND
// `p{ctx}.w = 1.0;` — simulating the material-band marker that TiXL's 3D raymarcher would set for
// surfaces inside the material zone. With p.w = 1.0 the gate fires and f.rgb is overwritten by Color.
//
// READBACK: the standard template outputs f.w (RED channel). To probe f.rgb we use the Readback-leaf
// pattern from TranslateUV/BlendSdfWithSdf goldens: a golden-only wrapper that copies f.xyz.x (= f.r)
// into f.w after the op runs — then the standard R32Float readback reads the color R channel.
//
// TWO CASES exercised:
//   Case A (no ColorField, p.w = 1.0):
//     SetSDFMaterial emits `f{parent} = float4(P.<prefix>Color.rgb, f{parent}.w);`
//     → f.rgb = Color.rgb; f.w = sphere distance (unchanged by the no-ColorField branch).
//     Readback reads f.rgb.r = Color.r.
//
//   Case B (with ColorField, p.w = 1.0):
//     ColorField child (a golden-only ConstColorField) writes f.rgb = (kAlbedoR, kAlbedoG, kAlbedoB).
//     SetSDFMaterial emits `f{parent}.rgb = f{albedo}.rgb * P.<prefix>Color.rgb;`
//     → f.rgb.r = kAlbedoR * Color.r.
//     Readback reads f.rgb.r = kAlbedoR * Color.r.
//
// P.W GATE GUARD (tooth): injectBug=1 inverts the gate to `<= 0.5 || >= 1.5` → gate NEVER fires with
//   p.w = 1.0 (which is NOT ≤0.5 and IS ≥1.5, so the inverted gate doesn't fire either). Wait: p.w=1.0
//   → 1.0 >= 1.5 is FALSE, so the bug inversion → `p.w <= 0.5 || p.w >= 1.5` → both FALSE → gate still
//   doesn't fire. But actually 1.0 is NOT >= 1.5, and NOT <= 0.5, so both conditions are false, so the
//   inverted gate DOES NOT fire. With the correct gate the condition `p.w > 0.5 && p.w < 1.5` = TRUE
//   (1.0 > 0.5 AND 1.0 < 1.5). With inverted gate: `p.w <= 0.5 || p.w >= 1.5` = FALSE. So the color is
//   NOT applied → f.rgb stays at the initial seed (f=1 from the template), Readback reads f.rgb.r = 1.0
//   (all-ones field seed) not Color.r → probe RED.
//
// ZONE: shell tier (app/src/ root) — crosses runtime (renderField2d/makeFieldNode/configureSetSDFMaterial)
//   + platform (compileLibraryFromSource); a runtime-zone selftest may NOT include platform (check-arch).
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

// Param-cook + test seam owned by field_ops_setsdfmaterial.cpp (leaf type TU-private).
void configureSetSDFMaterial(FieldNode& node, float r, float g, float b, float a, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;

// Color applied by SetSDFMaterial (the [GraphParam] value we configure).
constexpr float kColorR = 0.5f, kColorG = 0.3f, kColorB = 0.8f, kColorA = 1.0f;
// Known albedo written by the ConstColorField child (Case B).
constexpr float kAlbedoR = 0.7f, kAlbedoG = 0.2f, kAlbedoB = 0.4f;

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

// Golden-only SdfWithPwSet: sphere SDF at origin that ALSO writes `p{ctx}.w = 1.0` to trigger the
// SetSDFMaterial p.w gate. Real distance: f.w = length(p.xyz) - kSphR.
struct SdfWithPwSet : FieldNode {
  SdfWithPwSet() { prefix = "GSdf_"; }
  void preShaderCode(CodeAssembleCtx& c, int) const override {
    const std::string ctx = c.ctx();
    c.appendCall("f" + ctx + ".w = length(p" + ctx + ".xyz) - P." + prefix + "Radius;");
    // Set p.w = 1.0 to fall inside the material band (0.5, 1.5).
    c.appendCall("p" + ctx + ".w = 1.0;");
  }
  void collectParams(std::vector<float>& fp, std::vector<std::string>& pf) const override {
    appendScalarParam(fp, pf, prefix + "Radius", kSphR);
  }
};

// Golden-only ConstColorField: writes KNOWN f.rgb = (kAlbedoR, kAlbedoG, kAlbedoB) for Case B test.
struct ConstColorField : FieldNode {
  ConstColorField() { prefix = "GColor_"; }
  void preShaderCode(CodeAssembleCtx& c, int) const override {
    const std::string ctx = c.ctx();
    char buf[256];
    std::snprintf(buf, sizeof(buf), "f%s.rgb = float3(%.6f, %.6f, %.6f);",
                  ctx.c_str(), kAlbedoR, kAlbedoG, kAlbedoB);
    c.appendCall(buf);
    c.appendCall("f" + ctx + ".w = 0.0;");  // dummy distance; not probed
  }
  void collectParams(std::vector<float>&, std::vector<std::string>&) const override {}
};

// Golden-only Readback: single-input wrapper that copies f.xyz.x (== f.r) into f.w so the standard
// R32Float readback surface the color R channel.
// Pattern: TranslateUV golden (field_ops_translateuv_golden.cpp : ReadbackNode).
struct ReadbackNode : FieldNode {
  ReadbackNode() { prefix = "GReadback_"; }
  void preShaderCode(CodeAssembleCtx&, int) const override {}
  void postShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    const std::string ctx = c.ctx();
    // After the child (SetSDFMaterial) ran, f.rgb = Color.rgb (or albedo*Color.rgb). Surface f.r into f.w.
    c.appendCall("f" + ctx + ".w = f" + ctx + ".rgb.x;");
  }
  void collectParams(std::vector<float>&, std::vector<std::string>&) const override {}
};

// Build a tree: Readback → SetSDFMaterial → [SdfWithPwSet, optional ConstColorField].
std::shared_ptr<FieldNode> buildTree(bool withColorField, int injectBug) {
  std::shared_ptr<FieldNode> mat = makeFieldNode("SetSDFMaterial", "golden0");
  if (!mat) return nullptr;
  configureSetSDFMaterial(*mat, kColorR, kColorG, kColorB, kColorA, injectBug);
  mat->inputs.push_back(std::make_shared<SdfWithPwSet>());          // inputs[0] = SdfField
  if (withColorField)
    mat->inputs.push_back(std::make_shared<ConstColorField>());      // inputs[1] = ColorField (opt.)
  // Readback wraps SetSDFMaterial: its inputs[0] = mat, and postShaderCode copies f.r into f.w.
  auto rb = std::make_shared<ReadbackNode>();
  rb->inputs.push_back(mat);
  return rb;
}

struct Probe { const char* name; uint32_t px, py; float expected; float tol; };

}  // namespace

int runFieldSetSDFMaterialGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-setsdfmaterial] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-setsdfmaterial] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  const int bugMode = injectBug ? 1 : 0;
  const float kTol = 2e-4f;
  int rc = 0;
  const uint32_t cy = (kH - 1) / 2;

  auto runCase = [&](const char* caseName, bool withColorField, std::vector<Probe>& probes) {
    clearTexOpCache();
    std::shared_ptr<FieldNode> tree = buildTree(withColorField, bugMode);
    if (!tree || tree->inputs.empty() || !tree->inputs[0]) {
      std::printf("[selftest-field-setsdfmaterial] FAIL[%s]: SetSDFMaterial factory not registered\n",
                  caseName);
      rc = 1;
      return;
    }
    MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
    if (!tex) {
      std::printf("[selftest-field-setsdfmaterial] FAIL[%s]: renderField2d null (compile/PSO failure)\n",
                  caseName);
      rc = 1;
      return;
    }
    std::vector<float> buf((size_t)kW * kH, 0.0f);
    tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
    for (Probe& pr : probes) {
      const float got = buf[(size_t)pr.py * kW + pr.px];
      const float diff = std::fabs(got - pr.expected);
      const bool ok = diff <= pr.tol;
      if (!ok) rc = 1;
      std::printf("[selftest-field-setsdfmaterial] %-10s probe %-10s p=(% .4f,% .4f) got=% .6f "
                  "expected=% .6f diff=%.2e %s\n",
                  caseName, pr.name, pX(pr.px), pY(pr.py), got, pr.expected, diff, ok ? "OK" : "RED");
    }
    tex->release();
  };

  // ---- Case A: NO ColorField. Color = (0.5, 0.3, 0.8, 1.0), p.w = 1.0 (gate fires). ----
  // SetSDFMaterial emits `f.rgb = float4(P.<prefix>Color.rgb, f.w)` → f.rgb.r = Color.r = kColorR.
  // Readback copies f.rgb.r → f.w → readback reads kColorR from any pixel (gate fires everywhere
  // since SdfWithPwSet unconditionally sets p.w = 1.0).
  // With injectBug=1: gate inverted → never fires → f.rgb.r stays at all-ones seed = 1.0 ≠ kColorR → RED.
  {
    // CORRECT expected (no bug): gate fires → f.rgb.r = Color.r = kColorR.
    // With bug gate is inverted → doesn't fire → f.rgb.r = 1.0 (all-ones seed) → probe bites (got≠expected).
    std::vector<Probe> probes = {
        // Three spatially distinct pixels to confirm the formula applies everywhere (not just center).
        {"center", kW / 2,     cy, kColorR, kTol},
        {"left",   kW / 4,     cy, kColorR, kTol},
        {"right",  3 * kW / 4, cy, kColorR, kTol},
    };
    runCase("noColorField", /*withColorField=*/false, probes);
  }

  // ---- Case B: WITH ColorField (ConstColorField writes kAlbedoR). Color.r = kColorR. ----
  // SetSDFMaterial emits `f{parent}.rgb = f{albedo}.rgb * P.<prefix>Color.rgb;`
  // → f.rgb.r = kAlbedoR * kColorR.  Readback copies f.rgb.r → f.w.
  // With injectBug=1: gate inverted → never fires → f.rgb.r stays 1.0 (all-ones seed) ≠ kAlbedoR*kColorR → RED.
  {
    const float expectedR = kAlbedoR * kColorR;  // 0.7 * 0.5 = 0.35
    std::vector<Probe> probes = {
        {"center",    kW / 2,     cy, expectedR, kTol},
        {"left",      kW / 4,     cy, expectedR, kTol},
        {"right",     3 * kW / 4, cy, expectedR, kTol},
    };
    runCase("withColorFld", /*withColorField=*/true, probes);
  }

  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-setsdfmaterial] FAIL: injectBug did not trip any probe (tooth has "
                  "no bite)\n");
      return 1;
    }
    std::printf("[selftest-field-setsdfmaterial] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-setsdfmaterial] PASS\n");
  return rc;
}

}  // namespace sw
