// field_render_golden — --selftest-field-render. GPU DISTANCE-VALUE golden for the shader-graph
// island: assemble a SphereSDF field, runtime-compile it, render a fullscreen pass, read back the
// R32Float distance texture, and assert each probed texel's RED == the closed-form signed distance
// d = length(p.xy) - Radius at that texel's field-space p (z=0 plane).
//
// ZONE: shell tier (lives at app/src/ root like selftests.cpp / main.cpp). This file deliberately
// crosses runtime (renderField2d, SphereSDFNode) AND platform (compileLibraryFromSource) — exactly
// what main.cpp does to wire the field source compiler. A runtime-zone selftest may NOT include
// platform (check_arch: runtime ↛ platform), so this integration golden sits at the shell tier, the
// only place allowed to bind the two zones (selftests.cpp top comment: "may include any zone").
//
// This is the Build-2 承重 tooth: it proves the WHOLE runtime-compile -> source-PSO cache ->
// fullscreen dispatch -> readback chain produces the right number, AND that the texCoord->p
// coordinate mapping matches TiXL's FieldToImageTemplate regula (the boundary-sign probe pins the
// x where d flips sign at p.x = Radius).
//
// PIXEL -> FIELD-SPACE p (backward-traced, must match field_render_template.metal):
//   texCoord at pixel (px,py) center = ((px+0.5)/W, (py+0.5)/H)         [rasterizer]
//   vertex carries texCoord = clip*(0.5,-0.5)+0.5 ; fragment does uv.y=1-uv.y; uv-=0.5; uv*=2  ->
//     p.x = (2*px + 1)/W - 1
//     p.y = 1 - (2*py + 1)/H
//   p.z = 0, p.w = 0.  Expected RED = length(float2(p.x,p.y)) - Radius.
// The golden reads each texel's EXACT p (not an assumed p=0.5) and asserts against d(p) — robust to
// the half-texel offset, and it is the coordinate map itself that is under test.
//
// injectBug: shift the cooked field by +2*Radius (= +1.0) by corrupting the template's RED-channel
// write so every probe's expected d is off by 2*Radius -> all probes RED. (Same bite tier and same
// magnitude as the original `Radius -> -Radius` flip, re-expressed at the MSL-string tier now that
// SphereSDFNode is leaf-private — the golden builds its node via the FieldOp factory.) Proves the
// golden bites (it reads cooked pixels, not a blind pass).
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
#include "runtime/field_node_registry.h"  // makeFieldNode (SphereSDFNode is now leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache (fresh source-PSO cache per run-device)

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {
namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kRadius = 0.5f;

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

// Field-space p at pixel (px,py) (see header derivation).
float pX(uint32_t px) { return (2.0f * px + 1.0f) / kW - 1.0f; }
float pY(uint32_t py) { return 1.0f - (2.0f * py + 1.0f) / kH; }

}  // namespace

int runFieldRenderSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-render] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-render] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  // Wire the field source compiler (runtime->platform leaf seam) for this process — the same lambda
  // the live app registers in main.cpp, so the source-PSO cache can compile the assembled MSL.
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();  // a stale PSO built on a released device from a prior run must not be reused

  // SphereSDF leaf via the FieldOp factory (SphereSDFNode is now leaf-private — field_ops_spheresdf
  // .cpp). The factory builds it with the SphereSDF.t3 defaults Center=(0,0,0), Radius=0.5.
  std::shared_ptr<FieldNode> sphere = makeFieldNode("SphereSDF", "golden0");
  if (!sphere) {
    std::printf("[selftest-field-render] FAIL: SphereSDF factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // injectBug re-expressed at the MSL-string tier (the node is leaf-private now, so we no longer
  // mutate sphere->radius): corrupt the template's RED-channel write so every cooked distance is
  // shifted by +1.0 = +2*Radius. This reproduces the exact magnitude of the old `Radius -> -Radius`
  // flip (the whole field shifts by 2*Radius) -> all probes go RED. The substring is the unique
  // distance write in field_render_template.metal (line: `return float4(f.w, 0.0, 0.0, 1.0);`).
  std::string useTmpl = tmpl;
  if (injectBug) {
    const std::string from = "float4(f.w, 0.0, 0.0, 1.0)";
    const std::string to = "float4(f.w + 1.0, 0.0, 0.0, 1.0)";
    size_t pos = useTmpl.find(from);
    if (pos == std::string::npos) {
      std::printf("[selftest-field-render] FAIL: injectBug could not find the distance-write site "
                  "in the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  MTL::Texture* tex = renderField2d(dev, q, sphere, useTmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-render] FAIL: renderField2d returned null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Read back the R32Float distance texture (4 bytes / texel = one float).
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);

  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  // GPU float distance tolerance. Measured worst probe on this device (Apple GPU) = 2.89e-8 — the
  // GPU's length()/sqrt agrees with the host's sqrtf to ~ULP here. The blueprint's first try was
  // 1e-5; the measured margin is ~300x inside that, so 1e-5 holds with comfortable headroom (the
  // 1e-4 documented fallback is unnecessary on this hardware). Single-precision sqrt parity, no fp16.
  const float kTol = 1e-5f;
  int rc = 0;

  // (1) DISTANCE GOLDEN at three field positions, each asserted against d(exact p) = |p.xy| - R.
  //     Probe pixels chosen to land near the blueprint's (0.5,0)/(1.0,0)/(0,0):
  //       surface  : p.x ~= 0.5  (py = center row)   -> d ~= 0
  //       outside  : p.x ~= 1.0  (right edge)        -> d ~= 0.5
  //       inside   : p ~= (0,0)  (center)            -> d ~= -0.5
  struct Probe { const char* name; uint32_t px, py; };
  // center row: py with p.y closest to 0 -> py = (kH-1)/2 = 63 -> p.y = 1-(127)/128 = 0.0078125
  const uint32_t cy = (kH - 1) / 2;     // 63
  const uint32_t cx = (kW - 1) / 2;     // 63 -> p.x = -0.0078125 (≈ center)
  // p.x ≈ 0.5 -> px = ((0.5+1)*W - 1)/2 = (1.5*128-1)/2 = 95.5 -> px=96 gives p.x=0.5078125
  const uint32_t surfacePx = 96;
  const uint32_t rightPx = kW - 1;      // 127 -> p.x = 0.9921875 (≈ 1.0, outside)
  Probe probes[] = {
      {"surface", surfacePx, cy},
      {"outside", rightPx, cy},
      {"inside", cx, cy},
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = std::sqrt(px * px + py * py) - kRadius;
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-render] probe %-8s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  // (2) BOUNDARY-SIGN tooth: along the center row, scanning +x, the distance must flip from negative
  //     (inside) to positive (outside) exactly where p.x crosses Radius (=0.5). p.y≈0 on this row,
  //     so the crossing is at |p.x| = R. Find the first px where R turns >= 0 and assert its p.x is
  //     within one texel of Radius — this pins the texCoord->p constant (a wrong scale/offset shifts
  //     the crossing). (Skipped under injectBug: the flipped field has no inside region at all.)
  if (!injectBug) {
    int crossPx = -1;
    for (uint32_t px = cx; px < kW; ++px) {
      if (sampleAt(px, cy) >= 0.0f) { crossPx = (int)px; break; }
    }
    if (crossPx < 0) {
      std::printf("[selftest-field-render] FAIL: boundary sign never flipped on center row\n");
      rc = 1;
    } else {
      float crossPxField = pX((uint32_t)crossPx);
      float prevPxField = pX((uint32_t)(crossPx - 1));
      float texelW = 2.0f / kW;  // one texel in field-space x
      // The true crossing (p.y≈0) is at p.x = Radius; crossPx is the first texel at/over it, so
      // Radius must sit in (prevPxField, crossPxField] within one texel.
      bool ok = (crossPxField >= kRadius - texelW) && (prevPxField <= kRadius + texelW);
      if (!ok) rc = 1;
      std::printf("[selftest-field-render] boundary cross at px=%d p.x=% .4f (prev % .4f) want≈%.3f "
                  "texelW=%.4f %s\n",
                  crossPx, crossPxField, prevPxField, kRadius, texelW, ok ? "OK" : "RED");
    }
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-render] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-render] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-render] PASS\n");
  return rc;
}

}  // namespace sw
