// field_ops_boxsdf_golden — --selftest-field-boxsdf. GPU DISTANCE-VALUE golden for the BoxSDF leaf:
// assemble a BoxSDF field (defaults), runtime-compile it, render a fullscreen pass, read back the
// R32Float distance texture, and assert each probed texel's RED == the closed-form rounded-box signed
// distance fRoundedRect(p, center=0, CombinedScale=(0.5,0.5,0.5), r=0.05) at that texel's field-space
// p (z=0 plane). Mirrors field_render_golden.cpp (the SphereSDF GPU golden) for the box op.
//
// ZONE: shell tier (lives at app/src/ root like field_render_golden.cpp / selftests.cpp / main.cpp).
// It deliberately crosses runtime (renderField2d, makeFieldNode) AND platform
// (compileLibraryFromSource) — a runtime-zone selftest may NOT include platform (check_arch:
// runtime ↛ platform), so this integration golden sits at the shell tier (the only place allowed to
// bind both zones — same rationale as field_render_golden.cpp's header).
//
// PIXEL -> FIELD-SPACE p (backward-traced, identical to field_render_golden.cpp /
// field_render_template.metal):  p.x = (2*px+1)/W - 1 ; p.y = 1 - (2*py+1)/H ; p.z = 0 ; p.w = 0.
// The golden reads each texel's EXACT p and asserts against fRoundedRect(p) — robust to the half-texel
// offset.
//
// CLOSED-FORM (matches BoxSDF.cs fRoundedRect, center=0, size=CombinedScale=(0.5,0.5,0.5), r=0.05):
//   q = abs(p) - size + r ;  d = length(max(q,0)) + min(max(q.x,max(q.y,q.z)),0) - r.
//   Defaults give CombinedScale = Size*UniformScale/2 = (1,1,1)*1/2 = (0.5,0.5,0.5). Spot values on
//   z=0: p=(0,0,0)->d=-0.5 ; p=(1,0,0)->d=0.5 ; boundary p=(0.5,0,0)->d=0.0.
//
// injectBug: corrupt the template's RED-channel write so every cooked distance is shifted by +1.0 ->
// all probes RED (same technique/tier as field_render_golden.cpp; the substring is the unique distance
// write in field_render_template.metal). Proves the tooth bites (it reads cooked pixels, not a blind
// pass). The BOUNDARY-SIGN probe is skipped under injectBug (the shifted field has no inside region).
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
#include "runtime/field_node_registry.h"  // makeFieldNode (BoxSDFNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache (fresh source-PSO cache per run-device)

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {
namespace {

constexpr uint32_t kW = 128, kH = 128;

// CombinedScale (effective half-extent) and edge radius at the .t3 defaults (see header).
constexpr float kHalf = 0.5f;     // size*uniformScale/2 = (1*1)/2
constexpr float kEdgeR = 0.05f;

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

// Closed-form rounded-box SDF at z=0 (port of BoxSDF.cs fRoundedRect, center=0).
float boxSdf(float px, float py) {
  const float qx = std::fabs(px) - kHalf + kEdgeR;
  const float qy = std::fabs(py) - kHalf + kEdgeR;
  const float qz = 0.0f - kHalf + kEdgeR;  // p.z = 0 on this plane
  const float mx = std::fmax(qx, 0.0f), my = std::fmax(qy, 0.0f), mz = std::fmax(qz, 0.0f);
  const float outside = std::sqrt(mx * mx + my * my + mz * mz);
  const float inside = std::fmin(std::fmax(qx, std::fmax(qy, qz)), 0.0f);
  return outside + inside - kEdgeR;
}

}  // namespace

int runFieldBoxSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-boxsdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-boxsdf] FAIL: no Metal device\n");
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

  // BoxSDF leaf via the FieldOp factory (BoxSDFNode is leaf-private — field_ops_boxsdf.cpp). The
  // factory builds it with the BoxSDF.t3 defaults Center=(0,0,0), Size=(1,1,1), UniformScale=1,
  // EdgeRadius=0.05 -> CombinedScale=(0.5,0.5,0.5). The golden's closed-form uses those defaults.
  std::shared_ptr<FieldNode> box = makeFieldNode("BoxSDF", "golden0");
  if (!box) {
    std::printf("[selftest-field-boxsdf] FAIL: BoxSDF factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // injectBug at the MSL-string tier (the node is leaf-private, so we mutate the template): corrupt the
  // RED-channel write so every cooked distance is shifted by +1.0 -> all probes go RED. The substring
  // is the unique distance write in field_render_template.metal.
  std::string useTmpl = tmpl;
  if (injectBug) {
    const std::string from = "float4(f.w, 0.0, 0.0, 1.0)";
    const std::string to = "float4(f.w + 1.0, 0.0, 0.0, 1.0)";
    size_t pos = useTmpl.find(from);
    if (pos == std::string::npos) {
      std::printf("[selftest-field-boxsdf] FAIL: injectBug could not find the distance-write site in "
                  "the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  MTL::Texture* tex = renderField2d(dev, q, box, useTmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-boxsdf] FAIL: renderField2d returned null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Read back the R32Float distance texture (4 bytes / texel = one float).
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);

  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  // Same GPU float tolerance as the SphereSDF golden (length()/sqrt agrees with host sqrtf to ~ULP).
  const float kTol = 1e-5f;
  int rc = 0;

  // (1) DISTANCE GOLDEN at three field positions, each asserted against the closed-form box SDF at the
  //     texel's EXACT p. Probe pixels chosen to land near the blueprint's (0,0)/(1,0)/(0.5,0):
  //       inside   : p ~= (0,0)   (center)      -> d ~= -0.5
  //       outside  : p.x ~= 1.0   (right edge)  -> d ~=  0.5
  //       surface  : p.x ~= 0.5   (center row)  -> d ~=  0.0
  struct Probe { const char* name; uint32_t px, py; };
  const uint32_t cy = (kH - 1) / 2;     // 63 -> p.y = 0.0078125 (≈ center row)
  const uint32_t cx = (kW - 1) / 2;     // 63 -> p.x = -0.0078125 (≈ center)
  const uint32_t surfacePx = 96;        // p.x = 0.5078125 (≈ 0.5, on the face)
  const uint32_t rightPx = kW - 1;      // 127 -> p.x = 0.9921875 (≈ 1.0, outside)
  Probe probes[] = {
      {"inside", cx, cy},
      {"outside", rightPx, cy},
      {"surface", surfacePx, cy},
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = boxSdf(px, py);
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-boxsdf] probe %-8s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  // (2) BOUNDARY-SIGN tooth: along the center row (p.y≈0), scanning +x, the distance must flip from
  //     negative (inside the box) to positive (outside) where p.x crosses the rounded face. On y≈0 the
  //     rounded-box face is at |p.x| = CombinedScale.x = 0.5 (the +r/-r round cancels on the flat face),
  //     so the crossing is at p.x = 0.5 within one texel — pins the texCoord->p constant. (Skipped under
  //     injectBug: the shifted field has no inside region.)
  if (!injectBug) {
    int crossPx = -1;
    for (uint32_t px = cx; px < kW; ++px) {
      if (sampleAt(px, cy) >= 0.0f) { crossPx = (int)px; break; }
    }
    if (crossPx < 0) {
      std::printf("[selftest-field-boxsdf] FAIL: boundary sign never flipped on center row\n");
      rc = 1;
    } else {
      float crossPxField = pX((uint32_t)crossPx);
      float prevPxField = pX((uint32_t)(crossPx - 1));
      float texelW = 2.0f / kW;  // one texel in field-space x
      const float kFace = kHalf;  // face at |p.x| = CombinedScale.x = 0.5
      bool ok = (crossPxField >= kFace - texelW) && (prevPxField <= kFace + texelW);
      if (!ok) rc = 1;
      std::printf("[selftest-field-boxsdf] boundary cross at px=%d p.x=% .4f (prev % .4f) want≈%.3f "
                  "texelW=%.4f %s\n",
                  crossPx, crossPxField, prevPxField, kFace, texelW, ok ? "OK" : "RED");
    }
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-boxsdf] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-boxsdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-boxsdf] PASS\n");
  return rc;
}

}  // namespace sw
