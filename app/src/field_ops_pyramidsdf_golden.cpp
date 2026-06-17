// field_ops_pyramidsdf_golden — --selftest-field-pyramidsdf. GPU DISTANCE-VALUE golden for the
// PyramidSDF leaf on the shader-graph island: assemble a PyramidSDF field (axis=Y default), runtime-
// compile it, render a fullscreen pass, read back the R32Float distance texture, and assert each
// probed texel's RED == the closed-form signed distance at that texel's field-space p (z=0 plane).
//
// ZONE: shell tier (lives at app/src/ root like selftests.cpp / main.cpp / field_render_golden.cpp /
// field_ops_torussdf_golden.cpp). This file deliberately crosses runtime (renderField2d, makeFieldNode)
// AND platform (compileLibraryFromSource) — exactly what main.cpp / field_render_golden does to wire
// the field source compiler. A runtime-zone selftest may NOT include platform (check_arch:
// runtime ↛ platform), so this integration golden sits at the shell tier.
//
// PYRAMID DISTANCE (axis=Y = "xyz" identity; PyramidSDF.t3 defaults Scale=(1,1,1), UniformScale=1,
// Rounding=0.05, Center=(0,0,0)). The shader passes halfWidth=Scale.x*US=1, halfDepth=Scale.z*US=1,
// halfHeight=Scale.y*US=1, ra=Rounding=0.05 into fPyramid (the verbatim TheTurk Pyramid SDF). The 2D
// field template fixes p.z = 0. We compute the exact closed-form distance on the CPU (mirror of the
// fPyramid body) at each texel's p and assert byte-parity vs the GPU readback.
//
// PIXEL -> FIELD-SPACE p (backward-traced, must match field_render_template.metal, same as
// field_render_golden.cpp / field_ops_torussdf_golden.cpp):
//   p.x = (2*px + 1)/W - 1 ;  p.y = 1 - (2*py + 1)/H ;  p.z = 0, p.w = 0.
//
// PROBES — base-plane row (field p.y ≈ -1, where the helper does p.y += halfHeight(1) -> y=0, the
// pyramid base footprint, a clean d ≈ -Rounding plateau across |p.x| < halfWidth):
//   p≈(0,-1)   -> base center, d1=(0,..) -> d=0 -> sign +(>=0) -> 0 - 0.05 = -0.05 (inside base)
//   p≈(0.5,-1) -> inside base footprint              -> 0 - 0.05 = -0.05
// Both are GOLDEN DISCIPLINE-safe: p.x,p.y in [-1,1], on a flat d-plateau (no fwidth-sensitive
// half-decay pixel), value asserted by the same closed-form the shader runs.
//
// ★ GOLDEN DISCIPLINE (value probes only, no boundary-sign tooth): the base-row sign crossing lands at
// p.x = halfWidth + Rounding = 1.05, which is OFF-SCREEN for a 128-px field (max on-screen p.x =
// pX(127) = 0.9921875 < 1.05). A boundary-scan loop would never find the crossing and would fail on
// correct code. We use EXACT-VALUE probes only (base-center and base-mid, both inside the footprint at
// d ≈ -0.05): a wrong texCoord→p map, wrong param, or wrong sign shifts all of them.
//
// injectBug: shift the cooked field by +1.0 by corrupting the template's RED-channel write (same
// technique + magnitude as field_render_golden.cpp / torussdf_golden) so every probe's expected d is
// off by 1.0 -> all probes RED. Proves the golden bites (it reads cooked pixels, not a blind pass).
#include "runtime/field_render.h"

#include <algorithm>
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
#include "runtime/field_node_registry.h"  // makeFieldNode (PyramidSDFNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache (fresh source-PSO cache per run-device)

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {
namespace {

constexpr uint32_t kW = 128, kH = 128;
// PyramidSDF.t3 defaults baked by the node ctor (axis=Y -> "xyz" identity).
constexpr float kHalfWidth = 1.0f;   // Scale.x * UniformScale
constexpr float kHalfDepth = 1.0f;   // Scale.z * UniformScale
constexpr float kHalfHeight = 1.0f;  // Scale.y * UniformScale
constexpr float kRounding = 0.05f;   // ra

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

// CPU mirror of fPyramid (the verbatim TheTurk Pyramid SDF in PyramidSDF.cs) at field point (px,py),
// p.z = 0, center = 0, axis = Y -> identity swizzle. Byte-for-byte the same arithmetic the shader runs.
float pyramidD(float px, float py) {
  // p = (px, py, 0); p -= center(0); p.y += halfHeight; p.xz = abs(p.xz)
  float pxv = std::fabs(px);
  float pyv = py + kHalfHeight;
  float pzv = std::fabs(0.0f);
  // d1
  float d1x = std::max(pxv - kHalfWidth, 0.0f), d1y = pyv, d1z = std::max(pzv - kHalfDepth, 0.0f);
  float dot_d1 = d1x * d1x + d1y * d1y + d1z * d1z;
  // n1 = (0, halfDepth, 2*halfHeight); k1 = dot(n1,n1)
  float n1x = 0.0f, n1y = kHalfDepth, n1z = 2.0f * kHalfHeight;
  float k1 = n1x * n1x + n1y * n1y + n1z * n1z;
  // q = p - (halfWidth, 0, halfDepth)
  float qx = pxv - kHalfWidth, qy = pyv - 0.0f, qz = pzv - kHalfDepth;
  float h1 = (qx * n1x + qy * n1y + qz * n1z) / k1;
  // n2 = (k1, 2*halfHeight*halfWidth, -halfDepth*halfWidth)
  float n2x = k1, n2y = 2.0f * kHalfHeight * kHalfWidth, n2z = -kHalfDepth * kHalfWidth;
  float dot_n2 = n2x * n2x + n2y * n2y + n2z * n2z;
  float m1 = (qx * n2x + qy * n2y + qz * n2z) / dot_n2;
  // d2 = p - clamp(p - n1*h1 - n2*max(m1,0), 0, (halfWidth, 2*halfHeight, halfDepth))
  float m1c = std::max(m1, 0.0f);
  float c2x = pxv - n1x * h1 - n2x * m1c;
  float c2y = pyv - n1y * h1 - n2y * m1c;
  float c2z = pzv - n1z * h1 - n2z * m1c;
  c2x = std::min(std::max(c2x, 0.0f), kHalfWidth);
  c2y = std::min(std::max(c2y, 0.0f), 2.0f * kHalfHeight);
  c2z = std::min(std::max(c2z, 0.0f), kHalfDepth);
  float d2x = pxv - c2x, d2y = pyv - c2y, d2z = pzv - c2z;
  float dot_d2 = d2x * d2x + d2y * d2y + d2z * d2z;
  // n3 = (2*halfHeight, halfWidth, 0); k2 = dot(n3,n3)
  float n3x = 2.0f * kHalfHeight, n3y = kHalfWidth, n3z = 0.0f;
  float k2 = n3x * n3x + n3y * n3y + n3z * n3z;
  float h2 = (qx * n3x + qy * n3y + qz * n3z) / k2;
  // n4 = (-halfWidth*halfDepth, 2*halfHeight*halfDepth, k2)
  float n4x = -kHalfWidth * kHalfDepth, n4y = 2.0f * kHalfHeight * kHalfDepth, n4z = k2;
  float dot_n4 = n4x * n4x + n4y * n4y + n4z * n4z;
  float m2 = (qx * n4x + qy * n4y + qz * n4z) / dot_n4;
  float m2c = std::max(m2, 0.0f);
  float c3x = pxv - n3x * h2 - n4x * m2c;
  float c3y = pyv - n3y * h2 - n4y * m2c;
  float c3z = pzv - n3z * h2 - n4z * m2c;
  c3x = std::min(std::max(c3x, 0.0f), kHalfWidth);
  c3y = std::min(std::max(c3y, 0.0f), 2.0f * kHalfHeight);
  c3z = std::min(std::max(c3z, 0.0f), kHalfDepth);
  float d3x = pxv - c3x, d3y = pyv - c3y, d3z = pzv - c3z;
  float dot_d3 = d3x * d3x + d3y * d3y + d3z * d3z;
  float d = std::sqrt(std::min(std::min(dot_d1, dot_d2), dot_d3));
  float sign = (std::max(std::max(h1, h2), -pyv) < 0.0f) ? -1.0f : 1.0f;
  return sign * d - kRounding;
}

}  // namespace

int runFieldPyramidSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-pyramidsdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-pyramidsdf] FAIL: no Metal device\n");
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

  // PyramidSDF leaf via the FieldOp factory (PyramidSDFNode is leaf-private — field_ops_pyramidsdf.cpp).
  // The factory builds it with the PyramidSDF.t3 defaults Center=(0,0,0), Scale=(1,1,1), UniformScale=1,
  // Rounding=0.05, Axis=1 (Y) — the axis swizzle "xyz" (identity) is baked in the node ctor, no cook.
  std::shared_ptr<FieldNode> pyramid = makeFieldNode("PyramidSDF", "golden0");
  if (!pyramid) {
    std::printf("[selftest-field-pyramidsdf] FAIL: PyramidSDF factory not registered\n");
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
      std::printf("[selftest-field-pyramidsdf] FAIL: injectBug could not find the distance-write site "
                  "in the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  MTL::Texture* tex = renderField2d(dev, q, pyramid, useTmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-pyramidsdf] FAIL: renderField2d returned null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Read back the R32Float distance texture (4 bytes / texel = one float).
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  // GPU float distance tolerance — same as field_render_golden.cpp / torussdf_golden (Apple GPU
  // single-precision sqrt parity; 1e-5 holds with comfortable headroom).
  const float kTol = 1e-5f;
  int rc = 0;

  // Base-plane row: pick the py whose p.y is closest to -1 (the pyramid base, where the helper's
  // p.y += halfHeight maps to y=0 and the footprint is a flat d ≈ -Rounding plateau). py=127 ->
  // p.y = 1 - 255/128 = -0.9921875 (the nearest real row to -1).
  const uint32_t baseRow = kH - 1;  // 127 -> p.y ≈ -0.9921875
  // p.x ≈ 0 (base center): px=63 -> p.x = -0.0078125. p.x ≈ 0.5: choose px nearest 0.5.
  const uint32_t centerPx = (kW - 1) / 2;  // 63
  // px nearest p.x=0.5: (0.5+1)*W = 192, /2 = 96; px=96 -> p.x = (193/128)-1 = 0.5078125
  const uint32_t halfPx = 96;

  struct Probe { const char* name; uint32_t px, py; };
  Probe probes[] = {
      {"base-center", centerPx, baseRow},  // p≈(0,-1)   -> d≈-0.05 (inside base footprint)
      {"base-mid",    halfPx,   baseRow},  // p≈(0.5,-1) -> d≈-0.05 (inside base footprint)
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = pyramidD(px, py);
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-pyramidsdf] probe %-11s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-pyramidsdf] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-pyramidsdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-pyramidsdf] PASS\n");
  return rc;
}

}  // namespace sw
