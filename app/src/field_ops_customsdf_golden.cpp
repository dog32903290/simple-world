// field_ops_customsdf_golden — --selftest-field-customsdf. GPU DISTANCE-VALUE golden for the CustomSDF
// field leaf (the op that injects a user DistanceFunction string VERBATIM into the generated shader and
// recompiles). It builds a CustomSDF node with a FIXED known-good body, assembles its MSL via the
// FROZEN base, runtime-compiles it (newLibrary(source) — proving the verbatim-inject + recompile
// mechanics), renders a fullscreen pass, reads back the R32Float distance texture, and asserts each
// probed texel's RED == the closed-form distance the fixed body computes. Mirrors
// field_ops_combinesdf_golden.cpp's harness.
//
// ZONE: shell tier (lives at app/src/ root like field_render_golden.cpp / selftests.cpp / main.cpp).
// It deliberately crosses runtime (renderField2d, makeFieldNode, configureCustomSdf) AND platform
// (compileLibraryFromSource) — a runtime-zone selftest may NOT include platform (check_arch:
// runtime ↛ platform), so this integration golden sits at the shell tier (same rationale as
// field_render_golden.cpp's header).
//
// WHAT IS UNDER TEST: (1) the user DistanceFunction string lands VERBATIM inside dCustom<id> and the
// shader compiles; (2) the AdditionalDefines string lands in the Definitions block (a #define the body
// uses resolves); (3) the packed [GraphParam]s (Offset vec3, A scalar) flow through to the function
// args. The FIXED body is the .t3-shaped sphere `return length(p - Offset) - A;` (case A) and a
// define-driven variant `return length(p - Offset) - MYRAD;` with AdditionalDefines `#define MYRAD ...`
// (case B). Both are KNOWN closed forms (|p - Offset| - radius), so the GPU value is checked against the
// host re-derivation — and the sphere has a real on-screen negative interior (radius < 1, centered in
// [-1,1]) so a BOUNDARY-SIGN tooth is valid (probe 鐵律: sign teeth only where a real negative region
// exists on-screen).
//
// PIXEL -> FIELD-SPACE p (backward-traced, identical to field_render_golden.cpp / the template):
//   p.x = (2*px+1)/W - 1 ; p.y = 1 - (2*py+1)/H ; p.z = 0 ; p.w = 0. The golden reads each texel's
// EXACT p and asserts against the closed-form at that p — robust to the half-texel offset.
//
// injectBug: corrupt the template's RED-channel distance write so every cooked distance is shifted by
// +1.0 -> all VALUE probes RED (same technique/tier/magnitude as field_ops_combinesdf_golden.cpp). The
// boundary-sign probe is skipped under injectBug (the shifted field has no inside region). This proves
// the value teeth bite cooked pixels, not a blind pass. (A SEPARATE failure mode — a malformed user
// string that fails to compile -> renderField2d null -> the case FAILs loudly — is exercised implicitly:
// the fixed body is good, so a regression that breaks the inject/recompile path turns the render null.)
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
#include "runtime/field_node_registry.h"  // makeFieldNode (CustomSDFNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache (fresh source-PSO cache per run-device)

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {

// Param-cook seam owned by field_ops_customsdf.cpp (the leaf type is TU-private). Forward-declared here
// (no header) exactly as selftests forward-declare the golden entry points.
void configureCustomSdf(FieldNode& node, const std::string& distanceFunction,
                        const std::string& additionalDefines, float offsetX, float offsetY,
                        float offsetZ, float a, float b, float c);

namespace {

constexpr uint32_t kW = 128, kH = 128;

// Fixed sphere params: centered at Offset=(0.2,-0.1,0), radius A=0.4 -> a real negative interior fully
// inside the [-1,1] field window (so the boundary-sign tooth is valid).
constexpr float kOffX = 0.2f, kOffY = -0.1f, kOffZ = 0.0f, kRad = 0.4f;
constexpr float kMyRad = 0.35f;  // case B radius driven via AdditionalDefines #define

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

// Host closed-form for the fixed body `length(p - Offset) - radius` at z=0 (p.z=Offset.z=0).
float distSphere(float px, float py, float radius) {
  const float dx = px - kOffX, dy = py - kOffY;  // dz = 0 - kOffZ = 0
  return std::sqrt(dx * dx + dy * dy) - radius;
}

struct Probe { const char* name; uint32_t px, py; float expected; };

}  // namespace

int runFieldCustomSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-customsdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-customsdf] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();  // stale PSO from a released prior-run device must not be reused

  std::string useTmpl = tmpl;
  if (injectBug) {
    const std::string from = "float4(f.w, 0.0, 0.0, 1.0)";
    const std::string to = "float4(f.w + 1.0, 0.0, 0.0, 1.0)";
    size_t pos = useTmpl.find(from);
    if (pos == std::string::npos) {
      std::printf("[selftest-field-customsdf] FAIL: injectBug could not find the distance-write site "
                  "in the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  const float kTol = 1e-5f;
  int rc = 0;

  const uint32_t cy = (kH - 1) / 2;  // center row, p.y≈0
  auto pxFor = [](float target) -> uint32_t {
    float f = ((target + 1.0f) * kW - 1.0f) * 0.5f;
    int px = (int)std::lround(f);
    if (px < 0) px = 0;
    if (px >= (int)kW) px = kW - 1;
    return (uint32_t)px;
  };

  // Build a CustomSDF with the given body / defines / radius and render it.
  auto buildTree = [&](const std::string& body, const std::string& defines,
                       float a) -> std::shared_ptr<FieldNode> {
    std::shared_ptr<FieldNode> node = makeFieldNode("CustomSDF", "golden0");
    if (!node) return nullptr;
    configureCustomSdf(*node, body, defines, kOffX, kOffY, kOffZ, a, 0.0f, 0.0f);
    return node;
  };

  auto runCase = [&](const char* caseName, const std::string& body, const std::string& defines,
                     float a, std::vector<Probe>& probes) -> bool {
    std::shared_ptr<FieldNode> tree = buildTree(body, defines, a);
    if (!tree) {
      std::printf("[selftest-field-customsdf] FAIL[%s]: CustomSDF factory not registered\n", caseName);
      rc = 1;
      return false;
    }
    clearTexOpCache();  // each case is a distinct source string; do not reuse a prior case's PSO
    MTL::Texture* tex = renderField2d(dev, q, tree, useTmpl, kW, kH);
    if (!tex) {
      // null = the verbatim-inject string failed to compile (graceful path) OR a real regression.
      // The fixed body is known-good, so a null here is a genuine FAIL of the inject/recompile mechanic.
      std::printf("[selftest-field-customsdf] FAIL[%s]: renderField2d null (verbatim inject/recompile "
                  "broke, or compile failed)\n", caseName);
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
      std::printf("[selftest-field-customsdf] %-10s probe %-8s p=(% .4f,% .4f) got=% .6f "
                  "expected=% .6f diff=%.2e %s\n",
                  caseName, pr.name, px, py, got, pr.expected, diff, ok ? "OK" : "RED");
    }
    tex->release();
    return true;
  };

  // ---- Case A: verbatim body `return length(p - Offset) - A;` (the .t3-shaped sphere), A packed. ----
  // Proves: (1) the user string lands verbatim inside dCustom<id> and compiles; (2) Offset (vec3) and A
  // (scalar) packed params reach the function args. Probes in [-1,1] vs |p - Offset| - A.
  {
    const std::string body = "return length(p - Offset) - A;\n";
    uint32_t insidePx = pxFor(kOffX);   // p.x≈Offset.x -> near the center of the sphere -> ~ -A
    uint32_t surfacePx = pxFor(kOffX + kRad);  // p.x≈Offset.x+A on y≈0 row -> ~ 0 (surface)
    uint32_t outsidePx = pxFor(0.9f);   // p.x≈0.9 outside -> positive
    std::vector<Probe> probes = {
        {"inside", insidePx, cy, distSphere(pX(insidePx), pY(cy), kRad)},
        {"surface", surfacePx, cy, distSphere(pX(surfacePx), pY(cy), kRad)},
        {"outside", outsidePx, cy, distSphere(pX(outsidePx), pY(cy), kRad)},
    };
    runCase("verbatim", body, "", kRad, probes);
  }

  // ---- Case B: AdditionalDefines-driven radius. defines `#define MYRAD <kMyRad>`, body uses MYRAD. ----
  // Proves the AdditionalDefines string reaches the Definitions block (the #define resolves; if it did
  // NOT inject, the shader would fail to compile -> render null -> FAIL). A is set to a DIFFERENT value
  // (0.0) to prove the radius came from the define, not from A.
  {
    char defBuf[64];
    std::snprintf(defBuf, sizeof(defBuf), "#define MYRAD %.6f\n", kMyRad);
    const std::string defines = defBuf;
    const std::string body = "return length(p - Offset) - MYRAD;\n";
    uint32_t insidePx = pxFor(kOffX);
    uint32_t surfacePx = pxFor(kOffX + kMyRad);
    uint32_t outsidePx = pxFor(0.9f);
    std::vector<Probe> probes = {
        {"inside", insidePx, cy, distSphere(pX(insidePx), pY(cy), kMyRad)},
        {"surface", surfacePx, cy, distSphere(pX(surfacePx), pY(cy), kMyRad)},
        {"outside", outsidePx, cy, distSphere(pX(outsidePx), pY(cy), kMyRad)},
    };
    runCase("define", body, defines, 0.0f, probes);  // A=0.0 (unused by the body)
  }

  // ---- BOUNDARY-SIGN tooth (case A sphere): along the center row scanning +x from the center, the
  //   field flips sign at the sphere's right surface p.x = Offset.x + A = 0.6 (on y≈0, where p.y-Offset.y
  //   = 0.1 is small but nonzero — so the true crossing is where |p-Offset|=A, slightly past 0.6; we
  //   assert within a small tolerance band). Pins the texCoord->p map AND that A/Offset reached the body.
  //   Skipped under injectBug (the shifted field has no inside region). VALID per probe 鐵律: the sphere
  //   (radius 0.4 < 1, centered in [-1,1]) has a real on-screen negative interior.
  if (!injectBug) {
    const std::string body = "return length(p - Offset) - A;\n";
    std::shared_ptr<FieldNode> tree = buildTree(body, "", kRad);
    clearTexOpCache();
    MTL::Texture* tex = tree ? renderField2d(dev, q, tree, useTmpl, kW, kH) : nullptr;
    if (!tex) {
      std::printf("[selftest-field-customsdf] FAIL: boundary render null\n");
      rc = 1;
    } else {
      std::vector<float> buf((size_t)kW * kH, 0.0f);
      tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
      auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };
      // scan from the sphere center column to the right edge; first px where d>=0 is the crossing.
      const uint32_t cx0 = pxFor(kOffX);
      int crossPx = -1;
      for (uint32_t px = cx0; px < kW; ++px) {
        if (sampleAt(px, cy) >= 0.0f) { crossPx = (int)px; break; }
      }
      if (crossPx <= 0) {
        std::printf("[selftest-field-customsdf] FAIL: sphere boundary sign never flipped\n");
        rc = 1;
      } else {
        // True crossing on the cy row: |p - Offset| = A -> (p.x-Offx)^2 + (p.y-Offy)^2 = A^2 ->
        //   p.x = Offx + sqrt(A^2 - (p.y-Offy)^2). Compute it at the row's exact p.y for a tight band.
        const float pyRow = pY(cy);
        const float dy = pyRow - kOffY;
        const float trueCrossX = kOffX + std::sqrt(std::fmax(kRad * kRad - dy * dy, 0.0f));
        float crossPxField = pX((uint32_t)crossPx);
        float prevPxField = pX((uint32_t)(crossPx - 1));
        float texelW = 2.0f / kW;
        bool ok = (crossPxField >= trueCrossX - texelW) && (prevPxField <= trueCrossX + texelW);
        if (!ok) rc = 1;
        std::printf("[selftest-field-customsdf] boundary cross at px=%d p.x=% .4f (prev % .4f) "
                    "want≈%.4f texelW=%.4f %s\n",
                    crossPx, crossPxField, prevPxField, trueCrossX, texelW, ok ? "OK" : "RED");
      }
      tex->release();
    }
  }

  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-customsdf] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-customsdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-customsdf] PASS\n");
  return rc;
}

}  // namespace sw
