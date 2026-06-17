// field_ops_capsulelinesdf_golden — --selftest-field-capsulelinesdf. GPU DISTANCE-VALUE golden for the
// CapsuleLineSDF field leaf (sibling of field_render_golden.cpp, which proves SphereSDF). It assembles
// a CapsuleLineSDF field via the FieldOp factory, runtime-compiles the assembled MSL, renders a
// fullscreen pass, reads back the R32Float distance texture, and asserts each probed texel's RED ==
// the closed-form capsule distance fCapsule(p - Center, Start, End, Thickness) at that texel's exact
// field-space p (z=0 plane).
//
// Why a SEPARATE golden from field_render: CapsuleLineSDF exercises the GLOBAL-helper path
// (addGlobals -> /*{GLOBALS}*/ hook) that SphereSDF's inline leaf never touched, AND padForVec3's
// pad-insertion path (three float3 params force two interior pad floats). This golden is the GPU
// witness that BOTH paths produce the right number end-to-end (assemble -> source-PSO -> draw ->
// readback), not just the codegen string.
//
// ZONE: shell tier (lives at app/src/ root like field_render_golden.cpp / selftests.cpp / main.cpp).
// It deliberately crosses runtime (renderField2d, makeFieldNode) AND platform
// (compileLibraryFromSource) — a runtime-zone selftest may NOT include platform (check_arch:
// runtime ↛ platform), so this integration golden sits at the shell tier (the only place allowed to
// bind the two zones).
//
// PIXEL -> FIELD-SPACE p (identical to field_render_golden.cpp / field_render_template.metal):
//     p.x = (2*px + 1)/W - 1 ;  p.y = 1 - (2*py + 1)/H ;  p.z = 0, p.w = 0.
// Each probe picks the px/py whose p is closest to the target field point, then asserts the readback
// against the closed-form distance at that EXACT p (robust to the half-texel offset).
//
// PROBES (a=(0,0.5,0), b=(0,-0.5,0), r=0.125, Center=0 — the .t3 defaults):
//     inside   target p=(0,0,0)     -> ideal d = -0.125  (on the segment, distance to line = 0)
//     boundary target p=(0.125,0,0) -> ideal d =  0.0    (exactly Thickness from the segment)
//     above    target p=(0,1,0)     -> ideal d =  0.375  (past the b->a endpoint a=(0,0.5): 0.5-0.125)
// (Each assertion uses the closed-form at the chosen texel's exact p, so the printed "ideal" above is
// the field-space intent; the asserted expected value is fCapsule(p_texel).)
//
// injectBug: corrupt the template's RED-channel write so every cooked distance is shifted by +1.0 ->
// all probes go RED (same technique/tier as field_render_golden.cpp). Proves the tooth bites.
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
#include "runtime/field_node_registry.h"  // makeFieldNode (CapsuleLineSDFNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache (fresh source-PSO cache per run-device)

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {
namespace {

constexpr uint32_t kW = 128, kH = 128;

// CapsuleLineSDF.t3 defaults (Center=0, Start=(0,0.5,0), End=(0,-0.5,0), Thickness=0.125).
constexpr float kStartX = 0.0f, kStartY = 0.5f, kStartZ = 0.0f;
constexpr float kEndX = 0.0f, kEndY = -0.5f, kEndZ = 0.0f;
constexpr float kThickness = 0.125f;

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

// Field-space p at pixel (px,py) (must match field_render_template.metal).
float pX(uint32_t px) { return (2.0f * px + 1.0f) / kW - 1.0f; }
float pY(uint32_t py) { return 1.0f - (2.0f * py + 1.0f) / kH; }

// Closed-form fCapsule (host mirror of the MSL global helper / CapsuleLineSDF.cs), z=0 plane.
// p here is the field-space point AFTER subtracting Center (Center=0, so p == sample point).
float fCapsuleHost(float px, float py, float pz) {
  // pa = p - a ; ba = b - a ; h = clamp(dot(pa,ba)/dot(ba,ba),0,1) ; length(pa - ba*h) - r
  const float ax = kStartX, ay = kStartY, az = kStartZ;
  const float bx = kEndX, by = kEndY, bz = kEndZ;
  const float pax = px - ax, pay = py - ay, paz = pz - az;
  const float bax = bx - ax, bay = by - ay, baz = bz - az;
  float h = (pax * bax + pay * bay + paz * baz) / (bax * bax + bay * bay + baz * baz);
  h = h < 0.0f ? 0.0f : (h > 1.0f ? 1.0f : h);
  const float dx = pax - bax * h, dy = pay - bay * h, dz = paz - baz * h;
  return std::sqrt(dx * dx + dy * dy + dz * dz) - kThickness;
}

// Pick the pixel (px,py) whose field-space p is closest to target (tx,ty) on the z=0 plane.
void nearestPixel(float tx, float ty, uint32_t& outPx, uint32_t& outPy) {
  // Inverse of pX/pY (round to nearest texel), clamped to the grid.
  float fx = ((tx + 1.0f) * kW - 1.0f) * 0.5f;
  float fy = ((1.0f - ty) * kH - 1.0f) * 0.5f;
  long ix = std::lround(fx), iy = std::lround(fy);
  if (ix < 0) ix = 0; if (ix > (long)kW - 1) ix = kW - 1;
  if (iy < 0) iy = 0; if (iy > (long)kH - 1) iy = kH - 1;
  outPx = (uint32_t)ix;
  outPy = (uint32_t)iy;
}

}  // namespace

int runFieldCapsuleLineSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-capsulelinesdf] FAIL: could not load field template "
                "(SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-capsulelinesdf] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  // Wire the field source compiler (runtime->platform leaf seam) — same lambda the live app registers.
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();  // a stale PSO built on a released device from a prior run must not be reused

  // CapsuleLineSDF leaf via the FieldOp factory (CapsuleLineSDFNode is leaf-private). The factory
  // builds it with the CapsuleLineSDF.t3 defaults, which is exactly the golden's probe assumption.
  std::shared_ptr<FieldNode> capsule = makeFieldNode("CapsuleLineSDF", "golden0");
  if (!capsule) {
    std::printf("[selftest-field-capsulelinesdf] FAIL: CapsuleLineSDF factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // injectBug at the MSL-string tier: corrupt the template's RED-channel write so every cooked
  // distance is shifted by +1.0 -> all probes go RED. Same substring + magnitude as
  // field_render_golden.cpp (the unique distance write in field_render_template.metal).
  std::string useTmpl = tmpl;
  if (injectBug) {
    const std::string from = "float4(f.w, 0.0, 0.0, 1.0)";
    const std::string to = "float4(f.w + 1.0, 0.0, 0.0, 1.0)";
    size_t pos = useTmpl.find(from);
    if (pos == std::string::npos) {
      std::printf("[selftest-field-capsulelinesdf] FAIL: injectBug could not find the distance-write "
                  "site in the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  MTL::Texture* tex = renderField2d(dev, q, capsule, useTmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-capsulelinesdf] FAIL: renderField2d returned null (compile/PSO "
                "failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Read back the R32Float distance texture (4 bytes / texel = one float).
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  // GPU float distance tolerance — same as field_render_golden.cpp (measured ~3e-8 worst probe).
  const float kTol = 1e-5f;
  int rc = 0;

  // (1) DISTANCE GOLDEN at three field positions, each asserted against fCapsule(exact p).
  //   inside   p=(0,0,0)     -> ~ -0.125   boundary p=(0.125,0,0) -> ~ 0.0   above p=(0,1,0) -> ~ 0.375
  struct Probe { const char* name; float tx, ty; };
  Probe probes[] = {
      {"inside", 0.0f, 0.0f},
      {"boundary", 0.125f, 0.0f},
      {"above", 0.0f, 1.0f},
  };
  for (const Probe& pr : probes) {
    uint32_t px, py;
    nearestPixel(pr.tx, pr.ty, px, py);
    float fx = pX(px), fy = pY(py);
    float expected = fCapsuleHost(fx, fy, 0.0f);
    float got = sampleAt(px, py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-capsulelinesdf] probe %-8s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, fx, fy, got, expected, diff, ok ? "OK" : "RED");
  }

  // (2) BOUNDARY-SIGN tooth: along the center row (p.y≈0), scanning +x from the axis, the distance
  //   must flip from negative (inside the capsule, |p.x| < Thickness) to positive (outside) exactly
  //   where |p.x| crosses Thickness (=0.125). Find the first px past center where R turns >= 0 and
  //   assert its p.x is within one texel of Thickness — this pins both the texCoord->p map AND the
  //   capsule radius. (Skipped under injectBug: the +1.0 shift removes the inside region entirely.)
  if (!injectBug) {
    const uint32_t cy = (kH - 1) / 2;  // p.y ≈ 0 row
    const uint32_t cx = (kW - 1) / 2;  // start at axis (p.x ≈ 0, inside)
    int crossPx = -1;
    for (uint32_t px = cx; px < kW; ++px) {
      if (sampleAt(px, cy) >= 0.0f) { crossPx = (int)px; break; }
    }
    if (crossPx < 0) {
      std::printf("[selftest-field-capsulelinesdf] FAIL: boundary sign never flipped on center row\n");
      rc = 1;
    } else {
      float crossPxField = pX((uint32_t)crossPx);
      float prevPxField = pX((uint32_t)(crossPx - 1));
      float texelW = 2.0f / kW;  // one texel in field-space x
      bool ok = (crossPxField >= kThickness - texelW) && (prevPxField <= kThickness + texelW);
      if (!ok) rc = 1;
      std::printf("[selftest-field-capsulelinesdf] boundary cross at px=%d p.x=% .4f (prev % .4f) "
                  "want≈%.3f texelW=%.4f %s\n",
                  crossPx, crossPxField, prevPxField, kThickness, texelW, ok ? "OK" : "RED");
    }
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-capsulelinesdf] FAIL: injectBug did not trip any probe (tooth has "
                  "no bite)\n");
      return 1;
    }
    std::printf("[selftest-field-capsulelinesdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-capsulelinesdf] PASS\n");
  return rc;
}

}  // namespace sw
