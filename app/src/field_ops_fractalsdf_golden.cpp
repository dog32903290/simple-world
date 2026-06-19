// field_ops_fractalsdf_golden — --selftest-field-fractalsdf. GPU DISTANCE-VALUE golden for the
// FractalSDF field leaf (the Mandelbulb box+sphere fold distance estimator). It builds a FractalSDF
// node, assembles its MSL via the FROZEN base, runtime-compiles it, renders a fullscreen pass, reads
// back the R32Float distance texture, and asserts each probed texel's RED == a HOST re-implementation
// of fMandelBulbFractal evaluated at that texel's EXACT field-space p (z=0). The host re-impl is
// transcribed from the TiXL MATH (FractalSDF.cs:36-69), NOT from the leaf's emitted MSL — so a leaf
// that drops/garbles a fold term diverges from the host and goes RED. Mirrors
// field_ops_combinesdf_golden.cpp's harness.
//
// ZONE: shell tier (lives at app/src/ root like field_render_golden.cpp / selftests.cpp / main.cpp).
// It crosses runtime (renderField2d, makeFieldNode, configureFractalSdf) AND platform
// (compileLibraryFromSource) — a runtime-zone selftest may NOT include platform, so this integration
// golden sits at the shell tier (same rationale as field_render_golden.cpp's header).
//
// PROBE 鐵律: all probes in field space [-1,1]. A Mandelbulb sampled at z=0 is an off-screen-shaped
// estimator whose sign region is not trivially hand-verifiable, so this golden uses VALUE PROBES ONLY
// (no sign-flip/boundary tooth) — exactly the discipline the prior cuts' Pyramid lesson mandates for
// off-screen forms. The tooth is GPU-value == host-value at each probe; the injectBug RED counterpart
// shifts every cooked value so all probes go RED, proving the value teeth bite cooked pixels.
//
// DEFAULTS under test (FractalSDF.t3): Iterations=8, Scale=2.0, Minrad=0.303, Clamping=(0,0,0),
// Increment=(0,0,0), Fold=(0.5,1.0). The host re-impl uses the SAME clamped iteration count (8). A
// SECOND case at Iterations=3 exercises the compile-time-selector fork (a different baked loop count =>
// different emitted MSL => different srcHash => recompile) and confirms the host tracks the count.
//
// PIXEL -> FIELD-SPACE p (backward-traced, identical to field_render_golden.cpp / the template):
//   p.x = (2*px+1)/W - 1 ; p.y = 1 - (2*py+1)/H ; p.z = 0 ; p.w = 0.
//
// injectBug: corrupt the template's RED-channel distance write so every cooked value is shifted by +1.0
// -> all VALUE probes RED (same technique/tier/magnitude as field_ops_combinesdf_golden.cpp).
#include "runtime/field_render.h"

#include <array>
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
#include "runtime/field_node_registry.h"  // makeFieldNode (FractalSDFNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache (fresh source-PSO cache per run-device)

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {

// Param-cook seam owned by field_ops_fractalsdf.cpp (the leaf type is TU-private). Forward-declared here
// (no header) exactly as selftests forward-declare the golden entry points.
void configureFractalSdf(FieldNode& node, float scale, float minrad, float clampingX, float clampingY,
                         float clampingZ, float incrementX, float incrementY, float incrementZ,
                         float foldX, float foldY, int iterations);

namespace {

constexpr uint32_t kW = 128, kH = 128;

// FractalSDF.t3 defaults.
constexpr float kScale = 2.0f, kMinrad = 0.303f;
constexpr float kClampX = 0.0f, kClampY = 0.0f, kClampZ = 0.0f;
constexpr float kIncX = 0.0f, kIncY = 0.0f, kIncZ = 0.0f;
constexpr float kFoldX = 0.5f, kFoldY = 1.0f;

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

// Field-space p at pixel (px,py) (see header; identical to field_render_golden.cpp).
float pX(uint32_t px) { return (2.0f * px + 1.0f) / kW - 1.0f; }
float pY(uint32_t py) { return 1.0f - (2.0f * py + 1.0f) / kH; }

// ---- HOST re-implementation of fMandelBulbFractal (transcribed from FractalSDF.cs:36-69 MATH, NOT
//      from the leaf's emitted MSL). float4 represented as std::array<float,4>{x,y,z,w}. All ops use
//      single-precision float to match the MSL fp32 pipeline. clamp/abs/pow/dot mirror HLSL/MSL exactly.
float hostFractal(float posX, float posY, float posZ, float scale1, float clampX, float clampY,
                  float clampZ, float incX, float incY, float incZ, float minrad, float foldX,
                  float foldY, int iterations) {
  // precomputed constants
  const float minRad2 = std::fmin(std::fmax(minrad, 1.0e-9f), 1.0f);  // clamp(minrad, 1e-9, 1)
  // scale = float4(scale1.xxx, abs(scale1)) / minRad2  -> (scale1,scale1,scale1,|scale1|)/minRad2
  const float sx = scale1 / minRad2, sy = scale1 / minRad2, sz = scale1 / minRad2;
  const float sw = std::fabs(scale1) / minRad2;
  const float absScalem1 = std::fabs(scale1 - 1.0f);
  const float absScaleRaisedTo1mIters = std::pow(std::fabs(scale1), (float)(1 - iterations));

  // p = float4(pos, 1); p0 = p
  float px = posX, py = posY, pz = posZ, pw = 1.0f;
  const float p0x = posX, p0y = posY, p0z = posZ, p0w = 1.0f;

  for (int i = 0; i < iterations; i++) {
    // box folding:
    //   p.xyz = abs(1 + p.xyz) - p.xyz - abs(1.0 - p.xyz);
    px = std::fabs(1.0f + px) - px - std::fabs(1.0f - px);
    py = std::fabs(1.0f + py) - py - std::fabs(1.0f - py);
    pz = std::fabs(1.0f + pz) - pz - std::fabs(1.0f - pz);
    //   p.xyz = clamp(p.xyz, clamping.x, clamping.y) * clamping.z - p.xyz;
    auto clampS = [](float v, float lo, float hi) { return std::fmin(std::fmax(v, lo), hi); };
    px = clampS(px, clampX, clampY) * clampZ - px;
    py = clampS(py, clampX, clampY) * clampZ - py;
    pz = clampS(pz, clampX, clampY) * clampZ - pz;

    // sphere folding:
    //   float r2 = dot(p.xyz, p.xyz);
    const float r2 = px * px + py * py + pz * pz;
    //   p *= clamp(max(minRad2 / r2, minRad2), fold.x, fold.y);   (scales ALL FOUR components, incl w)
    const float factor =
        std::fmin(std::fmax(std::fmax(minRad2 / r2, minRad2), foldX), foldY);
    px *= factor; py *= factor; pz *= factor; pw *= factor;
    //   p.xyz += float3(increment.x, increment.y, increment.z);
    px += incX; py += incY; pz += incZ;
    //   p = p * scale + p0;   (component-wise: scale=(sx,sy,sz,sw), p0 added)
    px = px * sx + p0x;
    py = py * sy + p0y;
    pz = pz * sz + p0z;
    pw = pw * sw + p0w;
  }
  // float d = ((length(p.xyz) - absScalem1) / p.w - AbsScaleRaisedTo1mIters);
  const float len = std::sqrt(px * px + py * py + pz * pz);
  return ((len - absScalem1) / pw - absScaleRaisedTo1mIters);
}

struct Probe { const char* name; uint32_t px, py; float expected; };

}  // namespace

int runFieldFractalSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-fractalsdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-fractalsdf] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  std::string useTmpl = tmpl;
  if (injectBug) {
    const std::string from = "float4(f.w, 0.0, 0.0, 1.0)";
    const std::string to = "float4(f.w + 1.0, 0.0, 0.0, 1.0)";
    size_t pos = useTmpl.find(from);
    if (pos == std::string::npos) {
      std::printf("[selftest-field-fractalsdf] FAIL: injectBug could not find the distance-write site "
                  "in the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  // The fractal estimator's magnitude varies a lot across the plane; near r2->0 the sphere fold spikes.
  // We pick probes AWAY from degeneracies (see below) and use a relative-ish absolute tol of 2e-4 (the
  // host fp32 and GPU fp32 agree to ~1e-5 on benign probes; 2e-4 leaves headroom for the longer fold
  // chain's accumulated rounding without admitting a dropped-term divergence, which is O(0.1+)).
  const float kTol = 2e-4f;
  int rc = 0;

  auto pxFor = [](float target) -> uint32_t {
    float f = ((target + 1.0f) * kW - 1.0f) * 0.5f;
    int px = (int)std::lround(f);
    if (px < 0) px = 0;
    if (px >= (int)kW) px = kW - 1;
    return (uint32_t)px;
  };
  auto pyFor = [](float target) -> uint32_t {
    float f = ((1.0f - target) * kH - 1.0f) * 0.5f;
    int py = (int)std::lround(f);
    if (py < 0) py = 0;
    if (py >= (int)kH) py = kH - 1;
    return (uint32_t)py;
  };

  auto runCase = [&](const char* caseName, int iterations, std::vector<Probe>& probes) -> bool {
    std::shared_ptr<FieldNode> tree = makeFieldNode("FractalSDF", "golden0");
    if (!tree) {
      std::printf("[selftest-field-fractalsdf] FAIL[%s]: FractalSDF factory not registered\n", caseName);
      rc = 1;
      return false;
    }
    configureFractalSdf(*tree, kScale, kMinrad, kClampX, kClampY, kClampZ, kIncX, kIncY, kIncZ, kFoldX,
                        kFoldY, iterations);
    clearTexOpCache();  // a different baked iteration count is a distinct source string (the fork)
    MTL::Texture* tex = renderField2d(dev, q, tree, useTmpl, kW, kH);
    if (!tex) {
      std::printf("[selftest-field-fractalsdf] FAIL[%s]: renderField2d null (compile/PSO failure)\n",
                  caseName);
      rc = 1;
      return false;
    }
    std::vector<float> buf((size_t)kW * kH, 0.0f);
    tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
    auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };
    for (Probe& pr : probes) {
      float px = pX(pr.px), py = pY(pr.py);
      float got = sampleAt(pr.px, pr.py);
      float diff = std::fabs(got - pr.expected);
      bool ok = diff <= kTol;
      if (!ok) rc = 1;
      std::printf("[selftest-field-fractalsdf] %-10s probe %-8s p=(% .4f,% .4f) got=% .6f "
                  "expected=% .6f diff=%.2e %s\n",
                  caseName, pr.name, px, py, got, pr.expected, diff, ok ? "OK" : "RED");
    }
    tex->release();
    return true;
  };

  // Host expected at a (targetX,targetY) probe for `iterations`, computed at the texel's EXACT p.
  auto expectAt = [&](float targetX, float targetY, int iterations, uint32_t& outPx,
                      uint32_t& outPy) -> float {
    outPx = pxFor(targetX);
    outPy = pyFor(targetY);
    return hostFractal(pX(outPx), pY(outPy), 0.0f, kScale, kClampX, kClampY, kClampZ, kIncX, kIncY,
                       kIncZ, kMinrad, kFoldX, kFoldY, iterations);
  };

  // ---- Case A: defaults, Iterations=8. Probes at four asymmetric in-window points away from r2->0
  //   degeneracies (the sphere fold spikes when r2 is tiny). The probes are at distinct (x,y) so a fold
  //   term that only affects one axis cannot hide. Each expected is the host re-impl at the EXACT p. ----
  {
    const int iters = 8;
    struct PT { const char* name; float x, y; };
    const std::array<PT, 4> pts = {{
        {"pA", 0.40f, 0.25f},
        {"pB", -0.55f, 0.15f},
        {"pC", 0.20f, -0.45f},
        {"pD", -0.30f, -0.60f},
    }};
    std::vector<Probe> probes;
    for (const PT& t : pts) {
      uint32_t px, py;
      float exp = expectAt(t.x, t.y, iters, px, py);
      probes.push_back({t.name, px, py, exp});
    }
    runCase("iters8", iters, probes);
  }

  // ---- Case B: Iterations=3 (the compile-time-selector fork). Different baked loop count => different
  //   emitted MSL => recompile; the host tracks the same count. Proves the fork's recompile path AND that
  //   the loop count actually changes the field (a leaf that ignored the selector would match iters8's
  //   host values, not iters3's). ----
  {
    const int iters = 3;
    struct PT { const char* name; float x, y; };
    const std::array<PT, 3> pts = {{
        {"pA", 0.40f, 0.25f},
        {"pB", -0.55f, 0.15f},
        {"pC", 0.20f, -0.45f},
    }};
    std::vector<Probe> probes;
    for (const PT& t : pts) {
      uint32_t px, py;
      float exp = expectAt(t.x, t.y, iters, px, py);
      probes.push_back({t.name, px, py, exp});
    }
    runCase("iters3", iters, probes);
  }

  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-fractalsdf] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-fractalsdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-fractalsdf] PASS\n");
  return rc;
}

}  // namespace sw
