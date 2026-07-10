// runtime/point_ops_inputassembler — Seam 2: TiXL InputAssemblerStage → Metal primitive topology.
//
// TiXL InputAssemblerStage.cs is an immediate-mode DX11 context mutator: it saves the current
// PrimitiveTopology, sets a new one for its subtree, and restores on pop (Restore). Its other three inputs
// — InputLayout / VertexBuffers / IndexBuffer — bind the DX11 input-assembler VERTEX SOURCING.
//
// ★NAMED FORK (per BUILD_PLAN §3 line 86, NOT a silent drop): sw's vertex shaders are ALL SV_VertexID-driven
// (render_command.h DrawKind: every kind synthesizes its geometry in the VS from vertexID; DrawKind::Explicit
// draws N bare vertices of the bound shader). There is NO fixed-function input layout and NO app-supplied
// vertex/index buffer bound through an IA stage — the Draw leaf owns what little buffer binding exists. So:
//   - InputLayout      → DROPPED (no fixed-function vertex declaration in a SV_VertexID pipeline).
//   - VertexBuffers    → DROPPED (VS reads by vertexID / borrowed point-bag buffers bound per DrawKind).
//   - IndexBuffer      → DROPPED (sw indexing is DrawKind-specific: Mesh reads its own SwTriIndex buffer).
// The ONE closed-form thing IA carries across to Metal is PrimitiveTopology → MTL::PrimitiveType
// (dx11_metal_state_map.h metalPrimitiveType — a flat enum→enum table, Bucket-A, no reference frame needed).
//
// CENSUS (PLAN §1 / grep of external/tixl @395c4c55): 74 InputAssemblerStage consumers, and EVERY one leaves
// PrimitiveTopology at its .t3 default TriangleList (InputAssemblerStage.t3 DefaultValue "TriangleList") — TiXL
// only ever draws triangle lists. So the topology stamp is dormant-at-default in production; the non-triangle
// rows ship for completeness. This op therefore introduces ZERO render change on any existing chain (a stamped
// TriangleList == the pre-Seam-2 per-kind primitive for the triangle kinds; DrawKind::Explicit is the only kind
// that READS frozen.topology, and it is new).
//
// POSTURE: exactly like cookRasterizer / cookOutputMerger — a Command→Command wrapper that ACCUMULATES its one
// mutation (topology) into a FrozenRenderState and STAMPS it onto the subtree via the SHARED stampRenderState
// helper (innermost-wins push/pop). Both cook legs (flat/resident) gather cc.inputCommand identically then call
// this ONE registered fn, so flat/resident cannot diverge by construction (the render-state seam's core proof).
//
// PARITY AUTHORITY: docs/agent/census/DX11_METAL_CONVERSION_TABLE.md + SEAM2_RENDERSTATE_BUILD_PLAN.md §3.
// ZONE: runtime leaf. Contains: the InputAssembler op only (goldens live in point_ops_inputassembler_golden.cpp).
#include "runtime/render_command.h"       // RenderCommand / FrozenRenderState / stampRenderState
#include "runtime/point_graph.h"          // CmdCookCtx, registerCmdOp
#include "runtime/render_command_flow.h"  // frozenDeltaForRenderStateOp (the SINGLE param→frozen source)

#include <cstdint>

namespace sw {

// InputAssemblerStage: Command subtree in → Command out. Builds a FrozenRenderState whose ONLY non-default
// field is `topology` (via the SHARED frozenDeltaForRenderStateOp — the SAME param→frozen source the flat-
// sibling accumulation calls, so the enum-index table can never fork), and STAMPS it onto every unstamped
// subtree item via the shared push. An item already stamped by an INNER render-state op keeps its inner tuple
// (innermost-wins = TiXL restore-on-pop); its inner tuple already carries the default TriangleList topology,
// which — census being all-TriangleList — is the same value, so composition is observationally exact. (A non-
// default topology composed UNDER an inner Rasterizer/OM is a named deferred fork; census never wires one.)
// Unwired Command → empty chain (flat-sibling: the collector ACCUMULATES this op's topology delta instead).
RenderCommand cookInputAssembler(CmdCookCtx& c) {
  if (!c.inputCommand) return RenderCommand{};  // no subtree wired → empty (TiXL evals an empty subtree)
  FrozenRenderState st;  // defaults = DX11 defaults; only topology set below
  frozenDeltaForRenderStateOp("InputAssemblerStage", c.params, st);
  return stampRenderState(*c.inputCommand, st);
}

void registerInputAssemblerOp() { registerCmdOp("InputAssemblerStage", cookInputAssembler); }

}  // namespace sw
