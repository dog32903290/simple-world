// PickColorFromImage op (Texture2D → host vec4 color: CPU eyedropper on a cooked image).
// TiXL authority: Operators/Lib/numbers/color/PickColorFromImage.cs (verbatim mapping below)
//   + Core/DataTypes/Vector/Color.cs (byte→float normalize, cs:39-47) + PickColorFromImage.t3
//   (DefaultValues: Position=(0,0), InputImage=null, AlwaysUpdate=false).
//
//   PickColorFromImage.cs Update() (cs:19-155):
//     if (inputImage == null) return;                                     // cs:26-29 (no write)
//     column = clamp((int)(pos.X * width),  0, width-1);                  // cs:36
//     row    = clamp((int)(pos.Y * height), 0, height-1);                 // cs:37
//     if (alwaysUpdate || no staging copy || Description changed)         // cs:40-46
//         re-copy the WHOLE image to the CPU-readable staging copy;       // cs:48-65
//         (else the STALE cached copy is read — the TiXL staleness quirk is load-bearing)
//     column %= width; row %= height;                                     // cs:70-71 (no-op after clamp)
//     switch (Format):                                                    // cs:82-146
//       R8G8B8A8_UNorm      → Color(Byte4) = bytes * 1/255 (Color.cs:39-47, NORMALIZED)
//       R16G16B16A16_Float  → 4× half → float (raw values)
//       R16G16B16A16_UNorm  → the HIGH byte of each 16-bit channel, RAW 0..255 (cs:111-123 —
//                             TiXL does NOT normalize this case; the quirk is reproduced verbatim)
//       R32G32B32A32_Float  → 4× float (raw)
//       default             → Log.Warning + Color.White = (1,1,1,1)      // cs:142-145
//     Output.Value = color;                                              // cs:148
//
// COOK PLACEMENT (the point-into-frame family precedent, resident_point_value_output_cook /
// cookPointValueFromGraph): PickColorFromImage needs a TEXTURE into this frame, so its production
// pass runs AFTER pg.cookResident (frame_cook.cpp:391 → cook_host_values.cpp cookPointValueFromGraph),
// resolving the upstream texture through a TexAccessor over PointGraph::residentTexFor. Output rides
// extOut[0..3] (Output.x/.y/.z/.w = spec port indices 0..3 — outputs FIRST, the !evaluate readback
// returns extOut[full spec index]).
//
// FORKS (named):
//   - fork-pickcolorfromimage-vec4-as-4-floats: Output = Slot<Vector4> → 4 Float output ports
//     (family convention, PickColorFromList precedent).
//   - fork-pickcolorfromimage-value-one-frame-late: the pass runs after this frame's texture cook, so
//     a DOWNSTREAM Float consumer of the picked color resolves it on the NEXT frame's pull (extOut
//     persists on the resident graph) — the same placement trade the PointToMatrix family accepted.
//   - fork-pickcolorfromimage-unwired-zero: TiXL's `return` keeps the PREVIOUS Output.Value; sw's
//     extOut keeps its prior value too (zeros on a fresh graph) — observable only on the unwired edge.
//   - fork-pickcolorfromimage-resident-only: no flat-rail cook (the point-value-output family
//     precedent — production is resident; the flat rail never cooks this op).
//   - fork-pickcolorfromimage-miplevels-ignored: the staging-copy desc compare drops MipLevels (sw
//     copies level 0 only; TiXL compares it but reads subresource 0 the same way).
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <simd/simd.h>

#include "runtime/compound_graph.h"       // Symbol/SymbolChild/SymbolLibrary (golden graph build)
#include "runtime/eval_context.h"         // EvaluationContext (golden cook ctx)
#include "runtime/graph.h"                // NodeSpec / PortSpec / Widget / findSpec
#include "runtime/graph_bridge.h"         // atomicSymbolFromSpec (golden)
#include "runtime/point_graph.h"          // PointGraph (golden: cook the checker + residentTexFor)
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / ResidentInput / resolveResidentFloatInputs
#include "runtime/value_op_registry.h"    // ValueOp self-registration + valueOpSelfTests()

namespace sw {

int runPickColorFromImageSelfTest(bool injectBug);

// Test-only injection seam (golden): when set, the R8G8B8A8 decode DROPS the Color(Byte4) 1/255
// normalization (Color.cs:39-47) — the picked color reads raw 0..255 bytes, corrupting the REAL
// production output on the actual cook path (NOT an expected-value flip). Off in production.
// Mirror of pickColorInjectBug() / hostScalarInjectBug().
bool& pickColorImageInjectBug() {
  static bool b = false;
  return b;
}

namespace {

// IEEE 754 half → float (bit-exact; the R16G16B16A16_Float case's BitConverter.ToHalf analog).
float halfToFloat(uint16_t h) {
  const uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
  uint32_t exp = (h >> 10) & 0x1Fu;
  uint32_t man = h & 0x3FFu;
  uint32_t bits;
  if (exp == 0) {
    if (man == 0) {
      bits = sign;  // ±0
    } else {  // subnormal half → normalized float
      int e = -1;
      do { man <<= 1; ++e; } while (!(man & 0x400u));
      man &= 0x3FFu;
      bits = sign | (uint32_t)(127 - 15 - e) << 23 | (man << 13);
    }
  } else if (exp == 31) {
    bits = sign | 0x7F800000u | (man << 13);  // Inf/NaN
  } else {
    bits = sign | ((exp - 15 + 127) << 23) | (man << 13);
  }
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

// The CPU-readable image copy (= TiXL's _imageWithCpuAccess staging texture, cs:158). Cached per
// resident node path; refreshed per the cs:40-46 rule (alwaysUpdate || first || desc changed).
struct CachedImage {
  uint32_t w = 0, h = 0;
  uint64_t fmt = 0;  // MTL::PixelFormat raw
  uint32_t bpp = 0;
  std::vector<uint8_t> bytes;  // level 0, rowBytes = w*bpp
};
std::map<std::string, CachedImage>& imageCacheStore() {
  static std::map<std::string, CachedImage> s;  // process-lifetime (residentFloatListState precedent)
  return s;
}

uint32_t bytesPerPixel(MTL::PixelFormat f) {
  switch (f) {
    case MTL::PixelFormatRGBA8Unorm: return 4;
    case MTL::PixelFormatRGBA16Float: return 8;
    case MTL::PixelFormatRGBA16Unorm: return 8;
    case MTL::PixelFormatRGBA32Float: return 16;
    default: return 0;  // unhandled → the cs:142-145 White fallback (no copy needed)
  }
}

// Decode ONE texel from the cached copy per the TiXL format switch (cs:82-146).
simd::float4 decodeTexel(const CachedImage& img, int col, int row) {
  const size_t base = ((size_t)row * img.w + (size_t)col) * img.bpp;
  const uint8_t* p = img.bytes.data() + base;
  switch ((MTL::PixelFormat)img.fmt) {
    case MTL::PixelFormatRGBA8Unorm: {  // Color(Byte4): bytes * 1/255 (Color.cs:39-47)
      const float mult = pickColorImageInjectBug() ? 1.0f : (1.0f / 255.0f);  // bug: DROP normalize
      return simd::make_float4(p[0] * mult, p[1] * mult, p[2] * mult, p[3] * mult);
    }
    case MTL::PixelFormatRGBA16Float: {  // 4× BitConverter.ToHalf (cs:93-108)
      uint16_t hv[4];
      std::memcpy(hv, p, 8);
      return simd::make_float4(halfToFloat(hv[0]), halfToFloat(hv[1]), halfToFloat(hv[2]),
                               halfToFloat(hv[3]));
    }
    case MTL::PixelFormatRGBA16Unorm:  // TiXL quirk (cs:111-123): HIGH byte per channel, RAW 0..255
      return simd::make_float4((float)p[1], (float)p[3], (float)p[5], (float)p[7]);
    case MTL::PixelFormatRGBA32Float: {  // 4× raw float (cs:125-139)
      float fv[4];
      std::memcpy(fv, p, 16);
      return simd::make_float4(fv[0], fv[1], fv[2], fv[3]);
    }
    default:
      return simd::make_float4(1.0f, 1.0f, 1.0f, 1.0f);  // unreachable (no copy cached)
  }
}

}  // namespace

// The PRODUCTION cook pass (point-into-frame slot — called from cook_host_values.cpp's
// cookPointValueFromGraph AFTER pg.cookResident, with texFor = PointGraph::residentTexFor).
// Walks the resident graph, resolves each PickColorFromImage's upstream texture through its
// InputImage Connection driver, applies the cs:19-155 pick, writes extOut[0..3].
void cookPickColorFromImageNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx,
                                 const std::function<MTL::Texture*(const std::string&)>& texFor) {
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "PickColorFromImage") continue;

    // inputImage == null → return WITHOUT writing (cs:26-29; fork-pickcolorfromimage-unwired-zero).
    MTL::Texture* tex = nullptr;
    if (const ResidentInput* ri = rn.input("InputImage"))
      if (ri->driver == ResidentInput::Driver::Connection && texFor) tex = texFor(ri->srcNodePath);
    if (!tex) continue;

    const std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, ctx);
    auto param = [&](const char* k, float d) {
      auto it = P.find(k);
      return it != P.end() ? it->second : d;
    };
    const float posX = param("Position.x", 0.0f), posY = param("Position.y", 0.0f);
    const bool alwaysUpdate = param("AlwaysUpdate", 0.0f) != 0.0f;

    const uint32_t width = (uint32_t)tex->width(), height = (uint32_t)tex->height();
    const MTL::PixelFormat fmt = tex->pixelFormat();

    // column/row = clamp((int)(pos*dim), 0, dim-1) (cs:36-37); the later %= is a no-op after clamp.
    int col = (int)(posX * (float)width);
    if (col < 0) col = 0;
    else if (col >= (int)width) col = (int)width - 1;
    int row = (int)(posY * (float)height);
    if (row < 0) row = 0;
    else if (row >= (int)height) row = (int)height - 1;

    // Unknown format → Log.Warning + Color.White (cs:142-145). No staging copy attempted.
    const uint32_t bpp = bytesPerPixel(fmt);
    if (bpp == 0) {
      rn.extOut[0] = rn.extOut[1] = rn.extOut[2] = rn.extOut[3] = 1.0f;
      continue;
    }

    // The staging-copy refresh rule (cs:40-46): alwaysUpdate || first cook || desc changed. Otherwise
    // the STALE cached copy is read — TiXL's load-bearing staleness quirk, reproduced verbatim.
    CachedImage& img = imageCacheStore()[rn.path];
    if (alwaysUpdate || img.bytes.empty() || img.w != width || img.h != height ||
        img.fmt != (uint64_t)fmt) {
      img.w = width; img.h = height; img.fmt = (uint64_t)fmt; img.bpp = bpp;
      img.bytes.assign((size_t)width * height * bpp, 0);
      tex->getBytes(img.bytes.data(), (NS::UInteger)width * bpp,
                    MTL::Region::Make2D(0, 0, width, height), 0);  // level 0 (= subresource 0)
    }

    const simd::float4 c = decodeTexel(img, col, row);  // the cs:82-146 format switch
    rn.extOut[0] = c.x; rn.extOut[1] = c.y; rn.extOut[2] = c.z; rn.extOut[3] = c.w;
  }
}

// Self-registration: NodeSpec (Add menu / findSpec / atomicSymbolFromSpec) + the golden.
// Outputs FIRST (extOut readback = full spec port index): Output.x/.y/.z/.w at indices 0..3.
// .t3 defaults: Position=(0,0), AlwaysUpdate=false. InputImage = the Texture2D wire.
static const ValueOp _reg_pickcolorfromimage{
    {"PickColorFromImage", "PickColorFromImage",
     {{"Output.x", "Output.x", "Float", false},
      {"Output.y", "Output.y", "Float", false},
      {"Output.z", "Output.z", "Float", false},
      {"Output.w", "Output.w", "Float", false},
      {"InputImage", "InputImage", "Texture2D", true},
      {"Position.x", "Position", "Float", true, 0.0f, -1.0f, 2.0f, Widget::Vec, {}, false, 2},
      {"Position.y", "Position.y", "Float", true, 0.0f, -1.0f, 2.0f, Widget::Vec, {}, false, 1},
      {"AlwaysUpdate", "AlwaysUpdate", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
     /*evaluate=*/nullptr,  // host vec4 emit — cooked by the point-into-frame pass, read via extOut
     "numbers.color"},
    "pickcolorfromimage", runPickColorFromImageSelfTest};

}  // namespace sw
