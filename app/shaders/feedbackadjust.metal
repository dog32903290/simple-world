// FeedbackAdjustImage — the value-range STABILIZER fragment of AdvancedFeedback. Faithful HLSL->MSL
// port of external/tixl Operators/Lib/Assets/shaders/img/fx/FeedbackAdjustImage.hlsl (every constant
// kept exact). This is the ONE new shader AdvancedFeedback needs; the rest of the warp chain (Blur,
// Displace, Layer2d/Transform) reuses already-landed PSOs.
//
// Pipeline (HLSL psMain, lines 104-159), unchanged:
//   1. 9-tap box average (c + 4 axis + 4 diagonal*0.75) / 8  -> averageGray
//   2. inline edge-detect: sum |neighbour-center| over rgb * DetectEdges / 100
//   3. LimitDarks/LimitBrights value-range stabilizer: pull toward band [0.1, 0.8]
//        lowerD =  pow(clamp(0.1 - gray, 0,10), 2) * LimitDarks      (lifts darks)
//        upperD = -pow(clamp(gray - 0.8, 0,10), 2) * LimitBrights    (crushes brights)
//   4. c.rgb += limitShift + ShiftBrightness + edgeDelta
//   5. rgb->hsv, += (Hue, Saturation, 0), hsv->rgb   (Hue in DEGREES, raw ShiftHue — see op note)
//   6. c.a = clamp(c.a,0,1);  c.rgb = clamp(c.rgb, 0.0001, 1000)
//
// Sampler: linear clamp (same fork class as Blur/Displace — TiXL's MirrorOnce wrap is a fork, named
// in the op; FeedbackAdjust samples a small radius and the clamp edge is invisible at the band).
#include <metal_stdlib>
#include "feedbackadjust_params.h"  // FeedbackAdjustParams, FEEDBACKADJUST_Params
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle (same idiom as blur_vs / displace_vs). texCoord Y-flipped.
vertex VSOut feedbackadjust_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);  // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// HLSL rgbToHsv (FeedbackAdjustImage.hlsl:33-70) — verbatim, h in degrees [0,360).
static float3 rgbToHsv(float r, float g, float b) {
  float delta, h = 0.0f, s, v;
  float tmp = (r < g) ? r : g;
  float mn = (tmp < b) ? tmp : b;

  tmp = (r > g) ? r : g;
  float mx = (tmp > b) ? tmp : b;

  v = mx;
  delta = mx - mn;
  if (mx == mn) {
    return float3(0.0f, 0.0f, mx);
  } else if (mx != 0.0f) {
    s = delta / mx;
  } else {
    s = 0.0f;
    h = 0.0f;
    return float3(h, s, v);
  }
  if (r == mx)
    h = (g - b) / delta;          // between yellow & magenta
  else if (g == mx)
    h = 2.0f + (b - r) / delta;   // between cyan & yellow
  else
    h = 4.0f + (r - g) / delta;   // between magenta & cyan
  h *= 60.0f;                      // degrees
  if (h < 0.0f)
    h += 360.0f;
  return float3(h, s, v);
}

// HLSL hsvToRgb (FeedbackAdjustImage.hlsl:72-101) — verbatim.
static float3 hsvToRgb(float h, float s, float v) {
  float satR, satG, satB;
  if (h < 120.0f) {
    satR = (120.0f - h) / 60.0f;
    satG = h / 60.0f;
    satB = 0.0f;
  } else if (h < 240.0f) {
    satR = 0.0f;
    satG = (240.0f - h) / 60.0f;
    satB = (h - 120.0f) / 60.0f;
  } else {
    satR = (h - 240.0f) / 60.0f;
    satG = 0.0f;
    satB = (360.0f - h) / 60.0f;
  }
  satR = (satR < 1.0f) ? satR : 1.0f;
  satG = (satG < 1.0f) ? satG : 1.0f;
  satB = (satB < 1.0f) ? satB : 1.0f;

  return float3(v * (s * satR + (1.0f - s)),
                v * (s * satG + (1.0f - s)),
                v * (s * satB + (1.0f - s)));
}

fragment float4 feedbackadjust_fs(VSOut in [[stage_in]],
                                  texture2d<float> Image       [[texture(0)]],
                                  sampler texSampler           [[sampler(0)]],
                                  constant FeedbackAdjustParams& P [[buffer(FEEDBACKADJUST_Params)]]) {
  float width = (float)Image.get_width();
  float height = (float)Image.get_height();

  float4 c = Image.sample(texSampler, in.texCoord);

  float sx = P.SampleRadius / width;
  float sy = P.SampleRadius / height;

  float4 y1 = Image.sample(texSampler, float2(in.texCoord.x,      in.texCoord.y + sy));
  float4 y2 = Image.sample(texSampler, float2(in.texCoord.x,      in.texCoord.y - sy));

  float4 x1 = Image.sample(texSampler, float2(in.texCoord.x + sx, in.texCoord.y));
  float4 x2 = Image.sample(texSampler, float2(in.texCoord.x - sx, in.texCoord.y));

  float4 xy1 = Image.sample(texSampler, float2(in.texCoord.x + sx * 0.7f, in.texCoord.y + sy * 0.7f));
  float4 xy2 = Image.sample(texSampler, float2(in.texCoord.x + sx * 0.7f, in.texCoord.y - sy * 0.7f));

  float4 xy3 = Image.sample(texSampler, float2(in.texCoord.x - sx * 0.7f, in.texCoord.y + sy * 0.7f));
  float4 xy4 = Image.sample(texSampler, float2(in.texCoord.x - sx * 0.7f, in.texCoord.y - sy * 0.7f));

  float4 average = (c + y1 + y2 + x1 + x2 + (xy1 + xy2 + xy3 + xy4) * 0.75f) / 8.0f;
  float averageGray = (average.x + average.y + average.z) / 3.0f;

  // Detect Edges
  const float increasedEdgeParmeterResolution = 100.0f;
  float edgeDelta = (
      abs(x1.r - c.r) + abs(x2.r - c.r) + abs(y1.r - c.r) + abs(y2.r - c.r) +
      abs(x1.g - c.g) + abs(x2.g - c.g) + abs(y1.g - c.g) + abs(y2.g - c.g) +
      abs(x1.b - c.b) + abs(x2.b - c.b) + abs(y1.b - c.b) + abs(y2.b - c.b)
    ) * P.DetectEdges / increasedEdgeParmeterResolution;

  // Limit value range
  const float lowerRange = 0.1f;
  const float upperRange = 0.8f;

  float lowerD =  pow(clamp(lowerRange - averageGray, 0.0f, 10.0f), 2.0f) * P.LimitDarks;
  float upperD = -pow(clamp(averageGray - upperRange, 0.0f, 10.0f), 2.0f) * P.LimitBrights;
  float limitShift = lowerD + upperD;

  c.rgb += limitShift + P.ShiftBrightness + edgeDelta;

  // Shift colors
  float3 hsv = rgbToHsv(c.r, c.g, c.b);
  hsv += float3(P.Hue, P.Saturation, 0.0f);
  c.rgb = hsvToRgb(hsv.x, hsv.y, hsv.z);

  c.a = clamp(c.a, 0.0f, 1.0f);
  c.rgb = clamp(c.rgb, 0.0001f, 1000.0f);

  return c;
}

// Final composite: the CURRENT content drawn OVER the warped trail. In TiXL the new content is drawn
// INTO the RenderTarget that already holds the warped trail, via the Command stream (DrawPoints/etc.
// with src-alpha-over blend). We receive the current as a PRE-RENDERED texture, not as draw commands,
// and the RenderTarget clears the current to OPAQUE BLACK (alpha=1 everywhere; point_ops_rendertarget
// .cpp:185 ClearColor (0,0,0,1)) — so the texture alpha CANNOT distinguish drawn content from cleared
// background. We therefore derive coverage from the current's LUMINANCE: drawn (bright) pixels cover
// the trail, cleared (black) pixels leave the trail intact (an empty current -> the trail survives
// unchanged, exactly as an empty Command draws nothing).
//   cov = saturate(max(cur.r, cur.g, cur.b))      (0 where black-cleared, ~1 where content drawn)
//   out = trail*(1-cov) + cur                     (current over trail; black current = pure trail)
// ★NAMED FORK (texture-not-commands): TiXL composites the Command's per-primitive alpha; we only have
// the rasterized result, so luminance stands in for the drawn-content mask. For opaque drawn content
// over a black-cleared current (the production + golden case) the two agree; semi-transparent drawn
// primitives would differ (out of scope for a texture-input clone). Kept as a dedicated 2-input
// fragment (no BlendParams cbuffer -> Cut55 routing-trap avoidance). texture(0)=trail, texture(1)=cur.
fragment float4 feedbackadjust_composite_fs(VSOut in [[stage_in]],
                                            texture2d<float> trailTex   [[texture(0)]],
                                            texture2d<float> currentTex [[texture(1)]],
                                            sampler texSampler          [[sampler(0)]]) {
  float4 trail = trailTex.sample(texSampler, in.texCoord);
  float4 cur = currentTex.sample(texSampler, in.texCoord);
  float cov = clamp(max(cur.r, max(cur.g, cur.b)), 0.0f, 1.0f);  // drawn-content mask from luminance
  float3 rgb = trail.rgb * (1.0f - cov) + cur.rgb;               // current over trail (black = trail)
  return float4(rgb, 1.0f);                                       // opaque trail (frame format alpha)
}
