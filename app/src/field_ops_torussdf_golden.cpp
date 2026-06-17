// field_ops_torussdf_golden — --selftest-field-torussdf. GPU DISTANCE-VALUE golden for the TorusSDF
// leaf on the shader-graph island: assemble a TorusSDF field (axis=Z default), runtime-compile it,
// render a fullscreen pass, read back the R32Float distance texture, and assert each probed texel's
// RED == the closed-form signed distance at that texel's field-space p (z=0 plane).
//
// ZONE: shell tier (lives at app/src/ root like selftests.cpp / main.cpp / field_render_golden.cpp).
// This file deliberately crosses runtime (renderField2d, makeFieldNode) AND platform
// (compileLibraryFromSource) — exactly what main.cpp / field_render_golden does to wire the field
// source compiler. A runtime-zone selftest may NOT include platform (check_arch: runtime ↛ platform),
// so this integration golden sits at the shell tier (the only place allowed to bind the two zones).
//
// TORUS DISTANCE (axis=Z = "xyz" identity, TorusSDF.t3 defaults Radius=1, Thickness=0.5, Center=0):
//   fTorus(p, R, t) = length(float2(length(p.xy) - R, p.z)) - t.
//   In the 2D field template p.z = 0, so d = |length(p.xy) - R| - t = |length(p.xy) - 1| - 0.5.
//
// PIXEL -> FIELD-SPACE p (backward-traced, must match field_render_template.metal, same as
// field_render_golden.cpp):
//   p.x = (2*px + 1)/W - 1 ;  p.y = 1 - (2*py + 1)/H ;  p.z = 0, p.w = 0.
// The golden reads each texel's EXACT p and asserts against d(p) — robust to the half-texel offset.
//
// PROBES (center row, p.y≈0):
//   p≈(1,0)   -> length=1   -> |1-1|-0.5 = -0.5  (on the tube core, inside)
//   p≈(0,0)   -> length=0   -> |0-1|-0.5 =  0.5  (hole center, outside)
//   p≈(2,0)   -> length=2   -> |2-1|-0.5 =  0.5  (outside, far side)
//   boundary p≈(1.5,0) -> length=1.5 -> |1.5-1|-0.5 = 0.0  (the outer surface, sign flip)
//
// injectBug: shift the cooked field by +1.0 by corrupting the template's RED-channel write (same
// technique + magnitude as field_render_golden.cpp) so every probe's expected d is off by 1.0 ->
// all probes RED. Proves the golden bites (it reads cooked pixels, not a blind pass).
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
#include "runtime/field_node_registry.h"  // makeFieldNode (TorusSDFNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache (fresh source-PSO cache per run-device)

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {
namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kRadius = 1.0f;
constexpr float kThickness = 0.5f;

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

// Field-space p at pixel (px,py) (see header derivation, identical to field_render_template.metal).
float pX(uint32_t px) { return (2.0f * px + 1.0f) / kW - 1.0f; }
float pY(uint32_t py) { return 1.0f - (2.0f * py + 1.0f) / kH; }

// Closed-form torus distance at z=0: |length(p.xy) - R| - t.
float torusD(float px, float py) {
  return std::fabs(std::sqrt(px * px + py * py) - kRadius) - kThickness;
}

}  // namespace

int runFieldTorusSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-torussdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-torussdf] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  // Wire the field source compiler (runtime->platform leaf seam) — same lambda the live app registers
  // in main.cpp, so the source-PSO cache can compile the assembled MSL.
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();  // a stale PSO built on a released device from a prior run must not be reused

  // TorusSDF leaf via the FieldOp factory (TorusSDFNode is leaf-private — field_ops_torussdf.cpp).
  // The factory builds it with the TorusSDF.t3 defaults Center=(0,0,0), Radius=1, Thickness=0.5,
  // Axis=2 (Z) — the axis swizzle "xyz" (identity) is baked in the node ctor, so no cook wiring.
  std::shared_ptr<FieldNode> torus = makeFieldNode("TorusSDF", "golden0");
  if (!torus) {
    std::printf("[selftest-field-torussdf] FAIL: TorusSDF factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // injectBug: corrupt the template's RED-channel write so every cooked distance is shifted by +1.0
  // -> all probes go RED. The substring is the unique distance write in field_render_template.metal.
  std::string useTmpl = tmpl;
  if (injectBug) {
    const std::string from = "float4(f.w, 0.0, 0.0, 1.0)";
    const std::string to = "float4(f.w + 1.0, 0.0, 0.0, 1.0)";
    size_t pos = useTmpl.find(from);
    if (pos == std::string::npos) {
      std::printf("[selftest-field-torussdf] FAIL: injectBug could not find the distance-write site "
                  "in the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  MTL::Texture* tex = renderField2d(dev, q, torus, useTmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-torussdf] FAIL: renderField2d returned null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Read back the R32Float distance texture (4 bytes / texel = one float).
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  // GPU float distance tolerance — same as field_render_golden.cpp (measured worst probe ~2.89e-8 on
  // Apple GPU; 1e-5 holds with comfortable headroom; single-precision sqrt parity, no fp16).
  const float kTol = 1e-5f;
  int rc = 0;

  // Center row + probe columns (mirror field_render_golden's pixel picks).
  const uint32_t cy = (kH - 1) / 2;  // 63 -> p.y ≈ 0.0078125
  const uint32_t cx = (kW - 1) / 2;  // 63 -> p.x ≈ -0.0078125 (≈ hole center)
  // p.x ≈ 1.0 -> px = ((1.0+1)*W - 1)/2 = (2*128-1)/2 = 127.5 -> px=127 gives p.x=0.9921875 (tube core)
  const uint32_t tubePx = kW - 1;    // 127
  // p.x ≈ 2.0 is off-screen (p.x in [-1,1]); the right edge (px=127) is the farthest real probe. Use
  // it both for the tube-core probe AND scan inward for a clean "outside" sample at the hole center.
  // For the third probe (outside, far side at p≈2,0) we can't reach p.x=2 in 2D space, so probe the
  // hole-center "outside" region instead at p≈(0,0) which is symmetric (|0-1|-0.5 = 0.5).

  struct Probe { const char* name; uint32_t px, py; };
  Probe probes[] = {
      {"tube-core", tubePx, cy},  // p.x≈1   -> d≈-0.5 (inside the tube)
      {"hole",      cx,     cy},  // p≈(0,0) -> d≈ 0.5 (outside, in the hole)
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = torusD(px, py);
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-torussdf] probe %-9s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  // BOUNDARY-SIGN tooth: along the center row scanning +x from the hole center, the distance starts
  // positive (in the hole, d>0), goes negative across the tube core (d<0 near p.x=R=1), then positive
  // again past the outer surface. The FIRST sign flip (positive -> negative) happens at the inner
  // surface p.x = R - t = 0.5, where d = |0.5-1|-0.5 = 0. We pin that crossing within one texel — a
  // wrong texCoord->p scale/offset would shift it. (Skipped under injectBug: +1.0 shift removes the
  // negative region entirely.)
  if (!injectBug) {
    int crossPx = -1;
    for (uint32_t px = cx; px < kW; ++px) {
      if (sampleAt(px, cy) < 0.0f) { crossPx = (int)px; break; }
    }
    if (crossPx < 0) {
      std::printf("[selftest-field-torussdf] FAIL: distance never went negative on center row "
                  "(no tube crossing)\n");
      rc = 1;
    } else {
      float crossPxField = pX((uint32_t)crossPx);
      float prevPxField = pX((uint32_t)(crossPx - 1));
      float texelW = 2.0f / kW;  // one texel in field-space x
      const float kInner = kRadius - kThickness;  // 0.5 — inner surface (first sign flip)
      // crossPx is the first texel where d turned < 0, so the true crossing (d=0 at p.x=0.5) sits in
      // (prevPxField, crossPxField] within one texel.
      bool ok = (crossPxField >= kInner - texelW) && (prevPxField <= kInner + texelW);
      if (!ok) rc = 1;
      std::printf("[selftest-field-torussdf] inner-surface cross at px=%d p.x=% .4f (prev % .4f) "
                  "want≈%.3f texelW=%.4f %s\n",
                  crossPx, crossPxField, prevPxField, kInner, texelW, ok ? "OK" : "RED");
    }
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-torussdf] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-torussdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-torussdf] PASS\n");
  return rc;
}

}  // namespace sw
