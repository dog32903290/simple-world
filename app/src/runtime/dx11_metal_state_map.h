// runtime/dx11_metal_state_map — the CLOSED-FORM DX11 render-state → Metal enum table (Seam 2).
//
// ZONE: runtime (pure computation). NO Metal include — the table is plain integer constants whose
// values EQUAL the corresponding MTL::* enum integers (asserted against <Metal> only in the cook
// leaf + the golden, never here). A leaf that needs the real MTL enum casts these uint32_t through.
//
// PARITY AUTHORITY: docs/agent/census/DX11_METAL_CONVERSION_TABLE.md (deep-research wwbzxhsrd, the
// Seam 2 verification authority) + the census in SEAM2_RENDERSTATE_BUILD_PLAN.md §1. Every value here
// is a Bucket-A CLOSED-FORM transform: a flat enum→enum lookup with an exact, picture-free answer.
// The --selftest-blendstate / --selftest-rasterizerstate goldens assert each row against this table;
// the cook leaf (point_ops_rendertarget.cpp materializeFrozenPSO) reads it to build the PSO.
//
// DATA-DRIVEN (ARCHITECTURE rule 7): the mapping lives in ONE place (these constexpr switch fns), so a
// new factor/op/cull = one new case, asserted by the golden's full-table sweep. The cook leaf NEVER
// hand-writes a `BlendFactorSourceAlpha` literal — it calls metalBlendFactor(SourceAlpha).
//
// ★The integer values below are the metal-cpp enum integers (MTLRenderPipeline.hpp / MTLRenderCommand-
// Encoder.hpp / MTLDepthStencil.hpp). They are STABLE Metal ABI. A leaf static_asserts a handful against
// the real MTL:: enums (compile-time tripwire if Apple ever renumbers — they won't, the ABI is frozen).
#pragma once

#include <cstdint>

namespace sw {

// ── DX11 BLEND factor (D3D11_BLEND subset the TiXL census actually wires; see PLAN §1) ──
// Values are arbitrary sw-local ordinals (NOT the DX11 numbers — we never serialize DX11 enums; the
// .t3 carries TiXL's own BlendOptions which the op maps to these). The CLOSED-FORM content is the
// metalBlendFactor() answer, not the ordinal.
enum class Dx11Blend : uint32_t {
  Zero = 0,
  One = 1,
  SrcColor = 2,
  InvSrcColor = 3,
  SrcAlpha = 4,
  InvSrcAlpha = 5,
  DestColor = 6,
  InvDestColor = 7,
  DestAlpha = 8,
  InvDestAlpha = 9,
  // Dual-source (Bucket-C census: NEVER wired → guards only; mapping still closed-form per the table).
  Src1Color = 15,
  InvSrc1Color = 16,
  Src1Alpha = 17,
  InvSrc1Alpha = 18,
};

// ── DX11 BLEND op (D3D11_BLEND_OP; census wires Add / ReverseSubtract / Minimum) ──
enum class Dx11BlendOp : uint32_t {
  Add = 0,
  Subtract = 1,
  ReverseSubtract = 2,
  Min = 3,
  Max = 4,
};

// ── DX11 CULL (D3D11_CULL_MODE; census wires None / Back only — Front never appears) ──
enum class Dx11Cull : uint32_t {
  None = 0,
  Front = 1,
  Back = 2,
};

// ── DX11 FILL (D3D11_FILL_MODE; census wires Solid only — Wireframe dormant fork) ──
enum class Dx11Fill : uint32_t {
  Wireframe = 0,
  Solid = 1,
};

// ── DX11 PRIMITIVE TOPOLOGY (D3D_PRIMITIVE_TOPOLOGY; SharpDX PrimitiveTopology mirrors the native enum 1:1).
// The InputAssemblerStage's ONLY closed-form output (InputLayout/VertexBuffers/IndexBuffer are the named FORK —
// dropped: sw's VS is SV_VertexID-driven, buffers are bound by the Draw leaf, not an IA input layout). Census
// (PLAN §1 / grep of external/tixl): every TiXL consumer leaves PrimitiveTopology at its .t3 default TriangleList
// (InputAssemblerStage.t3 DefaultValue "TriangleList"); the non-triangle rows ship for completeness (sw could
// wire them) but are dormant. The ordinals below are the D3D_PRIMITIVE_TOPOLOGY native values (Undefined=0,
// PointList=1, LineList=2, LineStrip=3, TriangleList=4, TriangleStrip=5) — the same integers SharpDX's enum
// carries, so a serialized .t3 topology int maps directly. Adjacency/patch topologies are out of census scope.
enum class Dx11Topology : uint32_t {
  Undefined = 0,
  PointList = 1,
  LineList = 2,
  LineStrip = 3,
  TriangleList = 4,  // DX11 + TiXL default (every census consumer)
  TriangleStrip = 5,
};

// ── DX11 depth compare (D3D11_COMPARISON_FUNC; default LESS, mesh uses LESS_EQUAL) ──
enum class Dx11Compare : uint32_t {
  Never = 0,
  Less = 1,
  Equal = 2,
  LessEqual = 3,
  Greater = 4,
  NotEqual = 5,
  GreaterEqual = 6,
  Always = 7,
};

// ───────────────── CLOSED-FORM transforms → Metal enum integers (the authority) ─────────────────
// Each fn IS the conversion table row. The DX 'Inv'(1-x) maps to Metal 'OneMinus' (table §1 row
// "Blend factor + op mapping"). Returns the metal-cpp enum integer (== MTL::BlendFactor* value).
constexpr uint32_t metalBlendFactor(Dx11Blend b) {
  switch (b) {
    case Dx11Blend::Zero:         return 0;   // MTL::BlendFactorZero
    case Dx11Blend::One:          return 1;   // MTL::BlendFactorOne
    case Dx11Blend::SrcColor:     return 2;   // MTL::BlendFactorSourceColor
    case Dx11Blend::InvSrcColor:  return 3;   // MTL::BlendFactorOneMinusSourceColor
    case Dx11Blend::SrcAlpha:     return 4;   // MTL::BlendFactorSourceAlpha
    case Dx11Blend::InvSrcAlpha:  return 5;   // MTL::BlendFactorOneMinusSourceAlpha
    case Dx11Blend::DestColor:    return 6;   // MTL::BlendFactorDestinationColor
    case Dx11Blend::InvDestColor: return 7;   // MTL::BlendFactorOneMinusDestinationColor
    case Dx11Blend::DestAlpha:    return 8;   // MTL::BlendFactorDestinationAlpha
    case Dx11Blend::InvDestAlpha: return 9;   // MTL::BlendFactorOneMinusDestinationAlpha
    case Dx11Blend::Src1Color:    return 15;  // MTL::BlendFactorSource1Color
    case Dx11Blend::InvSrc1Color: return 16;  // MTL::BlendFactorOneMinusSource1Color
    case Dx11Blend::Src1Alpha:    return 17;  // MTL::BlendFactorSource1Alpha
    case Dx11Blend::InvSrc1Alpha: return 18;  // MTL::BlendFactorOneMinusSource1Alpha
  }
  return 1;  // unreachable (exhaustive switch); One = safe default
}

constexpr uint32_t metalBlendOp(Dx11BlendOp op) {
  switch (op) {
    case Dx11BlendOp::Add:             return 0;  // MTL::BlendOperationAdd
    case Dx11BlendOp::Subtract:        return 1;  // MTL::BlendOperationSubtract
    case Dx11BlendOp::ReverseSubtract: return 2;  // MTL::BlendOperationReverseSubtract
    case Dx11BlendOp::Min:             return 3;  // MTL::BlendOperationMin
    case Dx11BlendOp::Max:             return 4;  // MTL::BlendOperationMax
  }
  return 0;
}

constexpr uint32_t metalCullMode(Dx11Cull c) {
  switch (c) {
    case Dx11Cull::None:  return 0;  // MTL::CullModeNone
    case Dx11Cull::Front: return 1;  // MTL::CullModeFront
    case Dx11Cull::Back:  return 2;  // MTL::CullModeBack
  }
  return 0;
}

constexpr uint32_t metalFillMode(Dx11Fill f) {
  switch (f) {
    case Dx11Fill::Wireframe: return 1;  // MTL::TriangleFillModeLines
    case Dx11Fill::Solid:     return 0;  // MTL::TriangleFillModeFill
  }
  return 0;
}

constexpr uint32_t metalCompare(Dx11Compare c) {
  switch (c) {
    case Dx11Compare::Never:        return 0;  // MTL::CompareFunctionNever
    case Dx11Compare::Less:         return 1;  // MTL::CompareFunctionLess
    case Dx11Compare::Equal:        return 2;  // MTL::CompareFunctionEqual
    case Dx11Compare::LessEqual:    return 3;  // MTL::CompareFunctionLessEqual
    case Dx11Compare::Greater:      return 4;  // MTL::CompareFunctionGreater
    case Dx11Compare::NotEqual:     return 5;  // MTL::CompareFunctionNotEqual
    case Dx11Compare::GreaterEqual: return 6;  // MTL::CompareFunctionGreaterEqual
    case Dx11Compare::Always:       return 7;  // MTL::CompareFunctionAlways
  }
  return 1;
}

// PrimitiveTopology → MTL::PrimitiveType (the InputAssemblerStage's closed-form row). D3D primitive-LIST/STRIP
// discriminator maps to Metal's PrimitiveType. NOT arithmetic (`mtl = d3d-1` happens to hold for these six but
// adjacency/patch topologies — out of census scope — break the pattern), so the table is written explicitly.
// D3D Undefined(0) has no Metal counterpart → clamp to Triangle (the TiXL default; an Undefined topology never
// reaches a real draw). Returns the metal-cpp enum integer (== MTL::PrimitiveType* value).
constexpr uint32_t metalPrimitiveType(Dx11Topology t) {
  switch (t) {
    case Dx11Topology::PointList:     return 0;  // MTL::PrimitiveTypePoint
    case Dx11Topology::LineList:      return 1;  // MTL::PrimitiveTypeLine
    case Dx11Topology::LineStrip:     return 2;  // MTL::PrimitiveTypeLineStrip
    case Dx11Topology::TriangleList:  return 3;  // MTL::PrimitiveTypeTriangle
    case Dx11Topology::TriangleStrip: return 4;  // MTL::PrimitiveTypeTriangleStrip
    case Dx11Topology::Undefined:     return 3;  // no Metal equiv → Triangle (TiXL default; never drawn)
  }
  return 3;  // unreachable; Triangle = safe default
}

// FrontCounterClockwise → MTL winding. DX11 default FrontCounterClockwise=FALSE = CW front
// (table §1 "Default rasterizer state"). Metal default is CCW, so the cook MUST set this explicitly.
constexpr uint32_t metalWinding(bool frontCounterClockwise) {
  return frontCounterClockwise ? 1u   // MTL::WindingCounterClockwise
                               : 0u;  // MTL::WindingClockwise
}

}  // namespace sw
