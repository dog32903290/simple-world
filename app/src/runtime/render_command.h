// runtime/render_command — the Command stream's currency (TiXL's Slot<Command>).
//
// TiXL splits point rendering into three slot types: BufferWithViews (raw GPU point
// data) -> Command (deferred render instruction) -> Texture2D (displayable image).
// This header is the middle one. A point op outputs a buffer (already: MTL::Buffer of
// SwPoint). DrawPoints turns that buffer into a RenderCommand. RenderTarget executes
// the RenderCommand into a Texture2D, and is where resolution is pinned.
//
// Why a DATA RECORD, not a closure (TiXL's {PrepareAction,RestoreAction}):
// TiXL's Prepare/Restore inject into an immediate-mode DX11 device context. We are
// retained-mode — each RenderTarget opens its own commandBuffer→encoder→endEncoding,
// so Prepare/Restore are render-pass boundaries owned by the EXECUTOR, not carried by
// the op. And compositing (blend layering two point bags) is just chaining: a record
// chain appends with vector::insert (O(n), zero GPU, zero buffer copy) and stays
// introspectable (debug sees how many draws, each draw's count). N closures would each
// open their own pass and clear each other out.
//
// Zone: runtime leaf (no upward deps). Pure CPU container — borrows buffer pointers,
// never retains. A RenderCommand lives shorter than one cook() (single-frame memo);
// it must NOT be stored across frames (the borrowed buffers are PointGraph-owned and
// reused next frame).
#pragma once
#include <cstdint>
#include <vector>

namespace MTL {
class Buffer;
}  // namespace MTL

namespace sw {

// One draw in a render command chain: a point bag + how to draw it.
struct RenderDrawItem {
  const MTL::Buffer* points = nullptr;  // borrowed (PointGraph per-node out buffer); not retained
  uint32_t count = 0;                   // points to draw
  float viewExtent = 3.5f;              // world half-extent → NDC (today's hardcoded DrawPoints value)
  // Future (DrawPoints param family): color / blendMode / pointSize go here.
};

// A render command chain: draw items in execution order (later items composite on top).
// DrawPoints produces a 1-item chain; RenderTarget concatenates all upstream chains.
struct RenderCommand {
  std::vector<RenderDrawItem> items;
};

}  // namespace sw
