// VoronoiCells: TiXL-ported Inigo-Quilez Voronoi cell mosaic with correct border distances.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/VoronoiCells.hlsl psMain.
//
// The input texture is sampled as the FEATURE-POINT FIELD: each cell's jittered site comes from
// sampleTexture(cellIndex).xy (animated by Phase), and the final cell colour is the texture
// sampled at the winning cell. iq's two-pass algorithm: pass 1 finds the nearest site, pass 2
// computes the exact distance to the cell border; edges are tinted by EdgeColor via smoothstep.
//
// Kernel (verbatim, HLSL lines 63-148):
//   sampleTexture(p)  = inputTexture.Sample((p+0.5)/Scale / float2(aspectRatio,1))
//   voronoi(x):
//     n = floor(x); f = mod(x, 1)
//     pass1: for j,i in [-1,1]: g=(i,j); o = sampleTexture(n+g).xy; o = 0.5+0.5*sin(Phase+6.2831*o)
//            r = g+o-f; d = dot(r,r); track min -> md, mr, mg
//     pass2: md=8; for j,i in [-2,2]: g = mg+(i,j); color = sampleTexture(n+g); o = color.xy;
//            o = 0.5+0.5*sin(Phase+6.2831*o).xy; r = g+o-f;
//            if dot(mr-r,mr-r) > 1e-5: md = min(md, dot(0.5*(mr+r), normalize(r-mr)))
//     color = sampleTexture(n - 2 + g)   // last g of pass2 loop (verbatim iq quirk)
//     return float3(md, mr)
//   psMain: aspectRatio = TargetWidth/TargetHeight; uv.x *= aspectRatio; p = uv*Scale
//           c = voronoi(p, cellColor); col = Background.rgb * cellColor.rgb
//           col = lerp(EdgeColor.rgb, col, smoothstep(0.04,0.07, c.x - LineWidth*0.1 + 0.1))
//           return float4(col, 1)
//
// Forks (named, DX11->Metal):
//   - DX11 PS (VS+PS) -> Metal fullscreen-triangle VS+FS (same fork class as ChromaticAbberation).
//   - HLSL `static float aspectRatio` mutable global + `out float4 color`: Metal has no writable
//     file-scope global, so aspectRatio / Scale / Phase are threaded as explicit args and the
//     cell colour is returned via an inout/thread reference (semantically identical, named fork).
//   - mod(x,y) = x - y*floor(x/y) (HLSL #define, line 33) inlined.
//   - HLSL b2 Resolution cbuffer (TargetWidth/TargetHeight) bound at Metal cbuffer index 1
//     (host-filled from the output size).
//   - Fixed linear+clamp sampler (TiXL .t3 Wrap=Clamp verbatim).
#include <metal_stdlib>
#include "voronoicells_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut voronoicells_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC up vs texture down
  return o;
}

// mod(x,y) = x - y*floor(x/y) — HLSL #define mod (line 33), applied componentwise.
static float2 mod2(float2 x, float y) { return x - y * floor(x / y); }

// HLSL sampleTexture (lines 63-66). aspectRatio/Scale threaded explicitly (fork: no mutable global).
static float4 sampleTexture(texture2d<float> tex, sampler s, float2 p, float Scale, float aspectRatio) {
  return tex.sample(s, (p + 0.5f) / Scale / float2(aspectRatio, 1.0f));
}

// HLSL voronoi (lines 68-125). `color` (the cell colour) returned via the inout reference.
static float3 voronoi(float2 x, thread float4& color,
                      texture2d<float> tex, sampler s,
                      float Scale, float Phase, float aspectRatio) {
  float2 n = float2(floor(x.x), floor(x.y));
  float2 f = mod2(x, 1.0f);

  float2 mg = float2(0.0f), mr = float2(0.0f);
  color = float4(1.0f);

  // pass 1: regular voronoi (nearest site).
  float md = 8.0f;
  for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++) {
      float2 g = float2((float)i, (float)j);
      float2 o = sampleTexture(tex, s, n + g, Scale, aspectRatio).xy;
      o = 0.5f + 0.5f * sin(Phase + 6.2831f * o);

      float2 r = g + o - f;
      float d = dot(r, r);

      if (d < md) {
        md = d;
        mr = r;
        mg = g;
      }
    }

  // pass 2: distance to borders.
  md = 8.0f;

  float2 g = float2(0.0f);
  for (int j = -2; j <= 2; j++)
    for (int i = -2; i <= 2; i++) {
      g = mg + float2((float)i, (float)j);
      color = sampleTexture(tex, s, n + g, Scale, aspectRatio);
      float2 o = color.xy;

      o = 0.5f + 0.5f * sin(Phase + 6.2831f * o).xy;

      float2 r = g + o - f;

      if (dot(mr - r, mr - r) > 0.00001f)
        md = min(md, dot(0.5f * (mr + r), normalize(r - mr)));
    }
  color = sampleTexture(tex, s, n - 2.0f + g, Scale, aspectRatio);  // verbatim iq quirk (last g)

  return float3(md, mr);
}

// Mirror of VoronoiCells.hlsl psMain.
fragment float4 voronoicells_fs(VSOut in [[stage_in]],
                                texture2d<float> inputTexture [[texture(0)]],
                                sampler texSampler            [[sampler(0)]],
                                constant VoronoiCellsParams& P     [[buffer(VORONOI_Params)]],
                                constant VoronoiCellsResolution& R [[buffer(VORONOI_Resolution)]]) {
  float2 uv = in.texCoord;
  float aspectRatio = R.TargetWidth / R.TargetHeight;

  uv.x *= aspectRatio;

  float2 p = uv * P.Scale;
  float4 cellColor;
  float3 c = voronoi(p, cellColor, inputTexture, texSampler, P.Scale, P.Phase, aspectRatio);

  float3 Background = float3(P.BackgroundR, P.BackgroundG, P.BackgroundB);
  float3 EdgeColor  = float3(P.EdgeColorR, P.EdgeColorG, P.EdgeColorB);

  float3 col = Background * cellColor.rgb;
  col = mix(EdgeColor, col, smoothstep(0.04f, 0.07f, c.x - P.LineWidth * 0.1f + 0.1f));
  return float4(col, 1.0f);
}
