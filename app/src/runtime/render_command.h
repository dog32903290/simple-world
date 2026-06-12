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

// How a point bag is rasterized. The Command stream is shape-agnostic (TiXL splits the
// three draw ops as distinct Slot<Command> producers — DrawPoints/DrawLines/DrawBillboards);
// the EXECUTOR (cookRenderTarget) reads this discriminator to pick the PSO + primitive. Kept
// in the data record (not three executors) so a chain can MIX kinds in one render pass, and
// so the cmd ops stay pure data-stampers (zero render code), exactly like DrawPoints.
enum class DrawKind : uint32_t {
  Points = 0,      // draw_points_vs: 1 point-prim per Point (PrimitiveTypePoint)
  Lines = 1,       // draw_lines_vs: 6-vert screen-space quad per segment i→i+1 (PrimitiveTypeTriangle)
  Billboards = 2,  // draw_billboards_vs: 6-vert camera-facing quad per Point (PrimitiveTypeTriangle)
};

// One draw in a render command chain: a point bag + how to draw it. New fields are appended
// AFTER viewExtent so existing aggregate inits (RenderDrawItem{pts,count,extent}) stay valid —
// they default to a white DrawPoints item, the pre-batch-13 behavior.
struct RenderDrawItem {
  const MTL::Buffer* points = nullptr;  // borrowed (PointGraph per-node out buffer); not retained
  uint32_t count = 0;                   // points to draw (== segment producer count; see kind)
  float viewExtent = 3.5f;              // world half-extent → NDC (hardcoded DrawPoints value)
  DrawKind kind = DrawKind::Points;     // which shape op produced this item
  // Draw params (TiXL DrawLines.Color/LineWidth, DrawBillboards.Scale/Color). All draw kinds
  // read color (multiplied with per-Point.Color); Lines uses lineWidth, Billboards uses size.
  float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // tint (TiXL Color default = white)
  float lineWidth = 0.02f;                     // DrawLines.LineWidth (.t3 default 0.02)
  float size = 1.0f;                           // DrawBillboards.Scale (.t3 default 1.0)
};

// A render command chain: draw items in execution order (later items composite on top).
// DrawPoints produces a 1-item chain; RenderTarget concatenates all upstream chains.
struct RenderCommand {
  std::vector<RenderDrawItem> items;
};

}  // namespace sw
