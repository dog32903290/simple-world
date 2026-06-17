// field_ops_chainlinksdf_golden — --selftest-field-chainlinksdf. GPU DISTANCE-VALUE golden for the
// ChainLinkSDF field op on the shader-graph island: assemble a ChainLinkSDF field, runtime-compile it
// (this exercises the GLOBAL-helper path — fChainLink must land in /*{GLOBALS}*/ and compile), render a
// fullscreen pass, read back the R32Float distance texture, and assert each probed texel's RED == the
// closed-form ChainLink signed distance at that texel's field-space p (z=0 plane).
//
// ZONE: shell tier (lives at app/src/ root like field_render_golden.cpp / selftests.cpp). It crosses
// runtime (renderField2d, makeFieldNode) AND platform (compileLibraryFromSource) — exactly what
// main.cpp / field_render_golden.cpp do to wire the field source compiler. A runtime-zone selftest may
// NOT include platform (check_arch: runtime ↛ platform), so this integration golden sits at the shell
// tier, the only place allowed to bind the two zones.
//
// This proves the WHOLE runtime-compile -> source-PSO cache -> fullscreen dispatch -> readback chain
// produces the right number for an op whose codegen emits a GLOBAL HELPER FUNCTION (unlike SphereSDF,
// which was fully inline). If the globals path were broken (helper dropped), the compile would fail
// (undefined fChainLink) -> renderField2d returns nullptr -> this golden FAILs.
//
// PIXEL -> FIELD-SPACE p (same backward-traced map as field_render_golden.cpp / the template):
//   p.x = (2*px + 1)/W - 1 ;  p.y = 1 - (2*py + 1)/H ;  p.z = 0, p.w = 0.
// The golden reads each texel's EXACT p and asserts against the closed-form d(p) — robust to the
// half-texel offset.
//
// CLOSED-FORM ChainLink distance (ChainLinkSDF.cs fChainLink, le=Length, r1=Size, r2=Thickness):
//   q = float3( p.x, max(abs(p.y)-le, 0), p.z )
//   d = length( float2( length(q.xy) - r1, q.z ) ) - r2
// With the .t3 defaults le=0.5, r1=0.5, r2=0.25, center=0 and p on the z=0 plane:
//   p=(0,0)    -> q.xy=(0,0)   -> length=0   -> d=length(0-0.5,0)-0.25 = 0.25   (inside, +)
//   p=(0.5,0)  -> q.xy=(0.5,0) -> length=0.5 -> d=length(0.5-0.5,0)-0.25 = -0.25 (interior)
//   p=(0.75,0) -> q.xy=(.75,0) -> length=.75 -> d=length(.75-0.5,0)-0.25 = 0.0  (boundary)
//
// injectBug: corrupt the template's RED-channel write so every cooked distance is shifted by +1.0 ->
// all probes go RED. Same technique/magnitude as field_render_golden.cpp (the node is leaf-private, so
// we mutate the assembled MSL via a template string-replace, not the node). Proves the golden bites.
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
#include "runtime/field_node_registry.h"  // makeFieldNode (ChainLinkSDFNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache (fresh source-PSO cache per run-device)

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {
namespace {

constexpr uint32_t kW = 128, kH = 128;
// ChainLinkSDF.t3 defaults (mirrored in ChainLinkSDFNode ctor; the factory builds with these).
constexpr float kLength = 0.5f;     // le
constexpr float kSize = 0.5f;       // r1
constexpr float kThickness = 0.25f; // r2

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

// Field-space p at pixel (px,py) (same derivation as field_render_golden.cpp).
float pX(uint32_t px) { return (2.0f * px + 1.0f) / kW - 1.0f; }
float pY(uint32_t py) { return 1.0f - (2.0f * py + 1.0f) / kH; }

// Closed-form ChainLink signed distance on the z=0 plane (matches fChainLink with q.z=0).
float chainLinkDist(float px, float py) {
  float qx = px;
  float qy = std::fmax(std::fabs(py) - kLength, 0.0f);
  float lenq = std::sqrt(qx * qx + qy * qy);  // length(q.xy)
  float a = lenq - kSize;                      // q.z = 0 on this plane
  return std::sqrt(a * a) - kThickness;        // length(float2(a, 0)) - r2 = |a| - r2
}

}  // namespace

int runFieldChainLinkSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-chainlinksdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-chainlinksdf] FAIL: no Metal device\n");
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

  // ChainLinkSDF leaf via the FieldOp factory (ChainLinkSDFNode is leaf-private). The factory builds
  // it with the ChainLinkSDF.t3 defaults Center=(0,0,0), Length=0.5, Size=0.5, Thickness=0.25.
  std::shared_ptr<FieldNode> node = makeFieldNode("ChainLinkSDF", "golden0");
  if (!node) {
    std::printf("[selftest-field-chainlinksdf] FAIL: ChainLinkSDF factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // injectBug at the MSL-string tier: corrupt the template's RED-channel write so every cooked
  // distance is shifted by +1.0 -> all probes go RED. The substring is the unique distance write in
  // field_render_template.metal (line: `return float4(f.w, 0.0, 0.0, 1.0);`).
  std::string useTmpl = tmpl;
  if (injectBug) {
    const std::string from = "float4(f.w, 0.0, 0.0, 1.0)";
    const std::string to = "float4(f.w + 1.0, 0.0, 0.0, 1.0)";
    size_t pos = useTmpl.find(from);
    if (pos == std::string::npos) {
      std::printf("[selftest-field-chainlinksdf] FAIL: injectBug could not find the distance-write "
                  "site in the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  MTL::Texture* tex = renderField2d(dev, q, node, useTmpl, kW, kH);
  if (!tex) {
    // Note: a dropped GLOBAL helper (undefined fChainLink) would land here via a compile failure.
    std::printf("[selftest-field-chainlinksdf] FAIL: renderField2d returned null (compile/PSO failure "
                "— check the fChainLink global is emitted into /*{GLOBALS}*/)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Read back the R32Float distance texture (4 bytes / texel = one float).
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);

  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  const float kTol = 1e-5f;  // single-precision sqrt parity (same headroom as field_render_golden).
  int rc = 0;

  // DISTANCE GOLDEN at three field positions along the center row (p.y ~= 0), each asserted against
  // the closed-form chainLinkDist(exact p). Probe pixels chosen to land near the blueprint targets:
  //   p=(0,0)    inside    : cx (center column)
  //   p=(0.5,0)  interior  : px where p.x ~= 0.5
  //   p=(0.75,0) boundary  : px where p.x ~= 0.75 (d ~= 0, the sign-flip / surface probe)
  const uint32_t cy = (kH - 1) / 2;  // 63 -> p.y = 0.0078125 (~= center row)
  const uint32_t cx = (kW - 1) / 2;  // 63 -> p.x = -0.0078125 (~= 0)
  // p.x ~= 0.5 -> px = ((0.5+1)*W - 1)/2 = 95.5 -> 96 gives p.x = 0.5078125
  const uint32_t interiorPx = 96;
  // p.x ~= 0.75 -> px = ((0.75+1)*W - 1)/2 = 111.5 -> 112 gives p.x = 0.7578125
  const uint32_t boundaryPx = 112;
  struct Probe { const char* name; uint32_t px, py; };
  Probe probes[] = {
      {"inside", cx, cy},
      {"interior", interiorPx, cy},
      {"boundary", boundaryPx, cy},
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = chainLinkDist(px, py);
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-chainlinksdf] probe %-8s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  // BOUNDARY-SIGN tooth: along the center row scanning +x, the distance is positive near the center
  // (inside, d ~= 0.25), dips negative through the interior region, then crosses back to positive at
  // the outer surface near p.x = Size + Thickness = 0.75. Assert that an interior texel (p.x ~= 0.5)
  // is negative and a boundary/outer texel (p.x ~= 0.75) is >= 0 — pins the field's sign structure and
  // the texCoord->p map. (Skipped under injectBug: the +1.0-shifted field has no negative region.)
  if (!injectBug) {
    float interiorVal = sampleAt(interiorPx, cy);
    float boundaryVal = sampleAt(boundaryPx, cy);
    bool ok = (interiorVal < 0.0f) && (boundaryVal >= 0.0f);
    if (!ok) rc = 1;
    std::printf("[selftest-field-chainlinksdf] boundary-sign interior(p.x=%.4f)=% .6f<0 "
                "boundary(p.x=%.4f)=% .6f>=0 %s\n",
                pX(interiorPx), interiorVal, pX(boundaryPx), boundaryVal, ok ? "OK" : "RED");
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-chainlinksdf] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-chainlinksdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-chainlinksdf] PASS\n");
  return rc;
}

}  // namespace sw
