// field_ops_subdivpattern3d_golden — --selftest-field-subdivpattern3d. GPU VALUE golden for the
// SubDivPattern3d generate/texture COLOR field. The op writes ONLY f.rgb; the stock field_render
// template emits f.w into RED, so this golden uses the golden-LOCAL template variant that returns f.r
// (the Raster3dField golden precedent — production template untouched).
//
// ORACLE (independent-of-impl): a HOST C++ transcription of TiXL's ComputeSubdivision — transcribed
// from SubDivPattern3d.cs:53-131 + hash-functions.hlsl:115-123 (hash11u) / :4-6 (_PRIME defines), NOT
// from the sw MSL. Integer seed arithmetic is bit-exact on both sides (uint mul/xor/shift; the signed
// wrap is emulated via uint32 on the host — MSL ints are two's-complement). Float ops are the same
// IEEE float32 chain.
//
// PIXEL -> FIELD-SPACE p (identical to the template): p.x=(2*px+1)/W-1 ; p.y=1-(2*py+1)/H ; z=0.
//
// PROBE DISCIPLINE (plateau + margin scouted by the same mirror, W=H=128, params below):
//   All probes sit on SATURATED sGap plateaus (0 or 1) AND every data-dependent branch in their loop
//   walk has margin > 5e-3 (|hash*2-aspect|, |uv-phase|, |hash-Threshold|), so a few-ulp GPU/CPU float
//   wobble cannot flip a branch or slide into the feather band:
//     GAP   px=21,py=0  : d5=0.0092 << Padding-Feather=0.08 -> sGap=0 -> f.r = GapColor.r = 0.1
//     CELLA px=32,py=89 : d5=0.778, loop exhausted (step==3) -> splitF=1   -> f.r = ColorB.r = 0.8
//     CELLB px=50,py=40 : d5=0.411, Threshold break at step=1 -> splitF=1/3 -> f.r = 0.2+0.6/3 = 0.4
//   CELLA vs CELLB pins the SUBDIVISION LOOP itself (different break depths -> different splitF), not
//   just the shade line; GAP pins the Padding/Feather gap band; the trio pins all three lerp anchors.
//
// injectBug -> configureSubDivPattern3d(..., 1) swaps ColorA/ColorB in the OP'S REAL emitted param
// fill (not the expected values): CELLA reads 0.2 (want 0.8), CELLB reads 0.6 (want 0.4) -> RED.
//
// ZONE: shell tier (app/src/ root) — crosses runtime (renderField2d/makeFieldNode) + platform
// (compileLibraryFromSource); same rationale as field_ops_raster3dfield_golden.cpp.
#include "runtime/field_render.h"

#include <algorithm>
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
#include "runtime/field_node_registry.h"  // makeFieldNode (SubDivPattern3dNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource

namespace sw {

// Param-cook + test seam owned by field_ops_subdivpattern3d.cpp (leaf type TU-private).
void configureSubDivPattern3d(FieldNode& node, const float gapColor[4], const float colorA[4],
                              const float colorB[4], float splitPosition, float splitVariation,
                              float padding, int useAspectForSplit, int maxSubdivisions, int colorMode,
                              int randomSeed, float threshold, float feather, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;

// Golden params: .t3 defaults except distinct .r anchors (Gap 0.1 / A 0.2 / B 0.8), Padding=0.1
// (wider gap band), MaxSubdivisions=3 (short exact loop; CELLB still breaks earlier at step=1).
constexpr float kGap[4] = {0.1f, 0.0f, 0.0f, 1.0f};
constexpr float kCA[4] = {0.2f, 0.0f, 0.0f, 1.0f};
constexpr float kCB[4] = {0.8f, 0.0f, 0.0f, 1.0f};
constexpr float kSplitPos = 0.5f, kSplitVar = 0.8f, kPadding = 0.1f;
constexpr int kUseAspect = 1, kMaxSub = 3, kColorMode = 1, kSeed = 42;
constexpr float kThreshold = 0.2f, kFeather = 0.02f;

std::string loadTemplateSdp() {
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

std::string patchTemplateForColorSdp(std::string tmpl) {
  const std::string from = "return float4(f.w, 0.0, 0.0, 1.0);";
  const std::string to = "return float4(f.r, 0.0, 0.0, 1.0);";
  auto pos = tmpl.find(from);
  if (pos != std::string::npos) tmpl.replace(pos, from.size(), to);
  return tmpl;
}

float pXsdp(uint32_t px) { return (2.0f * px + 1.0f) / kW - 1.0f; }
float pYsdp(uint32_t py) { return 1.0f - (2.0f * py + 1.0f) / kH; }

// ---- HOST ORACLE: transcribed from TiXL (SubDivPattern3d.cs:53-131 + hash-functions.hlsl) ----------
float oracleHash11u(uint32_t x) {  // hash-functions.hlsl:115-123 (verbatim; _PRIME0 = 13331U)
  const uint32_t k = 1103515245U;
  x *= 13331U;
  x = ((x >> 8U) ^ x) * k;
  x = ((x >> 8U) ^ x) * k;
  return (float)x * (1.0f / (float)0xffffffffU);
}
float oracleMod1(float x) { return x - std::floor(x); }  // Common's floored mod, y=1
// signed wrap helper (MSL int mul wraps two's-complement; host emulates via uint32 to stay defined)
int32_t wmul(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }

// f.r at field-space (px,py) — the .cs walk with this golden's params. Mirrors .cs line-for-line.
float oracleSubDivR(float px, float py) {
  const int steps = std::clamp(kMaxSub, 1, 30);                          // :55
  float uvx = oracleMod1(px), uvy = oracleMod1(py);                      // :57
  const int cellIdx = (int)(px + 242.0f), cellIdy = (int)(py + 1241.0f); // :61
  int mainSeed = kSeed + cellIdx * 2 + wmul(cellIdy + 3, 12311);         // :62
  const float mainHash = oracleHash11u((uint32_t)mainSeed);              // :63
  float sizex = 1.0f, sizey = 1.0f;                                      // :65
  float phase = (mainHash - 0.5f) * kSplitVar + kSplitPos;               // :66
  int seedInCell = mainSeed + kSeed;                                     // :67
  int step;
  for (step = 0; step < steps; ++step) {                                 // :70
    const float aspect = kUseAspect == 1 ? sizex / sizey : 1.0f;         // :72
    if (oracleHash11u((uint32_t)seedInCell) * 2 < aspect) {              // :75
      if (uvx < phase) {                                                 // :77
        uvx /= phase; sizex *= phase;
        mainSeed += (int)(phase + 2123u);                                // :81
        seedInCell = wmul(seedInCell, 2);
      } else {
        uvx = (uvx - phase) / (1 - phase); sizex *= (1 - phase);         // :86-87
        mainSeed = (int)((uint32_t)(mainSeed + 213u) % 1251u);           // :88
        seedInCell = wmul(seedInCell, 3);
      }
    } else {
      if (uvy < phase) {                                                 // :97
        uvy /= phase; sizey *= phase;
        mainSeed = (int)((uint32_t)(mainSeed + 98777177u) % 1345777u);   // :101 (_PRIME2 % _PRIME1)
        seedInCell = wmul(seedInCell, 5);
      } else {
        uvy = (uvy - phase) / (1 - phase); sizey *= (1 - phase);         // :106-107
        mainSeed = (int)((uint32_t)(mainSeed + 1345777u) % 98777177u);   // :108 (_PRIME1 % _PRIME2)
        seedInCell = wmul(seedInCell, 7);
      }
    }
    const float hash = oracleHash11u((uint32_t)seedInCell);              // :114
    phase = (mainHash - 0.5f) * kSplitVar + kSplitPos;                   // :115
    if (hash <= kThreshold) break;                                       // :117-118
  }
  const float splitF =
      kColorMode == 0 ? oracleHash11u((uint32_t)mainHash) : step / (float)steps;  // :121
  const float ddx = (uvx - 0.5f) * sizex, ddy = (uvy - 0.5f) * sizey;    // :123
  const float d4x = sizex - std::fabs(ddx * 2), d4y = sizey - std::fabs(ddy * 2);  // :124
  const float d5 = std::min(d4x, d4y);                                   // :126
  const float e0 = kPadding - kFeather, e1 = kPadding + kFeather;        // :127
  float sGap;
  if (d5 <= e0) sGap = 0.0f;
  else if (d5 >= e1) sGap = 1.0f;
  else { float t = (d5 - e0) / (e1 - e0); sGap = t * t * (3.0f - 2.0f * t); }
  const float cellR = kCA[0] + (kCB[0] - kCA[0]) * splitF;               // :129 inner lerp .r
  return kGap[0] + (cellR - kGap[0]) * sGap;                             // :128-130 outer lerp .r
}

std::shared_ptr<FieldNode> buildSdpTree(int injectBug) {
  std::shared_ptr<FieldNode> node = makeFieldNode("SubDivPattern3d", "golden0");
  if (!node) return nullptr;
  configureSubDivPattern3d(*node, kGap, kCA, kCB, kSplitPos, kSplitVar, kPadding, kUseAspect, kMaxSub,
                           kColorMode, kSeed, kThreshold, kFeather, injectBug);
  return node;
}

struct ProbeSdp { const char* name; uint32_t px, py; };

}  // namespace

int runFieldSubDivPattern3dGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  std::string tmpl = loadTemplateSdp();
  if (tmpl.empty()) {
    std::printf("[selftest-field-subdivpattern3d] FAIL: could not load field template\n");
    pool->release();
    return 1;
  }
  tmpl = patchTemplateForColorSdp(tmpl);  // read f.r, not f.w

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-subdivpattern3d] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  int rc = 0;
  const float kTol = 1e-5f;  // plateau probes: exact lerp anchors on both sides

  std::shared_ptr<FieldNode> tree = buildSdpTree(injectBug ? 1 : 0);
  if (!tree) {
    std::printf("[selftest-field-subdivpattern3d] FAIL: SubDivPattern3d factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-subdivpattern3d] FAIL: renderField2d null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  tex->release();

  // Probes scouted by this oracle (header PROBE DISCIPLINE): plateau + all-branch margin > 5e-3.
  const ProbeSdp probes[] = {
      {"gap", 21, 0},      // sGap=0  -> GapColor.r = 0.1
      {"cellA", 32, 89},   // sGap=1, loop exhausted (splitF=1)   -> ColorB.r = 0.8
      {"cellB", 50, 40},   // sGap=1, Threshold break at step=1 (splitF=1/3) -> 0.4
  };
  for (const ProbeSdp& pr : probes) {
    const float fx = pXsdp(pr.px), fy = pYsdp(pr.py);
    const float expected = oracleSubDivR(fx, fy);
    const float got = buf[(size_t)pr.py * kW + pr.px];
    const float diff = std::fabs(got - expected);
    const bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-subdivpattern3d] probe %-5s p=(% .4f,% .4f) got=% .6f "
                "expected=% .6f diff=%.2e %s\n",
                pr.name, fx, fy, got, expected, diff, ok ? "OK" : "RED");
  }

  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-subdivpattern3d] injectBug did not trip any probe\n");
      return 0;  // dead tooth -> exit 0 so --bite NO-BITE catches it (GOLDEN_STANDARD P1)
    }
    std::printf("[selftest-field-subdivpattern3d] injectBug correctly RED (ColorA/ColorB swapped in "
                "the REAL emitted fill -> both cell probes diverged)\n");
    return 1;
  }
  std::printf("[selftest-field-subdivpattern3d] %s\n", rc == 0 ? "PASS" : "FAIL");
  return rc;
}

}  // namespace sw
