#pragma once
// mathv_ref_brdflookup — CPU scalar oracle for the ComputeBrdfLookupTexture kernel (split-sum BRDF LUT).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/3d/rendering/ComputeBrdfLookupTexture-cs.hlsl — NOT derived from sw's
// app/shaders/computeshaderstage_brdflookup.metal (intentionally never opened while writing this header).
//   static const PI/TwoPI/Epsilon/NumSamples/InvNumSamples  :8-13  -> the constants below
//   radicalInverse_VdC(uint bits)                            :19-27 -> radicalInverseVdC() below
//   sampleHammersley(uint i)                                 :30-33 -> sampleHammersley() below
//   sampleGGX(u1,u2,roughness)                               :38-48 -> sampleGGX() below
//   gaSchlickG1(cosTheta,k)                                  :51-54 -> gaSchlickG1() below
//   gaSchlickGGX_IBL(cosLi,cosLo,roughness)                  :57-62 -> gaSchlickGGXIBL() below
//   main(uint2 ThreadID)                                     :64-111 -> brdfLookupTexel() below
//     (cosLo=x/W, roughness=y/H, cosLo=max(.,Epsilon), Lo, the 1024-sample split-sum accumulate,
//      LUT = float4(float2(DFG1,DFG2)*InvNumSamples, 0, 1))
// Every statement below carries its own :NN back-reference.
//
// PROVENANCE (GOLDEN_STANDARD.md "P5-safe oracle 判準" / MATH_VERIFY_WORKFLOW.md §1.3): transcribed
// straight from the TiXL HLSL text above, never from any sw `app/shaders/*.metal` port. Zero metal
// include, zero app/shaders/ reference, zero sw math helper. The MSL kernel is a transpiler artifact of
// the SAME HLSL, so ref and kernel share ONE authority (the HLSL) yet neither derives from the other —
// exactly the P5-safe separation the mathv oracles rely on.
//
// FLOAT (not double) on purpose (§2.3 branchy-class rule): every arithmetic op in this kernel runs in
// float32 on the GPU (the `cosLi > 0` branch, the sqrt/pow transcendentals, the 1024-term accumulation
// order). A double ref would drift from the float GPU accumulation and land outside a tight ±LSB eps at
// 16-bit UNorm. Constants are held at their HLSL float32 spelling (PI = 3.141592f — the TRUNCATED
// literal the .hlsl uses, :8 — NOT M_PI).
#include <cmath>
#include <cstdint>

namespace mathv_ref {
namespace brdf_detail {

// :8-13 — the kernel's static constants at their HLSL float32 spelling.
constexpr float kPI = 3.141592f;                 // :8 (truncated literal, NOT M_PI)
constexpr float kTwoPI = 2.0f * kPI;             // :9
constexpr float kEpsilon = 0.001f;               // :10
constexpr uint32_t kNumSamples = 1024u;          // :12
constexpr float kInvNumSamples = 1.0f / (float)kNumSamples;  // :13 (= 0.0009765625f, exact)

// :19-27 — Van der Corput radical inverse (bit-reverse of `bits`, scaled to [0,1)).
inline float radicalInverseVdC(uint32_t bits) {
  bits = (bits << 16u) | (bits >> 16u);                                    // :21
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);       // :22
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);       // :23
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);       // :24
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);       // :25
  return (float)bits * 2.3283064365386963e-10f;                            // :26 (/ 0x100000000)
}

// :30-33 — the i-th Hammersley point: (i/NumSamples, radicalInverse(i)).
inline void sampleHammersley(uint32_t i, float& u1, float& u2) {
  u1 = (float)i * kInvNumSamples;   // :32
  u2 = radicalInverseVdC(i);        // :32
}

// :38-48 — GGX importance-sampled half-vector for a fixed roughness (tangent space, +Z up).
//
// roughness<0.1 KNIFE-EDGE (measured, not a port error): as alpha=roughness²→0, cosTheta below is a
// 0/0-shaped division; this CPU ref divides exactly (std::sqrt over an exact float divide) while the
// GPU kernel (app/shaders/computeshaderstage_brdflookup.metal) runs the SAME division in fast-math, so
// the two diverge hard on that row — MEASURED up to ~4006 LSB (16-bit UNorm, x=1,y=0 at a 512×512
// dispatch; ~3.4k-3.8k LSB at the smaller golden sizes). The golden (selftests_mathv_brdflookup.cpp)
// excludes roughness<0.1 from its tight interior check and applies a loose relative check there instead.
inline void sampleGGX(float u1, float u2, float roughness, float& hx, float& hy, float& hz) {
  const float alpha = roughness * roughness;                                          // :40
  const float cosTheta = std::sqrt((1.0f - u2) / (1.0f + (alpha * alpha - 1.0f) * u2));// :42
  const float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);                        // :43
  const float phi = kTwoPI * u1;                                                       // :44
  hx = sinTheta * std::cos(phi);  // :47
  hy = sinTheta * std::sin(phi);  // :47
  hz = cosTheta;                  // :47
}

// :51-54 — one Schlick-GGX term.
inline float gaSchlickG1(float cosTheta, float k) {
  return cosTheta / (cosTheta * (1.0f - k) + k);  // :53
}

// :57-62 — Smith geometric attenuation (IBL remap k = roughness²/2).
inline float gaSchlickGGXIBL(float cosLi, float cosLo, float roughness) {
  const float k = (roughness * roughness) / 2.0f;         // :60
  return gaSchlickG1(cosLi, k) * gaSchlickG1(cosLo, k);   // :61
}

}  // namespace brdf_detail

// :64-111 — the split-sum LUT value for the texel at (gx, gy) in a W×H texture. Writes RGBA:
//   R = DFG1 * InvNumSamples, G = DFG2 * InvNumSamples, B = 0, A = 1  (:110).
inline void brdfLookupTexel(uint32_t gx, uint32_t gy, uint32_t W, uint32_t H, float& r, float& g,
                            float& b, float& a) {
  using namespace brdf_detail;
  float cosLo = (float)gx / (float)W;      // :72
  const float roughness = (float)gy / (float)H;  // :73
  cosLo = cosLo > kEpsilon ? cosLo : kEpsilon;   // :76 max(cosLo, Epsilon)

  // :79 Lo = tangent-space view vector.
  const float loX = std::sqrt(1.0f - cosLo * cosLo);
  const float loY = 0.0f;
  const float loZ = cosLo;

  float dfg1 = 0.0f;  // :84
  float dfg2 = 0.0f;  // :85
  for (uint32_t i = 0; i < kNumSamples; ++i) {  // :87
    float u1, u2;
    sampleHammersley(i, u1, u2);  // :88
    float lhX, lhY, lhZ;
    sampleGGX(u1, u2, roughness, lhX, lhY, lhZ);  // :91

    const float loDotLh = loX * lhX + loY * lhY + loZ * lhZ;  // dot(Lo,Lh)
    // :94 Li = 2*dot(Lo,Lh)*Lh - Lo
    const float liX = 2.0f * loDotLh * lhX - loX;
    const float liY = 2.0f * loDotLh * lhY - loY;
    const float liZ = 2.0f * loDotLh * lhZ - loZ;

    const float cosLi = liZ;   // :96
    const float cosLh = lhZ;   // :97
    const float cosLoLh = loDotLh > 0.0f ? loDotLh : 0.0f;  // :98 max(dot(Lo,Lh),0)

    if (cosLi > 0.0f) {  // :100
      const float Gterm = gaSchlickGGXIBL(cosLi, cosLo, roughness);        // :101
      const float Gv = Gterm * cosLoLh / (cosLh * cosLo);                  // :102
      const float Fc = std::pow(1.0f - cosLoLh, 5.0f);                     // :103
      dfg1 += (1.0f - Fc) * Gv;  // :105
      dfg2 += Fc * Gv;           // :106
    }
  }
  r = dfg1 * kInvNumSamples;  // :110
  g = dfg2 * kInvNumSamples;  // :110
  b = 0.0f;                   // :110
  a = 1.0f;                   // :110
}

}  // namespace mathv_ref
