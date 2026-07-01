// runtime/point_ops_draw_explicit — Seam 2: TiXL Draw (explicit deviceContext.Draw) → DrawKind::Explicit.
//
// TiXL Draw.cs is the TERMINAL of the render-state chain: after IA sets topology and Rasterizer/OutputMerger
// mutate the context, Draw issues ONE `deviceContext.Draw(VertexCount, VertexStartLocation)` against the
// currently-bound vertex/pixel shader (Draw.cs:49). It is a RAW N-vertex draw — no point bag, no mesh, no
// texture; the vertices are synthesized by whatever shader is bound (SV_VertexID-driven).
//
// sw is retained-mode: instead of issuing an immediate draw, this op is a Command SOURCE (no upstream Command,
// like ClearRenderTarget) that EMITS a single DrawKind::Explicit RenderDrawItem carrying the raw draw call —
// explicitVertexCount (TiXL VertexCount) + explicitBaseVertex (TiXL VertexStartLocation → drawPrimitives'
// vertexStart). Its primitive type comes from the IA-stamped frozen.topology (metalPrimitiveType), NOT a fixed
// per-kind constant — DrawKind::Explicit is the ONLY kind whose primitive is topology-driven. A wrapping IA /
// Rasterizer / OutputMerger op then STAMPS its FrozenRenderState onto this item (the Camera/Group stamp path).
//
// CHAIN ORDER (faithful to TiXL): Draw is the INNERMOST (it produces the item); IA/Rasterizer/OM are the
// OUTER wrappers whose Restore-on-pop maps to the stamp's innermost-wins. So a graph is
//   Draw → InputAssemblerStage(topology) → Rasterizer(cull) → OutputMerger(blend) → RenderTarget
// where each wrapper stamps onto the Draw item the previous wrapper had not yet stamped.
//
// ★SCOPE (honest, per parity-without-reference-frame): this op MATERIALIZES the explicit-draw COMMAND (the
// item + its vertexCount/baseVertex/topology plumbing, all closed-form). It does NOT bind an arbitrary
// application vertex shader — sw has no generic "bind any VS + draw N verts" pipeline (every DrawKind ships a
// dedicated shader). Whether the executor renders a DrawKind::Explicit item with a real PSO is a SEPARATE
// executor concern (a no-op today: no census .t3 wires a bare Draw into a point graph — Draw is used inside
// TiXL's own _dx11 shader-op subgraphs, not the point-render chains sw clones). The command + closed-form
// mapping are built and verified here; the executor draw-path for Explicit is a named deferred leaf (needs a
// generic-VS binding that no census graph exercises). This is the same "build the closed-form seam, guard the
// dormant path" posture as Wireframe/A2C.
//
// PARITY AUTHORITY: external/tixl Draw.cs / Draw.t3 (VertexCount default 3, VertexStartLocation default 0) +
// SEAM2_RENDERSTATE_BUILD_PLAN.md §3. ZONE: runtime leaf. (Goldens: point_ops_draw_explicit_golden.cpp.)
#include "runtime/render_command.h"  // RenderCommand / RenderDrawItem / DrawKind::Explicit
#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp, cookParam

#include <cmath>
#include <cstdint>

namespace sw {

// Draw: Command SOURCE → Command (one DrawKind::Explicit item). Reads VertexCount / VertexStartLocation off the
// node (TiXL .t3 defaults 3 / 0) and emits the raw draw. topology stays at the default (TriangleList) until a
// wrapping InputAssemblerStage stamps a different one; frozen stays default until a wrapping render-state op
// stamps it (hasRenderState=false here → the executor/stamp treats it as unstamped, exactly like any producer).
RenderCommand cookDrawExplicit(CmdCookCtx& c) {
  RenderCommand rc;
  RenderDrawItem it{};
  it.kind = DrawKind::Explicit;
  // TiXL Draw.VertexCount default 3, VertexStartLocation default 0. Negative/garbage → clamp to 0 (a draw of
  // <0 vertices is a no-op; TiXL's int inputs would pass a negative straight to deviceContext.Draw = an error,
  // sw clamps defensively — a 0-vertex Explicit item the executor simply skips).
  int vc = (int)std::lround(cookParam(c, "VertexCount", 3.0f));
  int vs = (int)std::lround(cookParam(c, "VertexStartLocation", 0.0f));
  it.explicitVertexCount = vc > 0 ? (uint32_t)vc : 0u;
  it.explicitBaseVertex = vs > 0 ? (uint32_t)vs : 0u;
  it.count = it.explicitVertexCount;  // mirror onto the generic count (introspection / debug parity)
  rc.items.push_back(it);
  return rc;
}

void registerDrawExplicitOp() { registerCmdOp("Draw", cookDrawExplicit); }

}  // namespace sw
