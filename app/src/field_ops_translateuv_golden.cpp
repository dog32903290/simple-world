// field_ops_translateuv_golden — --selftest-field-translateuv. GPU VALUE golden for the TranslateUV
// single-input POST-wrap MODIFIER. TranslateUV shifts the field's CARRIED f.xyz (local-space / color),
// NOT its distance f.w — but the render template writes only f.w into RED. So this golden wraps the op
// in a golden-only READBACK leaf that copies the post-translate f.xyz.x INTO f.w, making the f.xyz
// effect observable in the standard R32Float readback. (Local golden leaves are the established pattern:
// field_ops_combinesdf_golden.cpp's GoldenSphere.)
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_combinesdf_golden.cpp).
//
// TREE: Readback( TranslateUV( ColorChild ) ).  Codegen order (nested single-input wrap,
// field_graph.cpp:82-86):
//   Readback.pre(none) -> [ TranslateUV.pre(none) -> ColorChild -> TranslateUV.post ] -> Readback.post
//   ColorChild   : f.xyz = (cvx,cvy,cvz);  f.w = 1.0;        (writes a KNOWN carried color + a dummy d)
//   TranslateUV.post : f.xyz -= p.w<0.5 ? Translation : 0;   (p.w=0 in this template -> branch TRUE ->
//                                                             f.xyz = (cvx,cvy,cvz) - Translation)
//   Readback.post    : f.w = f.xyz.x;                        (surface the translated x into the RED out)
// So root f.w == cvx - Translation.x.  With cvx=0.6, Translation.x=0.25 -> 0.35; Translation.x=0 -> 0.6.
//
// PARAM-PREFIX: the emitted token P.TranslateUV_<id>_Translation MUST match sw's frozen prefix
//   convention (combinesdf.cpp:288). A wrong prefix reads the wrong/0 member -> the Tx=0.25 probe
//   (expects 0.35 ONLY if Translation.x is read) goes RED. NOT forward-assumed.
//
// injectBug: configureTranslateUv(node, ..., injectBug=2) DROPS the OP's REAL postShaderCode (no f.xyz
//   shift) -> f.xyz stays (cvx,cvy,cvz) -> readback reads cvx (0.6) instead of 0.35 -> RED. Tooth bites
//   the OP's emit, not the template (no expected-value tautology). injectBug=1 (wrong sign +=) would also
//   bite (0.6+0.25=0.85 != 0.35); we use the drop mode here.
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

// Param-cook + test seam owned by field_ops_translateuv.cpp (leaf type TU-private). Forward-declared here.
void configureTranslateUv(FieldNode& node, float tx, float ty, float tz, int injectBug);

namespace {

constexpr uint32_t kW = 64, kH = 64;
constexpr float kCvx = 0.6f, kCvy = -0.2f, kCvz = 0.1f;  // child's KNOWN carried f.xyz color.

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

// Golden-only ColorChild: writes a KNOWN f.xyz (carried color/local-space) and a dummy f.w. No params.
// This is the field the TranslateUV op modifies (it only touches f.xyz).
struct ColorChild : FieldNode {
  ColorChild() { prefix = "GColor_"; }
  void preShaderCode(CodeAssembleCtx& c, int) const override {
    const std::string ctx = c.ctx();
    char buf[128];
    std::snprintf(buf, sizeof(buf), "f%s.xyz = float3(%.6f, %.6f, %.6f);", ctx.c_str(), kCvx, kCvy, kCvz);
    c.appendCall(buf);
    c.appendCall("f" + ctx + ".w = 1.0;");
  }
  void collectParams(std::vector<float>&, std::vector<std::string>&) const override {}
};

// Golden-only Readback: single-input wrapper whose POST surfaces the (post-translate) f.xyz.x into f.w
// so the template's RED readback exposes it. (No params; pure post copy.)
struct Readback : FieldNode {
  Readback() { prefix = "GReadback_"; }
  void preShaderCode(CodeAssembleCtx&, int) const override {}
  void postShaderCode(CodeAssembleCtx& c, int) const override {
    const std::string ctx = c.ctx();
    c.appendCall("f" + ctx + ".w = f" + ctx + ".xyz.x;");
  }
  void collectParams(std::vector<float>&, std::vector<std::string>&) const override {}
};

std::shared_ptr<FieldNode> buildTree(float tx, int injectBug) {
  std::shared_ptr<FieldNode> uv = makeFieldNode("TranslateUV", "golden0");
  if (!uv) return nullptr;
  configureTranslateUv(*uv, tx, 0.f, 0.f, injectBug);
  uv->inputs.push_back(std::make_shared<ColorChild>());
  auto readback = std::make_shared<Readback>();
  readback->inputs.push_back(uv);
  return readback;
}

int renderAndCheck(MTL::Device* dev, MTL::CommandQueue* q, const std::string& tmpl, const char* label,
                   float tx, int injectBug) {
  clearTexOpCache();
  std::shared_ptr<FieldNode> tree = buildTree(tx, injectBug);
  if (!tree) {
    std::printf("[selftest-field-translateuv] FAIL: TranslateUV factory not registered\n");
    return 1;
  }
  MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-translateuv] FAIL(%s): renderField2d null (compile/PSO failure)\n",
                label);
    return 1;
  }
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  // The value is uniform across the frame (no p dependence in ColorChild), so any texel works; read center.
  const float got = buf[(size_t)(kH / 2) * kW + (kW / 2)];
  const float expected = kCvx - tx;  // f.xyz.x = cvx - Translation.x (p.w=0 -> branch TRUE).
  const float diff = std::fabs(got - expected);
  const float kTol = 1e-5f;
  const bool ok = diff <= kTol;
  std::printf("[selftest-field-translateuv] probe %-10s Tx=%.2f got=% .6f expected=% .6f diff=%.2e %s\n",
              label, tx, got, expected, diff, ok ? "OK" : "RED");
  tex->release();
  return ok ? 0 : 1;
}

}  // namespace

int runFieldTranslateUvGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-translateuv] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-translateuv] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  const int bugMode = injectBug ? 2 : 0;  // 2 = drop the post f.xyz shift (production passes 0).
  int rc = 0;
  // Tx=0 -> identity (f.xyz.x unchanged = 0.6). With the bug dropped this also reads 0.6, so it alone
  // can't catch the bug — it confirms the wrapped path is correct in production.
  rc |= renderAndCheck(dev, q, tmpl, "tx0", 0.0f, bugMode);
  // Tx=0.25 -> f.xyz.x = 0.6 - 0.25 = 0.35 (the discriminator). injectBug drops the shift -> reads 0.6 -> RED.
  rc |= renderAndCheck(dev, q, tmpl, "tx025", 0.25f, bugMode);

  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-translateuv] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-translateuv] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-translateuv] PASS\n");
  return rc;
}

}  // namespace sw
