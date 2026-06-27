// Shared host<->shader params for the TiXL-ported PointColorWithField point MODIFIER (LEAF on the
// direct-Field gather seam, cloned from MoveToSDF). Mirrors external/tixl
// .../Assets/shaders/points/_research/ColorPointsWithField.hlsl (b0/b2 scalars; the field's FloatParams
// ride a SEPARATE assembled buffer at b1 in TiXL / slot 3 here, owned by assembleFieldMSL).
//
// A count-preserving modifier: reads an input bag, evaluates the wired SDF field's COLOR branch at each
// point's position (GetField(float4(pos,1)), w=1 selects the color branch identically to MoveToSDF's
// SetColor), and lerps the point Color toward that field color by `strength`. Count INHERITED.
//
// b0 (ColorPointsWithField.hlsl:8-12) = {Strength, Range}. NOTE: `Range` is declared in b0 but the kernel
// body NEVER reads it (it is dead cbuffer padding inherited from the shared field-template layout; the .t3
// FloatsToBuffer only routes Strength, leaving Range at 0). We keep the slot for byte-layout fidelity.
// b2 (hlsl:19-22) = {StrengthFactor} (FModes: 0=None ->x1, 1=F1 ->p.FX1, 2=F2 ->p.FX2).
//
// We flatten b0 (Strength/Range) + b2 (StrengthFactor) + Count into one 16-byte struct (no packed_float3 /
// matrix traps — particle_params.h discipline). 16-byte alignment maintained via static_assert. This
// struct's layout is byte-identical to the inlined `PcwfParams` in pointcolorwithfield_template.metal.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct PcwfParams {
  float Strength;  // .cs Strength, default 1.0 (lerp toward the field color)
  float Range;     // b0 slot (hlsl:11), DEAD in the kernel body; kept for cbuffer-layout fidelity
#ifdef __METAL_VERSION__
  uint  Count;            // inherited from upstream bag (modifier: count from input)
  int   StrengthFactor;   // .cs StrengthFactor (FModes), default 0=None -> x1
#else
  uint32_t Count;
  int32_t  StrengthFactor;
#endif
};

enum PcwfBinding {
  PCWF_SourcePoints = 0,  // const device SwPoint* (t0)
  PCWF_ResultPoints = 1,  // device SwPoint*       (u0)
  PCWF_Params       = 2,  // constant PcwfParams&  (b0)
  PCWF_FieldParams  = 3,  // constant FieldParams& (the assembled field's packed buffer)
};

#ifndef __METAL_VERSION__
// Strength(4)+Range(4)+Count(4)+StrengthFactor(4) = 16 bytes (one 16B slot)
static_assert(sizeof(PcwfParams) == 16, "PcwfParams must be 16 bytes");
#endif
