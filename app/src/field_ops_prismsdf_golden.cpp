// field_ops_prismsdf_golden — --selftest-field-prismsdf. GPU DISTANCE-VALUE golden for the PrismSDF
// leaf on the shader-graph island: assemble a PrismSDF field (resolved .t3 defaults -> HEX branch,
// axis=Y), runtime-compile it, render a fullscreen pass, read back the R32Float distance texture, and
// assert each probed texel's RED == the closed-form signed distance at that texel's field-space p
// (z=0 plane).
//
// ZONE: shell tier (lives at app/src/ root like selftests.cpp / main.cpp / field_render_golden.cpp).
// This file deliberately crosses runtime (renderField2d, makeFieldNode) AND platform
// (compileLibraryFromSource) — exactly what main.cpp / field_render_golden does to wire the field
// source compiler. A runtime-zone selftest may NOT include platform (check_arch: runtime ↛ platform),
// so this integration golden sits at the shell tier (the only place allowed to bind the two zones).
//
// PRISM DISTANCE (resolved .t3 defaults: Sides=1=_6 -> HEX, Axis=1=Y -> a="xzy", Radius=1, Length=1,
// EdgeRadius=0.05, Center=0). The call passes p{c}.xzy - Center.xzy and Radius*0.5, Length*0.5:
//   helper p = (field.x, field.z, field.y) ; field.z = 0 in the 2D template -> helper p = (px, 0, py).
//   r = 0.5 (Radius*0.5), l = 0.5 (Length*0.5), round = 0.05 (EdgeRadius).
//   fHexPrism(p, r, l, round) with k = (-0.8660254, 0.5, 0.57735):
//     p = abs(p);  p.xy -= 2*min(dot(k.xy,p.xy),0)*k.xy;
//     d = float2( length(p.xy - float2(clamp(p.x, -k.z*r, k.z*r), r)) * sign(p.y - r),  p.z - l );
//     return min(max(d.x,d.y),0) + length(max(d,0)) - round;
//
// PIXEL -> FIELD-SPACE p (backward-traced, must match field_render_template.metal, same as
// field_render_golden.cpp):  p.x = (2*px+1)/W - 1 ;  p.y = 1 - (2*py+1)/H ;  p.z = 0, p.w = 0.
// The golden reads each texel's EXACT p and asserts against the CPU-evaluated fHexPrism at that p.
//
// VALUE PROBES (all in p.x,p.y in [-1,1]):
//   center  p≈(0,0)  -> helper p=(0,0,0) -> d.x=-0.5,d.y=-0.5 -> -0.5 + 0 - 0.05 = -0.55 (inside)
//   top     p≈(0,1)  -> helper p=(0,0,1) -> d.x=-0.5,d.y= 0.5 ->  0   + 0.5 - 0.05 = 0.45 (above cap)
// BOUNDARY-SIGN tooth: scanning +y from center, d starts negative (inside) and flips positive crossing
//   the length cap at field p.y ≈ l + round = 0.55 (p.z-l dominates once outside). Pins texCoord->p.
//
// injectBug: shift the cooked field by +1.0 by corrupting the template's RED-channel write (same
// technique + magnitude as field_render_golden.cpp) so every probe's expected d is off by 1.0 -> all
// probes RED. (Boundary scan skipped under injectBug: +1.0 removes the inside region entirely.)
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
#include "runtime/field_node_registry.h"  // makeFieldNode (PrismSDFNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache (fresh source-PSO cache per run-device)

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {
namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kR = 0.5f;      // Radius * 0.5
constexpr float kL = 0.5f;      // Length * 0.5
constexpr float kRound = 0.05f; // EdgeRadius

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

// Closed-form fHexPrism at z=0 with axis="xzy" (helper p = (field.x, 0, field.y)), r=0.5,l=0.5,round=0.05.
// Mirrors the helper body byte-for-byte (CPU side) so the GPU value is checked against TiXL's formula.
float hexPrismD(float fx, float fy) {
  // helper input: p = field.xzy = (fx, fz=0, fy)
  float px = std::fabs(fx);
  float py = std::fabs(0.0f);
  float pz = std::fabs(fy);
  const float kx = -0.8660254f, ky = 0.5f, kz = 0.57735f;
  // p.xy -= 2*min(dot(k.xy, p.xy), 0)*k.xy;
  float dotk = kx * px + ky * py;
  float m = std::fmin(dotk, 0.0f);
  px -= 2.0f * m * kx;
  py -= 2.0f * m * ky;
  // d.x = length(p.xy - float2(clamp(p.x, -kz*r, kz*r), r)) * sign(p.y - r);
  float clampLo = -kz * kR, clampHi = kz * kR;
  float cx = std::fmin(std::fmax(px, clampLo), clampHi);
  float ex = px - cx;
  float ey = py - kR;
  float len = std::sqrt(ex * ex + ey * ey);
  float sgn = (py - kR) > 0.0f ? 1.0f : ((py - kR) < 0.0f ? -1.0f : 0.0f);
  float dx = len * sgn;
  float dy = pz - kL;
  // return min(max(d.x,d.y),0) + length(max(d,0)) - round;
  float inner = std::fmin(std::fmax(dx, dy), 0.0f);
  float ox = std::fmax(dx, 0.0f), oy = std::fmax(dy, 0.0f);
  float outer = std::sqrt(ox * ox + oy * oy);
  return inner + outer - kRound;
}

}  // namespace

int runFieldPrismSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-prismsdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-prismsdf] FAIL: no Metal device\n");
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

  // PrismSDF leaf via the FieldOp factory (PrismSDFNode is leaf-private — field_ops_prismsdf.cpp).
  // The factory builds it with the PrismSDF.t3 defaults Center=(0,0,0), Radius=1, Length=1,
  // EdgeRadius=0.05, Sides=_6 (-> hex), Axis=Y (-> swizzle "xzy") — all baked in the node ctor, so no
  // cook wiring is needed.
  std::shared_ptr<FieldNode> prism = makeFieldNode("PrismSDF", "golden0");
  if (!prism) {
    std::printf("[selftest-field-prismsdf] FAIL: PrismSDF factory not registered\n");
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
      std::printf("[selftest-field-prismsdf] FAIL: injectBug could not find the distance-write site "
                  "in the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  MTL::Texture* tex = renderField2d(dev, q, prism, useTmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-prismsdf] FAIL: renderField2d returned null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Read back the R32Float distance texture (4 bytes / texel = one float).
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  // GPU float distance tolerance — same as field_render_golden.cpp (single-precision sqrt parity).
  const float kTol = 1e-5f;
  int rc = 0;

  // Probe pixels: center (≈ field (0,0)) and a top-column pixel (≈ field (0,1), above the length cap).
  const uint32_t cx = (kW - 1) / 2;  // 63 -> p.x ≈ -0.0078125 (≈ center x)
  const uint32_t cy = (kH - 1) / 2;  // 63 -> p.y ≈  0.0078125 (≈ center y)
  const uint32_t topPy = 0;          // py=0 -> p.y = 0.9921875 (≈ 1, above the cap)

  struct Probe { const char* name; uint32_t px, py; };
  Probe probes[] = {
      {"center", cx, cy},     // helper p≈(0,0,0) -> d≈-0.55 (inside)
      {"top",    cx, topPy},  // helper p≈(0,0,1) -> d≈ 0.45 (above the length cap)
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = hexPrismD(px, py);
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-prismsdf] probe %-7s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  // BOUNDARY-SIGN tooth: scanning -py (= +field.y) from the center row, the distance starts negative
  // (inside) and flips positive crossing the length cap at field p.y ≈ l + round = 0.55. We find the
  // first row where d turns >= 0 and assert its field p.y is within one texel of 0.55 — a wrong
  // texCoord->p scale/offset would shift it. (Skipped under injectBug: +1.0 removes the inside region.)
  if (!injectBug) {
    const float kCap = kL + kRound;  // 0.55 — the +y outer surface (length cap + rounding)
    int crossPy = -1;
    // py decreasing from center -> field.y increasing; scan toward the top.
    for (int py = (int)cy; py >= 0; --py) {
      if (sampleAt(cx, (uint32_t)py) >= 0.0f) { crossPy = py; break; }
    }
    if (crossPy < 0) {
      std::printf("[selftest-field-prismsdf] FAIL: distance never went non-negative scanning +y "
                  "(no length-cap crossing)\n");
      rc = 1;
    } else {
      float crossPyField = pY((uint32_t)crossPy);
      float prevPyField = pY((uint32_t)(crossPy + 1));  // the texel just below (smaller field.y)
      float texelH = 2.0f / kH;  // one texel in field-space y
      // crossPy is the first texel (moving up) where d turned >= 0, so the true crossing (d=0 at
      // field.y=0.55) sits in [prevPyField, crossPyField] within one texel.
      bool ok = (crossPyField >= kCap - texelH) && (prevPyField <= kCap + texelH);
      if (!ok) rc = 1;
      std::printf("[selftest-field-prismsdf] cap cross at py=%d p.y=% .4f (prev % .4f) want≈%.3f "
                  "texelH=%.4f %s\n",
                  crossPy, crossPyField, prevPyField, kCap, texelH, ok ? "OK" : "RED");
    }
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-prismsdf] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-prismsdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-prismsdf] PASS\n");
  return rc;
}

}  // namespace sw
