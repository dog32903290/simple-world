// Shared host<->shader params for the TiXL-ported DoyleSpiralPoints2 GENERATOR.
// Authority:
//   external/tixl/Operators/Lib/point/generate/DoyleSpiralPoints2.cs  (high-level InputSlots)
//   external/tixl/Operators/Lib/point/generate/DoyleSpiralPoints2.t3  (compound graph wiring)
//   external/tixl/Operators/Lib/point/generate/_DoyleSpiralRoot.cs    (CPU Newton-Raphson algebra)
//   external/tixl/Operators/Lib/Assets/shaders/points/generate/DoyleSpiralPoints.hlsl (GPU kernel)
//
// DoyleSpiralPoints2 is a COMPOUND op in TiXL: the high-level inputs (Steps/Offset/
// PointsPerStep/SpiralSteepness/Scale/ScaleBias/CenterPositionScale/W/WBias/CenterSizeScale/
// Center/OrientationAxis/OrientationAngle) are wired through a .t3 graph that:
//   1. clamps PointsPerStep to 1..100  -> P  (cbuffer.P, and _DoyleSpiralRoot.P)
//   2. feeds SpiralSteepness           -> Q  (cbuffer.Q, and _DoyleSpiralRoot.Q)
//   3. runs _DoyleSpiralRoot's 2D Newton-Raphson on (P,Q) -> A=(AMag,AAng), B=(BMag,BAng), R
//   4. routes the rest verbatim into the cbuffer:
//        Scale<-Scale, Offset<-Offset, Bias<-WBias, Bias2<-ScaleBias,
//        CutOff<-CenterPositionScale, CutOff2<-CenterSizeScale,
//        Center<-Center, W<-W, OrientationAxis<-OrientationAxis, OrientationAngle<-OrientationAngle
//   5. sizes the output buffer to clamp(Steps,1,10000000) points (= total Count).
//
// The Newton-Raphson (P/Q -> A/B/R) is the "cheap-input != trivial-impl" core: it is CPU work
// done in the cook (mirroring _DoyleSpiralRoot.cs Update), NOT in the kernel. The kernel below
// is a verbatim port of DoyleSpiralPoints.hlsl which only consumes the derived A/B/R.
//
// HLSL cbuffer layout (DoyleSpiralPoints.hlsl lines 4-27), 16-byte rows:
//   row0: P, Q, __padding1, __padding2
//   row1: Center.xyz, W
//   row2: OrientationAxis.xyz, OrientationAngle
//   row3: AMag, AAng, BMag, BAng
//   row4: R, Scale, Offset, Bias
//   row5: Bias2, CutOff, CutOff2, (pad)
//
// In TiXL the kernel reads the buffer length via ResultPoints.GetDimensions(count,..). On Metal
// the dispatched buffer length is not queryable inside the kernel the same way, so the cook
// passes the element count explicitly in the cbuffer (Count field, occupying TiXL's __padding1
// slot — purely a host->kernel transport, not a TiXL-semantics fork).
//
// FORKS (named):
//   - Newton-Raphson done in double on CPU (TiXL: _DoyleSpiralRoot.cs uses System.Math double).
//   - On a non-convergent / singular solve we emit A=B=R=0 (TiXL: FindRootAngles returns 0s);
//     the kernel then yields mag=CutOff for every point (degenerate cluster) — same as TiXL.
//   - ToRad const in the kernel mirrors TiXL's literal `3.141578/180` (a typo for PI in TiXL).
//     We keep the typo'd literal for byte-parity with TiXL's shader; see kernel note.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct DoyleSpiralParams {
  // row0
  float P;            // = clamp(PointsPerStep, 1, 100)
  float Q;            // = SpiralSteepness
  float Count;        // = output buffer element count (TiXL: GetDimensions count; here host-passed)
  float _padding2;
  // row1
  float CenterX, CenterY, CenterZ;  // TiXL Center (Vector3)
  float W;                          // TiXL W (Single)
  // row2
  float OrientAxisX, OrientAxisY, OrientAxisZ;  // TiXL OrientationAxis (Vector3)
  float OrientAngle;                            // TiXL OrientationAngle (Single, degrees)
  // row3 — derived by _DoyleSpiralRoot Newton-Raphson
  float AMag, AAng, BMag, BAng;
  // row4
  float R;        // derived radius-ratio
  float Scale;    // TiXL Scale
  float Offset;   // TiXL Offset
  float Bias;     // TiXL WBias  (kernel: W exponent)
  // row5
  float Bias2;    // TiXL ScaleBias (kernel: mag exponent)
  float CutOff;   // TiXL CenterPositionScale
  float CutOff2;  // TiXL CenterSizeScale
  float _pad3;
};

enum DoyleSpiralBinding {
  DOYLE_Points = 0,  // device SwPoint* (u0)
  DOYLE_Params = 1,  // constant DoyleSpiralParams& (b0)
};

#ifndef __METAL_VERSION__
// 6 rows x 16 bytes = 96 bytes.
static_assert(sizeof(DoyleSpiralParams) == 96, "DoyleSpiralParams must be 96 bytes (6x16)");
#endif
