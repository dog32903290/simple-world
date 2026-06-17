// field_ops_rotatedplanesdf_golden — --selftest-field-rotatedplanesdf. GPU DISTANCE-VALUE golden for
// the RotatedPlaneSDF leaf on the shader-graph island: assemble a RotatedPlaneSDF field at its .t3
// defaults (Center=(0,0,0), Normal=(0,1,0)), runtime-compile it, render a fullscreen pass, read back
// the R32Float distance texture, and assert each probed texel's RED == the closed-form signed distance
// at that texel's field-space p (z=0 plane).
//
// ZONE: shell tier (lives at app/src/ root like selftests.cpp / main.cpp / field_render_golden.cpp).
// This file deliberately crosses runtime (renderField2d, makeFieldNode) AND platform
// (compileLibraryFromSource) — exactly what main.cpp / field_render_golden does to wire the field
// source compiler. A runtime-zone selftest may NOT include platform (check_arch: runtime ↛ platform),
// so this integration golden sits at the shell tier (the only place allowed to bind the two zones).
//
// ROTATED-PLANE DISTANCE (defaults: Center=0, Normal=(0,1,0) -> normalize=(0,1,0)):
//   d = dot(p.xyz - Center, normalize(Normal)) = dot((p.x,p.y,0), (0,1,0)) = p.y.
//   So at defaults this is the +Y half-space plane through the origin — the signed distance is exactly
//   the field-space p.y, independent of p.x. (Surface at p.y = 0; positive above, negative below.)
//
// PIXEL -> FIELD-SPACE p (backward-traced, must match field_render_template.metal, same as
// field_render_golden.cpp):
//   p.x = (2*px + 1)/W - 1 ;  p.y = 1 - (2*py + 1)/H ;  p.z = 0, p.w = 0.
// The golden reads each texel's EXACT p and asserts against d(p)=p.y — robust to the half-texel offset.
//
// PROBES (all p.x,p.y in [-1,1]):
//   p≈(0, 0.5)  -> d = 0.5   (above the plane)
//   p≈(0,-0.5)  -> d = -0.5  (below the plane)
//   p≈(0.7,0.3) -> d = 0.3   (x is irrelevant: d=p.y -> proves Normal picks Y, not the full vector)
//   boundary: scan -py (increasing p.y) on a column, find the +y crossing where d goes >= 0 at p.y≈0.
//
// injectBug: shift the cooked field by +1.0 by corrupting the template's RED-channel write (same
// technique + magnitude as field_render_golden.cpp) so every probe's expected d is off by 1.0 ->
// all probes RED, and the boundary crossing (p.y=0) is pushed off-plane so it never bites correctly.
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
#include "runtime/field_node_registry.h"  // makeFieldNode (RotatedPlaneSDFNode is leaf-private)
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

// Field-space p at pixel (px,py) (see header derivation, identical to field_render_template.metal).
float pX(uint32_t px) { return (2.0f * px + 1.0f) / kW - 1.0f; }
float pY(uint32_t py) { return 1.0f - (2.0f * py + 1.0f) / kH; }

// Closed-form rotated-plane distance at defaults (Center=0, Normal=(0,1,0)): d = p.y (x irrelevant).
float planeD(float /*px*/, float py) { return py; }

// Pick the pixel (px,py) whose field-space p is nearest a target (tx,ty); used so probes land on real
// texel centers (robust to the half-texel offset — the assert uses the texel's EXACT p, not the target).
void nearestPixel(float tx, float ty, uint32_t& outPx, uint32_t& outPy) {
  uint32_t bpx = 0, bpy = 0;
  float best = 1e30f;
  for (uint32_t py = 0; py < kH; ++py) {
    for (uint32_t px = 0; px < kW; ++px) {
      float dx = pX(px) - tx, dy = pY(py) - ty;
      float d2 = dx * dx + dy * dy;
      if (d2 < best) { best = d2; bpx = px; bpy = py; }
    }
  }
  outPx = bpx; outPy = bpy;
}

}  // namespace

int runFieldRotatedPlaneSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-rotatedplanesdf] FAIL: could not load field template "
                "(SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-rotatedplanesdf] FAIL: no Metal device\n");
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

  // RotatedPlaneSDF leaf via the FieldOp factory (RotatedPlaneSDFNode is leaf-private —
  // field_ops_rotatedplanesdf.cpp). The factory builds it with the .t3 defaults Center=(0,0,0),
  // Normal=(0,1,0) baked in the node ctor, so d = p.y — no cook wiring needed this batch.
  std::shared_ptr<FieldNode> plane = makeFieldNode("RotatedPlaneSDF", "golden0");
  if (!plane) {
    std::printf("[selftest-field-rotatedplanesdf] FAIL: RotatedPlaneSDF factory not registered\n");
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
      std::printf("[selftest-field-rotatedplanesdf] FAIL: injectBug could not find the distance-write "
                  "site in the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  MTL::Texture* tex = renderField2d(dev, q, plane, useTmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-rotatedplanesdf] FAIL: renderField2d returned null (compile/PSO "
                "failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Read back the R32Float distance texture (4 bytes / texel = one float).
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  // GPU float distance tolerance — same as field_render_golden.cpp / torussdf golden (1e-5 holds with
  // comfortable headroom; d=p.y here is a single multiply-add, even tighter than the sqrt path).
  const float kTol = 1e-5f;
  int rc = 0;

  // Value probes — nearest real texel to each field-space target; assert against the texel's EXACT p.y.
  struct Probe { const char* name; float tx, ty; };
  Probe probes[] = {
      {"above",      0.0f,  0.5f},  // d = 0.5
      {"below",      0.0f, -0.5f},  // d = -0.5
      {"x-irrelev",  0.7f,  0.3f},  // d = 0.3 (x must NOT change d -> proves Normal selects Y only)
  };
  for (const Probe& pr : probes) {
    uint32_t px, py;
    nearestPixel(pr.tx, pr.ty, px, py);
    float fpx = pX(px), fpy = pY(py);
    float expected = planeD(fpx, fpy);
    float got = sampleAt(px, py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-rotatedplanesdf] probe %-10s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, fpx, fpy, got, expected, diff, ok ? "OK" : "RED");
  }

  // BOUNDARY-SIGN tooth: along a fixed column, scanning py downward (py large -> small) increases p.y
  // from negative to positive, so the distance crosses 0 at p.y = 0 (the plane). Find the first texel
  // (scanning increasing p.y, i.e. decreasing py) where d goes >= 0 and pin that crossing within one
  // texel. A wrong texCoord->p scale/offset (or a Normal that didn't select Y) would shift it.
  // (Skipped under injectBug: the +1.0 shift removes the negative region — d = p.y + 1 >= 0 for all
  // p.y >= -1, so there is no crossing to find.)
  if (!injectBug) {
    const uint32_t col = (kW - 1) / 2;  // 63 -> p.x ≈ -0.0078125 (x is irrelevant to d=p.y anyway)
    // Scan from the bottom (py=kH-1, most negative p.y) upward (decreasing py -> increasing p.y); find
    // the first row where d turns >= 0 (the +y crossing at p.y=0).
    int crossPy = -1;
    for (int py = (int)kH - 1; py >= 0; --py) {
      if (sampleAt(col, (uint32_t)py) >= 0.0f) { crossPy = py; break; }
    }
    if (crossPy < 0) {
      std::printf("[selftest-field-rotatedplanesdf] FAIL: distance never went >= 0 on the column "
                  "(no +y crossing)\n");
      rc = 1;
    } else {
      float crossPyField = pY((uint32_t)crossPy);            // first p.y where d >= 0
      float prevPyField = pY((uint32_t)(crossPy + 1));       // the texel just below (d < 0)
      float texelH = 2.0f / kH;  // one texel in field-space y
      const float kSurface = 0.0f;  // the plane is at p.y = 0
      // crossPy is the first texel (going up) where d turned >= 0, so the true crossing (d=0 at p.y=0)
      // sits in [prevPyField, crossPyField] within one texel.
      bool ok = (crossPyField <= kSurface + texelH) && (prevPyField >= kSurface - texelH);
      if (!ok) rc = 1;
      std::printf("[selftest-field-rotatedplanesdf] +y cross at py=%d p.y=% .4f (prev % .4f) "
                  "want≈%.3f texelH=%.4f %s\n",
                  crossPy, crossPyField, prevPyField, kSurface, texelH, ok ? "OK" : "RED");
    }
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-rotatedplanesdf] FAIL: injectBug did not trip any probe (tooth has "
                  "no bite)\n");
      return 1;
    }
    std::printf("[selftest-field-rotatedplanesdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-rotatedplanesdf] PASS\n");
  return rc;
}

}  // namespace sw
