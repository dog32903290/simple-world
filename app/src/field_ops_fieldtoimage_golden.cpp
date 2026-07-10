// field_ops_fieldtoimage_golden — --selftest-field-fieldtoimage. GPU CLOSED-FORM golden for the
// FieldToImage render kernel (renderFieldToImage, field_to_image_template.metal): assemble a SphereSDF
// field, rasterize a red->green gradient row, render through the FieldToImage template at IDENTITY
// placement params (Center=0/Scale=1/Rotate=0/aspect=1 — same reduction field_ops_boxsdf_golden.cpp
// documents for field_render_template.metal), read back the RGBA32Float image, and assert:
//   (Mode=0, MapDistanceToColor) three probed texels' RGBA == SwGradient::sample(saturate(d)) at the
//     closed-form sphere distance d = length(p)-Radius for that texel's EXACT field-space p.
//   (Mode=1, UseColor) one probed texel's RGB == the SphereSDF leaf's f.xyz carry == p.xyz itself
//     (SphereSDFNode: `f.xyz = p.w<0.5 ? p.xyz : 1`; our template always seeds p.w=0).
//
// PIXEL -> FIELD-SPACE p: IDENTICAL to field_ops_boxsdf_golden.cpp / field_render_golden.cpp
// (p.x=(2*px+1)/W-1 ; p.y=1-(2*py+1)/H) — this template's uv mapping reduces to the SAME formula at
// Center=(0,0)/Scale=1/Rotate=0/aspect=1 (see field_to_image_template.metal fragment body).
//
// ZONE: shell tier (app/src/ root, like field_ops_boxsdf_golden.cpp) — crosses runtime (renderFieldToImage,
// makeFieldNode, rasterizeGradientRow) AND platform (compileLibraryFromSource) — a runtime-zone selftest
// may NOT include platform (check_arch: runtime ↛ platform).
//
// injectBug: corrupt the template's Range-remap line (the unique `d = (d - OP.RangeX)` substring) so
// Mode=0's normalized distance is forced to 0 regardless of the real field -> the interpolation-band probe
// (which needs a MID-gradient color) reads a saturated end color instead -> RED. Mode=1 is UNCHANGED by
// this corruption (its return sits before the corrupted line only for f.rgb, but the corrupted line is
// PAST the Mode branch's early return in source order — see fragment body — so Mode=1 truly never
// executes the corrupted line and is skipped under injectBug, GOLDEN_STANDARD 特徵3 discipline).
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

#include "runtime/field_graph.h"          // setFieldSourceCompiler, FieldNode
#include "runtime/field_node_registry.h"  // makeFieldNode (SphereSDF leaf-private)
#include "runtime/gradient_raster.h"      // rasterizeGradientRow, kGradientRowN
#include "runtime/sw_gradient.h"          // SwGradient
#include "runtime/tex_op_cache.h"         // clearTexOpCache

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {
namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kRadius = 0.5f;  // SphereSDF.t3 default

std::string loadTemplate() {
#ifdef SW_FIELD_TO_IMAGE_TEMPLATE
  std::ifstream f(SW_FIELD_TO_IMAGE_TEMPLATE);
  if (!f) return "";
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
#else
  return "";
#endif
}

// Field-space p at pixel (px,py) — identical derivation to field_ops_boxsdf_golden.cpp.
float pX(uint32_t px) { return (2.0f * px + 1.0f) / kW - 1.0f; }
float pY(uint32_t py) { return 1.0f - (2.0f * py + 1.0f) / kH; }

// Closed-form sphere distance at z=0, Center=(0,0,0) (SphereSDF.cs: length(p.xyz-Center)-Radius).
float sphereSdf(float px, float py) { return std::sqrt(px * px + py * py) - kRadius; }

// Red(0)->Green(1) linear gradient (the R-2 hardening — a non-default-gradient wire, same rationale as
// point_ops_boxgradient.cpp's redGreenGradient()).
SwGradient redGreenGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(1.0f, 0.0f, 0.0f, 1.0f)});
  g.steps.push_back({1.0f, simd::make_float4(0.0f, 1.0f, 0.0f, 1.0f)});
  return g;
}

bool near4(simd::float4 a, simd::float4 b, float tol) {
  return std::fabs(a.x - b.x) <= tol && std::fabs(a.y - b.y) <= tol && std::fabs(a.z - b.z) <= tol &&
         std::fabs(a.w - b.w) <= tol;
}

}  // namespace

int runFieldToImageGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-fieldtoimage] FAIL: could not load template (SW_FIELD_TO_IMAGE_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-fieldtoimage] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  std::shared_ptr<FieldNode> sphere = makeFieldNode("SphereSDF", "golden0");
  if (!sphere) {
    std::printf("[selftest-field-fieldtoimage] FAIL: SphereSDF factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // injectBug at the MSL-string tier (mirrors field_ops_boxsdf_golden.cpp): corrupt the Range-remap line
  // so Mode=0's normalized distance collapses to 0 (always the RangeX/red endpoint).
  std::string useTmpl = tmpl;
  if (injectBug) {
    const std::string from = "d = (d - OP.RangeX) / (OP.RangeY - OP.RangeX);";
    const std::string to = "d = 0.0f;";
    size_t pos = useTmpl.find(from);
    if (pos == std::string::npos) {
      std::printf("[selftest-field-fieldtoimage] FAIL: injectBug could not find the range-remap site "
                  "(tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 0;  // did-not-trip -> 0, NO-BITE list catches it
    }
    useTmpl.replace(pos, from.size(), to);
  }

  SwGradient ref = redGreenGradient();
  MTL::Texture* gradTex = rasterizeGradientRow(dev, ref, kGradientRowN);
  if (!gradTex) {
    std::printf("[selftest-field-fieldtoimage] FAIL: rasterizeGradientRow returned null\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  int rc = 0;

  // --- MODE=0 (MapDistanceToColor): identity placement (Center=0,Scale=1,Rotate=0,SliceDepth=0). ---
  {
    FieldToImageParams p{};  // mode=0, Center=(0,0), Scale=1, Rotate=0, SliceDepth=0, Range=(0,1),
                              // GainAndBias=(0.5,0.5) [identity], PingPong=0, Repeat=0 — all .t3 defaults.
    MTL::Texture* tex = renderFieldToImage(dev, q, sphere, useTmpl, p, gradTex, kW, kH);
    if (!tex) {
      std::printf("[selftest-field-fieldtoimage] FAIL: renderFieldToImage (mode0) returned null\n");
      rc = 1;
    } else {
      std::vector<float> buf((size_t)kW * kH * 4, 0.0f);
      tex->getBytes(buf.data(), kW * 4 * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
      auto colAt = [&](uint32_t px, uint32_t py) {
        size_t i = ((size_t)py * kW + px) * 4;
        return simd::make_float4(buf[i], buf[i + 1], buf[i + 2], buf[i + 3]);
      };
      const uint32_t cy = (kH - 1) / 2;  // 63 -> p.y ≈ 0 (center row)
      struct Probe { const char* name; uint32_t px; };
      // inside (d<0 -> saturate 0 -> red endpoint), band (d in (0,1) -> interpolated color — the
      // load-bearing tooth, corrupted by injectBug). NOTE: at this 128x128/Scale=1 frame, |p| never
      // reaches 1.5 (max corner distance from the R=0.5 sphere is ≈0.90), so d never saturates to the
      // GREEN(1) endpoint anywhere in-frame — only the RED(0)/mid-band cases are reachable here.
      const Probe probes[] = {
          {"inside", (kW - 1) / 2},  // p≈(0,0) -> d≈-0.5
          {"band", 112},              // p.x≈0.758 -> d≈0.258 (mid-gradient)
      };
      for (const Probe& pr : probes) {
        float px = pX(pr.px), py = pY(cy);
        float d = sphereSdf(px, py);
        float t = std::max(0.0f, std::min(1.0f, d));  // saturate (PingPong=Repeat=0 branch)
        simd::float4 want = injectBug ? ref.sample(0.0f) : ref.sample(t);
        simd::float4 got = colAt(pr.px, cy);
        const float kTol = 0.01f;
        bool ok = injectBug ? true : near4(got, want, kTol);  // under injectBug we only assert the
                                                                // BAND probe below (others may coincide)
        if (!injectBug && !ok) rc = 1;
        std::printf("[selftest-field-fieldtoimage] mode0 probe %-8s p=(% .4f,% .4f) d=% .4f t=%.4f "
                    "got=(%.3f,%.3f,%.3f,%.3f) want=(%.3f,%.3f,%.3f,%.3f) %s\n",
                    pr.name, px, py, d, t, got.x, got.y, got.z, got.w, want.x, want.y, want.z, want.w,
                    ok ? "OK" : "RED");
      }
      // The BAND probe is the load-bearing injectBug tooth: corrupted -> t forced to 0 -> the band
      // pixel reads the RED endpoint instead of the interpolated color -> mismatch vs the TRUE want.
      if (injectBug) {
        float px = pX(112), py = pY(cy);
        float d = sphereSdf(px, py);
        float trueT = std::max(0.0f, std::min(1.0f, d));
        simd::float4 trueWant = ref.sample(trueT);
        simd::float4 got = colAt(112, cy);
        bool bandOk = near4(got, trueWant, 0.01f);
        if (bandOk) rc = 0; else rc = 1;  // bandOk means the corruption did NOT bite (dead tooth)
        std::printf("[selftest-field-fieldtoimage] mode0 injectBug band-vs-true got=(%.3f,%.3f,%.3f) "
                    "true-want=(%.3f,%.3f,%.3f) bandMatchesTrue=%s (want NOT match under injectBug)\n",
                    got.x, got.y, got.z, trueWant.x, trueWant.y, trueWant.z, bandOk ? "yes" : "no");
      }
      tex->release();
    }
  }

  // --- MODE=1 (UseColor): SliceDepth=0.3, identity Center/Scale/Rotate. f.rgb == p.xyz (SphereSDF's
  //     local-space carry) — UNCHANGED by the mode0 injectBug corruption (source-order: the corrupted
  //     line sits AFTER the `if (OP.Mode > 0.5f) return ...;` early return). ---
  if (!injectBug) {
    FieldToImageParams p{};
    p.mode = 1.0f;
    p.sliceDepth = 0.3f;
    MTL::Texture* tex = renderFieldToImage(dev, q, sphere, useTmpl, p, gradTex, kW, kH);
    if (!tex) {
      std::printf("[selftest-field-fieldtoimage] FAIL: renderFieldToImage (mode1) returned null\n");
      rc = 1;
    } else {
      std::vector<float> buf((size_t)kW * kH * 4, 0.0f);
      tex->getBytes(buf.data(), kW * 4 * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
      const uint32_t cy = (kH - 1) / 2, px = 112;
      size_t i = ((size_t)cy * kW + px) * 4;
      simd::float4 got = simd::make_float4(buf[i], buf[i + 1], buf[i + 2], buf[i + 3]);
      simd::float4 want = simd::make_float4(pX(px), pY(cy), 0.3f, 1.0f);
      bool ok = near4(got, want, 1e-4f);
      if (!ok) rc = 1;
      std::printf("[selftest-field-fieldtoimage] mode1 UseColor got=(%.4f,%.4f,%.4f,%.4f) "
                  "want=(%.4f,%.4f,%.4f,%.4f) %s\n",
                  got.x, got.y, got.z, got.w, want.x, want.y, want.z, want.w, ok ? "OK" : "RED");
      tex->release();
    }
  }

  gradTex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-fieldtoimage] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 0;  // dead tooth -> exit 0 so --bite NO-BITE list catches it
    }
    std::printf("[selftest-field-fieldtoimage] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-fieldtoimage] PASS\n");
  return rc;
}

}  // namespace sw
