// field_ops_raster3dfield_golden — --selftest-field-raster3dfield. GPU VALUE golden for the
// Raster3dField generate/texture COLOR field. Raster3dField writes ONLY f.rgb (no f.w); the stock
// field_render template emits f.w into RED, so this golden uses a golden-LOCAL template variant whose
// fragment returns f.r (the op's authored color channel) — the production template is untouched.
//
// Builds a single registered Raster3dField leaf (makeFieldNode + configureRaster3dField), assembles via
// the FROZEN field rail, compiles the f.r-reading template, renders R32Float, reads back, and asserts
// each probe's R == the closed-form raster value at the texel's EXACT p (z=0).
//
// ZONE: shell tier (app/src/ root) — crosses runtime (renderField2d/makeFieldNode/configure) + platform
// (compileLibraryFromSource); a runtime selftest may not include platform (check_arch). Same rationale
// as field_ops_combinefieldcolor_golden.cpp.
//
// PIXEL -> FIELD-SPACE p (identical to the template / field_render_golden.cpp):
//   p.x = (2*px+1)/W - 1 ; p.y = 1 - (2*py+1)/H ; p.z = 0 ; p.w = 0.
//
// CLOSED-FORM (Raster3dField.cs:34-51, with Offset=(0,0,0.5), Scale=(1,1,1)):
//   q   = mod(p/Scale - Offset, 1) - 0.5          (mod(x,1) = x - floor(x))
//       qx = frac(px) - 0.5 ; qy = frac(py) - 0.5 ; qz = mod(0 - 0.5, 1) - 0.5 = 0
//   d   = vmax(abs(q)) = max(|qx|, |qy|, |qz|) = max(|qx|, |qy|)   (|qz|=0)
//   line= smoothstep(lw/2 + feather, lw/2 - feather, d)   (REVERSED edges -> descending)
//   f.r = lerp(ColorA.r, ColorB.r, line)
//
// WHY Offset.z=0.5 (live, not degenerate): with the default Offset.z=0 and p.z=0, qz = mod(0,1)-0.5 =
// -0.5 ALWAYS, so vmax pins d>=0.5 everywhere -> line==0 at every texel -> f.r==ColorA.r flat (the op's
// xy raster is invisible). Offset.z=0.5 makes qz=0, freeing d to track the xy cell — the raster lives.
//
// PROBE DISCIPLINE (d=0 SATURATED plateau, NOT the feather edge): lw=0.9 -> lw/2=0.45, feather=0.002 ->
// the smoothstep transitions in d∈[0.448,0.452]. Both probes sit FAR from that band so line is exactly
// 0 or 1 (machine-robust, no fwidth/feather sensitivity):
//   CELL-EDGE probe near p=(0,0): frac(0)=0 -> qx=qy=-0.5 -> d=0.5 > 0.452 -> line=0 -> f.r=ColorA.r.
//   CELL-CENTER probe near p=(0.5,0.5): frac(.5)=.5 -> qx=qy=0 -> d=0 < 0.448 -> line=1 -> f.r=ColorB.r.
//   (Expected is recomputed at the EXACT texel p, robust to the half-texel offset, like the combine golden.)
//
// COLORS: ColorA.r=0.2, ColorB.r=0.8 (distinct so the lerp endpoint each probe lands on is discriminating).
//
// injectBug: configureRaster3dField(..., injectBug) corrupts the OP'S REAL postShaderCode emit —
//   1 = swap ColorA/ColorB endpoints (lerp flips): the edge probe then reads ColorB.r=0.8 != 0.2 -> RED,
//       the center probe reads ColorA.r=0.2 != 0.8 -> RED.
//   The tooth bites the op's REAL emit (the swapped lerp), NOT the expected value (no tautology).
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
#include "runtime/field_node_registry.h"  // makeFieldNode (Raster3dFieldNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource

namespace sw {

// Param-cook + test seam owned by field_ops_raster3dfield.cpp (leaf type TU-private). Forward-declared
// (no header), as selftests forward-declare golden entry points.
void configureRaster3dField(FieldNode& node, float caR, float caG, float caB, float caA, float cbR,
                            float cbG, float cbB, float cbA, float ox, float oy, float oz, float sx,
                            float sy, float sz, float lineWidth, float feather, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;

// Op params for this golden (Offset.z=0.5 frees the xy raster; distinct ColorA.r/ColorB.r).
constexpr float kCAr = 0.2f, kCBr = 0.8f;
constexpr float kOffZ = 0.5f, kScale = 1.0f, kLW = 0.9f, kFeather = 0.002f;

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

// Golden-local template variant: the stock fragment returns float4(f.w,0,0,1); Raster3dField writes
// f.rgb (never f.w). Swap the return so RED carries f.r (the op's authored channel). Production template
// untouched (we mutate only this golden's in-memory copy).
std::string patchTemplateForColor(std::string tmpl) {
  const std::string from = "return float4(f.w, 0.0, 0.0, 1.0);";
  const std::string to = "return float4(f.r, 0.0, 0.0, 1.0);";
  auto pos = tmpl.find(from);
  if (pos != std::string::npos) tmpl.replace(pos, from.size(), to);
  return tmpl;
}

float pX(uint32_t px) { return (2.0f * px + 1.0f) / kW - 1.0f; }
float pY(uint32_t py) { return 1.0f - (2.0f * py + 1.0f) / kH; }

// Host floored mod (mod(x,1) = x - floor(x)).
float modf1(float x) { return x - std::floor(x); }

// Host closed-form f.r at a texel (z=0). Mirrors fRaster3d + the lerp endpoint.
float rasterR(float px, float py) {
  const float qx = modf1(px / kScale - 0.0f) - 0.5f;   // Offset.x=0
  const float qy = modf1(py / kScale - 0.0f) - 0.5f;   // Offset.y=0
  const float qz = modf1(0.0f / kScale - kOffZ) - 0.5f;  // p.z=0, Offset.z=0.5 -> qz=0
  const float d = std::fmax(std::fmax(std::fabs(qx), std::fabs(qy)), std::fabs(qz));
  // smoothstep(edge0,edge1,x) with edge0 > edge1 (reversed): below edge1 -> 1, above edge0 -> 0.
  const float e0 = kLW / 2.0f + kFeather;  // 0.452
  const float e1 = kLW / 2.0f - kFeather;  // 0.448
  float line;
  if (d >= e0)
    line = 0.0f;
  else if (d <= e1)
    line = 1.0f;
  else {
    float t = (d - e0) / (e1 - e0);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    line = t * t * (3.0f - 2.0f * t);
  }
  return kCAr + line * (kCBr - kCAr);  // lerp(ColorA.r, ColorB.r, line)
}

std::shared_ptr<FieldNode> buildTree(int injectBug) {
  std::shared_ptr<FieldNode> node = makeFieldNode("Raster3dField", "golden0");
  if (!node) return nullptr;
  // ColorA=(0.2,?,?,1), ColorB=(0.8,?,?,1); Offset=(0,0,0.5); Scale=(1,1,1); LineWidth/Feather defaults.
  configureRaster3dField(*node, kCAr, 0.3f, 0.4f, 1.0f, kCBr, 0.7f, 0.6f, 1.0f, 0.0f, 0.0f, kOffZ,
                         kScale, kScale, kScale, kLW, kFeather, injectBug);
  return node;
}

struct Probe { const char* name; uint32_t px, py; float expected; };

}  // namespace

int runFieldRaster3dFieldGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-raster3dfield] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }
  tmpl = patchTemplateForColor(tmpl);  // read f.r, not f.w

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-raster3dfield] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  const int bugMode = injectBug ? 1 : 0;
  const float kTol = 1e-5f;
  int rc = 0;

  std::shared_ptr<FieldNode> tree = buildTree(bugMode);
  if (!tree) {
    std::printf("[selftest-field-raster3dfield] FAIL: Raster3dField factory not registered\n");
    pool->release();
    return 1;
  }
  clearTexOpCache();
  MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-raster3dfield] FAIL: renderField2d null (compile/PSO failure)\n");
    q->release();
    dev->release();
    pool->release();
    return 1;
  }
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

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

  // CELL-EDGE near p=(0,0): d=0.5 plateau -> line=0 -> ColorA.r=0.2.
  uint32_t ex = pxFor(0.0f), ey = pyFor(0.0f);
  // CELL-CENTER near p=(0.5,0.5): d=0 plateau -> line=1 -> ColorB.r=0.8.
  uint32_t cx = pxFor(0.5f), cy = pyFor(0.5f);

  std::vector<Probe> probes = {
      {"edge", ex, ey, rasterR(pX(ex), pY(ey))},
      {"center", cx, cy, rasterR(pX(cx), pY(cy))},
  };

  for (Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - pr.expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-raster3dfield] probe %-7s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, pr.expected, diff, ok ? "OK" : "RED");
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-raster3dfield] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-raster3dfield] injectBug correctly RED\n");
    return 1;
  }
  std::printf("[selftest-field-raster3dfield] %s\n", rc == 0 ? "PASS" : "FAIL");
  return rc;
}

}  // namespace sw
