// GradientsToTexture tex op (the Gradient CONSUMER + the tex-output fork). It is the rail-crossing:
// it reads N host SwGradients (the 8th cook flow currency) + scalar params (Resolution/Direction) and
// turns them into an R32G32B32A32_Float, data-sized texture (4 floats/texel = sampled RGBA). Named
// point_ops_*.cpp so the CMake glob (SW_POINT_OP_SRCS) picks it up — identical to ValuesToTexture.
//
// TiXL authority: external/tixl/Operators/Lib/numbers/color/GradientsToTexture.cs (ported VERBATIM
// below — every line ref is to that file) + GradientsToTexture.t3 default mirror:
//   - Gradients  = MultiInputSlot<Gradient> (:135)        → our "Gradients" Gradient MultiInput port.
//   - Resolution = InputSlot<int>           (:138)        → Float param; .t3 default 256.
//   - Direction  = InputSlot<int> {Horizontal,Vertical} (:140-141) → Float Widget::Enum; .t3 default 0.
//   - useHorizontal = Direction == 0          (:25).
//   - gradientsCount = #wired gradients (CollectedInputs.Count); 0 → return (:30-32).
//   - sampleCount = Resolution.Clamp(1, 16384)            (:46).
//   - per sample: t = i / (sampleCount - 1), c = gradient.Sample(t) (:69-70 / :83-86).
//   - Horizontal fill: row-major, one ROW per gradient (outer=gradient, inner=sample) (:62-76).
//   - Vertical   fill: column-major, one COLUMN per gradient (outer=sample, inner=gradient) (:78-93).
//   - width  = useHorizontal ? sampleCount   : gradientsCount (:95)
//     height = useHorizontal ? gradientsCount : sampleCount   (:96).
//   - format = Format.R32G32B32A32_Float, 4 floats/texel = sizeof(float)*4 (:47-48,:114).
//
// ★TEX-OUTPUT FORK (named, same as ValuesToTexture): GradientsToTexture does NOT use the tex-walker's
//   ensureTex output (RGBA8Unorm, resolution-pinned). TiXL allocates its OWN Texture2DDescription
//   (:105-117) sized to the DATA, format R32G32B32A32_Float. We mirror this: the op is marked
//   registerTexOpOwnsOutput, so the cook driver hands it ownTexHost/ownTexW/ownTexH (NO ensureTex),
//   the op computes dims + writes the host float buffer (4 floats/texel), and the DRIVER allocates the
//   op-owned R32G32B32A32_Float texture via Impl::ensureOwnedTex (parked in texBuf → released on
//   realloc + in ~PointGraph → NO per-cook leak). ADDITIVE; FORCED by TiXL parity (not a taste call).
//
//   NB the own-tex rail's host buffer is std::vector<float>: ValuesToTexture writes 1 float/texel
//   (R32_Float); GradientsToTexture writes 4 floats/texel (RGBA). The driver allocates the format the
//   op asks for (texRegistersOwnFormat below) and uploads with the matching rowPitch.
#include <cmath>
#include <vector>

#include "runtime/gradient_raster.h"            // sampleGradientRowRGBA (shared row sampler — no drift)
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp (spec+selftest+registerTexOp sinks)
#include "runtime/point_graph.h"                // TexCookCtx, cookParam, registerTexOpOwns*
#include "runtime/sw_gradient.h"                // SwGradient (full def — the consumed currency)

namespace sw {

int runGradientsToTextureSelfTest(bool injectBug);
// Test-only injection seam (the chain golden's RED case corrupts the REAL cook path, not the expected
// value): when set, the op writes 0 into a NON-missing texel channel so the readback diverges.
bool& gradientsToTextureInjectBug() {
  static bool b = false;
  return b;
}

namespace {

// cookGradientsToTexture: read inputGradients (the N gathered SwGradients) + Resolution/Direction,
// sample each gradient at sampleCount uniform t, write *ownTexHost as 4 floats/texel (RGBA). The driver
// uploads it to an R32G32B32A32_Float texture (dims gradientsCount × sampleCount per useHorizontal).
void cookGradientsToTexture(TexCookCtx& c) {
  if (!c.ownTexHost || !c.ownTexW || !c.ownTexH) return;
  *c.ownTexW = 0;
  *c.ownTexH = 0;
  c.ownTexHost->clear();

  // Collect the gradients the driver gathered (one entry per wired Gradient source, wire-declaration
  // order). GradientsToTexture.cs:28-44: if no wired inputs, fall back to the single default gradient
  // value on the slot (the .t3 magenta→blue default). We mirror that: the driver always supplies at
  // least the slot-default gradient when nothing is wired (handed via inputGradients), so an empty
  // inputGradients means truly nothing → return (gradientsCount == 0 → :31-32).
  const std::vector<SwGradient>* grads = c.inputGradients;
  const int gradientsCount = grads ? (int)grads->size() : 0;
  if (gradientsCount == 0) return;  // :31-32 (no gradients → no texture)

  const int sampleCount =
      (int)std::min(std::max(cookParam(c, "Resolution", 256.0f), 1.0f), 16384.0f);  // :46 Clamp(1,16384)
  const bool useHorizontal = cookParam(c, "Direction", 0.0f) < 0.5f;               // :25 (0 == Horizontal)

  std::vector<float>& out = *c.ownTexHost;
  out.reserve((size_t)gradientsCount * sampleCount * 4);

  // sampleCount==1 → (sampleCount - 1f) == 0 → t = i/0. TiXL hits the same (float)0/0f = NaN for i=0;
  // Sample(NaN) clamps via clampf → NaN propagates only if Sample does. We transcribe the formula
  // verbatim; for sampleCount==1, i=0 yields 0/0 in BOTH engines (faithful, not a fork).
  auto tAt = [&](int i) -> float { return (float)i / (sampleCount - 1.0f); };  // :69 / :83

  if (useHorizontal) {
    // Row-major: one ROW per gradient (:62-76) — outer=gradient, inner=sample. The per-row sampling
    // is the SHARED sampleGradientRowRGBA (same t=i/(N-1) loop the gradient generators use — single
    // source of truth so the two can't drift; gradient_raster.h).
    for (const SwGradient& gr : *grads) sampleGradientRowRGBA(gr, sampleCount, out);
  } else {
    // Column-major: one COLUMN per gradient (:78-93) — outer=sample, inner=gradient.
    for (int i = 0; i < sampleCount; ++i)
      for (const SwGradient& gr : *grads) {
        simd::float4 col = gr.sample(tAt(i));  // :86
        out.push_back(col.x);                  // :87-90
        out.push_back(col.y);
        out.push_back(col.z);
        out.push_back(col.w);
      }
  }

  *c.ownTexW = useHorizontal ? (uint32_t)sampleCount : (uint32_t)gradientsCount;  // :95
  *c.ownTexH = useHorizontal ? (uint32_t)gradientsCount : (uint32_t)sampleCount;  // :96

  // Test-only: corrupt the REAL cook output — write a SENTINEL (-1) into texel(0,0)'s R channel so the
  // RED case bites here regardless of the expected value at that texel (the default gradient's t=0 R is
  // ~0, so writing 0 would NOT diverge; -1 always does). Off in production.
  if (gradientsToTextureInjectBug() && !out.empty()) out[0] = -1.0f;
}

}  // namespace

// Self-registration. ImageFilterOp feeds: registerTexOp + the spec sink (so findSpec sees it) + the
// selftest sink (so run_all discovers --selftest-gradientstotexture). NOT an ImageFilterComputeOp.
// We mark it OWN-TEXTURE (R32G32B32A32_Float, 4 floats/texel) so the tex-walker routes it through the
// ownTexHost path with the op-chosen format.
//   Ports: Gradients = Gradient MultiInput (the gathered host gradients); Resolution scalar (.t3 256);
//          Direction = Float Widget::Enum {Horizontal,Vertical}; out = Texture2D output.
static const ImageFilterOp _reg_gradientstotexture{
    {"GradientsToTexture", "GradientsToTexture",
     {{"Gradients", "Gradients", "Gradient", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"out", "out", "Texture2D", false},
      {"Resolution", "Resolution", "Float", true, 256.0f, 1.0f, 16384.0f, Widget::Slider},
      {"Direction", "Direction", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
       {"Horizontal", "Vertical"}, true}},
     /*evaluate=*/nullptr},  // Texture2D output cannot ride NodeSpec::evaluate (returns ONE float)
    "GradientsToTexture", cookGradientsToTexture, "gradientstotexture", runGradientsToTextureSelfTest};

// Mark OWN-TEXTURE + its format (R32G32B32A32_Float) at static-init (mirrors ValuesToTexture's
// OwnTexRegistrar). registerTexOpOwnsOutput + registerTexOpOwnFormat are idempotent.
namespace {
struct OwnTexRegistrar {
  OwnTexRegistrar() {
    registerTexOpOwnsOutput("GradientsToTexture");
    registerTexOpOwnFormat("GradientsToTexture", /*floatsPerTexel=*/4);  // RGBA32F
  }
};
static const OwnTexRegistrar _reg_gradientstotexture_owns;
}  // namespace

}  // namespace sw
