// ToneMapping: TiXL-ported tone mapping filter, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/ToneMap.hlsl.
//
// ToneMap.hlsl logic (verbatim port to Metal):
//   c.rgb *= Exposure
//   if      Mode < 0.5  -> tonemapAcesFilm(c.rgb)
//   else if Mode < 1.5  -> tonemapReinhard(c.rgb)
//   else if Mode < 2.5  -> pow(c.rgb,2.2) then tonemapFilmic(c.rgb)
//   else if Mode < 3.5  -> tonemapUncharted2(c.rgb)
//   else if Mode < 4.5  -> pow(c.rgb,2.2) then tonemapAgX(c.rgb, false)   // AgX
//   else if Mode < 4.5  -> [DEAD: AgX_Punchy branch, see fork note below]
//   (else: no-op, Exposure*input kept as-is for Mode>=4.5, i.e. None=6)
//   if CorrectGamma > 0.5 -> c.rgb = pow(c.rgb, 1.0/GammaValue)
//
// Fork (named, DX11->Metal):
// 1. DX11 PS pipeline -> Metal fullscreen triangle VS+FS (same as Tint/AdjustColors fork class).
// 2. sampler: fixed linear+clamp (TiXL host uses texSampler without explicit address mode;
//    clamp matches Metal default for interior UVs).
// 3. HLSL float3x3 row-major multiply: mul(col, M) in HLSL = col * M^T in column-major.
//    ToneMap.hlsl line 61: mul(col, float3x3(0.842, ...)) — HLSL mul(vector, matrix) treats
//    the vector as a row-vector and the matrix rows as operand rows (row-major = col * M).
//    Metal: col * M gives the same mathematical result when M is stored in the same row order
//    because Metal matrix constructors take columns, so we transpose the matrix layout.
//    See inline comments at each mul site.
// 4. HLSL SampleLevel(sampler, uv, 0.0) -> Metal sample(sampler, uv) — equivalent at mip 0.
#include <metal_stdlib>
#include "tonemapping_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut tonemapping_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC up vs texture down
  return o;
}

// --- Tonemap helper functions (verbatim port from ToneMap.hlsl) ---

// ToneMap.hlsl:20-28 — uncharted2Tonemap helper.
static float3 _uncharted2Tonemap(float3 x) {
  const float A = 0.15f;
  const float B = 0.50f;
  const float C = 0.10f;
  const float D = 0.20f;
  const float E = 0.02f;
  const float F = 0.30f;
  return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

// ToneMap.hlsl:30-36 — tonemapUncharted2.
static float3 _tonemapUncharted2(float3 color) {
  const float W = 3.0f;  // ToneMap.hlsl:31 'const float W = 3;//11.2;'
  const float exposureBias = 2.0f;
  float3 curr = _uncharted2Tonemap(exposureBias * color);
  float3 whiteScale = 1.0f / _uncharted2Tonemap(float3(W, W, W));
  return curr * whiteScale;
}

// ToneMap.hlsl:38-42 — tonemapFilmic (Filmic Tonemapping Operators filmicgames.com/archives/75).
static float3 _tonemapFilmic(float3 color) {
  float3 x = max(float3(0.0f, 0.0f, 0.0f), color - 0.004f);
  return (x * (6.2f * x + 0.5f)) / (x * (6.2f * x + 1.7f) + 0.06f);
}

// ToneMap.hlsl:44-52 — tonemapAcesFilm (knarkowicz.wordpress.com/2016/01/06/).
static float3 _tonemapAcesFilm(float3 x) {
  const float a = 2.51f;
  const float b = 0.03f;
  const float c = 2.43f;
  const float d = 0.59f;
  const float e = 0.14f;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

// ToneMap.hlsl:54-56 — tonemapReinhard.
static float3 _tonemapReinhard(float3 color) {
  return color / (color + 1.0f);
}

// ToneMap.hlsl:58-78 — tonemapAgX (with punchy variant).
// Fork note: HLSL mul(col, float3x3(row0, row1, row2)) is row-vector * row-major matrix.
// Metal float3x3 constructor takes COLUMNS. To replicate HLSL mul semantics, we transpose:
// HLSL matrix rows become Metal matrix columns (stored as float3x3(col0, col1, col2)).
static float3 _tonemapAgX(float3 col, bool punchy) {
  // ToneMap.hlsl:61: mul(col, float3x3(0.842, 0.0423, 0.0424, 0.0784, 0.878, 0.0784, 0.0792, 0.0792, 0.879))
  // HLSL row layout: row0=(0.842,0.0423,0.0424) row1=(0.0784,0.878,0.0784) row2=(0.0792,0.0792,0.879)
  // fork[DX11->Metal-mul]: HLSL mul(rowVec, M) == M^T * colVec. We apply M * col below, so the
  // Metal matrix must BE M^T, i.e. its columns are HLSL's ROWS. Metal's float3x3(c0,c1,c2) takes
  // COLUMNS, so we pass HLSL row0/row1/row2 verbatim as Metal col0/col1/col2 (no extra transpose —
  // the column-major read of the row-major data IS the transpose). Locked by AgX parity tooth.
  float3x3 m0 = float3x3(float3(0.842f,  0.0423f, 0.0424f),
                          float3(0.0784f, 0.878f,  0.0784f),
                          float3(0.0792f, 0.0792f, 0.879f));
  col = m0 * col;

  // ToneMap.hlsl:64: col = clamp((log2(col)+12.47393)/16.5, 0.0, 1.0)
  col = clamp((log2(col) + 12.47393f) / 16.5f, 0.0f, 1.0f);

  // ToneMap.hlsl:67: col = 0.5 + 0.5*sin(((-3.11*col+6.42)*col-0.378)*col-1.44)
  col = 0.5f + 0.5f * sin(((-3.11f * col + 6.42f) * col - 0.378f) * col - 1.44f);

  if (punchy) {
    // ToneMap.hlsl:71-72
    float luma = dot(col, float3(0.216f, 0.7152f, 0.0722f));
    col = mix(float3(luma, luma, luma), pow(col, float3(1.35f, 1.35f, 1.35f)), 1.4f);
  }

  // ToneMap.hlsl:75-76: mul(col, float3x3(1.2,-0.053,-0.053,-0.1,1.15,-0.1,-0.1,-0.1,1.15))
  // HLSL rows: row0=(1.2,-0.053,-0.053) row1=(-0.1,1.15,-0.1) row2=(-0.1,-0.1,1.15)
  // fork[DX11->Metal-mul]: same as m0 — HLSL rows become Metal columns verbatim (M * col == mul(col,M)).
  float3x3 m1 = float3x3(float3( 1.2f,   -0.053f, -0.053f),
                          float3(-0.1f,    1.15f,  -0.1f),
                          float3(-0.1f,   -0.1f,    1.15f));
  col = m1 * col;
  return col;
}

// Mirror of ToneMap.hlsl psMain.
fragment float4 tonemapping_fs(VSOut in [[stage_in]],
                               texture2d<float> inputTex [[texture(0)]],
                               sampler samLinear          [[sampler(0)]],
                               constant ToneMappingParams& P [[buffer(TONEMAPPING_Params)]]) {
  float2 uv = in.texCoord;
  // ToneMap.hlsl:85-86: sample + Exposure premultiply
  float4 c = inputTex.sample(samLinear, uv);
  c.rgb *= P.Exposure;

  // ToneMap.hlsl:88-108: mode dispatch (if/else chain, threshold-based float enum).
  // fork[verbatim-TiXL-bug]: ToneMap.hlsl:105 'Mode<4.5' 応為 5.5；AgX_Punchy 在 TiXL 也不可達，逐字保留
  if (P.Mode < 0.5f) {
    c = float4(_tonemapAcesFilm(c.rgb), 1.0f);
  } else if (P.Mode < 1.5f) {
    c = float4(_tonemapReinhard(c.rgb), 1.0f);
  } else if (P.Mode < 2.5f) {
    c.rgb = pow(c.rgb, 2.2f);
    c = float4(_tonemapFilmic(c.rgb), 1.0f);
  } else if (P.Mode < 3.5f) {
    c = float4(_tonemapUncharted2(c.rgb), 1.0f);
  } else if (P.Mode < 4.5f) {
    c.rgb = pow(c.rgb, 2.2f);
    c = float4(_tonemapAgX(c.rgb, false), 1.0f);
  } else if (P.Mode < 4.5f) {
    // fork[verbatim-TiXL-bug]: ToneMap.hlsl:105 'Mode<4.5' 應為 5.5；AgX_Punchy 在 TiXL 也不可達，逐字保留
    // This branch is unreachable because the prior condition already consumed Mode<4.5.
    // TiXL has this exact dead branch (AgX_Punchy=5 is unreachable in TiXL too).
    c.rgb = pow(c.rgb, 2.2f);
    c = float4(_tonemapAgX(c.rgb, true), 1.0f);
  }
  // else: Mode>=4.5 but !=AgX_Punchy path (due to bug above) -> Mode 6 (None) falls here.
  // c remains Exposure*input, no tone compression (pass-through).

  // ToneMap.hlsl:110-113: optional gamma correction
  if (P.CorrectGamma > 0.5f) {
    float gamma = P.GammaValue;
    c.rgb = pow(c.rgb, 1.0f / gamma);
  }

  return c;
}
