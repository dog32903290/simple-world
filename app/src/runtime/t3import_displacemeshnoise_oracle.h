#pragma once
// runtime/t3import_displacemeshnoise_oracle — the INDEPENDENT float oracle for 骨9's DisplaceMeshNoise
// golden (t3import_displacemeshnoise_golden.cpp). Split out so the harness .cpp stays a bounded ≤400-line
// seam-and-tooth file (ARCHITECTURE.md rule 4); this header is the pure-math half.
//
// ── Oracle: snoiseVec3 — a straight port of app/shaders/shared/noise.metal.h (Ashima 3-D simplex),
//    independent of the import/cook path (so GREEN is not self-proving). ────────────────────────────────
// NOTE the noise runs in FLOAT (not double): 3-D simplex has floor()/step() branch points, and the
// snoiseVec3 offsets push lookups to |coord|~120; a double oracle lands on DIFFERENT simplex cells than
// the float GPU there → the noise value forks entirely (not a rounding delta). Matching the GPU's FLOAT
// precision is the faithful oracle — it is STILL independent (this is our own float port of the Ashima
// algorithm, not derived from the import/cook path). Callers do the FINAL offset add in double.
//
// ZONE: runtime golden support (leaf; pure math, no import/cook/Metal dependency).
#include <algorithm>
#include <cmath>

namespace sw {
namespace dmn_oracle {

typedef float F;
struct V3 { F x, y, z; };
struct V4 { F x, y, z, w; };
inline V3 scl3(V3 a, F s) { return {a.x*s, a.y*s, a.z*s}; }
inline F dot3(V3 a, V3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline V3 add3(V3 a, V3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
inline V3 sub3(V3 a, V3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
inline V3 nrm3(V3 a) { F l = std::sqrt(dot3(a, a)); return {a.x/l, a.y/l, a.z/l}; }  // normalize (matches MSL)
inline V3 crs3(V3 a, V3 b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }  // cross
inline V3 mix3(V3 a, V3 b, F t) { return {a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t}; }  // mix(x,y,t)
inline F flr(F x) { return std::floor(x); }
inline F stp(F edge, F x) { return x < edge ? 0.0f : 1.0f; }  // step(edge,x)
inline F mod289d(F x) { return x - std::floor(x * (1.0f/289.0f)) * 289.0f; }
inline V4 mod289v4(V4 v) { return {mod289d(v.x), mod289d(v.y), mod289d(v.z), mod289d(v.w)}; }
inline V4 permute(V4 x) {  // ((x*34)+1)*x mod 289
  return mod289v4(V4{(x.x*34.0f+1.0f)*x.x, (x.y*34.0f+1.0f)*x.y, (x.z*34.0f+1.0f)*x.z, (x.w*34.0f+1.0f)*x.w});
}
inline V4 taylorInvSqrt(V4 r) {
  return {1.79284291400159f - 0.85373472095314f*r.x, 1.79284291400159f - 0.85373472095314f*r.y,
          1.79284291400159f - 0.85373472095314f*r.z, 1.79284291400159f - 0.85373472095314f*r.w};
}
inline F snoise3(V3 v) {
  const F Cx = 1.0f/6.0f, Cy = 1.0f/3.0f;
  const F Dy = 0.5f;
  V3 i0 = {flr(v.x + dot3(v, {Cy, Cy, Cy})), flr(v.y + dot3(v, {Cy, Cy, Cy})), flr(v.z + dot3(v, {Cy, Cy, Cy}))};
  V3 x0 = {v.x - i0.x + dot3(i0, {Cx, Cx, Cx}), v.y - i0.y + dot3(i0, {Cx, Cx, Cx}), v.z - i0.z + dot3(i0, {Cx, Cx, Cx})};
  V3 g = {stp(x0.y, x0.x), stp(x0.z, x0.y), stp(x0.x, x0.z)};   // g = step(x0.yzx, x0.xyz)
  V3 l = {1.0f-g.x, 1.0f-g.y, 1.0f-g.z};
  V3 lzxy = {l.z, l.x, l.y};
  V3 i1 = {std::min(g.x, lzxy.x), std::min(g.y, lzxy.y), std::min(g.z, lzxy.z)};  // min(g.xyz, l.zxy)
  V3 i2 = {std::max(g.x, lzxy.x), std::max(g.y, lzxy.y), std::max(g.z, lzxy.z)};  // max(g.xyz, l.zxy)
  V3 x1 = {x0.x - i1.x + Cx, x0.y - i1.y + Cx, x0.z - i1.z + Cx};
  V3 x2 = {x0.x - i2.x + Cy, x0.y - i2.y + Cy, x0.z - i2.z + Cy};
  V3 x3 = {x0.x - Dy, x0.y - Dy, x0.z - Dy};
  // Permutations (noise.metal.h: permute(permute(permute(iz+…) + iy+…) + ix+…)).
  i0 = {mod289d(i0.x), mod289d(i0.y), mod289d(i0.z)};
  V4 pz = permute(V4{i0.z + 0.0f, i0.z + i1.z, i0.z + i2.z, i0.z + 1.0f});
  V4 py = permute(V4{pz.x + i0.y + 0.0f, pz.y + i0.y + i1.y, pz.z + i0.y + i2.y, pz.w + i0.y + 1.0f});
  V4 p  = permute(V4{py.x + i0.x + 0.0f, py.y + i0.x + i1.x, py.z + i0.x + i2.x, py.w + i0.x + 1.0f});
  const F n_ = 0.142857142857f;  // 1/7
  V3 ns = {n_*2.0f - 0.0f, n_*0.5f - 1.0f, n_*1.0f - 0.0f};  // n_*D.wyz - D.xzx, D=(0,.5,1,2)
  F jarr[4] = {p.x, p.y, p.z, p.w};
  F xarr[4], yarr[4], harr[4];
  for (int k = 0; k < 4; ++k) {
    F j = jarr[k] - 49.0f * flr(jarr[k] * ns.z * ns.z);
    F x_ = flr(j * ns.z);
    F y_ = flr(j - 7.0f * x_);
    xarr[k] = x_ * ns.x + ns.y;
    yarr[k] = y_ * ns.x + ns.y;
    harr[k] = 1.0f - std::fabs(xarr[k]) - std::fabs(yarr[k]);
  }
  V4 b0 = {xarr[0], xarr[1], yarr[0], yarr[1]};
  V4 b1 = {xarr[2], xarr[3], yarr[2], yarr[3]};
  F s0[4] = {flr(b0.x)*2.0f+1.0f, flr(b0.y)*2.0f+1.0f, flr(b0.z)*2.0f+1.0f, flr(b0.w)*2.0f+1.0f};
  F s1[4] = {flr(b1.x)*2.0f+1.0f, flr(b1.y)*2.0f+1.0f, flr(b1.z)*2.0f+1.0f, flr(b1.w)*2.0f+1.0f};
  F sh[4] = {-stp(harr[0], 0.0f), -stp(harr[1], 0.0f), -stp(harr[2], 0.0f), -stp(harr[3], 0.0f)};
  F a0[4] = {b0.x + s0[0]*sh[0], b0.z + s0[2]*sh[0], b0.y + s0[1]*sh[1], b0.w + s0[3]*sh[1]};
  F a1[4] = {b1.x + s1[0]*sh[2], b1.z + s1[2]*sh[2], b1.y + s1[1]*sh[3], b1.w + s1[3]*sh[3]};
  V3 p0 = {a0[0], a0[1], harr[0]};
  V3 p1 = {a0[2], a0[3], harr[1]};
  V3 p2 = {a1[0], a1[1], harr[2]};
  V3 p3 = {a1[2], a1[3], harr[3]};
  V4 norm = taylorInvSqrt(V4{dot3(p0,p0), dot3(p1,p1), dot3(p2,p2), dot3(p3,p3)});
  p0 = scl3(p0, norm.x); p1 = scl3(p1, norm.y); p2 = scl3(p2, norm.z); p3 = scl3(p3, norm.w);
  F m[4] = {std::max(0.6f - dot3(x0,x0), 0.0f), std::max(0.6f - dot3(x1,x1), 0.0f),
            std::max(0.6f - dot3(x2,x2), 0.0f), std::max(0.6f - dot3(x3,x3), 0.0f)};
  for (int k = 0; k < 4; ++k) { m[k] = m[k]*m[k]; m[k] = m[k]*m[k]; }
  return 42.0f * (m[0]*dot3(p0,x0) + m[1]*dot3(p1,x1) + m[2]*dot3(p2,x2) + m[3]*dot3(p3,x3));
}
inline V3 snoiseVec3d(V3 p) {
  F s  = snoise3({p.x + 0.0001f, p.y,          p.z});
  F s1 = snoise3({p.y - 19.1f,   p.z + 33.4f,  p.x + 47.2f});
  F s2 = snoise3({p.z + 74.2f,   p.x - 124.5f, p.y + 99.4f});
  return {s, s1, s2};
}

// getNoise(pos, var) — verbatim mesh-LegacyNoiseDisplace.hlsl GetNoise() with var=0 (Variation=0 config).
// The SINGLE noise path shared by BOTH the Position oracle and the TBN oracle (kernel's getNoise() is
// likewise the one primitive Position + TBN both call), so there is no chance of the two forking. Mirrors
// the kernel's op order exactly: (pos*0.91)*Freq+Phase, then (noise+OffsetDir)*Amount/100*AmountDist.
inline V3 getNoiseOracle(V3 pos, F freq, F phase, F offdir, F amount, V3 amtdist) {
  V3 lookup = { pos.x*0.91f*freq + phase, pos.y*0.91f*freq + phase, pos.z*0.91f*freq + phase };
  V3 n = snoiseVec3d(lookup);
  return { (n.x+offdir)*amount/100.0f*amtdist.x,
           (n.y+offdir)*amount/100.0f*amtdist.y,
           (n.z+offdir)*amount/100.0f*amtdist.z };
}

}  // namespace dmn_oracle
}  // namespace sw
