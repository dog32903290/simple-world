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

const MTL::Buffer* PointGraph::debugCookedBuffer(int nodeId) const {
  auto it = p_->outBuf.find(flatKey(nodeId));
  return it != p_->outBuf.end() ? it->second : nullptr;
}

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
