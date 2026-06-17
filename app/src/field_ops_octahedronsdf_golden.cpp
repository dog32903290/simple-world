// field_ops_octahedronsdf_golden — --selftest-field-octahedronsdf. GPU DISTANCE-VALUE golden for the
// OctahedronSDF field leaf: assemble an OctahedronSDF field, runtime-compile it, render a fullscreen
// pass, read back the R32Float distance texture, and assert each probed texel's RED == the closed-form
// signed distance fsdOctahedron(p, 0, Size, EdgeRadius) at that texel's field-space p (z=0 plane).
//
// ZONE: shell tier (lives at app/src/ root like field_render_golden.cpp / selftests.cpp / main.cpp).
// This file deliberately crosses runtime (renderField2d, makeFieldNode) AND platform
// (compileLibraryFromSource) — a runtime-zone selftest may NOT include platform (check_arch:
// runtime ↛ platform), so this integration golden sits at the shell tier (the only place allowed to
// bind the two zones; selftests.cpp top comment: "may include any zone").
//
// PIXEL -> FIELD-SPACE p (must match field_render_template.metal; identical map to field_render_golden):
//   p.x = (2*px + 1)/W - 1 ;  p.y = 1 - (2*py + 1)/H ;  p.z = 0, p.w = 0.
//   Expected RED = fsdOctahedron(float3(p.x,p.y,0), center=0, Size, EdgeRadius).
// Each probe reads its EXACT p (not an assumed coord) and asserts against the host evaluation of the
// SAME closed-form helper — robust to the half-texel offset.
//
// PROBES (Size=0.5, EdgeRadius=0.002, center=0; checked against the work-order reference values):
//   boundary p=(0.5,0,0)  -> d = -EdgeRadius          = -0.002
//   inside   p=(0,0,0)    -> d = -(1/6)*sqrt(3)-0.002 ≈ -0.2907
//   outside  p=(1,0,0)    -> d =  0.5 - EdgeRadius     =  0.498
// plus a boundary-SIGN probe: along the center row scanning +x, d must flip negative->positive (the
// octahedron has a real inside region at the center). Skipped under injectBug (the +1.0 shift removes
// the inside region entirely).
//
// injectBug: corrupt the template's RED-channel write so every cooked distance is shifted by +1.0
// (same magnitude / technique as field_render_golden) -> all probes go RED. Proves the tooth bites
// (it reads cooked pixels, not a blind pass).
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
#include "runtime/field_node_registry.h"  // makeFieldNode (OctahedronSDFNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache (fresh source-PSO cache per run-device)

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {
namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSize = 0.5f;        // OctahedronSDF.t3 default
constexpr float kEdgeRadius = 0.002f;  // OctahedronSDF.t3 default

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

// Field-space p at pixel (px,py) (see header derivation; identical to field_render_golden).
float pX(uint32_t px) { return (2.0f * px + 1.0f) / kW - 1.0f; }
float pY(uint32_t py) { return 1.0f - (2.0f * py + 1.0f) / kH; }

// Host evaluation of the SAME fsdOctahedron helper the shader emits (parity reference). center=0.
float octaSdf(float px, float py, float pz, float s, float ra) {
  float ax = std::fabs(px), ay = std::fabs(py), az = std::fabs(pz);
  float m = (ax + ay + az - s) / 3.0f;
  float ox = ax - m, oy = ay - m, oz = az - m;
  float kx = std::fmin(ox, 0.0f), ky = std::fmin(oy, 0.0f), kz = std::fmin(oz, 0.0f);
  float ksum = kx + ky + kz;
  ox = ox + ksum * 0.5f - kx * 1.5f;
  oy = oy + ksum * 0.5f - ky * 1.5f;
  oz = oz + ksum * 0.5f - kz * 1.5f;
  ox = std::fmin(std::fmax(ox, 0.0f), s);
  oy = std::fmin(std::fmax(oy, 0.0f), s);
  oz = std::fmin(std::fmax(oz, 0.0f), s);
  float dx = ax - ox, dy = ay - oy, dz = az - oz;
  float len = std::sqrt(dx * dx + dy * dy + dz * dz);
  float sgn = (m > 0.0f) ? 1.0f : (m < 0.0f ? -1.0f : 0.0f);
  return len * sgn - ra;
}

}  // namespace

int runFieldOctahedronSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-octahedronsdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-octahedronsdf] FAIL: no Metal device\n");
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

  // OctahedronSDF leaf via the FieldOp factory (OctahedronSDFNode is leaf-private). The factory builds
  // it with the .t3 defaults Center=(0,0,0), Size=0.5, EdgeRadius=0.002 — which the probes assume.
  std::shared_ptr<FieldNode> octa = makeFieldNode("OctahedronSDF", "golden0");
  if (!octa) {
    std::printf("[selftest-field-octahedronsdf] FAIL: OctahedronSDF factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // injectBug at the MSL-string tier (the node is leaf-private): corrupt the template's RED-channel
  // write so every cooked distance is shifted by +1.0 -> all probes go RED. Same site/technique as
  // field_render_golden.cpp.
  std::string useTmpl = tmpl;
  if (injectBug) {
    const std::string from = "float4(f.w, 0.0, 0.0, 1.0)";
    const std::string to = "float4(f.w + 1.0, 0.0, 0.0, 1.0)";
    size_t pos = useTmpl.find(from);
    if (pos == std::string::npos) {
      std::printf("[selftest-field-octahedronsdf] FAIL: injectBug could not find the distance-write "
                  "site in the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  MTL::Texture* tex = renderField2d(dev, q, octa, useTmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-octahedronsdf] FAIL: renderField2d returned null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Read back the R32Float distance texture (4 bytes / texel = one float).
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);

  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  // GPU float distance tolerance (same as field_render_golden: single-precision length()/sqrt parity).
  const float kTol = 1e-5f;
  int rc = 0;

  // DISTANCE GOLDEN at the three reference field positions (boundary / inside / outside). Probe pixels
  // chosen so each probed p lands near the work-order targets (0.5,0)/(0,0)/(1,0); expected is the
  // host octaSdf at the EXACT probed p (robust to the half-texel offset).
  const uint32_t cy = (kH - 1) / 2;     // 63 -> p.y = 0.0078125 (≈ center row)
  const uint32_t cx = (kW - 1) / 2;     // 63 -> p.x = -0.0078125 (≈ center)
  // p.x ≈ 0.5 -> px = ((0.5+1)*W - 1)/2 = 95.5 -> px=96 gives p.x=0.5078125
  const uint32_t boundaryPx = 96;
  const uint32_t rightPx = kW - 1;      // 127 -> p.x = 0.9921875 (≈ 1.0, outside)
  struct Probe { const char* name; uint32_t px, py; };
  Probe probes[] = {
      {"boundary", boundaryPx, cy},
      {"inside", cx, cy},
      {"outside", rightPx, cy},
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = octaSdf(px, py, 0.0f, kSize, kEdgeRadius);
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-octahedronsdf] probe %-8s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  // BOUNDARY-SIGN tooth: along the center row, scanning +x, the distance must flip from negative
  //   (inside the octahedron) to positive (outside). Pins the texCoord->p map (a wrong scale/offset
  //   shifts the crossing). Skipped under injectBug (the +1.0 shift removes the inside region).
  if (!injectBug) {
    bool sawInside = false, sawOutside = false;
    for (uint32_t px = cx; px < kW; ++px) {
      float v = sampleAt(px, cy);
      if (v < 0.0f) sawInside = true;
      if (sawInside && v >= 0.0f) { sawOutside = true; break; }
    }
    bool ok = sawInside && sawOutside;
    if (!ok) rc = 1;
    std::printf("[selftest-field-octahedronsdf] boundary-sign center row: inside=%s outside=%s %s\n",
                sawInside ? "yes" : "no", sawOutside ? "yes" : "no", ok ? "OK" : "RED");
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-octahedronsdf] FAIL: injectBug did not trip any probe (tooth has "
                  "no bite)\n");
      return 1;
    }
    std::printf("[selftest-field-octahedronsdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-octahedronsdf] PASS\n");
  return rc;
}

}  // namespace sw
