// field_ops_cylindersdf_golden — --selftest-field-cylindersdf. GPU DISTANCE-VALUE golden for the
// CylinderSDF field leaf: assemble a CylinderSDF field at its .t3 defaults (Center=(0,0,0),
// Radius=0.5, Height=1.0, Rounding=0.05, Axis=1 Y), runtime-compile it, render a fullscreen pass, read
// back the R32Float distance texture, and assert each probed texel's RED == the closed-form rounded-
// cylinder signed distance at that texel's field-space p (z=0 plane).
//
// ZONE: shell tier (lives at app/src/ root like field_render_golden.cpp / selftests.cpp / main.cpp).
// This file deliberately crosses runtime (renderField2d, makeFieldNode) AND platform
// (compileLibraryFromSource) — exactly what field_render_golden.cpp does. A runtime-zone selftest may
// NOT include platform (check_arch: runtime ↛ platform), so this integration golden sits at the shell
// tier (selftests.cpp top comment: "may include any zone").
//
// FORMULA (Axis=Y default; ra = Radius*0.5 = 0.25, rb = Rounding = 0.05, h = Height*0.5 = 0.5; the
//   fRoundedCyl helper from CylinderSDF.cs, swizzle "xyz" so p.xz/p.y are the literal axes):
//     radial = length(p.xz)              // p.z = 0 on the render plane -> radial = |p.x|
//     d.x = radial - 2*ra + rb = radial - 0.45
//     d.y = abs(p.y) - h     = abs(p.y) - 0.5
//     dist = min(max(d.x,d.y),0) + length(max(float2(d.x,d.y),0)) - rb     // -0.05
//   Probes (verified by hand against the work order):
//     p=(0,0,0)     -> d = -0.5   (interior)
//     p=(0,1,0)     -> d =  0.45  (above the cap, outside)
//     p=(1,0,0)     -> d =  0.5   (outside the radial wall)
//     p=(0,0.55,0)  -> d =  0.0   (top cap surface at y = h + rb = 0.55)  [boundary-sign probe]
//
// The golden reads each texel's EXACT p (not an assumed value) via the same pX/pY map as
// field_render_golden.cpp and asserts dist(p) within kTol — robust to the half-texel offset, and it is
// the coordinate map + the assembled MSL that are under test.
//
// injectBug: shift the cooked field by +1.0 by corrupting the template's RED-channel write (same +1.0
// technique as field_render_golden.cpp's `float4(f.w,...)` -> `float4(f.w + 1.0,...)`), so every
// probe's RED is off by 1.0 -> all probes RED. Proves the golden bites cooked pixels.
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
#include "runtime/field_node_registry.h"  // makeFieldNode (CylinderSDFNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache (fresh source-PSO cache per run-device)

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {
namespace {

constexpr uint32_t kW = 128, kH = 128;

// CylinderSDF.t3 defaults baked into the host expectation (Axis=Y).
constexpr float kRadius = 0.5f;    // -> ra = Radius*0.5 = 0.25
constexpr float kHeight = 1.0f;    // -> h  = Height*0.5 = 0.5
constexpr float kRounding = 0.05f; // -> rb

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

// Field-space p at pixel (px,py) — identical map to field_render_template.metal / field_render_golden.
float pX(uint32_t px) { return (2.0f * px + 1.0f) / kW - 1.0f; }
float pY(uint32_t py) { return 1.0f - (2.0f * py + 1.0f) / kH; }

// Host reference: rounded-cylinder signed distance (fRoundedCyl, Axis=Y) at field-space p (z=0).
float cylinderDist(float px, float py) {
  const float ra = kRadius * 0.5f;  // 0.25
  const float rb = kRounding;       // 0.05
  const float h = kHeight * 0.5f;   // 0.5
  const float pz = 0.0f;            // render plane
  const float radial = std::sqrt(px * px + pz * pz);  // length(p.xz)
  const float dx = radial - 2.0f * ra + rb;           // VERBATIM C precedence: (radial - 2*ra) + rb
  const float dy = std::fabs(py) - h;
  const float mx = dx > 0.0f ? dx : 0.0f;             // max(d.x,0)
  const float my = dy > 0.0f ? dy : 0.0f;             // max(d.y,0)
  const float outside = std::sqrt(mx * mx + my * my); // length(max(d,0))
  const float inner = (dx > dy ? dx : dy);            // max(d.x,d.y)
  const float innerClamped = inner < 0.0f ? inner : 0.0f;  // min(.,0)
  return innerClamped + outside - rb;
}

}  // namespace

int runFieldCylinderSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-cylindersdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-cylindersdf] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  // Wire the field source compiler (runtime->platform leaf seam) for this process — same lambda the
  // live app registers in main.cpp, so the source-PSO cache can compile the assembled MSL.
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();  // a stale PSO built on a released device from a prior run must not be reused

  // CylinderSDF leaf via the FieldOp factory (CylinderSDFNode is leaf-private). The factory builds it
  // with the CylinderSDF.t3 defaults (Center=0, Radius=0.5, Height=1, Rounding=0.05, Axis=1 Y) — Axis
  // is baked in the ctor, so no cook wiring is needed for this golden.
  std::shared_ptr<FieldNode> cyl = makeFieldNode("CylinderSDF", "golden0");
  if (!cyl) {
    std::printf("[selftest-field-cylindersdf] FAIL: CylinderSDF factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // injectBug at the MSL-string tier (node is leaf-private): corrupt the template's RED-channel write so
  // every cooked distance is shifted by +1.0 -> all probes RED. The substring is the unique distance
  // write in field_render_template.metal.
  std::string useTmpl = tmpl;
  if (injectBug) {
    const std::string from = "float4(f.w, 0.0, 0.0, 1.0)";
    const std::string to = "float4(f.w + 1.0, 0.0, 0.0, 1.0)";
    size_t pos = useTmpl.find(from);
    if (pos == std::string::npos) {
      std::printf("[selftest-field-cylindersdf] FAIL: injectBug could not find the distance-write site "
                  "in the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  MTL::Texture* tex = renderField2d(dev, q, cyl, useTmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-cylindersdf] FAIL: renderField2d returned null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Read back the R32Float distance texture (4 bytes / texel = one float).
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  // Pick the pixel whose field-space center is nearest a target (fx,fy). Inverse of pX/pY.
  auto pixForField = [&](float fx, float fy) -> std::pair<uint32_t, uint32_t> {
    // fx = (2*px+1)/W - 1  -> px = ((fx+1)*W - 1)/2 ; fy = 1-(2*py+1)/H -> py = ((1-fy)*H - 1)/2
    long px = std::lround(((fx + 1.0f) * kW - 1.0f) / 2.0f);
    long py = std::lround(((1.0f - fy) * kH - 1.0f) / 2.0f);
    if (px < 0) px = 0; if (px >= (long)kW) px = kW - 1;
    if (py < 0) py = 0; if (py >= (long)kH) py = kH - 1;
    return {(uint32_t)px, (uint32_t)py};
  };

  // GPU float distance tolerance — same single-precision sqrt parity as field_render_golden.cpp.
  const float kTol = 1e-5f;
  int rc = 0;

  // DISTANCE GOLDEN at the four work-order probes, each asserted against the host formula at the
  // probed texel's EXACT field-space p (not the ideal target) — robust to the half-texel snap.
  struct Probe { const char* name; float fx, fy; };
  Probe probes[] = {
      {"interior",  0.0f, 0.0f},   // d = -0.5
      {"above-cap", 0.0f, 1.0f},   // d =  0.45
      {"radial-out",1.0f, 0.0f},   // d =  0.5
      {"top-cap",   0.0f, 0.55f},  // d =  0.0  (boundary)
  };
  for (const Probe& pr : probes) {
    auto [px, py] = pixForField(pr.fx, pr.fy);
    float fpx = pX(px), fpy = pY(py);
    float expected = cylinderDist(fpx, fpy);
    float got = sampleAt(px, py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-cylindersdf] probe %-10s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, fpx, fpy, got, expected, diff, ok ? "OK" : "RED");
  }

  // BOUNDARY-SIGN tooth: along the center column (p.x≈0), scanning +y from the center, the distance
  // must flip from negative (inside) to positive (outside) where p.y crosses the top cap (h+rb=0.55).
  // (Skipped under injectBug: the +1.0-shifted field has no inside region.)
  if (!injectBug) {
    auto [colPx, midPy] = pixForField(0.0f, 0.0f);
    int crossPy = -1;
    for (int py = (int)midPy; py >= 0; --py) {  // scanning toward +y (py decreases as field y rises)
      if (sampleAt(colPx, (uint32_t)py) >= 0.0f) { crossPy = py; break; }
    }
    if (crossPy < 0) {
      std::printf("[selftest-field-cylindersdf] FAIL: boundary sign never flipped on center column\n");
      rc = 1;
    } else {
      float crossField = pY((uint32_t)crossPy);
      float prevField = pY((uint32_t)(crossPy + 1));  // one texel lower in field y (still inside)
      float texelH = 2.0f / kH;
      const float capY = kHeight * 0.5f + kRounding;  // 0.55
      bool ok = (crossField >= capY - texelH) && (prevField <= capY + texelH);
      if (!ok) rc = 1;
      std::printf("[selftest-field-cylindersdf] boundary cross at py=%d p.y=% .4f (prev % .4f) "
                  "want≈%.3f texelH=%.4f %s\n",
                  crossPy, crossField, prevField, capY, texelH, ok ? "OK" : "RED");
    }
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-cylindersdf] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-cylindersdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-cylindersdf] PASS\n");
  return rc;
}

}  // namespace sw
