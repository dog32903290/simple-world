// runtime/render_command_state — Seam 2 OutputMerger PSO/depth-stencil MATERIALIZATION helpers.
//
// PEELED OFF render_command.h so the closed-form MTL mapping (Device / DepthStencilState /
// RenderPipelineColorAttachmentDescriptor forward-decls + the two helpers) rides its OWN header with only its
// TWO real consumers — point_ops_rendertarget.cpp (executor: depth-stencil) and point_ops_renderstate.cpp
// (implements both) — instead of dragging MTL forward-decls through render_command.h's ~249 includers.
//
// FrozenRenderState (the currency struct) stays in render_command.h: it is the DATA every stamp carries and is
// read by many. THIS header is only the two ops that turn a frozen tuple into live Metal PSO/depth-stencil state.
//
// Zone: runtime leaf (no upward deps). Definitions live in point_ops_renderstate.cpp.
#pragma once

#include "runtime/render_command.h"  // FrozenRenderState

namespace MTL {
class Device;
class DepthStencilState;
class RenderPipelineColorAttachmentDescriptor;
}  // namespace MTL

namespace sw {

// Seam 2 OutputMerger materialization helpers (point_ops_renderstate.cpp — the render-state leaf owns the
// closed-form MTL mapping, like applyFrozenRasterEncoderState). makeDrawPSO calls applyFrozenBlend to set a
// STAMPED item's PSO blend (metalBlend* table); the executor calls makeFrozenDepthStencilState for its
// depth-stencil (compare+write). Unstamped → legacy hardcoded blend + dsMesh/dsDisabled (byte-identical).
void applyFrozenBlend(MTL::RenderPipelineColorAttachmentDescriptor* att, const FrozenRenderState& st);
MTL::DepthStencilState* makeFrozenDepthStencilState(MTL::Device* dev, const FrozenRenderState& st);

}  // namespace sw
