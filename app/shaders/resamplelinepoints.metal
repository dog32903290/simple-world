// resamplelinepoints.metal — faithful Metal port of TiXL's ResampleLinePoints.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/modify/ResampleLinePoints.hlsl
//
// A COUNT-CHANGING MODIFIER: resamples the source line bag into ResultCount points. For output
// point i, f = the normalized parameter along the source list (NOT true arc-length — TiXL samples
// SourcePoints by linear index parameter sourceF = saturate(f)*(SourceCount-1)). Each output is a
// SmoothDistance-weighted average over (1 + 2*SampleCount) parameter taps centred on f.
//
// VERBATIM port of main() (ResampleLinePoints.hlsl:69-129):
//   float fNormlized = (float)i.x / ResultCount;                 // <- /ResultCount (endpoint EXCLUSIVE)
//   float rightFactor = SampleMode > 0.5 ? SampleRange.x : 0;
//   float f = SampleRange.x + fNormlized * (SampleRange.y - rightFactor);
//   if (f < 0 || f > 1) { ResultPoints[i.x].Scale = NAN; return; } // out-of-range -> dead point
//   sumF1 = 0; sampledCount = 0; SamplePosAtF(f);
//   float stepSize = SmoothDistance / (SampleCount * SourceCount);
//   float d = stepSize;
//   float3 minPos = SamplePosAtF(f - d); float3 maxPos = SamplePosAtF(f + d);
//   for (stepIndex 1..SampleCount-1) { d += stepSize; minPos += SamplePosAtF(f-d); maxPos += SamplePosAtF(f+d); }
//   if (sampledCount == 0) sumF1 = NAN;
//   float3 pos = sumPos / sampledCount; ... Color/Scale/FX1/FX2 = sum.. / sampledCount;
//   if (RotationMode == 1) { minPos /= stepSize; maxPos /= stepSize;
//                            tangent = normalize(minPos - maxPos); Rotation = qLookAt(tangent, UpVector); }
//   else                   { Rotation = SampleRotationAtF(f); }
//
// SEPARATOR PRESERVATION (NAMED FORK — none; verbatim TiXL):
//   The source line is split into segments by SEPARATOR points (NaN-Scale).  TiXL's SamplePosAtF
//   bails on line 43: `if (isnan(SourcePoints[index].Scale.x * SourcePoints[index+1].Scale.x)) return;`
//   — when either of the two source samples straddling f is a separator the product is NaN and that
//   tap is skipped (no sumPos add, no sampledCount++).  We port this check VERBATIM, so a tap that
//   lands on a separator boundary is dropped from the average exactly as TiXL drops it.  Output
//   points whose f falls in a fully-separator region get sampledCount==0 -> Scale stays whatever the
//   averaging division produces (NaN), marking them dead downstream — same observable result.
//
// NAMED FORK — OOB guard:
//   The .hlsl reads SourcePoints[index+1] (StructuredBuffer OOB read in HLSL returns 0 silently).
//   At index == SourceCount-1 (f==1) that's OOB. The line-39 guard `if (index > SourceCount-1) return`
//   already bails when index exceeds the last element; with fNormlized=i.x/ResultCount, f<1 for all
//   outputs so index<SourceCount-1 in the common path. To be safe against a real Metal OOB we clamp
//   the +1 read to the last element (SourceCount-1) — at index==SourceCount-1 fraction is ~0 so the
//   lerp endpoint choice is moot, matching the HLSL-returns-0-but-fraction-0 result.
#include <metal_stdlib>
#include "tixl_point.h"                  // SwPoint (64B)
#include "resamplelinepoints_params.h"   // ResampleLineParams, ResampleLineBinding
#include "shared/quat.metal.h"           // qSlerp, qLookAt
using namespace metal;

// Per-thread accumulators (TiXL uses file-static; in Metal each invocation gets its own locals).
struct ResampleAccum {
  float3 sumPos;
  float  sumF1;
  float  sumF2;
  float4 sumColor;
  float3 sumScale;
  int    sampledCount;
};

// VERBATIM port of SamplePosAtF (ResampleLinePoints.hlsl:34-58).
// Accumulates one parameter tap at f into acc; returns the (un-averaged) sampled position.
inline float3 samplePosAtF(float f,
                           device const SwPoint* SourcePoints,
                           uint SourceCount,
                           thread ResampleAccum& acc) {
  float3 pos = float3(0);
  float sourceF = saturate(f) * (float)((int)SourceCount - 1);
  uint index = (uint)(int)sourceF;
  if (index > SourceCount - 1)
    return pos;

  // OOB guard (NAMED FORK above): clamp the +1 neighbour read to the last element.
  uint indexN = min(index + 1u, SourceCount - 1u);

  // Separator check (.hlsl:43): if either straddling sample is a separator (NaN Scale), bail —
  // the tap is dropped from the average (no accumulation, no sampledCount++).
  if (isnan(SourcePoints[index].Scale.x * SourcePoints[indexN].Scale.x)) {
    return pos;
  }

  float fraction = sourceF - (float)index;
  acc.sumF1    += mix(SourcePoints[index].FX1,      SourcePoints[indexN].FX1,      fraction);
  acc.sumF2    += mix(SourcePoints[index].FX2,      SourcePoints[indexN].FX2,      fraction);
  pos           = mix((float3)SourcePoints[index].Position,
                      (float3)SourcePoints[indexN].Position, fraction);
  acc.sumPos   += pos;
  acc.sumColor += mix(SourcePoints[index].Color,    SourcePoints[indexN].Color,    fraction);
  acc.sumScale += mix((float3)SourcePoints[index].Scale,
                      (float3)SourcePoints[indexN].Scale, fraction);

  acc.sampledCount++;
  return pos;
}

// VERBATIM port of SampleRotationAtF (ResampleLinePoints.hlsl:60-67).
inline float4 sampleRotationAtF(float f,
                                device const SwPoint* SourcePoints,
                                uint SourceCount) {
  float sourceF = saturate(f) * (float)((int)SourceCount - 1);
  int index = (int)sourceF;
  float fraction = sourceF - (float)index;
  index = clamp(index, 0, (int)SourceCount - 1);
  // OOB guard: clamp the +1 read (HLSL reads SourcePoints[index+1] unguarded).
  int indexN = min(index + 1, (int)SourceCount - 1);
  return qSlerp(SourcePoints[index].Rotation, SourcePoints[indexN].Rotation, fraction);
}

kernel void resamplelinepoints(
    device const SwPoint*        SourcePoints [[buffer(RESAMPLELINE_SourcePoints)]],
    device       SwPoint*        ResultPoints [[buffer(RESAMPLELINE_ResultPoints)]],
    constant ResampleLineParams& P            [[buffer(RESAMPLELINE_Params)]],
    uint3 i [[thread_position_in_grid]])
{
  if (i.x >= P.ResultCount)
    return;

  uint  SourceCount = P.SourceCount;
  int   SampleMode  = P.SampleMode;
  int   SampleCount = P.SampleCount;
  int   RotationMode = P.RotationMode;
  float2 SampleRange = float2(P.SampleRangeX, P.SampleRangeY);
  float3 UpVector    = float3(P.UpVectorX, P.UpVectorY, P.UpVectorZ);

  float fNormlized = (float)i.x / (float)P.ResultCount;

  float rightFactor = SampleMode > 0.5 ? SampleRange.x : 0.0f;
  float f = SampleRange.x + fNormlized * (SampleRange.y - rightFactor);

  if (f < 0.0f || f > 1.0f) {
    ResultPoints[i.x].Scale = packed_float3(NAN, NAN, NAN);
    return;
  }

  ResampleAccum acc;
  acc.sumPos = float3(0); acc.sumF1 = 0.0f; acc.sumF2 = 0.0f;
  acc.sumColor = float4(0); acc.sumScale = float3(0); acc.sampledCount = 0;

  samplePosAtF(f, SourcePoints, SourceCount, acc);

  // float stepSize = SmoothDistance / (SampleCount * SourceCount);
  float stepSize = P.SmoothDistance / ((float)SampleCount * (float)SourceCount);
  float d = stepSize;

  float3 minPos = samplePosAtF(f - d, SourcePoints, SourceCount, acc);
  float3 maxPos = samplePosAtF(f + d, SourcePoints, SourceCount, acc);

  for (int stepIndex = 1; stepIndex < SampleCount; stepIndex++) {
    d += stepSize;
    minPos += samplePosAtF(f - d, SourcePoints, SourceCount, acc);
    maxPos += samplePosAtF(f + d, SourcePoints, SourceCount, acc);
  }

  if (acc.sampledCount == 0)
    acc.sumF1 = NAN;

  float invN = 1.0f / (float)acc.sampledCount;
  float3 pos = acc.sumPos * invN;
  ResultPoints[i.x].Position = packed_float3(pos);

  ResultPoints[i.x].Color = acc.sumColor * invN;
  ResultPoints[i.x].Scale = packed_float3(acc.sumScale * invN);
  ResultPoints[i.x].FX1   = acc.sumF1 * invN;
  ResultPoints[i.x].FX2   = acc.sumF2 * invN;

  if (RotationMode == 1) {
    minPos /= stepSize;
    maxPos /= stepSize;

    float3 tangent = normalize(minPos - maxPos);
    ResultPoints[i.x].Rotation = qLookAt(tangent, UpVector);
  } else {
    ResultPoints[i.x].Rotation = sampleRotationAtF(f, SourcePoints, SourceCount);
  }
}
