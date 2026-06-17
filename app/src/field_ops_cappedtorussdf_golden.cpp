// field_ops_cappedtorussdf_golden — --selftest-field-cappedtorussdf. GPU DISTANCE-VALUE golden for
// the CappedTorusSDF leaf on the shader-graph island: assemble a CappedTorusSDF field (axis=Z
// default), runtime-compile it, render a fullscreen pass, read back the R32Float distance texture, and
// assert each probed texel's RED == the closed-form signed distance at that texel's field-space p
// (z=0 plane).
//
// ZONE: shell tier (lives at app/src/ root like selftests.cpp / main.cpp / field_render_golden.cpp /
// field_ops_torussdf_golden.cpp). This file deliberately crosses runtime (renderField2d, makeFieldNode)
// AND platform (compileLibraryFromSource) — exactly what main.cpp / field_render_golden does to wire
// the field source compiler. A runtime-zone selftest may NOT include platform (check_arch:
// runtime -/-> platform), so this integration golden sits at the shell tier (the only place allowed to
// bind the two zones).
//
// CAPPED-TORUS DISTANCE (axis=Z = "xyz" identity, CappedTorusSDF.t3 defaults Fill=2, Radius=2,
// Thickness=0.5, Center=0):
//   fCappedTorus(p, size, ra, rb):
//     an = 2.5*(0.5 + 0.5*(size*1.1 + 3));  sc = (sin(an), cos(an));
//     p.x = abs(p.x);
//     k = (sc.y*p.x > sc.x*p.y) ? dot(p.xy, sc) : length(p.xy);
//     return sqrt(dot(p,p) + ra*ra - 2*ra*k) - rb;
//   In the 2D field template p.z = 0, so dot(p,p) = p.x*p.x + p.y*p.y. Closed form mirrored in
//   cappedTorusD() below — byte-identical to the helper math.
//
// ★ GOLDEN DISCIPLINE (off-screen shape -> VALUE PROBES ONLY, NO sign-flip/boundary tooth):
//   At the .t3 defaults Radius=2 puts the ring OFF-SCREEN in 2D field space [-1,1] -> the whole visible
//   z=0 plane is OUTSIDE the shape (d > 0 everywhere on screen). There is NO negative region and NO
//   surface crossing inside [-1,1], so a boundary-sign tooth (like TorusSDF's) CANNOT bite here. We use
//   EXACT-VALUE probes only, all with p.x,p.y in [-1,1]: a wrong texCoord->p map, a wrong arg order,
//   a wrong axis swizzle, or a wrong param all shift the asserted distance and trip a probe.
//
// PIXEL -> FIELD-SPACE p (backward-traced, must match field_render_template.metal, same as
// field_render_golden.cpp / field_ops_torussdf_golden.cpp):
//   p.x = (2*px + 1)/W - 1 ;  p.y = 1 - (2*py + 1)/H ;  p.z = 0, p.w = 0.
// The golden reads each texel's EXACT p and asserts against cappedTorusD(p) — robust to the
// half-texel offset.
//
// PROBES (an=7.75, sc=(sin 7.75, cos 7.75), ra=2, rb=0.5, p.x=abs(p.x); all in [-1,1], all positive):
//   p≈(0, 1)   -> d ≈ 0.5078   (top center)
//   p≈(0, 0)   -> d ≈ 1.4931   (hole center; the deepest "outside" point on screen)
//   p≈(1, 0)   -> d ≈ 0.5200   (right center)
//
// injectBug: shift the cooked field by +1.0 by corrupting the template's RED-channel write (same
// technique + magnitude as field_render_golden.cpp) so every probe's expected d is off by 1.0 ->
// all probes RED. Proves the golden bites (it reads cooked pixels, not a blind pass).
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
#include "runtime/field_node_registry.h"  // makeFieldNode (CappedTorusSDFNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache (fresh source-PSO cache per run-device)

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {
namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kFill = 2.0f;       // CappedTorusSDF.t3 default
constexpr float kRadius = 2.0f;     // ra in the helper
constexpr float kThickness = 0.5f;  // rb in the helper

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

// Closed-form capped-torus distance at z=0 — byte-identical to fCappedTorus (Fill=size, Radius=ra,
// Thickness=rb). p.x = abs(p.x); the ternary selects dot vs length exactly as the helper does.
float cappedTorusD(float px, float py) {
  const float an = 2.5f * (0.5f + 0.5f * (kFill * 1.1f + 3.0f));  // 7.75 at Fill=2
  const float scx = std::sin(an);
  const float scy = std::cos(an);
  const float x = std::fabs(px);
  const float k = (scy * x > scx * py) ? (x * scx + py * scy) : std::sqrt(x * x + py * py);
  return std::sqrt(x * x + py * py + kRadius * kRadius - 2.0f * kRadius * k) - kThickness;
}

}  // namespace

int runFieldCappedTorusSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf(
        "[selftest-field-cappedtorussdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-cappedtorussdf] FAIL: no Metal device\n");
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

  // CappedTorusSDF leaf via the FieldOp factory (CappedTorusSDFNode is leaf-private). The factory
  // builds it with the .t3 defaults Center=(0,0,0), Fill=2, Radius=2, Thickness=0.5, Axis=2 (Z) — the
  // axis swizzle "xyz" (identity) is baked in the node ctor, so no cook wiring is needed.
  std::shared_ptr<FieldNode> shape = makeFieldNode("CappedTorusSDF", "golden0");
  if (!shape) {
    std::printf("[selftest-field-cappedtorussdf] FAIL: CappedTorusSDF factory not registered\n");
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
      std::printf("[selftest-field-cappedtorussdf] FAIL: injectBug could not find the distance-write "
                  "site in the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  MTL::Texture* tex = renderField2d(dev, q, shape, useTmpl, kW, kH);
  if (!tex) {
    std::printf(
        "[selftest-field-cappedtorussdf] FAIL: renderField2d returned null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Read back the R32Float distance texture (4 bytes / texel = one float).
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  // GPU float distance tolerance — same as field_render_golden.cpp / torussdf golden (1e-5 holds with
  // comfortable headroom; single-precision sqrt/sin/cos parity, no fp16).
  const float kTol = 1e-5f;
  int rc = 0;

  // VALUE PROBES ONLY (off-screen ring -> no negative region; see header GOLDEN DISCIPLINE). All
  // pixels are in [-1,1]; expected is computed from each texel's EXACT p so the half-texel offset is
  // accounted for. px=64 -> p.x≈0.0078; py=0 -> p.y≈0.9922 (top); py=64 -> p.y≈-0.0078 (center);
  // px=127 -> p.x≈0.9922 (right).
  const uint32_t cx = (kW) / 2;  // 64 -> p.x ≈ 0.0078125 (≈ center column)
  const uint32_t cy = (kH) / 2;  // 64 -> p.y ≈ -0.0078125 (≈ center row)
  struct Probe { const char* name; uint32_t px, py; };
  Probe probes[] = {
      {"top",    cx,       0},   // p≈(0, 1)  -> d≈0.5078
      {"hole",   cx,       cy},  // p≈(0, 0)  -> d≈1.4931 (deepest outside point on screen)
      {"right",  kW - 1,   cy},  // p≈(1, 0)  -> d≈0.5200
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = cappedTorusD(px, py);
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-cappedtorussdf] probe %-6s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-cappedtorussdf] FAIL: injectBug did not trip any probe (tooth has "
                  "no bite)\n");
      return 1;
    }
    std::printf("[selftest-field-cappedtorussdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-cappedtorussdf] PASS\n");
  return rc;
}

}  // namespace sw
