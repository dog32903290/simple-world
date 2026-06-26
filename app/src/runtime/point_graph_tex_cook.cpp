// runtime/point_graph_tex_cook — cookFlatTexNode: the FLAT-path cook for the TEXTURE currency (the
// Texture2D stream = RenderTarget / image-filter / own-texture / cross-frame FEEDBACK ping-pong).
//
// Extracted VERBATIM from the cookTexNode lambda that lived inside PointGraph::cook (point_graph.cpp) —
// a zero-behaviour-change move that buys ratchet headroom (point_graph.cpp was at its line cap),
// following the Cut-6 pilot (point_graph_mesh_cook.cpp) and the Cut-4 host-value extraction
// (point_graph_hostvalue_cook.cpp). The biggest single flow in the flat cook (~226 lines).
//
// THE MECHANISM = thin-lambda → Impl-method delegation (NOT a context struct). The body moves to
// PointGraph::Impl::cookFlatTexNode (so it can name the private nested Impl + reach ensureTex /
// ensureOwnedTex / ensureFeedbackPair / feedbackToggle / feedbackOut / texBuf). cook()'s original
// cookTexNode std::function slot stays AS a thin forwarding lambda so the closure web is UNTOUCHED:
// cookNode's Texture2D-into-points gather, cookCommand's Texture2D (FORK#1) gather, the feedback-input
// recursion, and the terminal dispatch all keep calling through the slot.
//
// THE COUPLING — tex is the MOST cross-wired flat flow (it is NOT a closed sub-graph). The method takes
// the minimal shared cook-stack state by reference:
//  • cookCommand — MUTUAL recursion: the standard branch concatenates each Command input into the draw
//    chain (cookCommand calls back into cookTexNode for its own Texture2D input, FORK#1). Rides in as a
//    const std::function<RenderCommand(int)>& (same contract as ColorList's cookNode).
//  • cookTexNode (SELF) — the Texture2D-input gather recurses the tex flow (both the feedback branch and
//    the standard branch). Threaded as a by-ref slot so there is ONE closure source: cook()'s thin
//    lambda forwards into this method, and the method's self-recursion calls the SAME slot (lambda →
//    method → slot → lambda → …), mirroring gatherStringInputs' cookStringNode threading.
//  • cookFloatListNode / cookGradientNode — the rail-crossings: ValuesToTexture gathers FloatList inputs,
//    GradientsToTexture / the gradient generators gather Gradient inputs. By-ref slots.
//  • texVisiting — the per-cook cycle-guard set (a malformed Texture2D cycle must not hang). By-ref.
//  • feedbackCooked — the per-frame feedback memo. By-ref so the blit + toggle run EXACTLY ONCE per node
//    per frame even when BOTH outputs (PreviousFrame + CurrentFrame) are pulled (KeepPreviousFrame's
//    cross-frame ping-pong correctness — the silent-bug-prone seam; see the toggle note below).
// The cross-frame texture PAIR (feedbackTexBuf), the toggle bit (feedbackToggle), and the persisted
// outputs (feedbackOut) are Impl members → reached directly. NO *Inline twin is needed: unlike the mesh
// flow (which read back via the OWNER method PointGraph::debugCookedMesh), cookTexNode only ever touches
// Impl maps + Impl ensure* helpers directly, so there is nothing to inline.
//
// ★ FEEDBACK ping-pong toggle EXACTLY ONCE: preserved verbatim. The op (KeepPreviousFrame) flips
// *fc.toggle inside fb(fc) — and fb is invoked AT MOST ONCE per node per frame because the feedbackCooked
// memo short-circuits any second output pull BEFORE the op runs (the `memo != end()` early return). The
// memo write (feedbackCooked[id] = outs) happens AFTER fb(fc), so the toggle has already flipped once;
// the cached read returns the resolved outputs without re-invoking fb. Byte-identical to the in-lambda
// version: same memo, same single fb call, same toggle ownership (&feedbackToggle[flatKey(id)]).
//
// PLACEMENT: runtime leaf (depends only on the flat Graph + the tex/feedback/image-filter registries +
//   PointGraph::Impl — all runtime). Defined as a method on PointGraph::Impl; cook() wraps it in a
//   forwarding lambda.
#include "runtime/point_graph_internal.h"  // PointGraph::Impl (ensureTex / feedbackTexBuf / ...) + decl

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <Metal/Metal.hpp>

#include "runtime/graph.h"  // Graph/Node/NodeSpec/PortSpec/Connection/pinId/pinNode/findSpec
#include "runtime/image_filter_op_registry.h"  // imageFilter{Size,Compute,MippedOutput,Asset}* + cachedAssetTexture
#include "runtime/render_command.h"  // RenderCommand (cookCommand slot return + the draw chain)
#include "runtime/sw_gradient.h"     // SwGradient (the Gradient rail-crossing element)
#include "runtime/tixl_point.h"      // EvaluationContext (TexCookCtx::ctx)

namespace sw {

using pgdetail::flatKey;
using pgdetail::texReg;

// Cook a TEXTURE-flow node into its OWN resolution-sized texture (ensureTex, keyed by flat id) and return
// it — the Texture2D gather direct-through (lane I). Body extracted VERBATIM from cookTexNode; the
// captured cook()-stack slots ride in by reference (see leaf header). `cookTexNode` is the SELF slot the
// self-recursive Texture2D gather calls; `outAbsPort` selects WHICH Texture2D output (feedback: previous
// vs current; -1 = the node's terminal/first output).
MTL::Texture* PointGraph::Impl::cookFlatTexNode(
    const Graph& g, const EvaluationContext& ctx, const SourceRegistry* reg,
    const NodeParamsFn& nodeParams, const std::function<RenderCommand(int)>& cookCommand,
    const std::function<MTL::Texture*(int, int)>& cookTexNode,
    const std::function<const std::vector<float>*(int)>& cookFloatListNode,
    const std::function<const SwGradient*(int)>& cookGradientNode, std::set<int>& texVisiting,
    std::map<int, std::array<MTL::Texture*, FeedbackCookCtx::kMaxTexOutputs>>& feedbackCooked, int id,
    int outAbsPort) {
  const Node* n = g.node(id);
  const NodeSpec* s = n ? findSpec(n->type) : nullptr;
  if (!n || !s) return nullptr;

  // FEEDBACK branch (the cross-frame ping-pong flow = KeepPreviousFrame / SwapTextures): a feedback
  // op is NOT in texReg() — route it through the multi-tex-output + optional cross-frame-pair path.
  // outAbsPort selects WHICH Texture2D output (PreviousFrame vs CurrentFrame); the per-frame memo
  // makes the blit + toggle run EXACTLY ONCE even if both outputs are pulled this frame.
  if (isFeedbackOp(n->type)) {
    PointFeedbackFn fb = findFeedbackOp(n->type);
    if (!fb) return nullptr;
    const int outOrd = (outAbsPort < 0) ? 0 : pgdetail::texOutputOrdinal(*s, outAbsPort);
    auto memo = feedbackCooked.find(id);
    if (memo != feedbackCooked.end()) {  // already cooked this frame → read the cached output
      return (outOrd >= 0 && outOrd < FeedbackCookCtx::kMaxTexOutputs) ? memo->second[outOrd]
                                                                       : nullptr;
    }
    if (!texVisiting.insert(id).second) return nullptr;  // cycle guard
    // Gather Texture2D inputs in spec port order (each Texture2D INPUT occupies the next slot,
    // wired or not — same contract as the normal tex path). Recurse on the source's OWN wired
    // output (c->fromPin's port index → that source's terminal output).
    const MTL::Texture* fbInputs[FeedbackCookCtx::kMaxTexInputs] = {nullptr, nullptr, nullptr,
                                                                    nullptr};
    int fbInputCount = 0;
    for (size_t i = 0; i < s->ports.size(); ++i) {
      const PortSpec& port = s->ports[i];
      if (!port.isInput || port.dataType != "Texture2D") continue;
      if (fbInputCount < FeedbackCookCtx::kMaxTexInputs) {
        const Connection* c = g.connectionToInput(pinId(id, (int)i));
        fbInputs[fbInputCount] = c ? cookTexNode(pinNode(c->fromPin), (c->fromPin - 1) % 100)
                                   : nullptr;
        ++fbInputCount;
      }
    }
    FeedbackCookCtx fc;
    fc.dev = dev; fc.lib = lib; fc.queue = queue;
    fc.params = nodeParams(id);
    for (int k = 0; k < fbInputCount; ++k) fc.inputTextures[k] = fbInputs[k];
    fc.inputTextureCount = fbInputCount;
    // Cross-frame pair: sized to the FIRST Texture2D input's description (KeepPreviousFrame.cs:33-54
    // sizes _prevTextureA/B off `texture.Description`). A stateless feedback op (SwapTextures) skips
    // this (needsPair=false → pairA/B stay null → no allocation, no toggle state touched).
    if (feedbackNeedsPair(n->type) && fbInputs[0]) {
      const uint32_t w = (uint32_t)fbInputs[0]->width();
      const uint32_t h = (uint32_t)fbInputs[0]->height();
      const MTL::PixelFormat fmt = (MTL::PixelFormat)feedbackPairFormat(n->type);
      MTL::Texture* pa = nullptr;
      MTL::Texture* pb = nullptr;
      if (ensureFeedbackPair(flatKey(id), w, h, fmt, pa, pb)) {
        fc.pairA = pa;
        fc.pairB = pb;
        fc.toggle = &feedbackToggle[flatKey(id)];
      }
    }
    fb(fc);
    texVisiting.erase(id);
    std::array<MTL::Texture*, FeedbackCookCtx::kMaxTexOutputs> outs{};
    for (int k = 0; k < FeedbackCookCtx::kMaxTexOutputs; ++k) outs[k] = fc.outputs[k];
    feedbackCooked[id] = outs;
    // Persist the resolved outputs for a post-cook debug readback (debugCookedFeedbackOutput). Width
    // is kMaxFeedbackOut (== kMaxTexOutputs); borrowed pointers (no ownership — pair freed elsewhere).
    {
      std::array<MTL::Texture*, Impl::kMaxFeedbackOut> persist{};
      for (int k = 0; k < Impl::kMaxFeedbackOut && k < FeedbackCookCtx::kMaxTexOutputs; ++k)
        persist[k] = fc.outputs[k];
      feedbackOut[flatKey(id)] = persist;
    }
    return (outOrd >= 0 && outOrd < FeedbackCookCtx::kMaxTexOutputs) ? outs[outOrd] : nullptr;
  }

  auto tx = texReg().find(n->type);
  if (tx == texReg().end() || !tx->second) return nullptr;
  if (!texVisiting.insert(id).second) return nullptr;  // cycle guard: already on the stack

  const std::map<std::string, float>* tp = nodeParams(id);

  // S1 OUTPUT-RESOLUTION SEAM — push/pop RequestedResolution around the Command subtree (parity
  // RenderTarget.cs:81 save / :140 set / :158 restore). A RenderTarget pins resolution AND cooks a
  // Command subtree below it, so anything inside that subtree (camera aspect, a NESTED RenderTarget
  // with Resolution=WindowFollow=0 → it adopts the pushed value) must see THIS target's resolution,
  // not the window. Detection = the op declares a "Command" INPUT port — that is exactly TiXL's
  // RenderTarget shape (the ONLY tex op that owns a Command subtree). Every image filter (Blur/Crop/
  // …) takes Texture2D inputs, has NO Command port → pushesResolution=false → byte-identical path.
  // The resolved size uses the CURRENT requestedResolution as the WindowFollow fallback (NOT raw
  // window), so a nested WindowFollow RenderTarget INHERITS its parent's pushed resolution
  // (RenderTarget.cs:53-56: Resolution==0 → adopt context.RequestedResolution).
  bool pushesResolution = false;
  for (const PortSpec& port : s->ports)
    if (port.isInput && port.dataType == "Command") { pushesResolution = true; break; }
  // RAII restore (RenderTarget.cs:158): pop runs on EVERY exit path (the function has several early
  // returns). Restores requestedResolution to its pre-push value so a SIBLING subtree never leaks
  // this RenderTarget's size. A no-op (saves+restores the same value) when this op does not push.
  struct ReqResGuard {
    RenderResolution& slot;
    RenderResolution saved;
    ~ReqResGuard() { slot = saved; }
  } reqResGuard{requestedResolution, requestedResolution};
  if (pushesResolution) {
    requestedResolution = resolveRenderResolution(  // SET before the Command subtree (RenderTarget.cs:140)
        tp ? *tp : std::map<std::string, float>{}, requestedResolution);
  }

  // Gather inputs in spec order FIRST (moved ahead of output sizing): concat Command inputs,
  // recurse EACH Texture2D input (lane D2: Displace needs Image + DisplaceMap; texInputs[] indexes
  // by Texture2D-input port order). Gathering before sizing is harmless for pixel ops (they size
  // off the Resolution pin and ignore the input dims) — done UNCONDITIONALLY so the one code path
  // can hand a compute leaf's SizeFn the cooked INPUT size (Crop's output = input minus margins).
  RenderCommand chain;
  const MTL::Texture* texInputs[TexCookCtx::kMaxTexInputs] = {nullptr, nullptr, nullptr, nullptr};
  int texInputCount = 0;
  // FloatList inputs (Slice B rail-crossing): a tex op with "FloatList" input ports (ValuesToTexture)
  // gathers its upstream host lists here, in spec port order with MultiInput ports expanded into
  // wire-declaration order — the SAME gather contract as cookFloatListNode. Empty for every existing
  // tex op (no FloatList port) → tc.inputLists stays null → byte-identical path.
  std::vector<std::vector<float>> floatListInputs;
  bool hasFloatListInput = false;
  // GRADIENT inputs (the 8th cook flow rail-crossing): a tex op with "Gradient" input ports
  // (GradientsToTexture) gathers its upstream host gradients here, same gather contract as the
  // FloatList branch below. Empty for every existing tex op → tc.inputGradients stays null.
  std::vector<SwGradient> gradientInputs;
  bool hasGradientInput = false;
  for (size_t i = 0; i < s->ports.size(); ++i) {
    const PortSpec& port = s->ports[i];
    if (!port.isInput) continue;
    if (port.dataType == "Command") {
      const Connection* c = g.connectionToInput(pinId(id, (int)i));
      if (!c) continue;
      RenderCommand up = cookCommand(pinNode(c->fromPin));
      chain.items.insert(chain.items.end(), up.items.begin(), up.items.end());
    } else if (port.dataType == "Texture2D") {
      if (port.multiInput) {
        // MultiInput Texture2D (BlendImages/PickTexture/FirstValidTexture's `Input`): ONE port
        // gathers its N wired textures into CONSECUTIVE inputTextures[] slots, in wire-declaration
        // order (g.connections order) — capped at kMaxTexInputs. Mirrors the FloatList/Gradient
        // MultiInput loops above (recurse every wire) instead of the single-wire FIXED branch below.
        // A FIXED numbered Texture2D port (Combine3Images' ImageA/B/C, multiInput==false) takes the
        // else branch → exactly one slot, unchanged.
        for (const Connection& c : g.connections) {
          if (c.toPin != pinId(id, (int)i)) continue;
          if (texInputCount >= TexCookCtx::kMaxTexInputs) break;
          texInputs[texInputCount] = cookTexNode(pinNode(c.fromPin), (c.fromPin - 1) % 100);
          ++texInputCount;
        }
      } else {
        int slot = texInputCount;  // wired or not, this port occupies the next Texture2D slot
        if (slot < TexCookCtx::kMaxTexInputs) {
          const Connection* c = g.connectionToInput(pinId(id, (int)i));
          texInputs[slot] = c ? cookTexNode(pinNode(c->fromPin), (c->fromPin - 1) % 100) : nullptr;
          texInputCount = slot + 1;
        }
      }
    } else if (port.dataType == "FloatList") {
      hasFloatListInput = true;
      // Recurse each wired FloatList source. MultiInput → every wire is a separate gathered list, in
      // wire-declaration order (g.connections order); single-input → at most one. Mirrors the
      // cookFloatListNode FloatList-port gather exactly.
      for (const Connection& c : g.connections) {
        if (c.toPin != pinId(id, (int)i)) continue;
        const std::vector<float>* up = cookFloatListNode(pinNode(c.fromPin));
        floatListInputs.push_back(up ? *up : std::vector<float>{});
        if (!port.multiInput) break;
      }
    } else if (port.dataType == "Gradient") {
      hasGradientInput = true;
      // Recurse each wired Gradient source (cookGradientNode). MultiInput → one gathered gradient
      // per wire (wire-declaration order); single-input → at most one. Mirrors cookGradientNode's
      // own Gradient-port gather. (No slot-default fallback like TiXL's HasInputConnections branch:
      // an unwired Gradients pin contributes nothing → gradientsCount 0 → GradientsToTexture returns.)
      for (const Connection& c : g.connections) {
        if (c.toPin != pinId(id, (int)i)) continue;
        const SwGradient* up = cookGradientNode(pinNode(c.fromPin));
        gradientInputs.push_back(up ? *up : SwGradient{});
        if (!port.multiInput) break;
      }
    }
  }

  // OWN-TEXTURE fork (Slice B): a tex op that allocates its OWN data-sized, non-RGBA8 texture
  // (ValuesToTexture: R32Float, dims = sampleCount × listCount) does NOT use ensureTex (RGBA8 +
  // resolution-pinned). The op computes its dims + writes a host float buffer into ownTexHost; the
  // DRIVER then sizes the op-owned texture via ensureOwnedTex (parked in texBuf → released on
  // realloc + in ~PointGraph, no leak) and uploads. This is the FIRST non-RGBA8, non-resolution-
  // pinned texture in the engine. Every other tex op skips this branch entirely (byte-identical).
  if (texOpOwnsOutput(n->type)) {
    std::vector<float> hostOut;
    uint32_t ow = 0, oh = 0;
    TexCookCtx tc;
    tc.dev = dev; tc.lib = lib; tc.queue = queue;
    tc.ctx = &ctx; tc.graph = &g; tc.reg = reg;
    tc.nodeId = id; tc.command = &chain;
    tc.inputLists = hasFloatListInput ? &floatListInputs : nullptr;
    tc.inputGradients = hasGradientInput ? &gradientInputs : nullptr;
    tc.ownTexHost = &hostOut; tc.ownTexW = &ow; tc.ownTexH = &oh;
    tc.params = tp;
    tx->second(tc);
    texVisiting.erase(id);
    // The op writes `floatsPerTexel` floats per texel (1 → R32Float / ValuesToTexture; 4 →
    // R32G32B32A32_Float / GradientsToTexture). ensureOwnedTex keys realloc on (w,h,fmt).
    const int fpt = texOpOwnFormat(n->type);
    const MTL::PixelFormat fmt = fpt == 4 ? MTL::PixelFormatRGBA32Float : MTL::PixelFormatR32Float;
    // No data (empty inputs / guard / nothing to upload) → no texture this cook.
    if (ow == 0 || oh == 0 || hostOut.size() < (size_t)ow * oh * fpt) return nullptr;
    MTL::Texture* owned = ensureOwnedTex(flatKey(id), ow, oh, fmt);
    if (owned)
      owned->replaceRegion(MTL::Region::Make2D(0, 0, ow, oh), 0, hostOut.data(),
                           (NS::UInteger)ow * fpt * sizeof(float));
    return owned;
  }

  // Size this node's own output texture. Default = the Resolution pin; WindowFollow (Resolution==0)
  // adopts the ACTIVE requestedResolution (S1 seam: window at top level, the parent RenderTarget's
  // pushed size when nested — RenderTarget.cs:53-56). A RenderTarget's own requestedResolution was
  // just SET to this same resolved size above, so re-resolving here is identical for it.
  // A COMPUTE leaf may override via its SizeFn (Crop: output = inputSize - margins): the output
  // can be SMALLER/larger than the Resolution pin, so we compute it from the cooked input dims.
  RenderResolution res = resolveRenderResolution(
      tp ? *tp : std::map<std::string, float>{}, requestedResolution);
  auto sizeIt = imageFilterSizeFns().find(n->type);
  if (sizeIt != imageFilterSizeFns().end() && texInputs[0]) {
    res = sizeIt->second(tp ? *tp : std::map<std::string, float>{},
                         RenderResolution{(uint32_t)texInputs[0]->width(),
                                          (uint32_t)texInputs[0]->height()});
  }
  // Compute leaves write via RWTexture2D -> their output needs MTL::TextureUsageShaderWrite.
  bool needsWrite = imageFilterComputeTypes().count(n->type) != 0;
  // Mipped-output ops carry a full mip pyramid on their output (allocated by ensureTex) so a
  // downstream consumer can sample(uv, level(lod)). The mip-WRITE (generateMipmaps blit) happens
  // AFTER the leaf fills level 0 (below).
  bool needsMips = imageFilterMippedOutputTypes().count(n->type) != 0;
  MTL::Texture* tex = ensureTex(flatKey(id), res.w, res.h, needsWrite, needsMips);

  TexCookCtx tc;
  tc.dev = dev; tc.lib = lib; tc.queue = queue;
  tc.ctx = &ctx; tc.graph = &g; tc.reg = reg;
  tc.nodeId = id; tc.command = &chain; tc.output = tex;
  for (int k = 0; k < texInputCount; ++k) tc.inputTextures[k] = texInputs[k];
  tc.inputTextureCount = texInputCount;
  tc.inputTexture = texInputs[0];  // back-compat: single-input ops (Blur) read inputTexture
  tc.inputLists = hasFloatListInput ? &floatListInputs : nullptr;  // null for every existing tex op
  // GRADIENT inputs (Gradient->t1 binding seam): the STANDARD (non-own-output) tex branch was
  // DROPPING the gathered gradients — only the own-output branch (GradientsToTexture) wired them.
  // The 4 gradient generators (LinearGradient et al.) draw into a resolution-pinned ensureTex, so
  // they fall through HERE and need the gradients. hasGradientInput is true ONLY for specs with a
  // "Gradient" dataType port (every existing tex op has none → nullptr → byte-identical).
  tc.inputGradients = hasGradientInput ? &gradientInputs : nullptr;
  // ASSET texture ((E)-seam phase 2): if this op type declared an asset key, decode-and-cache it
  // ONCE (cachedAssetTexture memoizes; NO per-frame decode) and bind via tc.assetTexture. Absent
  // type = tc.assetTexture stays null -> every existing op's path is byte-identical.
  {
    auto ai = imageFilterAssetTextures().find(n->type);
    if (ai != imageFilterAssetTextures().end())
      tc.assetTexture = cachedAssetTexture(dev, ai->second, /*mipped=*/false);
  }
  tc.params = tp;
  tx->second(tc);
  // mip-WRITE: the leaf cook committed+waited internally, so level 0 is ready. Fill levels 1..N
  // by a blit generateMipmaps (NOT a shader — pattern from point_ops_combinebuffers.cpp). A
  // separate command buffer; commit+wait so a same-frame downstream sample(level(lod)) sees mips.
  if (needsMips && tex) {
    MTL::CommandBuffer* mc = queue->commandBuffer();
    MTL::BlitCommandEncoder* blit = mc->blitCommandEncoder();
    blit->generateMipmaps(tex);
    blit->endEncoding();
    mc->commit();
    mc->waitUntilCompleted();
  }
  texVisiting.erase(id);
  return tex;
}

}  // namespace sw
