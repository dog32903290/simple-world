#pragma once
// tixl_noise_oracle — HOST ORACLE transcription of TiXL's curlNoise dependency closure, used by
// turbulence_parity_golden.cpp (TOOTH 1a). Split out of the golden TU per ARCHITECTURE.md rule 4
// (≤400-line files), same shape as runtime/t3import_displacemeshnoise_oracle.h.
//
// PROVENANCE (P5 discipline — GOLDEN_STANDARD.md rule 1): TRANSCRIBED from TiXL source (SHA 395c4c55)
// external/tixl Operators/Lib/Assets/shaders/shared/noise-functions.hlsl:
//   mod289 :16-24, permute :26-29, taylorInvSqrt :31-34, snoise :179-252, snoiseVec3 :254-261,
//   curlNoise :263-283 (:282 — the curl is NORMALIZED, hence |curlNoise| == 1 for every lookup).
// NOT copied from sw's shaders/shared/noise.metal.h port and NOT calibrated from any kernel readback —
// so a port-side noise drift cannot self-certify.
//
// FLOAT (not double) on purpose: the simplex has floor()/step() branch points and the snoiseVec3
// offsets push lookups to |coord|~124, where a double oracle can land on a DIFFERENT simplex cell than
// the float GPU (same rationale as runtime/t3import_displacemeshnoise_oracle.h).
//
// ZONE: shell-tier golden support (pure math; no runtime/platform/Metal dependency).
#include <algorithm>
#include <cmath>

namespace sw {
namespace tixl_noise {

typedef float F;
struct V3 { F x, y, z; };
struct V4 { F x, y, z, w; };
inline F dot3(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline F flr(F x) { return std::floor(x); }
inline F stp(F e, F x) { return x < e ? 0.0f : 1.0f; }  // HLSL step(edge, x)
inline F mod289(F x) { return x - std::floor(x * (1.0f / 289.0f)) * 289.0f; }  // :16-24
inline V4 permute(V4 x) {  // :26-29 — mod289(((x*34)+1)*x)
  return {mod289((x.x * 34.0f + 1.0f) * x.x), mod289((x.y * 34.0f + 1.0f) * x.y),
          mod289((x.z * 34.0f + 1.0f) * x.z), mod289((x.w * 34.0f + 1.0f) * x.w)};
}
inline V4 taylorInvSqrt(V4 r) {  // :31-34
  return {1.79284291400159f - 0.85373472095314f * r.x, 1.79284291400159f - 0.85373472095314f * r.y,
          1.79284291400159f - 0.85373472095314f * r.z, 1.79284291400159f - 0.85373472095314f * r.w};
}
inline F snoise(V3 v) {  // :179-252 (Ashima 3-D simplex; float4 lanes unrolled to arrays)
  const F Cx = 1.0f / 6.0f, Cy = 1.0f / 3.0f;  // :181 C
  const F Dy = 0.5f;                           // :182 D.y
  F sk = (v.x + v.y + v.z) * Cy;               // :185 first corner
  V3 i = {flr(v.x + sk), flr(v.y + sk), flr(v.z + sk)};
  F t = (i.x + i.y + i.z) * Cx;                // :186
  V3 x0 = {v.x - i.x + t, v.y - i.y + t, v.z - i.z + t};
  V3 g = {stp(x0.y, x0.x), stp(x0.z, x0.y), stp(x0.x, x0.z)};  // :189 step(x0.yzx, x0.xyz)
  V3 l = {1.0f - g.x, 1.0f - g.y, 1.0f - g.z};                 // :190
  V3 i1 = {std::fmin(g.x, l.z), std::fmin(g.y, l.x), std::fmin(g.z, l.y)};  // :191 min(g, l.zxy)
  V3 i2 = {std::fmax(g.x, l.z), std::fmax(g.y, l.x), std::fmax(g.z, l.y)};  // :192 max(g, l.zxy)
  V3 x1 = {x0.x - i1.x + Cx, x0.y - i1.y + Cx, x0.z - i1.z + Cx};  // :198
  V3 x2 = {x0.x - i2.x + Cy, x0.y - i2.y + Cy, x0.z - i2.z + Cy};  // :199
  V3 x3 = {x0.x - Dy, x0.y - Dy, x0.z - Dy};                       // :200
  i = {mod289(i.x), mod289(i.y), mod289(i.z)};                     // :203
  V4 pz = permute({i.z + 0.0f, i.z + i1.z, i.z + i2.z, i.z + 1.0f});  // :204-207
  V4 py = permute({pz.x + i.y + 0.0f, pz.y + i.y + i1.y, pz.z + i.y + i2.y, pz.w + i.y + 1.0f});
  V4 p = permute({py.x + i.x + 0.0f, py.y + i.x + i1.x, py.z + i.x + i2.x, py.w + i.x + 1.0f});
  const F n_ = 0.142857142857f;                               // :211  1/7
  const F nsx = n_ * 2.0f, nsy = n_ * 0.5f - 1.0f, nsz = n_;  // :212  ns = n_*D.wyz - D.xzx
  F pa[4] = {p.x, p.y, p.z, p.w};
  F xg[4], yg[4], hg[4];
  for (int k = 0; k < 4; ++k) {                               // :214-221
    F j = pa[k] - 49.0f * flr(pa[k] * nsz * nsz);             // mod(p, 7*7)
    F x_ = flr(j * nsz);
    F y_ = flr(j - 7.0f * x_);
    xg[k] = x_ * nsx + nsy;
    yg[k] = y_ * nsx + nsy;
    hg[k] = 1.0f - std::fabs(xg[k]) - std::fabs(yg[k]);
  }
  F b0[4] = {xg[0], xg[1], yg[0], yg[1]};                     // :223 b0 = (x.xy, y.xy)
  F b1[4] = {xg[2], xg[3], yg[2], yg[3]};                     // :224 b1 = (x.zw, y.zw)
  F s0[4] = {flr(b0[0]) * 2.0f + 1.0f, flr(b0[1]) * 2.0f + 1.0f, flr(b0[2]) * 2.0f + 1.0f,
             flr(b0[3]) * 2.0f + 1.0f};                       // :228
  F s1[4] = {flr(b1[0]) * 2.0f + 1.0f, flr(b1[1]) * 2.0f + 1.0f, flr(b1[2]) * 2.0f + 1.0f,
             flr(b1[3]) * 2.0f + 1.0f};                       // :229
  F sh[4] = {-stp(hg[0], 0.0f), -stp(hg[1], 0.0f), -stp(hg[2], 0.0f), -stp(hg[3], 0.0f)};  // :230
  F a0[4] = {b0[0] + s0[0] * sh[0], b0[2] + s0[2] * sh[0], b0[1] + s0[1] * sh[1],
             b0[3] + s0[3] * sh[1]};                          // :232 a0 = b0.xzyw + s0.xzyw*sh.xxyy
  F a1[4] = {b1[0] + s1[0] * sh[2], b1[2] + s1[2] * sh[2], b1[1] + s1[1] * sh[3],
             b1[3] + s1[3] * sh[3]};                          // :233 a1 = b1.xzyw + s1.xzyw*sh.zzww
  V3 p0 = {a0[0], a0[1], hg[0]};                              // :235-238
  V3 p1 = {a0[2], a0[3], hg[1]};
  V3 p2 = {a1[0], a1[1], hg[2]};
  V3 p3 = {a1[2], a1[3], hg[3]};
  V4 nrm = taylorInvSqrt({dot3(p0, p0), dot3(p1, p1), dot3(p2, p2), dot3(p3, p3)});  // :241-245
  p0 = {p0.x * nrm.x, p0.y * nrm.x, p0.z * nrm.x};
  p1 = {p1.x * nrm.y, p1.y * nrm.y, p1.z * nrm.y};
  p2 = {p2.x * nrm.z, p2.y * nrm.z, p2.z * nrm.z};
  p3 = {p3.x * nrm.w, p3.y * nrm.w, p3.z * nrm.w};
  F m[4] = {std::fmax(0.6f - dot3(x0, x0), 0.0f), std::fmax(0.6f - dot3(x1, x1), 0.0f),  // :248
            std::fmax(0.6f - dot3(x2, x2), 0.0f), std::fmax(0.6f - dot3(x3, x3), 0.0f)};
  for (int k = 0; k < 4; ++k) { m[k] = m[k] * m[k]; m[k] = m[k] * m[k]; }                // :249
  return 42.0f * (m[0] * dot3(p0, x0) + m[1] * dot3(p1, x1) + m[2] * dot3(p2, x2) +
                  m[3] * dot3(p3, x3));                       // :250-251
}
inline V3 snoiseVec3(V3 p) {  // :254-261
  F s = snoise({p.x + 0.0001f, p.y, p.z});
  F s1 = snoise({p.y - 19.1f, p.z + 33.4f, p.x + 47.2f});
  F s2 = snoise({p.z + 74.2f, p.x - 124.5f, p.y + 99.4f});
  return {s, s1, s2};
}
inline V3 curlNoise(V3 p) {  // :263-283
  const F e = 0.001f;
  V3 px0 = snoiseVec3({p.x - e, p.y, p.z});
  V3 px1 = snoiseVec3({p.x + e, p.y, p.z});
  V3 py0 = snoiseVec3({p.x, p.y - e, p.z});
  V3 py1 = snoiseVec3({p.x, p.y + e, p.z});
  V3 pz0 = snoiseVec3({p.x, p.y, p.z - e});
  V3 pz1 = snoiseVec3({p.x, p.y, p.z + e});
  F x = py1.z - py0.z - pz1.y + pz0.y;  // :277-279
  F y = pz1.x - pz0.x - px1.z + px0.z;
  F z = px1.y - px0.y - py1.x + py0.x;
  const F divisor = 1.0f / (2.0f * e);  // :281
  F lx = x * divisor, ly = y * divisor, lz = z * divisor;
  F len = std::sqrt(lx * lx + ly * ly + lz * lz);  // :282 normalize(...) → |curl| == 1
  return {lx / len, ly / len, lz / len};
}

}  // namespace tixl_noise
}  // namespace sw
