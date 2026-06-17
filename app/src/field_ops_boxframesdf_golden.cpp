// field_ops_boxframesdf_golden — --selftest-field-boxframesdf. GPU DISTANCE-VALUE golden for the
// BoxFrameSDF field leaf on the shader-graph island: assemble a BoxFrameSDF field via the FieldOp
// factory, runtime-compile it, render a fullscreen pass, read back the R32Float distance texture, and
// assert each probed texel's RED == the closed-form fBoxFrame distance at that texel's field-space p
// (z=0 plane). Mirrors field_render_golden.cpp (the SphereSDF golden) for the box-frame op.
//
// ZONE: shell tier (lives at app/src/ root like field_render_golden.cpp / selftests.cpp / main.cpp).
// It deliberately crosses runtime (renderField2d, makeFieldNode) AND platform
// (compileLibraryFromSource) — a runtime-zone selftest may NOT include platform (check_arch:
// runtime ↛ platform), so this integration golden sits at the shell tier (selftests.cpp top comment:
// "may include any zone").
//
// WHY THIS OP NEEDS ITS OWN GOLDEN (vs the SphereSDF render golden): BoxFrameSDF is the FIRST field
// leaf to emit a GLOBAL helper via addGlobals() — it exercises the /*{GLOBALS}*/ injection path that
// SphereSDF (inline distance) never touched. If addGlobals were dropped, the assembled MSL would call
// an undeclared `fBoxFrame` and FAIL TO COMPILE -> renderField2d returns null -> this golden FAILs.
// So a clean PASS proves the globals path is wired end-to-end (assemble -> compile -> dispatch).
//
// PIXEL -> FIELD-SPACE p (identical to field_render_golden.cpp / field_render_template.metal):
//   p.x = (2*px + 1)/W - 1 ;  p.y = 1 - (2*py + 1)/H ;  p.z = 0, p.w = 0.
// Expected RED = fBoxFrame(p.xyz, center=0, b=(0.5,0.5,0.5), e=0.05) — the SAME closed form as the
// shader helper, evaluated host-side. The golden reads each texel's EXACT p (not an assumed p) and
// asserts against fBoxFrame(p) — robust to the half-texel offset, like the SphereSDF golden.
//
// PROBES (.t3 defaults: b=(0.5,0.5,0.5), e=0.05, center=0):
//   center  p~=(0,0,0)     -> d ~= 0.5657 (= 0.4*sqrt(2))   [deep interior of the hollow frame]
//   edge    p~=(0.5,0,0)   -> d ~= 0.4                       [face-center, away from the frame bars]
//   corner  p~=(0.5,0.5,0) -> d ~= 0.0  (BOUNDARY-SIGN)      [on a frame bar -> distance ~ 0]
// The corner probe doubles as the boundary-sign tooth: fBoxFrame ~ 0 there, and a sign/scale error in
// the texCoord->p map or the helper would push it off zero.
//
// injectBug: corrupt the template's RED-channel write so every cooked distance is shifted by +1.0
// (>> tolerance) -> all probes go RED. Same technique/tier as field_render_golden.cpp (the node is
// leaf-private, so we bite at the MSL-string tier, not by mutating the node).
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
#include "runtime/field_node_registry.h"  // makeFieldNode (BoxFrameSDFNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache (fresh source-PSO cache per run-device)

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {
namespace {

constexpr uint32_t kW = 128, kH = 128;
// BoxFrameSDF.t3 defaults -> derived params (see field_ops_boxframesdf.cpp).
constexpr float kThickness = 0.05f;             // e (frame edge thickness)
constexpr float kB = 0.5f;                      // box half-extent (CombinedScale = Size*UniformScale/2)

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

// Host replica of the shader helper fBoxFrame (byte-for-byte the same math), evaluated at the texel's
// EXACT p (z=0), center=0, b=(kB,kB,kB), e=kThickness. This is the closed-form expected RED.
float fBoxFrameHost(float px, float py, float pz) {
  // p = abs(p - center) - b;  center = 0
  float ax = std::fabs(px) - kB;
  float ay = std::fabs(py) - kB;
  float az = std::fabs(pz) - kB;
  // q = abs(p + e) - e;
  float qx = std::fabs(ax + kThickness) - kThickness;
  float qy = std::fabs(ay + kThickness) - kThickness;
  float qz = std::fabs(az + kThickness) - kThickness;
  auto len3 = [](float x, float y, float z) { return std::sqrt(x * x + y * y + z * z); };
  auto mx0 = [](float v) { return v > 0.0f ? v : 0.0f; };  // max(v,0)
  auto mn0 = [](float v) { return v < 0.0f ? v : 0.0f; };  // min(v,0)
  float t1 = len3(mx0(ax), mx0(qy), mx0(qz)) + mn0(std::fmax(ax, std::fmax(qy, qz)));
  float t2 = len3(mx0(qx), mx0(ay), mx0(qz)) + mn0(std::fmax(qx, std::fmax(ay, qz)));
  float t3 = len3(mx0(qx), mx0(qy), mx0(az)) + mn0(std::fmax(qx, std::fmax(qy, az)));
  return std::fmin(std::fmin(t1, t2), t3);
}

}  // namespace

int runFieldBoxFrameSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-boxframesdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-boxframesdf] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  // Wire the field source compiler (runtime->platform leaf seam) — same lambda main.cpp registers.
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();  // a stale PSO built on a released device from a prior run must not be reused

  // BoxFrameSDF leaf via the FieldOp factory (BoxFrameSDFNode is leaf-private — field_ops_boxframesdf
  // .cpp). The factory builds it with the .t3-derived defaults Center=0, Thickness=0.05,
  // CombinedScale=(0.5,0.5,0.5) — exactly the constants this golden asserts against.
  std::shared_ptr<FieldNode> box = makeFieldNode("BoxFrameSDF", "golden0");
  if (!box) {
    std::printf("[selftest-field-boxframesdf] FAIL: BoxFrameSDF factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // injectBug at the MSL-string tier: corrupt the template's RED-channel write so every cooked
  // distance is shifted by +1.0 (>> tolerance) -> all probes go RED. Substring is the unique distance
  // write in field_render_template.metal.
  std::string useTmpl = tmpl;
  if (injectBug) {
    const std::string from = "float4(f.w, 0.0, 0.0, 1.0)";
    const std::string to = "float4(f.w + 1.0, 0.0, 0.0, 1.0)";
    size_t pos = useTmpl.find(from);
    if (pos == std::string::npos) {
      std::printf("[selftest-field-boxframesdf] FAIL: injectBug could not find the distance-write site "
                  "in the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  MTL::Texture* tex = renderField2d(dev, q, box, useTmpl, kW, kH);
  if (!tex) {
    // A null here also catches the addGlobals-dropped case: an undeclared fBoxFrame fails to compile.
    std::printf("[selftest-field-boxframesdf] FAIL: renderField2d returned null (compile/PSO failure "
                "— e.g. fBoxFrame helper not emitted)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Read back the R32Float distance texture (4 bytes / texel = one float).
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);

  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  // GPU float distance tolerance — same hardware/path as the SphereSDF render golden (worst probe
  // ~2.89e-8 there); fBoxFrame is a few more sqrt/min/max ops, still comfortably inside 1e-5.
  const float kTol = 1e-5f;
  int rc = 0;

  // Probe pixels chosen to land near the blueprint targets (0,0)/(0.5,0)/(0.5,0.5):
  //   center : p ~= (0,0)     -> px=cx, py=cy
  //   edge   : p.x ~= 0.5     -> px=96 (p.x=0.5078125), py=cy (face-center, d~=0.4)
  //   corner : p ~= (0.5,0.5) -> px=96, py with p.y~=0.5 -> py=31 (p.y=0.5078125), boundary d~=0
  const uint32_t cy = (kH - 1) / 2;  // 63 -> p.y = 0.0078125
  const uint32_t cx = (kW - 1) / 2;  // 63 -> p.x = -0.0078125
  const uint32_t edgePx = 96;        // p.x = 0.5078125
  const uint32_t cornerPy = 31;      // p.y = 1 - (63)/128 = 0.5078125
  struct Probe { const char* name; uint32_t px, py; };
  Probe probes[] = {
      {"center", cx, cy},
      {"edge", edgePx, cy},
      {"corner", edgePx, cornerPy},
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = fBoxFrameHost(px, py, 0.0f);
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-boxframesdf] probe %-6s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  // BOUNDARY-SIGN tooth: scanning +x along the corner row (p.y ~= 0.5, near a frame bar), the field
  // must transition through ~0 — find the texel whose |distance| is minimal and assert it is within
  // tolerance of zero (the frame surface). A wrong texCoord->p scale/offset, or a corrupted helper,
  // would move the zero off this row. (Skipped under injectBug: the shifted field has no zero level.)
  if (!injectBug) {
    float minAbs = 1e30f; int minPx = -1;
    for (uint32_t px = cx; px < kW; ++px) {
      float v = std::fabs(sampleAt(px, cornerPy));
      if (v < minAbs) { minAbs = v; minPx = (int)px; }
    }
    // On the cornerPy row, the box frame bar at |p.x|=b=0.5 produces a distance that touches ~0 (the
    // frame surface) within one texel; assert the minimum-|d| texel really is on the surface.
    float texelMag = 2.0f / kW;  // one texel of field-space x
    bool ok = (minPx >= 0) && (minAbs <= texelMag);
    if (!ok) rc = 1;
    std::printf("[selftest-field-boxframesdf] boundary min|d| at px=%d p.x=% .4f |d|=%.4f want<=%.4f %s\n",
                minPx, minPx >= 0 ? pX((uint32_t)minPx) : 0.0f, minAbs, texelMag, ok ? "OK" : "RED");
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-boxframesdf] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-boxframesdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-boxframesdf] PASS\n");
  return rc;
}

}  // namespace sw
