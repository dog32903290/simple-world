// ConvertColors: TiXL-ported RGB<->OkLab / RGB<->LCh color-space converter, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/adjust/img-fx-ConvertColors.hlsl
// + its load-bearing include shared/color-functions.hlsl (RgbToOkLab/OklabToRgb/RgbToLCh/LChToRgb
// and the four float3x3 constants fwdA/fwdB/invA/invB).
//
// ============================ MATRIX FORK (named, DX11->Metal) ============================
// color-functions.hlsl declares the matrices with HLSL row-major initializers
//   static const float3x3 M = {row0; row1; row2};   // braces fill ROWS
// and multiplies them in BOTH directions depending on the function:
//   RgbToOkLab: mul(invB, c)   = HLSL mul(MATRIX, colVec)  -> result_r = dot(row_r, c)
//   RgbToLCh:   mul(col, invB) = HLSL mul(rowVec, MATRIX)  -> result_c = sum_r col[r]*row_r[c]
// i.e. the SAME matrix invB is used as M*v in one function and v*M in another. MSL float3x3 is
// column-major: float3x3(c0,c1,c2) stores COLUMNS, and M*v / v*M have the OPPOSITE meaning to HLSL.
//
// We build each Metal matrix by passing the HLSL ROWS as Metal COLUMNS: m = float3x3(R0,R1,R2).
// With that single layout choice the equivalences are (derived + verified by hand, see
// point_ops_convertcolors.cpp runConvertColorsSelfTest Test A):
//   HLSL mul(M, v)  ==  MSL  (v * m)      // M*v  (column-vector semantics)
//   HLSL mul(v, M)  ==  MSL  (m * v)      // v*M  (row-vector semantics)
// So each mul site below is ported per-function, NOT assuming a single mul direction.
#include <metal_stdlib>
#include "convertcolors_params.h"
using namespace metal;

// color-functions.hlsl:4-18 — HLSL row-major initializers, passed here as Metal COLUMNS (= the
// row-major data read column-major IS the transpose, replicating HLSL mul as derived above).
//   fwdA rows: (1,1,1) (0.3963377774,-0.1055613458,-0.0894841775) (0.2158037573,-0.0638541728,-1.2914855480)
constant float3x3 fwdA = float3x3(float3(1.0,            1.0,            1.0),
                                  float3(0.3963377774,  -0.1055613458,  -0.0894841775),
                                  float3(0.2158037573,  -0.0638541728,  -1.2914855480));
//   fwdB rows: (4.0767245293,-1.2681437731,-0.0041119885) (-3.3072168827,2.6093323231,-0.7034763098) (0.2307590544,-0.3411344290,1.7068625689)
constant float3x3 fwdB = float3x3(float3(4.0767245293,  -1.2681437731,  -0.0041119885),
                                  float3(-3.3072168827,   2.6093323231,  -0.7034763098),
                                  float3(0.2307590544,   -0.3411344290,   1.7068625689));
//   invB rows: (0.4121656120,0.2118591070,0.0883097947) (0.5362752080,0.6807189584,0.2818474174) (0.0514575653,0.1074065790,0.6302613616)
constant float3x3 invB = float3x3(float3(0.4121656120,   0.2118591070,   0.0883097947),
                                  float3(0.5362752080,   0.6807189584,   0.2818474174),
                                  float3(0.0514575653,   0.1074065790,   0.6302613616));
//   invA rows: (0.2104542553,1.9779984951,0.0259040371) (0.7936177850,-2.4285922050,0.7827717662) (-0.0040720468,0.4505937099,-0.8086757660)
constant float3x3 invA = float3x3(float3(0.2104542553,   1.9779984951,   0.0259040371),
                                  float3(0.7936177850,  -2.4285922050,   0.7827717662),
                                  float3(-0.0040720468,   0.4505937099,  -0.8086757660));

// color-functions.hlsl:20-24  float3 RgbToOkLab(float3 c)
//   float3 lms = mul(invB, c);                              // HLSL M*v  -> MSL v*m
//   return mul(invA, sign(lms)*pow(abs(lms),1/3));          // HLSL M*v  -> MSL v*m
static inline float3 RgbToOkLab(float3 c) {
  float3 lms = c * invB;
  return (sign(lms) * pow(abs(lms), float3(0.3333333333333))) * invA;
}

// color-functions.hlsl:26-29  float3 OklabToRgb(float3 c)
//   float3 lms = mul(fwdA, c);                              // HLSL M*v  -> MSL v*m
//   return mul(fwdB, lms*lms*lms);                          // HLSL M*v  -> MSL v*m
static inline float3 OklabToRgb(float3 c) {
  float3 lms = c * fwdA;
  return (lms * lms * lms) * fwdB;
}

// color-functions.hlsl:32-41  float3 RgbToLCh(float3 col)
//   col = mul(col, invB);                                   // HLSL v*M  -> MSL m*v
//   col = mul(sign(col)*pow(abs(col),1/3), invA);           // HLSL v*M  -> MSL m*v
//   polar.x = col.x; polar.y = length(col.yz); polar.z = atan2(col.z,col.y)/(2*pi)+0.5
static inline float3 RgbToLCh(float3 col) {
  col = invB * col;
  col = invA * (sign(col) * pow(abs(col), float3(0.3333333333333)));
  float3 polar = float3(0.0);
  polar.x = col.x;
  polar.y = sqrt(col.y * col.y + col.z * col.z);
  polar.z = atan2(col.z, col.y) / (2.0 * 3.141592) + 0.5;  // Normalized Hue (TiXL literal 3.141592)
  return polar;
}

// color-functions.hlsl:44-53  float3 LChToRgb(float3 polar)
//   col.x = polar.x; polar.z = (polar.z-0.5)*2*pi; col.y = polar.y*cos; col.z = polar.y*sin
//   float3 lms = mul(col, fwdA);                            // HLSL v*M  -> MSL m*v
//   return mul(lms*lms*lms, fwdB);                          // HLSL v*M  -> MSL m*v
static inline float3 LChToRgb(float3 polar) {
  float3 col = float3(0.0);
  col.x = polar.x;
  polar.z = (polar.z - 0.5) * 2.0 * 3.141592;  // Normalized Hue
  col.y = polar.y * cos(polar.z);
  col.z = polar.y * sin(polar.z);
  float3 lms = fwdA * col;
  return fwdB * (lms * lms * lms);
}

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
// Identical to tint_vs (shared fullscreen-triangle convention across the image filters).
vertex VSOut convertcolors_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC up vs texture down
  return o;
}

// Mirror of img-fx-ConvertColors.hlsl psMain (lines 25-49): SampleLevel(uv,0) then the four
// if(Mode<n.5) branches, else passthrough. Alpha is carried through (c.a) in every branch.
fragment float4 convertcolors_fs(VSOut in [[stage_in]],
                                 texture2d<float> inputTex          [[texture(0)]],
                                 sampler samPoint                   [[sampler(0)]],
                                 constant ConvertColorsParams& P    [[buffer(CC_Params)]]) {
  float2 uv = in.texCoord;
  float4 c = inputTex.sample(samPoint, uv, level(0.0));  // HLSL SampleLevel(...,0.0)

  if (P.Mode < 0.5f) {
    return float4(RgbToOkLab(c.rgb), c.a);
  }
  if (P.Mode < 1.5f) {
    return float4(OklabToRgb(c.rgb), c.a);
  }
  if (P.Mode < 2.5f) {
    return float4(RgbToLCh(c.rgb), c.a);
  }
  if (P.Mode < 3.5f) {
    return float4(LChToRgb(c.rgb), c.a);
  }
  return c;
}
