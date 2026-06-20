// Shared host<->shader params for the TiXL-ported RepeatAtPoints GENERATE op (count-product seam).
//
// TiXL authority:
//   external/tixl/Operators/Lib/point/generate/RepeatAtPoints.cs   (12 InputSlots + enums)
//   external/tixl/Operators/Lib/point/generate/RepeatAtPoints.t3    (count graph + child wiring)
//   external/tixl/Operators/Lib/Assets/shaders/points/generate/RepeatAtGPoints.hlsl (the math)
//
// RepeatAtPoints places each Source point into EACH Target point's local frame → the CARTESIAN PRODUCT
// with TiXL's separator expansion: ResultCount = (source.N + sepSrc) * (target.N + sepTgt). The .t3
// derives the buffer length as MultiplyInt(AddInts(source.N, sepSrc), AddInts(target.N, sepTgt)) where
// the two CompareInt gates run Mode=IsEqual (CompareInt.t3 Mode DefaultValue=1, NOT the C# field 0):
//   sepSrc = (CombineMode==0 Linear)     ? BoolToInt(AddSeparators) : 0   (source loop +1, HLSL line 53)
//   sepTgt = (CombineMode==1 Interwoven) ? BoolToInt(AddSeparators) : 0   (target loop +1)
// Production default (Linear, AddSeparators=true) → count = (source.N + 1) * target.N.
//
// The HLSL splits b0 (Scale only) + b1 (the int-mode block). We pack BOTH into one Metal cbuffer
// (separate buffer slots would be byte-identical; one struct is simpler and our setBytes does the
// same upload). 16-byte aligned: 1 float + 7 ints = 32 bytes (rounds to 32, 16-aligned).
//
// TiXL cbuffer (RepeatAtGPoints.hlsl):
//   register b0: float Scale;
//   register b1: int ApplyTargetOrientation, ApplyTargetScale, ScaleFactorMode, SetF1To, SetF2To,
//                    ConnectPointsMode, AddSeperators;   (sic — TiXL's spelling)
//
// Port routing (RepeatAtPoints.cs InputSlot → shader field):
//   Scale (float)                 -> Scale            (b0)
//   ApplyOrientation (bool)       -> ApplyTargetOrientation  (BoolToInt @99a368bc... actually @a77358d8)
//   ApplyPointScale (bool)        -> ApplyTargetScale  (BoolToInt @95230b28)
//   ScaleFactor (UseFSources int) -> ScaleFactorMode
//   SetF1To (UseFSources int)     -> SetF1To
//   SetF2To (UseFSources int)     -> SetF2To
//   CombineMode (ConnectionModes) -> ConnectPointsMode (0=Linear, 1=Interwoven)
//   AddSeparators (bool)          -> AddSeperators (BoolToInt @6f97ffeb)
//
// NAMED FORKS:
//   fork[count-product-core]: ResultCount = (source.N + sepSrc) * (target.N + sepTgt) — the full .t3
//     count graph including the AddSeparators expansion (resolved; see header above). The two CompareInt
//     gates were the trap: their Mode is IsEqual by the symbol .t3 DefaultValue=1, NOT the C# field's 0
//     — reading the C# default would mis-gate Linear↔Interwoven. Verified against RepeatAtGPoints.hlsl
//     self-consistency (Linear sourceLength=source.N+1 ⇒ count must be (source.N+1)*target.N, else the
//     targetIndex %targetPointCount wraps and duplicates a loop).
//   fork[two-cbuffers-merged]: HLSL uses b0+b1; we pack one struct (same bytes uploaded).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct RepeatAtPointsParams {
  float Scale;                    // b0
  int   ApplyTargetOrientation;   // b1.0
  int   ApplyTargetScale;         // b1.1
  int   ScaleFactorMode;          // b1.2  (UseFSources: 0..6 -> factors[] index)
  int   SetF1To;                  // b1.3  (UseFSources)
  int   SetF2To;                  // b1.4  (UseFSources)
  int   ConnectPointsMode;        // b1.5  (0=Linear, 1=Interwoven)
  int   AddSeperators;            // b1.6  (sic; 0/1)
};  // 8 * 4 = 32 bytes, 16-byte aligned

enum RepeatAtPointsBinding {
  REPEATATPOINTS_SourcePoints = 0,  // const device SwPoint* (GPoints / SourcePoints input, t0)
  REPEATATPOINTS_TargetPoints = 1,  // const device SwPoint* (GTargets / TargetPoints input, t1)
  REPEATATPOINTS_Result       = 2,  // device SwPoint*       (output, u0)
  REPEATATPOINTS_Params       = 3,  // constant RepeatAtPointsParams&
};

#ifndef __METAL_VERSION__
static_assert(sizeof(RepeatAtPointsParams) == 32, "RepeatAtPointsParams must be 32 bytes");
#endif
