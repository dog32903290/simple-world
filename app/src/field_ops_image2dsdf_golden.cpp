// field_ops_image2dsdf_golden — --selftest-field-image2dsdf. GPU DISTANCE-VALUE golden for the
// Image2dSDF field leaf — the FIRST field op that BINDS A TEXTURE (it rides the texture-into-field
// "Seam A": FieldNode::collectTextures + the template's /*{TEXTURES}*/ / /*{TEXTURE_PARAMS}*/ /
// /*{TEXTURE_ARGS}*/ hooks + field_render's setFragmentTexture loop). It host-builds a small constant
// R32Float texture, binds it through configureImage2dSdf (the cook seam standing in for the deferred
// Seam B image port), assembles the field MSL via the FROZEN base, runtime-compiles it, renders a
// fullscreen pass, reads back the R32Float distance texture, and asserts each probed texel's RED ==
// the closed-form sdf2DColumn value. Mirrors field_ops_customsdf_golden.cpp's harness.
//
// ZONE: shell tier (lives at app/src/ root like field_render_golden.cpp / selftests.cpp / main.cpp).
// It deliberately crosses runtime (renderField2d, makeFieldNode, configureImage2dSdf) AND platform
// (compileLibraryFromSource) — a runtime-zone selftest may NOT include platform (check_arch:
// runtime ↛ platform), so this integration golden sits at the shell tier (same rationale as
// field_render_golden.cpp's header). It also names MTL directly to author the input texture.
//
// WHY A CONSTANT TEXTURE (determinism): the input is filled with a single known value V everywhere, so
// sdf2DColumn is a CLOSED FORM independent of which texel the (nearest) sampler lands on:
//   in-bounds  (uv in [0,1]): texDist = (1 - saturate(V)) * sdfScale ; f.w = texDist + Offset
//   out-bounds (uv outside [0,1]): texDist same (constant tex); delta = uv - clamp(uv,0,1);
//     worldDelta = delta * imageSize; outsideDist = length(worldDelta);
//     f.w = sqrt(outsideDist^2 + texDist^2) + Offset
// The template's clampedSampler is NEAREST, so even the in-bounds sample reads the EXACT authored texel
// (no bilinear blur) — but with a constant texture every texel is V, so the golden is reproducible
// across machines regardless of texel-center alignment. Texture content is host-authored HERE (version-
// controlled, no external asset).
//
// PIXEL -> FIELD-SPACE p (backward-traced, identical to field_render_golden.cpp / the template):
//   p.x = (2*px+1)/W - 1 ; p.y = 1 - (2*py+1)/H ; p.z = 0 ; p.w = 0. The golden reads each texel's
// EXACT p and re-derives uv -> the closed-form value at that p (robust to the half-texel offset).
//
// CASE A (in-bounds): imageSize = (4,4) so the WHOLE [-1,1] field window maps to uv in [0.25,0.75]
//   (always in-bounds). Every probe takes the in-bounds branch -> f.w = (1-V)*scale + offset, a
//   constant. Pins the texture SAMPLE + the in-bounds branch + Size/Scale/Offset params reaching the
//   call. CASE B (out-of-bounds): imageSize = (0.5,0.5) so |p| > 0.25 maps uv outside [0,1] -> the
//   out-of-bounds branch (sqrt of squared edge-delta + texDist). Pins the edge branch + the worldDelta
//   geometry. A center probe (p≈0, uv≈0.5 in-bounds) is kept as the in-bounds control in case B too.
//
// injectBug: corrupt the template's RED-channel distance write so every cooked distance is shifted by
// +1.0 -> all VALUE probes RED (same technique/tier/magnitude as field_ops_customsdf_golden.cpp). This
// proves the value teeth bite cooked pixels, not a blind pass. (A separate failure mode — the texture
// failing to bind / the texture hook breaking -> sdf2DColumn samples garbage or the source fails to
// compile -> renderField2d null or wrong values -> the case FAILs loudly — is exercised implicitly: the
// fixed texture is good, so a regression in the Seam A bind/hook path turns the render null or shifts
// every probe.)
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
#include "runtime/field_node_registry.h"  // makeFieldNode (Image2dSDFNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache (fresh source-PSO cache per run-device)

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {

// Param-cook seam owned by field_ops_image2dsdf.cpp (the leaf type is TU-private). Forward-declared
// here (no header) exactly as selftests forward-declare the golden entry points. `texture` is an
// opaque MTL::Texture* (passed as void* across the seam — runtime stays pure-compute).
void configureImage2dSdf(FieldNode& node, const void* texture, float sizeX, float sizeY, float scale,
                         float offset);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr uint32_t kTexN = 8;        // 8x8 host-authored R32Float input texture
constexpr float kTexVal = 0.25f;     // constant authored texel value V (in [0,1] so saturate is a no-op)
constexpr float kScale = 0.8f;       // sdfScale (multiplies in-bounds texDist)
constexpr float kOffset = 0.1f;      // Offset (added to the whole column distance)

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

// Host re-derivation of sdf2DColumn for a CONSTANT texture (value kTexVal). pos = (px_field, py_field),
// imageSize = (sx, sy). Mirrors ExecuteImage2dSdf.cs sdf2DColumn byte-for-byte (constant tex -> texDist
// is uv-independent). Returns f.w = column + offset.
float hostColumn(float posX, float posY, float sx, float sy, float scale, float offset) {
  float uvx = posX / sx;
  float uvy = posY / sy;
  uvy *= -1.0f;
  uvx += 0.5f;
  uvy += 0.5f;
  float cuvx = std::fmin(std::fmax(uvx, 0.0f), 1.0f);
  float cuvy = std::fmin(std::fmax(uvy, 0.0f), 1.0f);
  float dx = uvx - cuvx, dy = uvy - cuvy;
  float satV = std::fmin(std::fmax(kTexVal, 0.0f), 1.0f);  // saturate(V)
  float texDist = (1.0f - satV) * scale;
  float wdx = dx * sx, wdy = dy * sy;
  float outsideDist = std::sqrt(wdx * wdx + wdy * wdy);
  bool inBounds = (uvx >= 0.0f && uvy >= 0.0f && uvx <= 1.0f && uvy <= 1.0f);
  float column = inBounds ? texDist : std::sqrt(outsideDist * outsideDist + texDist * texDist);
  return column + offset;
}

struct Probe { const char* name; uint32_t px, py; float expected; };

// Build an 8x8 R32Float texture filled with a constant value (host-authored, reproducible). Returns an
// OWNED MTL::Texture* (caller release()s). StorageModeManaged-equivalent via replaceRegion upload.
MTL::Texture* makeConstTexture(MTL::Device* dev, float value) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatR32Float, kTexN, kTexN,
                                                  /*mipmapped=*/false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);  // CPU-writable, GPU-readable (unified memory)
  MTL::Texture* tex = dev->newTexture(td);
  if (!tex) return nullptr;
  std::vector<float> data((size_t)kTexN * kTexN, value);
  tex->replaceRegion(MTL::Region::Make2D(0, 0, kTexN, kTexN), 0, data.data(), kTexN * sizeof(float));
  return tex;
}

}  // namespace

int runFieldImage2dSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-image2dsdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-image2dsdf] FAIL: no Metal device\n");
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
      std::printf("[selftest-field-image2dsdf] FAIL: injectBug could not find the distance-write site "
                  "in the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  MTL::Texture* inputTex = makeConstTexture(dev, kTexVal);
  if (!inputTex) {
    std::printf("[selftest-field-image2dsdf] FAIL: could not author input texture\n");
    q->release(); dev->release(); pool->release();
    return 1;
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

  auto buildTree = [&](float sx, float sy) -> std::shared_ptr<FieldNode> {
    std::shared_ptr<FieldNode> node = makeFieldNode("Image2dSDF", "golden0");
    if (!node) return nullptr;
    configureImage2dSdf(*node, inputTex, sx, sy, kScale, kOffset);
    return node;
  };

  auto runCase = [&](const char* caseName, float sx, float sy,
                     std::vector<Probe>& probes) -> bool {
    std::shared_ptr<FieldNode> tree = buildTree(sx, sy);
    if (!tree) {
      std::printf("[selftest-field-image2dsdf] FAIL[%s]: Image2dSDF factory not registered\n", caseName);
      rc = 1;
      return false;
    }
    clearTexOpCache();  // each case is the same source but be safe across run-devices
    MTL::Texture* tex = renderField2d(dev, q, tree, useTmpl, kW, kH);
    if (!tex) {
      // null = the Seam A texture-bind/hook path broke (the source failed to compile or PSO failed) OR
      // a real regression. The fixed texture is known-good, so a null here is a genuine FAIL.
      std::printf("[selftest-field-image2dsdf] FAIL[%s]: renderField2d null (texture seam/compile "
                  "broke)\n", caseName);
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
      std::printf("[selftest-field-image2dsdf] %-10s probe %-8s p=(% .4f,% .4f) got=% .6f "
                  "expected=% .6f diff=%.2e %s\n",
                  caseName, pr.name, px, py, got, pr.expected, diff, ok ? "OK" : "RED");
    }
    tex->release();
    return true;
  };

  // ---- Case A: imageSize=(4,4) -> WHOLE [-1,1] window in-bounds. f.w = (1-V)*scale + offset (const). --
  // Proves: the texture is SAMPLED (constant V reaches texDist), the in-bounds branch runs, and
  // Size/Scale/Offset packed params reach the call. Three probes across the row all read the constant.
  {
    const float sx = 4.0f, sy = 4.0f;
    uint32_t leftPx = pxFor(-0.6f);
    uint32_t midPx = pxFor(0.0f);
    uint32_t rightPx = pxFor(0.6f);
    std::vector<Probe> probes = {
        {"in_left", leftPx, cy, hostColumn(pX(leftPx), pY(cy), sx, sy, kScale, kOffset)},
        {"in_mid", midPx, cy, hostColumn(pX(midPx), pY(cy), sx, sy, kScale, kOffset)},
        {"in_right", rightPx, cy, hostColumn(pX(rightPx), pY(cy), sx, sy, kScale, kOffset)},
    };
    runCase("inbounds", sx, sy, probes);
  }

  // ---- Case B: imageSize=(0.5,0.5) -> |p|>0.25 maps uv OUTSIDE [0,1] (out-of-bounds branch). --------
  // Pins the edge branch (sqrt(outsideDist^2 + texDist^2) + offset) and the worldDelta geometry. The
  // center probe (p≈0) stays in-bounds as a control (uv≈0.5). The far probes exercise the OOB sqrt.
  {
    const float sx = 0.5f, sy = 0.5f;
    uint32_t ctrlPx = pxFor(0.0f);    // uv≈0.5 -> in-bounds control
    uint32_t oobLeftPx = pxFor(-0.8f); // uv far below 0 -> OOB
    uint32_t oobRightPx = pxFor(0.8f); // uv far above 1 -> OOB
    std::vector<Probe> probes = {
        {"oob_ctrl", ctrlPx, cy, hostColumn(pX(ctrlPx), pY(cy), sx, sy, kScale, kOffset)},
        {"oob_left", oobLeftPx, cy, hostColumn(pX(oobLeftPx), pY(cy), sx, sy, kScale, kOffset)},
        {"oob_right", oobRightPx, cy, hostColumn(pX(oobRightPx), pY(cy), sx, sy, kScale, kOffset)},
    };
    runCase("outbounds", sx, sy, probes);
  }

  inputTex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-image2dsdf] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-image2dsdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-image2dsdf] PASS\n");
  return rc;
}

}  // namespace sw
