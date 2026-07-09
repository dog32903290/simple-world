#pragma once
// tixl_simplex_oracle — HOST ORACLE transcription of TiXL's fSimplexNoiseDisplace HLSL global
// (3-D Ashima simplex + the pos/scale+offset*amount wrapper), used by
// field_ops_noisedisplacesdf_golden.cpp (PRONG 3) and intended for REUSE by the spatialdisplace
// lane goldens (SpatialDisplaceSDF shares the same fSimplexNoiseDisplace global). Split out of the
// golden TU per ARCHITECTURE.md rule 4 (≤400-line files), same shape as tixl_noise_oracle.h /
// runtime/t3import_displacemeshnoise_oracle.h.
//
// PROVENANCE (P5 discipline — GOLDEN_STANDARD.md rule 1): TiXL simplex noise, TRANSCRIBED from
// the HLSL global "fSimplexNoiseDisplace" LINE-BY-LINE in external/tixl
// Operators/Lib/field/adjust/NoiseDisplaceSDF.cs:41-127 (pinned TiXL SHA 395c4c55) — NOT from the
// leaf's emitted MSL and NOT from any sw host helper (P5 discipline). ALL arithmetic is fp32 (float
// literals, no double promotion): the mod289/permute hash chain is exact-integer float math below
// 2^24 that a double-promoted floor could round differently. HLSL/MSL step(edge, x) =
// (x >= edge) ? 1 : 0. Swizzle lines carry the original expression as a comment. The NoiseMargins
// out-param is TEST SCAFFOLDING (kink distances for probe selection, see the golden TU's header),
// not TiXL math.
//
// ZONE: shell-tier golden support (pure math; no runtime/platform/Metal dependency).
#include <cmath>

namespace sw {
namespace tixl_simplex {

struct HV3 { float x, y, z; };
struct HV4 { float x, y, z, w; };

inline float tixlDot3(HV3 a, HV3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float tixlDot4(HV4 a, HV4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline float tixlStep(float edge, float x) { return x >= edge ? 1.0f : 0.0f; }

// _noiseOffset_mod289(float3) — NoiseDisplaceSDF.cs:45-47. (The scalar float overload at cs:41-43 is
// present in the TiXL block but never called by the noise body; omitted here to keep -Wall clean.)
inline HV3 tixlMod289(HV3 v) {
  return {v.x - std::floor(v.x * (1.0f / 289.0f)) * 289.0f,
          v.y - std::floor(v.y * (1.0f / 289.0f)) * 289.0f,
          v.z - std::floor(v.z * (1.0f / 289.0f)) * 289.0f};
}
// _noiseOffset_mod289(float4) — NoiseDisplaceSDF.cs:49-51.
inline HV4 tixlMod289(HV4 v) {
  return {v.x - std::floor(v.x * (1.0f / 289.0f)) * 289.0f,
          v.y - std::floor(v.y * (1.0f / 289.0f)) * 289.0f,
          v.z - std::floor(v.z * (1.0f / 289.0f)) * 289.0f,
          v.w - std::floor(v.w * (1.0f / 289.0f)) * 289.0f};
}
// _noiseOffset_permute — NoiseDisplaceSDF.cs:53-55: mod289(((x * 34.0) + 1.0) * x).
inline HV4 tixlPermute(HV4 v) {
  return tixlMod289(HV4{((v.x * 34.0f) + 1.0f) * v.x, ((v.y * 34.0f) + 1.0f) * v.y,
                        ((v.z * 34.0f) + 1.0f) * v.z, ((v.w * 34.0f) + 1.0f) * v.w});
}
// _noiseOffset_taylorInvSqrt — NoiseDisplaceSDF.cs:57-59.
inline HV4 tixlTaylorInvSqrt(HV4 r) {
  return {1.79284291400159f - 0.85373472095314f * r.x, 1.79284291400159f - 0.85373472095314f * r.y,
          1.79284291400159f - 0.85373472095314f * r.z, 1.79284291400159f - 0.85373472095314f * r.w};
}

// Host-side distances from every discontinuity (floor lattice / step tie / step(h,0) / max clamp)
// that GPU fast-math ulps could legally flip. Probe selection requires all >= the caller's margin.
struct NoiseMargins {
  float outerFloor;  // min distance of (v + dot(v,C.yyy)) components from the integer lattice (cs:66)
  float cornerTie;   // min pairwise |x0 component diff| — step(x0.yzx, x0.xyz) tie (cs:70)
  float shKink;      // min |h| lane — sh = -step(h, 0.0) (cs:102); HASH-driven: ~1/7 lanes hit h==0
  float mKink;       // min |0.6 - dot(xi,xi)| lane — max(..., 0.0) clamp (cs:120)
};

// _noiseOffset_simplexNoise3D — NoiseDisplaceSDF.cs:61-123, one HLSL statement per line below.
inline float tixlSimplexNoise3D(HV3 v, NoiseMargins* mg) {
  const float Cx = 1.0f / 6.0f, Cy = 1.0f / 3.0f;  // cs:62 C = float2(1/6, 1/3)
  // cs:63 D = float4(0, 0.5, 1, 2) — declared in the HLSL but never read; omitted.

  // First corner (cs:65-67)
  const float sk = tixlDot3(v, HV3{Cy, Cy, Cy});  // dot(v, C.yyy)
  HV3 i{std::floor(v.x + sk), std::floor(v.y + sk), std::floor(v.z + sk)};
  const float tk = tixlDot3(i, HV3{Cx, Cx, Cx});  // dot(i, C.xxx)
  const HV3 x0{v.x - i.x + tk, v.y - i.y + tk, v.z - i.z + tk};

  // Other corners (cs:69-73)
  const HV3 g{tixlStep(x0.y, x0.x), tixlStep(x0.z, x0.y), tixlStep(x0.x, x0.z)};  // step(x0.yzx, x0.xyz)
  const HV3 l{1.0f - g.x, 1.0f - g.y, 1.0f - g.z};
  const HV3 i1{std::fmin(g.x, l.z), std::fmin(g.y, l.x), std::fmin(g.z, l.y)};  // min(g.xyz, l.zxy)
  const HV3 i2{std::fmax(g.x, l.z), std::fmax(g.y, l.x), std::fmax(g.z, l.y)};  // max(g.xyz, l.zxy)

  const HV3 x1{x0.x - i1.x + Cx, x0.y - i1.y + Cx, x0.z - i1.z + Cx};  // cs:75
  const HV3 x2{x0.x - i2.x + Cy, x0.y - i2.y + Cy, x0.z - i2.z + Cy};  // cs:76
  const HV3 x3{x0.x - 0.5f, x0.y - 0.5f, x0.z - 0.5f};                 // cs:77

  // Permutations (cs:79-84): permute(permute(permute(i.z + f4(0,i1.z,i2.z,1)) + i.y + f4(0,i1.y,i2.y,1))
  //                                  + i.x + f4(0,i1.x,i2.x,1))
  i = tixlMod289(i);
  const HV4 perm1 = tixlPermute(HV4{i.z + 0.0f, i.z + i1.z, i.z + i2.z, i.z + 1.0f});
  const HV4 perm2 = tixlPermute(
      HV4{perm1.x + i.y + 0.0f, perm1.y + i.y + i1.y, perm1.z + i.y + i2.y, perm1.w + i.y + 1.0f});
  const HV4 p = tixlPermute(
      HV4{perm2.x + i.x + 0.0f, perm2.y + i.x + i1.x, perm2.z + i.x + i2.x, perm2.w + i.x + 1.0f});

  // Gradients (cs:86-110)
  const HV4 j{p.x - 49.0f * std::floor(p.x * (1.0f / 49.0f)),  // mod(p, 7*7)
              p.y - 49.0f * std::floor(p.y * (1.0f / 49.0f)),
              p.z - 49.0f * std::floor(p.z * (1.0f / 49.0f)),
              p.w - 49.0f * std::floor(p.w * (1.0f / 49.0f))};
  const HV4 x_{std::floor(j.x * (1.0f / 7.0f)), std::floor(j.y * (1.0f / 7.0f)),
               std::floor(j.z * (1.0f / 7.0f)), std::floor(j.w * (1.0f / 7.0f))};
  const HV4 y_{std::floor(j.x - 7.0f * x_.x), std::floor(j.y - 7.0f * x_.y),
               std::floor(j.z - 7.0f * x_.z), std::floor(j.w - 7.0f * x_.w)};  // mod(j,7)
  const HV4 x{(x_.x * 2.0f + 0.5f) / 7.0f - 1.0f, (x_.y * 2.0f + 0.5f) / 7.0f - 1.0f,
              (x_.z * 2.0f + 0.5f) / 7.0f - 1.0f, (x_.w * 2.0f + 0.5f) / 7.0f - 1.0f};
  const HV4 y{(y_.x * 2.0f + 0.5f) / 7.0f - 1.0f, (y_.y * 2.0f + 0.5f) / 7.0f - 1.0f,
              (y_.z * 2.0f + 0.5f) / 7.0f - 1.0f, (y_.w * 2.0f + 0.5f) / 7.0f - 1.0f};
  const HV4 h{1.0f - std::fabs(x.x) - std::fabs(y.x), 1.0f - std::fabs(x.y) - std::fabs(y.y),
              1.0f - std::fabs(x.z) - std::fabs(y.z), 1.0f - std::fabs(x.w) - std::fabs(y.w)};
  const HV4 b0{x.x, x.y, y.x, y.y};  // float4(x.xy, y.xy)
  const HV4 b1{x.z, x.w, y.z, y.w};  // float4(x.zw, y.zw)
  const HV4 s0{std::floor(b0.x) * 2.0f + 1.0f, std::floor(b0.y) * 2.0f + 1.0f,
               std::floor(b0.z) * 2.0f + 1.0f, std::floor(b0.w) * 2.0f + 1.0f};
  const HV4 s1{std::floor(b1.x) * 2.0f + 1.0f, std::floor(b1.y) * 2.0f + 1.0f,
               std::floor(b1.z) * 2.0f + 1.0f, std::floor(b1.w) * 2.0f + 1.0f};
  const HV4 sh{-tixlStep(h.x, 0.0f), -tixlStep(h.y, 0.0f), -tixlStep(h.z, 0.0f),
               -tixlStep(h.w, 0.0f)};  // -step(h, 0.0)
  // a0 = b0.xzyw + s0.xzyw * sh.xxyy; a1 = b1.xzyw + s1.xzyw * sh.zzww (cs:104-105)
  const HV4 a0{b0.x + s0.x * sh.x, b0.z + s0.z * sh.x, b0.y + s0.y * sh.y, b0.w + s0.w * sh.y};
  const HV4 a1{b1.x + s1.x * sh.z, b1.z + s1.z * sh.z, b1.y + s1.y * sh.w, b1.w + s1.w * sh.w};
  HV3 g0{a0.x, a0.y, h.x};
  HV3 g1{a0.z, a0.w, h.y};
  HV3 g2{a1.x, a1.y, h.z};
  HV3 g3{a1.z, a1.w, h.w};

  // Normalize gradients (cs:112-117)
  const HV4 norm =
      tixlTaylorInvSqrt(HV4{tixlDot3(g0, g0), tixlDot3(g1, g1), tixlDot3(g2, g2), tixlDot3(g3, g3)});
  g0 = {g0.x * norm.x, g0.y * norm.x, g0.z * norm.x};
  g1 = {g1.x * norm.y, g1.y * norm.y, g1.z * norm.y};
  g2 = {g2.x * norm.z, g2.y * norm.z, g2.z * norm.z};
  g3 = {g3.x * norm.w, g3.y * norm.w, g3.z * norm.w};

  // Mix contributions (cs:119-122): m = max(0.6 - dot(xi,xi), 0); m = m*m;
  //   return 42.0 * dot(m*m, float4(dot(g0,x0), dot(g1,x1), dot(g2,x2), dot(g3,x3)))
  HV4 m{std::fmax(0.6f - tixlDot3(x0, x0), 0.0f), std::fmax(0.6f - tixlDot3(x1, x1), 0.0f),
        std::fmax(0.6f - tixlDot3(x2, x2), 0.0f), std::fmax(0.6f - tixlDot3(x3, x3), 0.0f)};
  m = {m.x * m.x, m.y * m.y, m.z * m.z, m.w * m.w};
  const float res =
      42.0f * tixlDot4(HV4{m.x * m.x, m.y * m.y, m.z * m.z, m.w * m.w},
                       HV4{tixlDot3(g0, x0), tixlDot3(g1, x1), tixlDot3(g2, x2), tixlDot3(g3, x3)});

  if (mg) {  // ---- scaffolding: kink distances for probe selection (NOT TiXL math) ----
    auto fracDist = [](float q) {
      const float f = q - std::floor(q);
      return std::fmin(f, 1.0f - f);
    };
    mg->outerFloor =
        std::fmin(fracDist(v.x + sk), std::fmin(fracDist(v.y + sk), fracDist(v.z + sk)));
    mg->cornerTie = std::fmin(std::fabs(x0.x - x0.y),
                              std::fmin(std::fabs(x0.y - x0.z), std::fabs(x0.z - x0.x)));
    mg->shKink = std::fmin(std::fmin(std::fabs(h.x), std::fabs(h.y)),
                           std::fmin(std::fabs(h.z), std::fabs(h.w)));
    mg->mKink =
        std::fmin(std::fmin(std::fabs(0.6f - tixlDot3(x0, x0)), std::fabs(0.6f - tixlDot3(x1, x1))),
                  std::fmin(std::fabs(0.6f - tixlDot3(x2, x2)), std::fabs(0.6f - tixlDot3(x3, x3))));
  }
  return res;
}

// fSimplexNoiseDisplace — NoiseDisplaceSDF.cs:125-127: simplexNoise3D(pos / scale + offset) * amount.
inline float tixlFSimplexNoiseDisplace(HV3 pos, float amount, float scale, HV3 offset,
                                       NoiseMargins* mg) {
  return tixlSimplexNoise3D(
             HV3{pos.x / scale + offset.x, pos.y / scale + offset.y, pos.z / scale + offset.z}, mg) *
         amount;
}

}  // namespace tixl_simplex
}  // namespace sw
