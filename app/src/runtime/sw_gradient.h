// runtime/sw_gradient — the host-side Gradient value type (the 8th cook flow's currency).
//
// 1:1 transcription of external/tixl Core/DataTypes/Gradient.cs (the Core/DataTypes concrete type,
// like SwPoint mirrors point.hlsl). A Gradient is a CPU value that rides between "Gradient" ports:
// a sorted list of {position, RGBA-color} stops + an interpolation mode, with a Sample(t) that maps
// t∈[0,1] → a color. PURE FLOAT MATH — no Metal, no platform dependency (host-only, like SwPoint's
// host half). It is the value-graph parallel of FloatList/String/PointList: a producer port hands a
// SwGradient to a consumer port (GradientsToTexture). It never touches the 16-byte GPU ctx.
//
// FORK (named):
//   • drop-Guid: TiXL's Gradient.Step carries a Guid (GradientEditor stop identity for UI drag). The
//     HOST runtime never edits stops interactively (the GradientEditor widget is carved OUT, attended
//     — design/UX), so SwGradientStep drops Guid. Sample()/default/serialization parity is unaffected
//     (Sample reads only NormalizedPosition + Color; SortHandles sorts on position; the Guid is UI-only).
//   • Sample() is transcribed VERBATIM from Gradient.cs:123-169 (clamp / sorted scan / remap / branch
//     by interpolation). OkLab.Mix (Gradient.cs:157 → OkLab.cs:74-81, with its hard-coded matrices
//     transliterated) + CubicSpline (per-channel, Gradient.cs:284-300 → CubicSpline.cs) are ported
//     below. Every line ref points at those TiXL files.
#pragma once

#include <simd/simd.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace sw {

// One gradient stop. Mirror of Gradient.Step (Gradient.cs:200-219), minus the UI-only Guid (fork above).
struct SwGradientStep {
  float pos = 0.0f;                                  // Gradient.Step.NormalizedPosition (cs:202)
  simd::float4 color = simd::make_float4(0, 0, 0, 1);  // Gradient.Step.Color RGBA (cs:203)
};

// Interpolation modes — 1:1 with Gradient.Interpolations (Gradient.cs:222-229). Values are the enum
// ordinals (DefineGradient.Interpolation feeds the int → these), so the cast is the .cs's
// `(Gradient.Interpolations)Interpolation.GetValue(...)` (DefineGradient.cs:51).
enum SwGradientInterpolation {
  kGradientLinear = 0,  // Gradient.cs:224
  kGradientHold = 1,    // Gradient.cs:225
  kGradientSmooth = 2,  // Gradient.cs:226
  kGradientOkLab = 3,   // Gradient.cs:227
  kGradientSpline = 4,  // Gradient.cs:228
};

// ---- transcribed math helpers (the exact MathUtils / OkLab / CubicSpline used by Gradient.Sample) ----
namespace gradient_detail {

// MathUtils.Clamp<T> (MathUtils.cs:252-253): T.Min(T.Max(v,min),max).
inline float clampf(float v, float mn, float mx) { return std::min(std::max(v, mn), mx); }

// MathUtils.RemapAndClamp (MathUtils.cs:357-365). In Gradient.Sample the call is
// t.RemapAndClamp(prev.pos, step.pos, 0, 1): outMin=0 < outMax=1 so the Swap branch never fires; we
// transcribe it faithfully anyway (Utilities.Swap when outMin > outMax) for any future caller.
inline float remapAndClamp(float value, float inMin, float inMax, float outMin, float outMax) {
  float factor = (value - inMin) / (inMax - inMin);     // cs:360
  float v = factor * (outMax - outMin) + outMin;        // cs:361
  if (outMin > outMax) std::swap(outMin, outMax);       // cs:362-363 (Utilities.Swap)
  return clampf(v, outMin, outMax);                     // cs:364 (v.Clamp(outMin,outMax))
}

// MathUtils.Fade (cs:141-144) + SmootherStep (cs:146-150).
inline float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }  // cs:143
inline float smootherStep(float mn, float mx, float value) {
  float t = std::max(0.0f, std::min(1.0f, (value - mn) / (mx - mn)));  // cs:148
  return fade(t);                                                       // cs:149
}

// System.Numerics.Vector4.Lerp == MathUtils.Lerp(Vector4) (MathUtils.cs:427-433): a + (b-a)*t per ch.
inline simd::float4 lerp4(simd::float4 a, simd::float4 b, float t) {
  return simd::make_float4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                           a.w + (b.w - a.w) * t);
}

// --- OkLab (OkLab.cs), the OkLab interpolation mode's color-space round trip ---
// Vector4.Max(c, Vector4.Zero): per-channel max with 0 (OkLab.cs:76-77).
inline simd::float4 max0(simd::float4 c) {
  return simd::make_float4(std::max(c.x, 0.0f), std::max(c.y, 0.0f), std::max(c.z, 0.0f),
                           std::max(c.w, 0.0f));
}

// OkLab.Degamma (OkLab.cs:52-61): pow(c, gamma=2.2) per RGB channel, alpha passthrough.
inline simd::float4 degamma(simd::float4 c) {
  constexpr float gamma = 2.2f;
  return simd::make_float4(std::pow(c.x, gamma), std::pow(c.y, gamma), std::pow(c.z, gamma), c.w);
}

// OkLab.ToGamma (OkLab.cs:63-72): pow(c, 1/gamma) per RGB channel, alpha passthrough.
inline simd::float4 toGamma(simd::float4 c) {
  constexpr float gamma = 2.2f;
  return simd::make_float4(std::pow(c.x, 1.0f / gamma), std::pow(c.y, 1.0f / gamma),
                           std::pow(c.z, 1.0f / gamma), c.w);
}

// OkLab.RgbAToOkLab (OkLab.cs:12-31): linear RGB → OkLab. The cube-root path uses double (Math.Pow,
// 1.0/3.0) exactly as the .cs, then casts each lab channel to float — transcribed faithfully so the
// byte-parity reference (computed from the same doubles) matches.
inline simd::float4 rgbAToOkLab(simd::float4 c) {
  double cr = c.x, cg = c.y, cb = c.z;                                  // cs:14-16
  double l = 0.4122214708 * cr + 0.5363325363 * cg + 0.0514459929 * cb;  // cs:18
  double m = 0.2119034982 * cr + 0.6806995451 * cg + 0.1073969566 * cb;  // cs:19
  double s = 0.0883024619 * cr + 0.2817188376 * cg + 0.6299787005 * cb;  // cs:20
  double lCbrt = std::pow(l, 1.0 / 3.0);                                // cs:22
  double mCbrt = std::pow(m, 1.0 / 3.0);                                // cs:23
  double sCbrt = std::pow(s, 1.0 / 3.0);                                // cs:24
  return simd::make_float4(
      (float)(0.2104542553 * lCbrt + 0.793617785 * mCbrt - 0.0040720468 * sCbrt),   // cs:27
      (float)(1.9779984951 * lCbrt - 2.428592205 * mCbrt + 0.4505937099 * sCbrt),   // cs:28
      (float)(0.0259040371 * lCbrt + 0.7827717662 * mCbrt - 0.808675766 * sCbrt),   // cs:29
      (float)c.w);                                                                  // cs:30
}

// OkLab.OkLabToRgba (OkLab.cs:34-50): OkLab → linear RGB (float math, matching the .cs's float ops).
inline simd::float4 okLabToRgba(simd::float4 c) {
  float l1 = c.x + 0.3963377774f * c.y + 0.2158037573f * c.z;  // cs:36
  float m1 = c.x - 0.1055613458f * c.y - 0.0638541728f * c.z;  // cs:37
  float s1 = c.x - 0.0894841775f * c.y - 1.2914855480f * c.z;  // cs:38
  float l = l1 * l1 * l1;                                       // cs:40
  float m = m1 * m1 * m1;                                       // cs:41
  float s = s1 * s1 * s1;                                       // cs:42
  return simd::make_float4(+4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,   // cs:45
                           -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,   // cs:46
                           -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s,   // cs:47
                           c.w);                                                         // cs:48
}

// OkLab.Mix (OkLab.cs:74-81): degamma both (max0 first) → lerp in OkLab → back to gamma.
inline simd::float4 okLabMix(simd::float4 c1, simd::float4 c2, float t) {
  simd::float4 c1Linear = degamma(max0(c1));                          // cs:76
  simd::float4 c2Linear = degamma(max0(c2));                          // cs:77
  simd::float4 labMix = lerp4(rgbAToOkLab(c1Linear), rgbAToOkLab(c2Linear), t);  // cs:79
  return toGamma(okLabToRgba(labMix));                                // cs:80
}

// --- CubicSpline (CubicSpline.cs) — natural cubic spline, per channel (Gradient spline mode) ---
// Solve a tridiagonal system via the Thomas algorithm (TriDiagonalMatrixF.Solve, CubicSpline.cs:566-603).
inline std::vector<float> triDiagSolve(const std::vector<float>& A, const std::vector<float>& B,
                                       const std::vector<float>& C, const std::vector<float>& d) {
  int n = (int)d.size();
  std::vector<float> cPrime(n), dPrime(n), x(n);
  cPrime[0] = C[0] / B[0];                                            // cs:577
  for (int i = 1; i < n; ++i)
    cPrime[i] = C[i] / (B[i] - cPrime[i - 1] * A[i]);                 // cs:581
  dPrime[0] = d[0] / B[0];                                            // cs:586
  for (int i = 1; i < n; ++i)
    dPrime[i] = (d[i] - dPrime[i - 1] * A[i]) / (B[i] - cPrime[i - 1] * A[i]);  // cs:590
  x[n - 1] = dPrime[n - 1];                                           // cs:595
  for (int i = n - 2; i >= 0; --i)
    x[i] = dPrime[i] - cPrime[i] * x[i + 1];                          // cs:599
  return x;
}

// One fitted natural cubic spline over (xOrig, yOrig). Fit (CubicSpline.cs:173-255, natural-spline
// branch: startSlope/endSlope = NaN) + Eval at a single x (cs:130-137 / 270-288).
struct CubicSpline1 {
  std::vector<float> a, b, xOrig, yOrig;

  void fit(const std::vector<float>& x, const std::vector<float>& y) {
    xOrig = x;
    yOrig = y;
    int n = (int)x.size();
    std::vector<float> A(n, 0), B(n, 0), C(n, 0), r(n, 0);  // TriDiagonalMatrixF + rhs (cs:185-187)
    float dx1, dx2, dy1, dy2;
    // First row (natural-spline branch, startSlope=NaN; cs:191-197).
    dx1 = x[1] - x[0];
    C[0] = 1.0f / dx1;
    B[0] = 2.0f * C[0];
    r[0] = 3 * (y[1] - y[0]) / (dx1 * dx1);
    // Body rows (cs:205-217).
    for (int i = 1; i < n - 1; ++i) {
      dx1 = x[i] - x[i - 1];
      dx2 = x[i + 1] - x[i];
      A[i] = 1.0f / dx1;
      C[i] = 1.0f / dx2;
      B[i] = 2.0f * (A[i] + C[i]);
      dy1 = y[i] - y[i - 1];
      dy2 = y[i + 1] - y[i];
      r[i] = 3 * (dy1 / (dx1 * dx1) + dy2 / (dx2 * dx2));
    }
    // Last row (natural-spline branch, endSlope=NaN; cs:220-227).
    dx1 = x[n - 1] - x[n - 2];
    dy1 = y[n - 1] - y[n - 2];
    A[n - 1] = 1.0f / dx1;
    B[n - 1] = 2.0f * A[n - 1];
    r[n - 1] = 3 * (dy1 / (dx1 * dx1));
    // Solve + coefficients (cs:238-251).
    std::vector<float> k = triDiagSolve(A, B, C, r);
    a.assign(n - 1, 0);
    b.assign(n - 1, 0);
    for (int i = 1; i < n; ++i) {
      dx1 = x[i] - x[i - 1];
      dy1 = y[i] - y[i - 1];
      a[i - 1] = k[i - 1] * dx1 - dy1;   // cs:249 (equation 10)
      b[i - 1] = -k[i] * dx1 + dy1;      // cs:250 (equation 11)
    }
  }

  // Eval at one x. GetNextXIndex (cs:108-121): clamp the spline-index to [0, n-2]; EvalSpline (cs:130-137).
  float eval(float x) const {
    int n = (int)xOrig.size();
    int j = 0;
    while (j < n - 2 && x > xOrig[j + 1]) ++j;  // simultaneous-traverse, single ascending x (cs:115-117)
    float dx = xOrig[j + 1] - xOrig[j];
    float t = (x - xOrig[j]) / dx;
    return (1 - t) * yOrig[j] + t * yOrig[j + 1] + t * (1 - t) * (a[j] * (1 - t) + b[j] * t);  // cs:134
  }
};

}  // namespace gradient_detail

// The host Gradient value (1:1 with Gradient.cs). Steps + interpolation + Sample. NO Guid (fork).
struct SwGradient {
  std::vector<SwGradientStep> steps;
  int interpolation = kGradientLinear;

  // Gradient.SortHandles (Gradient.cs:100-103): sort steps by position ascending (Sample assumes sorted).
  void sortHandles() {
    std::stable_sort(steps.begin(), steps.end(),
                     [](const SwGradientStep& a, const SwGradientStep& b) { return a.pos < b.pos; });
  }

  // Gradient.Sample (Gradient.cs:123-169) — VERBATIM. t clamped to [0,1]; scan for the first step at/
  // past t; interpolate between previousStep and step per the interpolation mode.
  simd::float4 sample(float t) const {
    using namespace gradient_detail;
    t = clampf(t, 0.0f, 1.0f);                                   // cs:125
    const SwGradientStep* previousStep = nullptr;                // cs:126

    for (size_t i = 0; i < steps.size(); ++i) {                  // cs:128
      const SwGradientStep& step = steps[i];                     // cs:129
      if (!(step.pos >= t)) {                                    // cs:131
        previousStep = &step;                                    // cs:133
        continue;                                                // cs:134
      }
      if (previousStep == nullptr || previousStep->pos >= step.pos)  // cs:137
        return step.color;                                       // cs:139

      if (interpolation == kGradientHold)                        // cs:142
        return previousStep->color;                              // cs:143

      float fraction = remapAndClamp(t, previousStep->pos, step.pos, 0, 1);  // cs:145

      switch (interpolation) {                                   // cs:147
        case kGradientLinear:                                    // cs:149
          break;
        case kGradientSmooth:                                    // cs:152
          fraction = smootherStep(0, 1, fraction);              // cs:153
          break;
        case kGradientOkLab: {                                   // cs:156
          simd::float4 v = okLabMix(previousStep->color, step.color, fraction);  // cs:157
          // cs:158 `Vector4.Max(vector4, Vector4.Zero);` — the .cs DISCARDS this return (no assign),
          // so the OkLab branch returns the un-max'd mix. Transcribed faithfully (the line is a no-op).
          return v;                                              // cs:159
        }
        case kGradientSpline:                                    // cs:161
          return sampleSpline(t);                                // cs:162
      }
      return lerp4(previousStep->color, step.color, fraction);   // cs:165
    }
    return previousStep ? previousStep->color : simd::make_float4(1, 1, 1, 1);  // cs:168
  }

 private:
  // Gradient.SampleSpline (Gradient.cs:284-300): fit one natural cubic spline per channel over the
  // step positions, eval each at t, max(.,0) for RGB and clamp A to [0,1]. (No hash caching here — the
  // host re-fits each Sample; correctness over the .cs's premature-opt cache, which is purely internal.)
  simd::float4 sampleSpline(float t) const {
    using namespace gradient_detail;
    int n = (int)steps.size();
    std::vector<float> xs(n), y0(n), y1(n), y2(n), y3(n);
    for (int i = 0; i < n; ++i) {                                // InitCubicSplines (cs:248-256)
      xs[i] = steps[i].pos;
      y0[i] = steps[i].color.x;
      y1[i] = steps[i].color.y;
      y2[i] = steps[i].color.z;
      y3[i] = steps[i].color.w;
    }
    CubicSpline1 s0, s1, s2, s3;
    s0.fit(xs, y0);
    s1.fit(xs, y1);
    s2.fit(xs, y2);
    s3.fit(xs, y3);
    return simd::make_float4(std::max(s0.eval(t), 0.0f),   // cs:295 MathF.Max(...,0)
                             std::max(s1.eval(t), 0.0f),   // cs:296
                             std::max(s2.eval(t), 0.0f),   // cs:297
                             clampf(s3.eval(t), 0.0f, 1.0f));  // cs:298 (.Clamp(0,1))
  }
};

// Gradient.CreateDefaultSteps (Gradient.cs:181-198): the fallback two-stop gradient (magenta→blue),
// used when a deserialized gradient has no steps. Provided so a host default mirrors the .cs default.
inline SwGradient swGradientDefault() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(1, 0, 1, 1)});  // cs:185-190
  g.steps.push_back({1.0f, simd::make_float4(0, 0, 1, 1)});  // cs:191-196
  return g;
}

}  // namespace sw
