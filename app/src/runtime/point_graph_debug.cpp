// runtime/point_graph_debug — the PointGraph::debugCooked* test-support readback accessors. Extracted
// out of point_graph.cpp to keep that file at-or-below its line-count cap (ARCHITECTURE.md rule 4
// ratchet). These are pure, side-effect-free borrowed-pointer accessors into the per-flow host transport
// buffers in PointGraph::Impl (floatListBuf / colorListBuf / stringBuf / stringListBuf / pointListBuf /
// gradientBuf / outBuf / outCount / meshVtxBuf / feedbackOut). They are used ONLY by the goldens /
// selftests (a downstream consumer reads the production extStrOut / outBuf channels, not these). Each
// returns the value the node produced on its LAST cook, keyed by flatKey(id); nullptr / 0 / false when
// the node never cooked that flow.
#include "runtime/point_graph.h"

#include <map>
#include <string>
#include <vector>

#include <simd/simd.h>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"       // SymbolLibrary / Symbol / SymbolChild (defaultDrawTarget overload)
#include "runtime/graph.h"                 // Graph / Node / Connection / pinNode
#include "runtime/point_graph_internal.h"  // PointGraph::Impl + pgdetail::flatKey + texReg/cmdReg/drawReg
#include "runtime/tixl_point.h"            // SwPoint / SwGradient

namespace sw {

using pgdetail::cmdReg;
using pgdetail::drawReg;
using pgdetail::flatKey;
using pgdetail::texReg;

uint32_t PointGraph::debugCookedCount(int nodeId) const {
  auto it = p_->outCount.find(flatKey(nodeId));
  return it != p_->outCount.end() ? it->second : 0u;
}

// value-output-rail Phase 1: the window resolution (= the seed PointGraph::cook plants into
// p_->requestedResolution at point_graph.cpp:122). frame_cook feeds this to RequestedResolution's
// cook-emit pass (cookValueOutputNodes) via ResidentEvalCtx.requestedWidth/Height.
RenderResolution PointGraph::windowResolution() const {
  return RenderResolution{p_->width, p_->height};
}

// S1 frame-level override hook (TiXL OutputWindow.cs:411-414, precedence export > selector > Fill). The
// optional sentinel keeps the UNSET path byte-identical to Fill — frameResolution() returns the window
// size exactly as windowResolution() does when no override is engaged.
void PointGraph::setFrameResolutionOverride(RenderResolution res) { p_->frameResOverride = res; }
void PointGraph::clearFrameResolutionOverride() { p_->frameResOverride.reset(); }
RenderResolution PointGraph::frameResolution() const {
  return p_->frameResOverride ? *p_->frameResOverride : RenderResolution{p_->width, p_->height};
}

// OUTPUT-CAMERA OVERRIDE hook (phase-B "Output camera orbit"). Same sentinel shape + idempotence as the
// frame-resolution override above: a plain assign / optional.reset, no cook churn on set. UNSET (reset) ==
// the hard-wired SetDefaultCamera on both cook legs (byte-identical to today) — the parity floor. The stored
// value is a COPY (optional<ViewCamera>); the cook legs engage a ViewCameraScope borrowing &*viewCameraOverride,
// alive for the whole cook (the optional is stable in Impl across the cook). Doc view_camera_active.h.
void PointGraph::setViewCameraOverride(const ViewCamera& cam) { p_->viewCameraOverride = cam; }
void PointGraph::clearViewCameraOverride() { p_->viewCameraOverride.reset(); }
bool PointGraph::hasViewCameraOverride() const { return p_->viewCameraOverride.has_value(); }

// S1-fill window-follow. TiXL's Fill is the LIVE Output-window size, re-read every frame:
//   ResolutionHandling.cs:120  `var windowSize = ImGui.GetWindowSize();`
//   ResolutionHandling.cs:124-127  `if (Size.Width <= 0 || Size.Height <= 0) return new Int2(
//       (int)windowSize.X - paddingForFocusBorder * 2, (int)windowSize.Y - ...);`
//   OutputWindow.cs:411-414  `RequestedResolution = ...TryGetActiveExportResolution(...) ?
//       overrideResolution : _selectedResolution.ComputeResolution();
//       EvaluationContext.RequestedResolution = RequestedResolution;`   (runs EVERY frame)
// sw analogue: the Output window pushes its content-region each frame (ui → shell → here); the
// push only RECORDS — the rebuild happens at cook entry (Impl::seedFrameResolution below), never
// mid-imgui-frame while the current draw list still references the old `target` texture.
void PointGraph::setWindowSize(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0) return;  // degenerate (collapsed window) → keep the last size
  p_->pendingWidth = width;
  p_->pendingHeight = height;
}

// Both cook entries (flat point_graph.cpp / resident point_graph_resident.cpp) call this FIRST:
// apply a pending window resize, then seed the per-cook RequestedResolution exactly as before
// (TiXL OutputWindow.cs:411-414 precedence export/selector-override > Fill window).
//
// The preview `target` texture is sized to the EFFECTIVE resolution = frameResolution() (=
// frameResOverride ? *override : {width,height}), NOT the raw window size. This is the core fix
// for "selecting a 1080p/4k preset didn't change the WxH overlay / preview" (root cause: `target`
// only tracked the window and the preset override lived solely in requestedResolution, which the
// default DrawPoints/Command preview surface — no displayTex — never consumed). Now a fixed preset
// retargets `target` directly, so previewTexture()->width()/height() (main.cpp previewTextureSize)
// report the preset dims. TiXL parity: OutputWindow.cs:411-414 seeds RequestedResolution
// export>selector>Fill EVERY frame and the RenderTarget output texture adopts it (the sw `target`
// is that adopting surface for the default preview path). Fill (no override) → effective ==
// {width,height} == today, byte-identical. Export is UNAFFECTED: it drives a SEPARATE PointGraph
// with its own frameResOverride = export size (export_session.cpp:105-107), never this live one,
// so precedence export > selector > Fill holds by construction.
//
// Rebuild ONLY when the effective resolution differs from targetW/targetH (RESOURCE_LIFETIME: the
// every-frame Fill push + on-change preset set are both idempotent, zero realloc churn). The gate
// is the EFFECTIVE size now, not width!=pendingWidth — so a preset change with a STILL window is
// caught. Releasing the old texture here is GPU-safe: a committed command buffer holds its own
// retain on referenced resources (Metal default tracking).
void PointGraph::Impl::seedFrameResolution() {
  if (pendingWidth > 0) {
    width = pendingWidth;    // Fill baseline (windowResolution / frameResolution's window leg) — the
    height = pendingHeight;  // live Output content region; drives frameResolution() when no override.
  }
  pendingWidth = pendingHeight = 0;
  requestedResolution = frameResOverride ? *frameResOverride : RenderResolution{width, height};
  // Effective preview size = the resolution the cook renders at this frame (== requestedResolution).
  // TEST-ONLY injectBug (bugTargetFollowsWindow): size `target` from the raw window and IGNORE the
  // override — the exact pre-fix regression the preview-target-preset tooth guards.
  const uint32_t effW = bugTargetFollowsWindow ? width : requestedResolution.w;
  const uint32_t effH = bugTargetFollowsWindow ? height : requestedResolution.h;
  if (effW > 0 && effH > 0 && (!target || effW != targetW || effH != targetH)) {
    if (target) target->release();
    MTL::TextureDescriptor* td =
        MTL::TextureDescriptor::texture2DDescriptor(kPointTargetFormat, effW, effH, false);
    td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    td->setStorageMode(MTL::StorageModeShared);
    target = dev->newTexture(td);
    targetW = effW;
    targetH = effH;
  }
}

const MTL::Buffer* PointGraph::debugCookedBuffer(int nodeId) const {
  auto it = p_->outBuf.find(flatKey(nodeId));
  return it != p_->outBuf.end() ? it->second : nullptr;
}

// TEST-ONLY bug seam for the preview-target-preset tooth (declared in point_graph.h). Flips the Impl
// flag seedFrameResolution reads; the next cook then sizes `target` from the window, ignoring the override.
void PointGraph::debugSetTargetFollowsWindowBug(bool on) { p_->bugTargetFollowsWindow = on; }

bool PointGraph::debugCookedMesh(int nodeId, const MTL::Buffer*& vtx, uint32_t& vtxCount,
                                 const MTL::Buffer*& idx, uint32_t& idxCount) const {
  const std::string key = flatKey(nodeId);
  auto vb = p_->meshVtxBuf.find(key);
  auto ib = p_->meshIdxBuf.find(key);
  if (vb == p_->meshVtxBuf.end() || ib == p_->meshIdxBuf.end() || !vb->second || !ib->second)
    return false;
  vtx = vb->second;
  idx = ib->second;
  vtxCount = p_->meshVtxCount.count(key) ? p_->meshVtxCount[key] : 0u;
  idxCount = p_->meshIdxCount.count(key) ? p_->meshIdxCount[key] : 0u;
  return true;
}

const std::vector<float>* PointGraph::debugCookedFloatList(int nodeId) const {
  auto it = p_->floatListBuf.find(flatKey(nodeId));
  return it != p_->floatListBuf.end() ? &it->second : nullptr;
}

const std::vector<simd::float4>* PointGraph::debugCookedColorList(int nodeId) const {
  auto it = p_->colorListBuf.find(flatKey(nodeId));
  return it != p_->colorListBuf.end() ? &it->second : nullptr;
}

const std::string* PointGraph::debugCookedString(int nodeId) const {
  auto it = p_->stringBuf.find(flatKey(nodeId));
  return it != p_->stringBuf.end() ? &it->second : nullptr;
}

const std::string* PointGraph::debugCookedStringPort(int nodeId, int portIdx) const {
  // MAIN String output (port 0) lives at flatKey(id); EXTRA outputs at flatKey(id)+":"+portIdx (the
  // multi-output port dimension written by cookStringNode's extra-output distribution).
  if (portIdx == 0) return debugCookedString(nodeId);
  auto it = p_->stringBuf.find(flatKey(nodeId) + ":" + std::to_string(portIdx));
  return it != p_->stringBuf.end() ? &it->second : nullptr;
}

const std::vector<std::string>* PointGraph::debugCookedStringList(int nodeId) const {
  auto it = p_->stringListBuf.find(flatKey(nodeId));
  return it != p_->stringListBuf.end() ? &it->second : nullptr;
}

const std::vector<SwPoint>* PointGraph::debugCookedPointList(int nodeId) const {
  auto it = p_->pointListBuf.find(flatKey(nodeId));
  return it != p_->pointListBuf.end() ? &it->second : nullptr;
}

const SwGradient* PointGraph::debugCookedGradient(int nodeId) const {
  auto it = p_->gradientBuf.find(flatKey(nodeId));
  return it != p_->gradientBuf.end() ? &it->second : nullptr;
}

const SwBuffer* PointGraph::debugCookedSwBuffer(int nodeId) const {
  auto it = p_->bufferMeta.find(flatKey(nodeId));
  return it != p_->bufferMeta.end() ? &it->second : nullptr;
}

// Buffer-rail FEEDBACK dual-output (KeepPreviousPointBuffer): the SwBuffer this node routed to its
// `ordinal`-th Buffer OUTPUT last cook (0 = BufferA, 1 = BufferB). Reads feedbackBufOut[flatKey(id)] —
// the twin of debugCookedFeedbackOutput (texture). Borrowed; nullptr off-range / never cooked. Goldens.
const SwBuffer* PointGraph::debugCookedFeedbackBuffer(int nodeId, int ordinal, bool resident) const {
  if (ordinal < 0 || ordinal >= Impl::kMaxFeedbackOut) return nullptr;
  // Key space: flat "#id" (test cook) vs resident path "id" (production cook, cookResidentBuffer's key).
  const std::string key = resident ? std::to_string(nodeId) : flatKey(nodeId);
  auto it = p_->feedbackBufOut.find(key);
  return it != p_->feedbackBufOut.end() ? &it->second[ordinal] : nullptr;
}

// Seam-1 RESIDENT face (WO-E): the SwBuffer a RESIDENT Buffer node cooked LAST, keyed by its resident PATH
// (cookResidentBuffer's bufferMeta[path] key — NO flatKey prefix). Mirror of residentGradientFor (8th flow)
// / residentCookedPoints (point face). Borrowed; nullptr when the path never cooked a Buffer node.
const SwBuffer* PointGraph::residentSwBufferFor(const std::string& path) const {
  auto it = p_->bufferMeta.find(path);
  return it != p_->bufferMeta.end() ? &it->second : nullptr;
}

// PRODUCTION (gradient-inspector face): the SwGradient a RESIDENT Gradient-flow node cooked LAST frame,
// keyed by its resident PATH — same key cookResidentGradient writes to p_->gradientBuf[path]. Borrowed
// (PointGraph-owned, valid until the next cook of that node). nullptr when the path never cooked a
// gradient node. Mirrors residentTexFor (Texture2D face) for the 8th-flow gradient inspector widget.
const SwGradient* PointGraph::residentGradientFor(const std::string& path) const {
  auto it = p_->gradientBuf.find(path);
  return it != p_->gradientBuf.end() ? &it->second : nullptr;
}

// PRODUCTION (value-output-rail Phase 4, point-into-frame value emit): the cooked Shared point buffer a
// RESIDENT Points node produced LAST cook, keyed by its resident PATH (the SAME key cookResident's
// ensureOut writes p_->outBuf[path], point_graph_resident.cpp). ResourceStorageModeShared (ensureOut), so
// contents() reinterprets to a `const SwPoint*` host-readable with ZERO blit — the host-side
// pointList.TypedElements[i] analog (PointToMatrix.cs:27 / GetPointDataFromList.cs:40). `count` ←
// p_->outCount[path] (the count this node cooked). nullptr + count 0 when the path never cooked points
// (off the cooked draw chain — only the viewed node's upstream subtree cooks). Borrowed (PointGraph-owned;
// valid until next cook). Feeds the PointAccessor cookPointValueOutputNodes (resident_point_value_output_
// cook.cpp) reads.
const ::SwPoint* PointGraph::residentCookedPoints(const std::string& path, uint32_t& count) const {
  count = 0;
  auto bit = p_->outBuf.find(path);
  if (bit == p_->outBuf.end() || !bit->second) return nullptr;
  auto cit = p_->outCount.find(path);
  count = cit != p_->outCount.end() ? cit->second : 0u;
  return static_cast<const ::SwPoint*>(bit->second->contents());
}

MTL::Texture* PointGraph::debugCookedTexture(int nodeId) const {
  auto it = p_->texBuf.find(flatKey(nodeId));
  return it != p_->texBuf.end() ? it->second : nullptr;
}

// PRODUCTION (node-thumbnail face, TiXL MagGraphCanvas.TryDrawTexturePreview parity): the resolution-
// sized texture a RESIDENT Texture2D-flow node cooked LAST frame, keyed by its resident PATH (raw, the
// same key cookResidentTexNode passes to ensureTex). Borrowed (PointGraph-owned, valid until the next
// cook of that node). nullptr when the path never cooked a tex node (e.g. a Float node, or a Texture2D
// node off the currently-cooked target chain — only the viewed node's upstream subtree is cooked).
MTL::Texture* PointGraph::residentTexFor(const std::string& path) const {
  auto it = p_->texBuf.find(path);
  return it != p_->texBuf.end() ? it->second : nullptr;
}

MTL::Texture* PointGraph::debugCookedFeedbackOutput(int nodeId, int ordinal, bool resident) const {
  if (ordinal < 0 || ordinal >= Impl::kMaxFeedbackOut) return nullptr;
  // Flat keys by "#id" (flatKey); resident keys by the path "id" (== node id as string, libFromGraph).
  const std::string key = resident ? std::to_string(nodeId) : flatKey(nodeId);
  auto it = p_->feedbackOut.find(key);
  return it != p_->feedbackOut.end() ? it->second[ordinal] : nullptr;
}

// --- terminal selection (the most-downstream realizable node) — extracted here too to keep
// point_graph.cpp at-or-below cap. Tex node (RenderTarget/Blur, prefer the SINK so chained image filters
// show the LAST filter not the un-filtered source) > Command (DrawPoints) > legacy draw op. ---
int PointGraph::defaultDrawTarget(const Graph& g) const {
  auto outputConsumed = [&](int id) {
    for (const Connection& c : g.connections)
      if (pinNode(c.fromPin) == id) return true;
    return false;
  };
  int firstTex = 0;
  for (const Node& n : g.nodes)
    if (texReg().find(n.type) != texReg().end()) {
      if (!firstTex) firstTex = n.id;
      if (!outputConsumed(n.id)) return n.id;  // a sink tex node = the real terminal
    }
  if (firstTex) return firstTex;  // all tex nodes feed each other (cycle): fall back to the first
  for (const Node& n : g.nodes)
    if (cmdReg().find(n.type) != cmdReg().end()) return n.id;
  // Legacy draw terminal (PointDrawFn, retired in batch 4): production registers none, but a golden
  // selftest may register a capture-only draw op as its terminal — keep it discoverable for cook().
  for (const Node& n : g.nodes)
    if (drawReg().find(n.type) != drawReg().end()) return n.id;
  return 0;
}

int PointGraph::defaultDrawTarget(const SymbolLibrary& lib, const std::string& symbolId) const {
  // Same terminal priority as the flat overload, scanning one symbol's children (prefer the SINK tex).
  const Symbol* s = lib.find(symbolId);
  if (!s) return 0;
  auto outputConsumed = [&](int id) {
    for (const SymbolConnection& c : s->connections)
      if (c.srcChild == id) return true;
    return false;
  };
  int firstTex = 0;
  for (const SymbolChild& c : s->children)
    if (texReg().find(c.symbolId) != texReg().end()) {
      if (!firstTex) firstTex = c.id;
      if (!outputConsumed(c.id)) return c.id;
    }
  if (firstTex) return firstTex;
  for (const SymbolChild& c : s->children)
    if (cmdReg().find(c.symbolId) != cmdReg().end()) return c.id;
  for (const SymbolChild& c : s->children)
    if (drawReg().find(c.symbolId) != drawReg().end()) return c.id;
  return 0;
}

}  // namespace sw
