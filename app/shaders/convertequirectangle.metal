// ConvertEquirectangle: TiXL-ported cubemap-cross -> equirectangular panorama remap. Faithful port
// of external/tixl Operators/Lib/Assets/shaders/img/fx/ConvertEquirectangle.hlsl psMain (verbatim
// below, line refs to that file).
//
// Kernel (verbatim):
//   phi = uv.x * PI * 2; theta = uv.y * PI                                          // :26-27
//   x = -sin(phi)*sin(theta); y = cos(theta); z = -cos(phi)*sin(theta)              // :29-31
//   a = max(|x|, max(|y|, |z|)); xa=x/a; ya=y/a; za=z/a                             // :33-36
//   c.rgb = (x,y,z); c.a = 1 (unconditional fallback, THEN maybe overwritten below) // :38 (c=(1,1,0,1)
//                                                                                        init) + :39
//   if      |xa-1| < 1e-3: srcUV = ((-za,-ya)+1)/2; srcUV.x = srcUV.x/6 + 0/6; c.rgb = Image(srcUV)
//   else if |xa+1| < 1e-3: srcUV = (( za,-ya)+1)/2; srcUV.x = srcUV.x/6 + 2/6; c.rgb = Image(srcUV)
//   else if |za-1| < 1e-3: srcUV = (( xa,-ya)+1)/2; srcUV.x = srcUV.x/6 + 3/6; c.rgb = Image(srcUV)
//   else if |za+1| < 1e-3: srcUV = ((-xa,-ya)+1)/2; srcUV.x = srcUV.x/6 + 1/6; c.rgb = Image(srcUV)
//   else if |ya-1| < 1e-3: srcUV = ((-xa,-za)+1)/2; srcUV.x = srcUV.x/6 + 4/6; c.rgb = Image(srcUV)
//   else if |ya+1| < 1e-3: srcUV = (( xa,-za)+1)/2; srcUV.x = srcUV.x/6 + 5/6; c.rgb = Image(srcUV)
//   (no branch fires only at genuine numerical ties, e.g. exact cube edges/corners -> c.rgb stays
//   the raw (x,y,z) direction, byte-identical to the HLSL's own fallthrough)                // :42-72
//
// Forks (named, DX11 pixel-shader -> Metal fullscreen-triangle VS+FS; same fork class as
// ColorGradeDepth/Displace):
//   - DX11 psMain (VS+PS fed by a fullscreen-quad Draw) -> Metal fullscreen-triangle VS+FS.
//   - `Image.GetDimensions(width,height)` (:24) is DEAD CODE in the original (the shadowed locals
//     are written, then immediately overwritten by TargetWidth/TargetHeight from the cbuffer, and
//     THAT pair is never read again either) — dropped; zero shader constants needed for the real
//     math. See convertequirectangle_params.h header for why FaceCount exists anyway (test-only).
//   - Single TiXL sampler (ConvertEquirectangle.t3 SamplerState :182-201, Clamp/Clamp/Clamp, no
//     explicit Filter -> host default) -> we bind ONE Linear+ClampToEdge sampler (same convention as
//     ColorGradeDepth/Displace/RgbTV). [named fork: assumed filter mode, inert for the golden — every
//     golden face-column here is a UNIFORM solid color, so point vs linear sampling is byte-identical.]
#include <metal_stdlib>
#include "convertequirectangle_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut convertequirectangle_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC up vs texture down
  return o;
}

constant float PI = 3.14159265359f;

fragment float4 convertequirectangle_fs(VSOut in [[stage_in]],
                                        texture2d<float> Image [[texture(0)]],
                                        sampler texSampler     [[sampler(0)]],
                                        constant ConvertEquirectangleParams& P
                                            [[buffer(CONVERTEQUIRECTANGLE_Params)]]) {
  float2 uv = in.texCoord;
  const float fc = P.FaceCount;  // production 6.0 (== HLSL's hardcoded /6); golden injectBug: 3.0

  float phi = uv.x * PI * 2.0f;      // :26
  float theta = uv.y * PI;           // :27

  float x = sin(phi) * sin(theta) * -1.0f;   // :29
  float y = cos(theta);                       // :30
  float z = cos(phi) * sin(theta) * -1.0f;    // :31

  float a = max(abs(x), max(abs(y), abs(z)));  // :33
  float xa = x / a;                             // :34
  float ya = y / a;                             // :35
  float za = z / a;                             // :36

  float4 c = float4(x, y, z, 1.0f);  // :38-39 fallback (init c=(1,1,0,1), then c.rgb=(x,y,z))

  if (abs(xa - 1.0f) < 1e-3f) {
    float2 s = (float2(-za, -ya) + 1.0f) * 0.5f;
    s.x = s.x / fc + 0.0f / fc;
    c.rgb = Image.sample(texSampler, s).rgb;
  } else if (abs(xa + 1.0f) < 1e-3f) {
    float2 s = (float2(za, -ya) + 1.0f) * 0.5f;
    s.x = s.x / fc + 2.0f / fc;
    c.rgb = Image.sample(texSampler, s).rgb;
  } else if (abs(za - 1.0f) < 1e-3f) {
    float2 s = (float2(xa, -ya) + 1.0f) * 0.5f;
    s.x = s.x / fc + 3.0f / fc;
    c.rgb = Image.sample(texSampler, s).rgb;
  } else if (abs(za + 1.0f) < 1e-3f) {
    float2 s = (float2(-xa, -ya) + 1.0f) * 0.5f;
    s.x = s.x / fc + 1.0f / fc;
    c.rgb = Image.sample(texSampler, s).rgb;
  } else if (abs(ya - 1.0f) < 1e-3f) {
    float2 s = (float2(-xa, -za) + 1.0f) * 0.5f;
    s.x = s.x / fc + 4.0f / fc;
    c.rgb = Image.sample(texSampler, s).rgb;
  } else if (abs(ya + 1.0f) < 1e-3f) {
    float2 s = (float2(xa, -za) + 1.0f) * 0.5f;
    s.x = s.x / fc + 5.0f / fc;
    c.rgb = Image.sample(texSampler, s).rgb;
  }

  return c;
}
