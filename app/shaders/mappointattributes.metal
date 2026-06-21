// mappointattributes — faithful port of external/tixl
// .../Assets/shaders/points/modify/MapPointAttributes.hlsl. The bake-into-point seam consumer: the op
// BAKES its host Curve into a CurveImage (R32_Float, t1) + host Gradient into a GradientImage (RGBA32,
// t2, .t3 resolution 512) host-side, then per point maps an input coordinate f0 → f (InputMode +
// MappingMode with Range/Phase) and samples both at (f, 0.5) with a Clamp/Linear sampler.
//
// TiXL parity (MapPointAttributes.hlsl:54-196), VERBATIM:
//   strength = Strength * (StrengthFactor==0 ? 1 : StrengthFactor==1 ? p.FX1 : p.FX2)   (:66-69)
//   f0 by InputMode: 0 BufferOrder = index/(count-1); 1 F1 = p.FX1; 2 F2 = p.FX2;
//      3 Random = hash11u(index)                                                         (:71-87)
//   isnan(f0) -> copy p through, return                                                  (:89-93)
//   f by MappingMode (Centered/ForStart/PingPong/Repeat — fmod helper at :30-33)         (:95-115)
//   WriteTo!=0: curveValue = CurveImage.SampleLevel(s, (f,0.5), 0).r;
//      org = (WriteTo==1)?p.FX1 : (WriteTo==2)?p.FX1 : 1   ★★ case 2 reads p.FX1 (TiXL BUG,
//            faithful: BOTH case 1 and case 2 read p.FX1 — see :148-154; do NOT "fix");
//      newValue = ApplyMode(Replace/Multiply/Add)(org, curveValue);
//      WriteTo 1: p.FX1 = lerp(org, newValue, strength);
//      WriteTo 2: p.FX2 = lerp(org, newValue, strength);  (org is p.FX1, the bug)
//      WriteTo 3: p.Scale = lerp(org=1, p.Scale*newValue, strength)                       (:141-182)
//   gradientColor = GradientImage.SampleLevel(s, (f,0.5), 0);   (ALWAYS sampled, :184)
//      WriteColor 1 Replace:  p.Color = lerp(p.Color, gradientColor, strength);
//      WriteColor 2 Multiply: p.Color = lerp(p.Color, p.Color*gradientColor, strength)    (:185-193)
//
#include <metal_stdlib>
#include "tixl_point.h"                 // SwPoint (64B)
#include "mappointattributes_params.h"  // MapPointAttrParams, MPA_* bindings
#include "shared/hash.metal.h"          // _PRIME0 (used by hash11u below)
using namespace metal;

// hash11u — uint -> float in [0,1). Verbatim port from TiXL hash-functions.hlsl:115-123 (the SAME LCG
// MapPointAttributes.hlsl:86 / ClearSomePoints.hlsl use for the Random InputMode). hash.metal.h exports
// hash41u/hash11/etc. but NOT this single-uint hash, so it is restated here (identical to
// clearsomepoints.metal's local copy — kept in sync, the canonical TiXL hash11u).
static float hash11u(uint x) {
  const uint k = 1103515245u;
  x *= _PRIME0;
  x = ((x >> 8u) ^ x) * k;
  x = ((x >> 8u) ^ x) * k;
  return float(x) * (1.0f / float(0xffffffffu));
}

// fmod helper — MapPointAttributes.hlsl:30-33: x - y*floor(x/y) (the floored mod, distinct from MSL
// fmod's truncated mod for negatives).
static float mpaFmod(float x, float y) { return x - y * floor(x / y); }

#define MPA_INPUTMODE_BUFFERORDER 0
#define MPA_INPUTMODE_F1 1
#define MPA_INPUTMODE_F2 2
#define MPA_INPUTMODE_RANDOM 3

#define MPA_MAPPING_NORMAL 0
#define MPA_MAPPING_FORSTART 1
#define MPA_MAPPING_PINGPONG 2
#define MPA_MAPPING_REPEAT 3

#define MPA_APPLYMODE_REPLACE 0
#define MPA_APPLYMODE_MULTIPLY 1
#define MPA_APPLYMODE_ADD 2

kernel void mappointattributes(const device SwPoint*          src          [[buffer(MPA_SourcePoints)]],
                               device SwPoint*                 dst          [[buffer(MPA_ResultPoints)]],
                               constant MapPointAttrParams&    P            [[buffer(MPA_Params)]],
                               texture2d<float>                CurveImage    [[texture(MPA_CurveImage)]],
                               texture2d<float>                GradientImage [[texture(MPA_GradientImage)]],
                               sampler                         ClampedSampler [[sampler(MPA_ClampedSampler)]],
                               uint                            index [[thread_position_in_grid]]) {
  uint pointCount = P.Count;
  if (index >= pointCount) return;
  SwPoint p = src[index];

  int InputMode = (int)P.InputMode;
  int MappingMode = (int)P.MappingMode;
  int ApplyMode = (int)P.ApplyMode;
  int WriteTo = (int)P.WriteTo;
  int WriteColor = (int)P.WriteColor;
  int StrengthFactor = (int)P.StrengthFactor;

  float strength = P.Strength * (StrengthFactor == 0
                                     ? 1.0f
                                 : (StrengthFactor == 1) ? p.FX1
                                                         : p.FX2);

  float f0 = 0.0f;
  switch (InputMode) {
    case MPA_INPUTMODE_BUFFERORDER:
      f0 = (float)index / (float)(pointCount - 1);
      break;
    case MPA_INPUTMODE_F1:
      f0 = p.FX1;
      break;
    case MPA_INPUTMODE_F2:
      f0 = p.FX2;
      break;
    case MPA_INPUTMODE_RANDOM:
      f0 = hash11u(index);
      break;
  }

  if (isnan(f0)) {
    dst[index] = p;
    return;
  }

  float f = 0.0f;
  switch (MappingMode) {
    case MPA_MAPPING_NORMAL:
      f = (f0 - 0.5f) / P.Range + 0.5f - P.Phase / P.Range;
      break;
    case MPA_MAPPING_FORSTART:
      f = f0 / P.Range - P.Phase;
      break;
    case MPA_MAPPING_PINGPONG:
      f = mpaFmod((f0 * 2.0f - 1.0f - 2.0f * P.Phase * P.Range) / P.Range, 2.0f);
      f += -1.0f;
      f = abs(f);
      break;
    case MPA_MAPPING_REPEAT:
      f = f0 / P.Range - 0.5f - P.Phase;
      f = mpaFmod(f, 1.0f);
      break;
  }

  if (WriteTo != 0) {
    float curveValue = CurveImage.sample(ClampedSampler, float2(f, 0.5f), level(0.0f)).r;

    float org = 1.0f;
    switch (WriteTo) {
      case 1: org = p.FX1; break;
      case 2: org = p.FX1; break;  // ★ TiXL bug: case 2 reads p.FX1 (NOT p.FX2). Faithful port.
    }

    float newValue = 0.0f;
    if (ApplyMode == MPA_APPLYMODE_REPLACE) {
      newValue = curveValue;
    } else if (ApplyMode == MPA_APPLYMODE_MULTIPLY) {
      newValue = org * curveValue;
    } else if (ApplyMode == MPA_APPLYMODE_ADD) {
      newValue = org + curveValue;
    }

    switch (WriteTo) {
      case 1:
        p.FX1 = mix(org, newValue, strength);
        break;
      case 2:
        p.FX2 = mix(org, newValue, strength);
        break;
      case 3: {
        // p.Scale is packed_float3 (TiXL Point.Scale is float3); org=1 broadcasts, newValue is scalar.
        float3 s = float3(p.Scale);
        p.Scale = mix(float3(org), s * newValue, strength);
        break;
      }
    }
  }

  float4 gradientColor = GradientImage.sample(ClampedSampler, float2(f, 0.5f), level(0.0f));
  switch (WriteColor) {
    case 1:  // Replace
      p.Color = mix(p.Color, gradientColor, strength);
      break;
    case 2:  // Multiply
      p.Color = mix(p.Color, p.Color * gradientColor, strength);
      break;
  }

  dst[index] = p;
}
