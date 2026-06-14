// ChromaKey: TiXL-ported HSB-distance chroma keyer, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/ChromaKey.hlsl psMain.
//
// Kernel (verbatim, ChromaKey.hlsl):
//   rgb2hsb(c)          — RGB -> (hue, sat, brightness) (lines 36-49, the iq/standard form)
//   HueDistance2(a,b)   — circular hue distance, min(|a-b|, 1-|a-b|) (lines 55-62)
//   GetColorDistance(c) — hsb = rgb2hsb(saturate(c.rgb)); keyHsb = rgb2hsb(KeyColor.rgb);
//                         weights = (smoothstep(0,1,hsb.y*10)*WeightHue, WeightSat, WeightBright);
//                         distance = saturate(length((HueDistance2(h,kh),
//                                                     hsb.yz-keyHsb.yz) * weights)*Exposure
//                                             - Amplify)                       (lines 65-77)
//   sx = ChokeRadius/width; sy = ChokeRadius/height
//   distance = min(center, min(min(y1,y2), min(x1,x2)))  over the 4-neighbourhood
//   Mode < 0.5 : (c.rgb, saturate(distance*c.a))
//   Mode < 1.5 : lerp(lerp(c, Background, c.a), c, distance)
//   Mode < 2.5 : lerp(1, Background, distance)
//   else       : lerp((c.rgb, saturate(1-distance*c.a)),
//                      lerp(Background, c, saturate(1-distance*c.a)), Background.a)
//
// Forks (named, DX11->Metal):
//   - DX11 PS -> Metal fullscreen-triangle VS+FS (same fork class as DetectEdges/Tint).
//   - HLSL GetDimensions -> Metal Image.get_width()/get_height() (no Resolution cbuffer/port).
//   - b1 TimeConstants unused -> not bound. HLSL `static float PI=3.141578` unused -> dropped.
//   - GetColorDistance is scalar in TiXL but stored into float4 locals y1/x1.. then min()'d
//     (HLSL float->float4 splat); we keep it scalar (equivalent — all 4 lanes equal).
//   - Fixed linear+clamp sampler.
#include <metal_stdlib>
#include "chromakey_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

vertex VSOut chromakey_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// Mirror of ChromaKey.hlsl rgb2hsb (lines 36-49).
static inline float3 rgb2hsb(float3 c) {
  float4 K = float4(0.0f, -1.0f / 3.0f, 2.0f / 3.0f, -1.0f);
  float4 p = mix(float4(c.b, c.g, K.w, K.z), float4(c.g, c.b, K.x, K.y), step(c.b, c.g));
  float4 q = mix(float4(p.x, p.y, p.w, c.r), float4(c.r, p.y, p.z, p.x), step(p.x, c.r));

  float d = q.x - min(q.w, q.y);
  float e = 1.0e-10f;

  return float3(abs(q.z + (q.w - q.y) / (6.0f * d + e)),
                d / (q.x + e),
                q.x * 0.5f);
}

// Mirror of ChromaKey.hlsl HueDistance2 (lines 55-62).
static inline float HueDistance2(float hue1, float hue2) {
  float hueDistance = abs(hue1 - hue2);
  return min(hueDistance, 1.0f - hueDistance);
}

// Mirror of ChromaKey.hlsl GetColorDistance (lines 65-77).
static inline float GetColorDistance(float4 c, constant ChromaKeyParams& P) {
  float3 hsb = rgb2hsb(saturate(c.rgb));
  float3 keyColorHsb = rgb2hsb(float3(P.KeyR, P.KeyG, P.KeyB));
  float3 weights = float3(smoothstep(0.0f, 1.0f, hsb.y * 10.0f) * P.WeightHue,
                          P.WeightSaturation, P.WeightBrightness);
  float distance = saturate(
      length(float3(abs(HueDistance2(hsb.x, keyColorHsb.x)),  // Hue
                    (hsb.y - keyColorHsb.y),                  // Saturation
                    (hsb.z - keyColorHsb.z))                  // Brightness
             * weights) * P.Exposure - P.Amplify);
  return distance;
}

// Mirror of ChromaKey.hlsl psMain.
fragment float4 chromakey_fs(VSOut in [[stage_in]],
                             texture2d<float> Image      [[texture(0)]],
                             sampler texSampler          [[sampler(0)]],
                             constant ChromaKeyParams& P  [[buffer(CHROMAKEY_Params)]]) {
  float width  = (float)Image.get_width();
  float height = (float)Image.get_height();

  float sx = P.ChokeRadius / width;
  float sy = P.ChokeRadius / height;

  float2 uv = in.texCoord;
  float4 c = Image.sample(texSampler, uv, level(0.0f));

  float distanceCenter = GetColorDistance(c, P);

  float y1 = GetColorDistance(Image.sample(texSampler, float2(uv.x, uv.y + sy)), P);
  float y2 = GetColorDistance(Image.sample(texSampler, float2(uv.x, uv.y - sy)), P);
  float x1 = GetColorDistance(Image.sample(texSampler, float2(uv.x + sx, uv.y)), P);
  float x2 = GetColorDistance(Image.sample(texSampler, float2(uv.x - sx, uv.y)), P);

  float distance = min(distanceCenter, min(min(y1, y2), min(x1, x2)));

  float4 Background = float4(P.BgR, P.BgG, P.BgB, P.BgA);

  if (P.Mode < 0.5f) {
    return float4(c.rgb, saturate(distance * c.a));
  }

  if (P.Mode < 1.5f) {
    return mix(mix(c, Background, c.a), c, distance);
  }

  if (P.Mode < 2.5f) {
    return mix(float4(1.0f), Background, distance);
  }

  float k = saturate(1.0f - distance * c.a);
  return mix(float4(c.rgb, k), mix(Background, c, k), Background.a);
}
