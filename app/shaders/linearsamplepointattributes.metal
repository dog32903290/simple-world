// linearsamplepointattributes — faithful port of external/tixl
// .../Assets/shaders/points/modify/LinearSamplePointAttributes.hlsl. A Points op with a Texture2D INPUT
// that samples the texture along the point INDEX (uv = (i/pointCount, 0.5) — a 1D linear strip) and
// routes the sampled R/G/B/Brightness(L) channels — each through a per-channel Factor/Offset gain —
// into a SELECTED point attribute (position xyz / F1 / rotate xyz / stretch xyz / F2), blended by
// Strength. UNLIKE SamplePointColorAttributes / AttributesFromImageChannels there is NO
// transformSampleSpace and NO Center: the sample uv is purely the normalized point index.
//
// TiXL parity (LinearSamplePointAttributes.hlsl:48-189), reproduced VERBATIM including its quirks:
//   strength = Strength * (StrengthFactor==0?1 : StrengthFactor==1?p.FX1 : p.FX2);
//   divider  = pointCount<2 ? 1 : pointCount;  f = i.x / divider;  uv = (f, 0.5);
//   c = inputTexture.SampleLevel(s, uv, 0);  gray = (c.r+c.g+c.b)/3;
//   // Rotation: each axis factor = sum over channels routed to 5/6/7; tau = 3.141578/180 (TiXL TYPO,
//   // kept verbatim — NOT pi/180); rot2 = qMul chain (X then Y then Z); rot = orgRot;
//   p.Rotation = qSlerp(p.Rotation, qMul(rot, rot2), strength);
//   // Stretch: product of channels routed to 8/9/10 (default 1 per axis); Mode<0.5 ? factor : factor*Scale;
//   p.Scale *= lerp(1, stretchOffset, strength);
//   // Position: ff = sum over FactorsForPositionAndW[clamp(ch,0,5.1)] * (channel*Factor+Offset);
//   offset = Mode<0.5 ? ff.xyz : ff.xyz*p.Position; if(TranslationSpace>0.5) offset = rotate(offset, p.Rotation);
//   newPos = p.Position + offset; if(RotationSpace<0.5) newPos = rotate(newPos, rot2);
//   p.Position = lerp(p.Position, newPos, strength);
//   // FX1 (TiXL DOUBLE-write quirk, kept verbatim): p.FX1 += fx1Factor; then
//   //   p.FX1 = lerp(p.FX1, Mode==0 ? p.FX1+fx1Factor : p.FX1*(1+fx1Factor), strength);
//   // FX2: p.FX2 = lerp(p.FX2, Mode==0 ? p.FX2+fx2Factor : p.FX2*(1+fx2Factor), strength);
//
// FORKS / VERBATIM-QUIRKS (named):
//   [fork-tau-typo]  TiXL's `tau = 3.141578 / 180` (.hlsl:102) is a typo for pi/180 — reproduced EXACTLY
//                    so the rotation angles byte-match TiXL (do NOT "fix" to M_PI_F/180).
//   [fork-fx1-double-write]  .hlsl:171-173 writes p.FX1 twice (`+=` then a `lerp` that ALSO adds
//                    fx1Factor when Mode==0). Reproduced verbatim — a TiXL quirk, not a port bug.
//   [fork-factorsforpositionandw]  index 4 (For_F1) maps the table to .w (unused: ff.xyz only); F1
//                    routing actually moves FX1 via the separate fx1Factor branch (channel==4).
#include <metal_stdlib>
#include "tixl_point.h"                          // SwPoint (64B)
#include "linearsamplepointattributes_params.h"  // LspaParams, LSPA_* bindings
#include "shared/quat.metal.h"                    // qFromAngleAxis, qMul, qRotateVec3, qSlerp
using namespace metal;

// Attribute routing indices — verbatim from LinearSamplePointAttributes.cs Attributes enum.
#define LATTR_NotUsed   0
#define LATTR_For_X     1
#define LATTR_For_Y     2
#define LATTR_For_Z     3
#define LATTR_For_F1    4
#define LATTR_Rotate_X  5
#define LATTR_Rotate_Y  6
#define LATTR_Rotate_Z  7
#define LATTR_Stretch_X 8
#define LATTR_Stretch_Y 9
#define LATTR_Stretch_Z 10
#define LATTR_For_F2    11

// FactorsForPositionAndW[6] — verbatim from LinearSamplePointAttributes.hlsl:4-12. Indexed by an
// Attribute id clamped to [0,5.1]: 0 nothing, 1->x, 2->y, 3->z, 4->w, 5->0 (avoid rotation effects).
constant float4 kFactorsForPositionAndW[6] = {
    float4(0, 0, 0, 0),  // 0 nothing
    float4(1, 0, 0, 0),  // 1 for x
    float4(0, 1, 0, 0),  // 2 for y
    float4(0, 0, 1, 0),  // 3 for z
    float4(0, 0, 0, 1),  // 4 for w
    float4(0, 0, 0, 0),  // 5 avoid rotation effects
};

kernel void linearsamplepointattributes(const device SwPoint* src [[buffer(LSPA_SourcePoints)]],
                                        device SwPoint*       dst [[buffer(LSPA_ResultPoints)]],
                                        constant LspaParams&  P   [[buffer(LSPA_Params)]],
                                        texture2d<float>      inputTexture [[texture(LSPA_InputTexture)]],
                                        sampler               texSampler   [[sampler(LSPA_TexSampler)]],
                                        uint                  tid [[thread_position_in_grid]]) {
  if (tid >= P.Count) return;
  SwPoint p = src[tid];

  float strength = P.Strength *
                   (P.StrengthFactor == 0 ? 1.0f : (P.StrengthFactor == 1 ? p.FX1 : p.FX2));

  // uv = (index / divider, 0.5). divider = pointCount<2 ? 1 : pointCount (.hlsl:62-66).
  float divider = (P.Count < 2u) ? 1.0f : (float)P.Count;
  float f = (float)tid / divider;
  float2 uv = float2(f, 0.5f);

  float4 c = inputTexture.sample(texSampler, uv, level(0.0f));  // SampleLevel(...,0) -> explicit LOD 0
  float gray = (c.r + c.g + c.b) / 3.0f;

  // ── Rotation (.hlsl:73-105) ──
  float4 rot = p.Rotation;  // orgRot snapshot
  float rotXFactor = (P.R == 5 ? (c.r * P.RFactor + P.ROffset) : 0.0f) +
                     (P.G == 5 ? (c.g * P.GFactor + P.GOffset) : 0.0f) +
                     (P.B == 5 ? (c.b * P.BFactor + P.BOffset) : 0.0f) +
                     (P.L == 5 ? (gray * P.LFactor + P.LOffset) : 0.0f);
  float rotYFactor = (P.R == 6 ? (c.r * P.RFactor + P.ROffset) : 0.0f) +
                     (P.G == 6 ? (c.g * P.GFactor + P.GOffset) : 0.0f) +
                     (P.B == 6 ? (c.b * P.BFactor + P.BOffset) : 0.0f) +
                     (P.L == 6 ? (gray * P.LFactor + P.LOffset) : 0.0f);
  float rotZFactor = (P.R == 7 ? (c.r * P.RFactor + P.ROffset) : 0.0f) +
                     (P.G == 7 ? (c.g * P.GFactor + P.GOffset) : 0.0f) +
                     (P.B == 7 ? (c.b * P.BFactor + P.BOffset) : 0.0f) +
                     (P.L == 7 ? (gray * P.LFactor + P.LOffset) : 0.0f);

  const float tau = 3.141578f / 180.0f;  // [fork-tau-typo] TiXL's literal (.hlsl:102), NOT pi/180
  float4 rot2 = float4(0, 0, 0, 1);
  if (rotXFactor != 0.0f) rot2 = qMul(rot2, qFromAngleAxis(rotXFactor * tau, float3(1, 0, 0)));
  if (rotYFactor != 0.0f) rot2 = qMul(rot2, qFromAngleAxis(rotYFactor * tau, float3(0, 1, 0)));
  if (rotZFactor != 0.0f) rot2 = qMul(rot2, qFromAngleAxis(rotZFactor * tau, float3(0, 0, 1)));
  rot2 = normalize(rot2);
  p.Rotation = qSlerp(p.Rotation, qMul(rot, rot2), strength);

  // ── Stretch (.hlsl:108-130) ── product of channels routed to 8/9/10 (default 1 per axis)
  float3 stretchFactor = float3(
      (P.R == 8 ? (c.r * P.RFactor + P.ROffset) : 1.0f) *
      (P.G == 8 ? (c.g * P.GFactor + P.GOffset) : 1.0f) *
      (P.B == 8 ? (c.b * P.BFactor + P.BOffset) : 1.0f) *
      (P.L == 8 ? (gray * P.LFactor + P.LOffset) : 1.0f),

      (P.R == 9 ? (c.r * P.RFactor + P.ROffset) : 1.0f) *
      (P.G == 9 ? (c.g * P.GFactor + P.GOffset) : 1.0f) *
      (P.B == 9 ? (c.b * P.BFactor + P.BOffset) : 1.0f) *
      (P.L == 9 ? (gray * P.LFactor + P.LOffset) : 1.0f),

      (P.R == 10 ? (c.r * P.RFactor + P.ROffset) : 1.0f) *
      (P.G == 10 ? (c.g * P.GFactor + P.GOffset) : 1.0f) *
      (P.B == 10 ? (c.b * P.BFactor + P.BOffset) : 1.0f) *
      (P.L == 10 ? (gray * P.LFactor + P.LOffset) : 1.0f));

  float3 stretchOffset = (P.Mode < 0.5) ? stretchFactor : (stretchFactor * float3(p.Scale));
  p.Scale = float3(p.Scale) * mix(float3(1.0f), stretchOffset, strength);

  // ── Position (.hlsl:133-156) ──
  float4 ff = kFactorsForPositionAndW[(uint)clamp((float)P.L, 0.0f, 5.1f)] * (gray * P.LFactor + P.LOffset) +
              kFactorsForPositionAndW[(uint)clamp((float)P.R, 0.0f, 5.1f)] * (c.r * P.RFactor + P.ROffset) +
              kFactorsForPositionAndW[(uint)clamp((float)P.G, 0.0f, 5.1f)] * (c.g * P.GFactor + P.GOffset) +
              kFactorsForPositionAndW[(uint)clamp((float)P.B, 0.0f, 5.1f)] * (c.b * P.BFactor + P.BOffset);

  float3 offset = (P.Mode < 0.5) ? ff.xyz : (ff.xyz * float3(p.Position));
  if (P.TranslationSpace > 0.5) offset = qRotateVec3(offset, p.Rotation);
  float3 newPos = float3(p.Position) + offset;
  if (P.RotationSpace < 0.5) newPos = qRotateVec3(newPos, rot2);
  p.Position = mix(float3(p.Position), newPos, strength);

  // ── FX1 (.hlsl:159-172) — [fork-fx1-double-write] verbatim ──
  float fx1Factor = (P.R == 4 ? (c.r * P.RFactor + P.ROffset) : 0.0f) +
                    (P.G == 4 ? (c.g * P.GFactor + P.GOffset) : 0.0f) +
                    (P.B == 4 ? (c.b * P.BFactor + P.BOffset) : 0.0f) +
                    (P.L == 4 ? (gray * P.LFactor + P.LOffset) : 0.0f);
  p.FX1 += fx1Factor;
  p.FX1 = mix(p.FX1, (P.Mode == 0) ? (p.FX1 + fx1Factor) : (p.FX1 * (1.0f + fx1Factor)), strength);

  // ── FX2 (.hlsl:174-187) ──
  float fx2Factor = (P.R == 11 ? (c.r * P.RFactor + P.ROffset) : 0.0f) +
                    (P.G == 11 ? (c.g * P.GFactor + P.GOffset) : 0.0f) +
                    (P.B == 11 ? (c.b * P.BFactor + P.BOffset) : 0.0f) +
                    (P.L == 11 ? (gray * P.LFactor + P.LOffset) : 0.0f);
  p.FX2 = mix(p.FX2, (P.Mode == 0) ? (p.FX2 + fx2Factor) : (p.FX2 * (1.0f + fx2Factor)), strength);

  dst[tid] = p;
}
