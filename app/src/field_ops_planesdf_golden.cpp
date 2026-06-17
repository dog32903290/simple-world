// field_ops_planesdf_golden — --selftest-field-planesdf. GPU DISTANCE-VALUE golden for the PlaneSDF
// field leaf: assemble a PlaneSDF field at its .t3 defaults (Center=(0,0,0), Axis=1=Y), runtime-compile
// it, render a fullscreen pass, read back the R32Float distance texture, and assert each probed texel's
// RED == the closed-form signed distance to the Y-plane through the origin: d = p.y - Center.y = p.y.
//
// ZONE: shell tier (lives at app/src/ root like selftests.cpp / main.cpp / field_render_golden.cpp).
// This file deliberately crosses runtime (renderField2d, makeFieldNode) AND platform
// (compileLibraryFromSource) — a runtime-zone selftest may NOT include platform (check_arch:
// runtime ↛ platform), so this integration golden sits at the shell tier (the only place allowed to
// bind the two zones). It mirrors field_render_golden.cpp's structure exactly.
//
// WHY PlaneSDF proves a DIFFERENT thing than SphereSDF: SphereSDF's distance uses ALL of p.xy
// (length). PlaneSDF (axis=Y) uses ONLY p.y — the x and z components are IGNORED. The probe
// p=(5,-2,3) -> d=-2.0 (NOT length-based) pins single-axis selection: a wrong swizzle (e.g. ".x")
// would read 5.0 and the probe goes RED. This is the load-bearing test of the axis CODE SELECTOR.
//
// PIXEL -> FIELD-SPACE p (backward-traced, must match field_render_template.metal — identical to
// field_render_golden.cpp):
//     p.x = (2*px + 1)/W - 1
//     p.y = 1 - (2*py + 1)/H
//   p.z = 0, p.w = 0.  Expected RED (axis=Y, Center=0) = p.y.
//
// injectBug: shift the cooked field by +1.0 by corrupting the template's RED-channel write so every
// probe's expected d is off by 1.0 -> all probes RED (same string-replace technique + magnitude as
// field_render_golden.cpp). Proves the golden bites (it reads cooked pixels, not a blind pass).
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
#include "runtime/field_node_registry.h"  // makeFieldNode (PlaneSDFNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache (fresh source-PSO cache per run-device)

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {
namespace {

constexpr uint32_t kW = 128, kH = 128;

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

// Field-space p at pixel (px,py) (see header derivation; identical to field_render_golden.cpp).
float pX(uint32_t px) { return (2.0f * px + 1.0f) / kW - 1.0f; }
float pY(uint32_t py) { return 1.0f - (2.0f * py + 1.0f) / kH; }

// Nearest pixel to a target field-space coordinate (invert pX/pY, round, clamp).
uint32_t nearestPx(float targetX) {
  float pf = (targetX + 1.0f) * kW / 2.0f - 0.5f;  // invert pX
  int v = (int)std::lround(pf);
  if (v < 0) v = 0;
  if (v > (int)kW - 1) v = (int)kW - 1;
  return (uint32_t)v;
}
uint32_t nearestPy(float targetY) {
  float pf = (1.0f - targetY) * kH / 2.0f - 0.5f;  // invert pY
  int v = (int)std::lround(pf);
  if (v < 0) v = 0;
  if (v > (int)kH - 1) v = (int)kH - 1;
  return (uint32_t)v;
}

}  // namespace

int runFieldPlaneSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-planesdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-planesdf] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  // Wire the field source compiler (runtime->platform leaf seam) for this process.
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();  // a stale PSO built on a released device from a prior run must not be reused

  // PlaneSDF leaf via the FieldOp factory (PlaneSDFNode is leaf-private — field_ops_planesdf.cpp).
  // The factory builds it with the PlaneSDF.t3 defaults Center=(0,0,0), Axis=1 (Y) baked in the ctor.
  std::shared_ptr<FieldNode> plane = makeFieldNode("PlaneSDF", "golden0");
  if (!plane) {
    std::printf("[selftest-field-planesdf] FAIL: PlaneSDF factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // injectBug: corrupt the template's RED-channel write so every cooked distance is shifted by +1.0
  // -> all probes go RED. Same unique substring + magnitude as field_render_golden.cpp.
  std::string useTmpl = tmpl;
  if (injectBug) {
    const std::string from = "float4(f.w, 0.0, 0.0, 1.0)";
    const std::string to = "float4(f.w + 1.0, 0.0, 0.0, 1.0)";
    size_t pos = useTmpl.find(from);
    if (pos == std::string::npos) {
      std::printf("[selftest-field-planesdf] FAIL: injectBug could not find the distance-write site "
                  "in the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  MTL::Texture* tex = renderField2d(dev, q, plane, useTmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-planesdf] FAIL: renderField2d returned null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Read back the R32Float distance texture (4 bytes / texel = one float).
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  const float kTol = 1e-5f;  // single-axis difference is exact in fp32; same tolerance as SphereSDF golden.
  int rc = 0;

  // DISTANCE GOLDEN at three field positions (axis=Y, Center=0 -> d = p.y). Each asserted against the
  // EXACT p of the probed texel (robust to the half-texel offset).
  //   (1) p ~= (0, 1, 0)  -> d ~= +1.0   (top, outside the +Y half-space boundary)
  //   (2) p ~= (5,-2, 3)  -> d ~= -2.0   (x/z IGNORED — pins single-axis selection; x,z clamp to grid)
  //   (3) p ~= (0, 0, 0)  -> d ~=  0.0   (boundary)
  struct Probe { const char* name; float wantX, wantY; };
  Probe probes[] = {
      {"top(+Y)", 0.0f, 1.0f},
      {"axis-select", 5.0f, -2.0f},  // wantX=5 is out of the [-1,1] viewport -> clamps to right edge;
                                     // what matters is p.y -> d, proving x is ignored.
      {"boundary", 0.0f, 0.0f},
  };
  for (const Probe& pr : probes) {
    uint32_t px = nearestPx(pr.wantX), py = nearestPy(pr.wantY);
    float p_x = pX(px), p_y = pY(py);
    float expected = p_y;  // axis=Y, Center.y=0  -> d = p.y - 0 = p.y  (x/z ignored)
    float got = sampleAt(px, py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-planesdf] probe %-12s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, p_x, p_y, got, expected, diff, ok ? "OK" : "RED");
  }

  // BOUNDARY-SIGN tooth: scanning DOWN a column (increasing py = decreasing p.y), the distance must
  // flip from positive (above the plane, p.y>0) to negative (below) exactly where p.y crosses 0.
  // (Skipped under injectBug: the +1.0-shifted field has no negative region near the top.)
  if (!injectBug) {
    const uint32_t col = (kW - 1) / 2;  // any column (x ignored for axis=Y)
    int crossPy = -1;
    for (uint32_t py = 0; py < kH; ++py) {
      if (sampleAt(col, py) < 0.0f) { crossPy = (int)py; break; }
    }
    if (crossPy < 0) {
      std::printf("[selftest-field-planesdf] FAIL: boundary sign never flipped down the column\n");
      rc = 1;
    } else {
      float crossPyField = pY((uint32_t)crossPy);       // first p.y that is < 0
      float prevPyField = pY((uint32_t)(crossPy - 1));  // last p.y that is >= 0
      float texelH = 2.0f / kH;  // one texel in field-space y
      // The true crossing is at p.y = 0; crossPy is the first texel below it, so 0 must sit in
      // [crossPyField, prevPyField] within one texel.
      bool ok = (crossPyField <= texelH) && (prevPyField >= -texelH);
      if (!ok) rc = 1;
      std::printf("[selftest-field-planesdf] boundary cross at py=%d p.y=% .4f (prev % .4f) want≈0.000 "
                  "texelH=%.4f %s\n",
                  crossPy, crossPyField, prevPyField, texelH, ok ? "OK" : "RED");
    }
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-planesdf] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-planesdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-planesdf] PASS\n");
  return rc;
}

}  // namespace sw
