// runtime/point_graph_resident_tex_cook — cookResidentTexNode: the PRODUCTION (resident-path) cook for
// the TEXTURE currency (the Texture2D stream = RenderTarget / image-filter / own-texture / cross-frame
// FEEDBACK ping-pong). Resident twin of the FLAT cookFlatTexNode (point_graph_tex_cook.cpp, Cut-5).
//
// Extracted VERBATIM from the cookTexNode lambda that lived inside PointGraph::cookResident
// (point_graph_resident.cpp) — a zero-behaviour-change move that buys ratchet headroom (the resident
// driver was at its 820-line cap) and clears room for the follow-up depth-export seam. Precedent:
// resident_mesh_cook.cpp already extracted the resident MESH cook as an Impl method.
//
// THE MECHANISM = thin-lambda → Impl-method delegation (mirror of the flat-tex extraction). The body
// moves to PointGraph::Impl::cookResidentTexNode (so it can name the private nested Impl + reach
// ensureTex / ensureOwnedTex / ensureFeedbackPair / feedbackToggle / feedbackOut). cookResident's
// `cookTexNode` std::function slot stays AS a thin forwarding lambda so the closure web is UNTOUCHED:
// cookNode's Texture2D gather, cookCommand's Texture2D gather, the feedback-input recursion, and the
// terminal dispatch all keep calling THROUGH the slot.
//
// ★ ONE DIFFERENCE from the flat-tex twin (NOT a behaviour change): the FLAT method resolved params via
// the by-ref NodeParamsFn slot too — same here. The lambda captured cookResident's `nodeParams` MEMO
// (paramsMemo, single-resolve-per-node); we thread that EXACT slot in by reference, so the memo
// semantics are byte-identical (NOT the memo-free inline resolve cookResidentMesh chose — the tex flow
// pulls nodeParams in BOTH the feedback and the standard/own-tex branches, so we keep the shared memo).
//
// THE COUPLING — tex is the MOST cross-wired resident flow (NOT a closed sub-graph). The method takes the
// minimal shared cook-stack state by reference (mirror of the flat-tex leaf):
//  • cookTexNode (SELF) — the Texture2D-input gather recurses the tex flow (feedback + standard branch).
//    Threaded as a by-ref slot so there is ONE closure source: cookResident's thin lambda forwards into
//    this method, and the method's self-recursion calls the SAME slot (lambda → method → slot → …).
//  • cookCommand — MUTUAL recursion: the standard branch concats each Command input into the draw chain
//    (cookCommand recurses back into cookTexNode for ITS own Texture2D input). By-ref slot.
//  • cookResidentGradient — the Gradient rail-crossing (GradientsToTexture / the gradient generators).
//    By-ref slot. The FloatList rail-crossing uses the FREE function cookResidentFloatList(rg, …, rc)
//    directly (no slot — it is not a cookResident-local lambda), so it is called inline, not threaded.
//  • feedbackCooked — the per-frame feedback memo (string-keyed). By-ref so the blit + toggle run EXACTLY
//    ONCE per node per frame even when BOTH outputs (PreviousFrame + CurrentFrame) are pulled.
//  • depthCap — cookResident's file-local kCookDepthCap (anonymous namespace). Threaded in as a param
//    (the leaf cannot name the file-local constant) — same numeric value (64), same safe-fail semantics.
// The cross-frame texture PAIR (feedbackTexBuf), the toggle bit (feedbackToggle), and the persisted
// outputs (feedbackOut) are Impl members → reached directly (the lambda reached them via p_->).
//
// ★ FEEDBACK ping-pong toggle EXACTLY ONCE: preserved verbatim. The op (KeepPreviousFrame) flips
// *fc.toggle inside fb(fc) — and fb runs AT MOST ONCE per node per frame because the feedbackCooked memo
// short-circuits any second output pull BEFORE the op runs (the `memo != end()` early return). The memo
// write (feedbackCooked[path] = outs) happens AFTER fb(fc), so the toggle has already flipped once; the
// cached read returns the resolved outputs without re-invoking fb. Byte-identical to the in-lambda
// version: same memo, same single fb call, same toggle ownership (&feedbackToggle[path]).
//
// ★ STANDARD branch ignores outSlotId (verbatim): a non-feedback tex op returns its single ensureTex
// color texture; outSlotId is consumed ONLY in the feedback branch (PreviousFrame vs CurrentFrame). The
// depth-export routing of outSlotId for non-feedback ops is a SEPARATE follow-up batch — NOT this move.
//
// PLACEMENT: runtime leaf (depends only on the resident graph + the tex/feedback/image-filter registries
//   + PointGraph::Impl — all runtime). Defined as a method on PointGraph::Impl; cookResident wraps it in
//   a forwarding lambda.
#include "runtime/point_graph_internal.h"  // PointGraph::Impl (ensureTex / feedbackTexBuf / …) + the decl

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <Metal/Metal.hpp>

#include "runtime/graph.h"                     // NodeSpec / PortSpec / findSpec
#include "runtime/image_filter_op_registry.h"  // imageFilter{Size,Compute,MippedOutput,Asset}* + cachedAssetTexture
#include "runtime/render_command.h"            // RenderCommand (cookCommand slot return + the draw chain)
#include "runtime/resident_eval_graph.h"       // ResidentEvalGraph / drivers / ResidentEvalCtx
#include "runtime/resident_value_cooks.h"      // cookResidentFloatList (the FloatList rail-crossing)
#include "runtime/sw_gradient.h"               // SwGradient (the Gradient rail-crossing element)
#include "runtime/tixl_point.h"                // EvaluationContext (TexCookCtx::ctx)

namespace sw {

using pgdetail::texReg;

// Cook a TEXTURE-flow node (RenderTarget OR an image filter like Blur) into its OWN resolution-sized
// texture and return it (resident mirror of cook()'s cookTexNode). Body extracted VERBATIM from the
// cookResident-local cookTexNode lambda; the captured cook-stack slots ride in by reference (see leaf
// header). `cookTexNode` is the SELF slot the self-recursive Texture2D gather calls; `outSlotId` selects
// WHICH Texture2D output (feedback: previous vs current; empty = the node's terminal/first output).
MTL::Texture* PointGraph::Impl::cookResidentTexNode(
    const ResidentEvalGraph& rg, const EvaluationContext& ctx, const SourceRegistry* reg,
    const ResidentEvalCtx& rc, int depthCap,
    const std::function<const std::map<std::string, float>*(const std::string&)>& nodeParams,
    const std::function<RenderCommand(const std::string&, int)>& cookCommand,
    const std::function<MTL::Texture*(const std::string&, int, const std::string&)>& cookTexNode,
    const std::function<const SwGradient*(const std::string&, int)>& cookResidentGradient,
    std::map<std::string, std::array<MTL::Texture*, FeedbackCookCtx::kMaxTexOutputs>>& feedbackCooked,
    const std::string& path, int depth, const std::string& outSlotId) {
  if (depth > depthCap) return nullptr;
  const ResidentNode* n = rg.node(path);
  const NodeSpec* s = n ? findSpec(n->opType) : nullptr;
  if (!n || !s) return nullptr;

  // FEEDBACK branch (cross-frame ping-pong flow = KeepPreviousFrame / SwapTextures): resident
  // mirror of the flat cookTexNode feedback branch. Routes Texture2D inputs/outputs through the
  // multi-output + optional cross-frame-pair path. outSlotId selects WHICH Texture2D output; the
  // per-frame memo makes the blit + toggle run EXACTLY ONCE even if both outputs are pulled.
  if (isFeedbackOp(n->opType)) {
    PointFeedbackFn fb = findFeedbackOp(n->opType);
    if (!fb) return nullptr;
    // Map outSlotId → ordinal among Texture2D OUTPUT ports (0 = first; empty/unknown = 0).
    auto outOrdinal = [&]() -> int {
      if (outSlotId.empty()) return 0;
      int ord = 0;
      for (const PortSpec& p : s->ports) {
        if (p.isInput || p.dataType != "Texture2D") continue;
        if (p.id == outSlotId) return ord;
        ++ord;
      }
      return 0;
    };
    const int outOrd = outOrdinal();
    auto memo = feedbackCooked.find(path);
    if (memo != feedbackCooked.end())
      return (outOrd >= 0 && outOrd < FeedbackCookCtx::kMaxTexOutputs) ? memo->second[outOrd]
                                                                       : nullptr;
    // Gather Texture2D inputs in spec port order through the resident Connection drivers.
    const MTL::Texture* fbInputs[FeedbackCookCtx::kMaxTexInputs] = {nullptr, nullptr, nullptr,
                                                                    nullptr};
    int fbInputCount = 0;
    for (const PortSpec& port : s->ports) {
      if (!port.isInput || port.dataType != "Texture2D") continue;
      if (fbInputCount < FeedbackCookCtx::kMaxTexInputs) {
        const ResidentInput* ri = n->input(port.id);
        const bool wired = ri && ri->driver == ResidentInput::Driver::Connection;
        fbInputs[fbInputCount] =
            wired ? cookTexNode(ri->srcNodePath, depth + 1, ri->srcSlotId) : nullptr;
        ++fbInputCount;
      }
    }
    FeedbackCookCtx fc;
    fc.dev = dev; fc.lib = lib; fc.queue = queue;
    fc.params = nodeParams(path);
    for (int k = 0; k < fbInputCount; ++k) fc.inputTextures[k] = fbInputs[k];
    fc.inputTextureCount = fbInputCount;
    if (feedbackNeedsPair(n->opType) && fbInputs[0]) {
      const uint32_t w = (uint32_t)fbInputs[0]->width();
      const uint32_t h = (uint32_t)fbInputs[0]->height();
      const MTL::PixelFormat fmt = (MTL::PixelFormat)feedbackPairFormat(n->opType);
      MTL::Texture* pa = nullptr;
      MTL::Texture* pb = nullptr;
      if (ensureFeedbackPair(path, w, h, fmt, pa, pb)) {
        fc.pairA = pa;
        fc.pairB = pb;
        fc.toggle = &feedbackToggle[path];  // path-keyed cross-frame toggle (production R-2)
      }
    }
    fb(fc);
    std::array<MTL::Texture*, FeedbackCookCtx::kMaxTexOutputs> outs{};
    for (int k = 0; k < FeedbackCookCtx::kMaxTexOutputs; ++k) outs[k] = fc.outputs[k];
    feedbackCooked[path] = outs;
    {  // persist for a post-cook debug readback (debugCookedFeedbackOutput, resident path)
      std::array<MTL::Texture*, Impl::kMaxFeedbackOut> persist{};
      for (int k = 0; k < Impl::kMaxFeedbackOut && k < FeedbackCookCtx::kMaxTexOutputs; ++k)
        persist[k] = fc.outputs[k];
      feedbackOut[path] = persist;
    }
    return (outOrd >= 0 && outOrd < FeedbackCookCtx::kMaxTexOutputs) ? outs[outOrd] : nullptr;
  }

  auto tx = texReg().find(n->opType);
  if (tx == texReg().end() || !tx->second) return nullptr;
  const std::map<std::string, float>* tp = nodeParams(path);

  // S1 OUTPUT-RESOLUTION SEAM (resident mirror of the flat cookFlatTexNode push/pop, RenderTarget.cs:
  // 81 save / :140 set / :158 restore). A RenderTarget pins resolution AND cooks a Command subtree;
  // anything inside (camera aspect, a NESTED WindowFollow RenderTarget) must see THIS target's
  // resolution. Under the resident FLATTEN the save/restore is reconstructed PER RenderTarget subtree
  // via this RAII guard threaded through the recursion: cookCommand recurses with requestedResolution
  // already SET, and the guard's dtor restores it when THIS node's subtree finishes (before a SIBLING
  // RenderTarget subtree cooks) — so the flattened order preserves "restore after subtree". Detection
  // = a "Command" INPUT port (TiXL's RenderTarget shape; image filters take Texture2D → no push).
  bool pushesResolution = false;
  for (const PortSpec& port : s->ports)
    if (port.isInput && port.dataType == "Command") { pushesResolution = true; break; }
  struct ReqResGuard {
    RenderResolution& slot;
    RenderResolution saved;
    ~ReqResGuard() { slot = saved; }
  } reqResGuard{requestedResolution, requestedResolution};
  if (pushesResolution) {
    requestedResolution = resolveRenderResolution(
        tp ? *tp : std::map<std::string, float>{}, requestedResolution);
  }

  // Gather inputs in spec order FIRST (mirror of flat cookTexNode, point_graph.cpp): concat
  // Command inputs, recurse EACH Texture2D input. Gathering BEFORE output sizing is harmless for
  // pixel ops (they size off the Resolution pin and ignore input dims) — done UNCONDITIONALLY so
  // a compute leaf's SizeFn can be handed the cooked INPUT size (Crop's output = input - margins).
  RenderCommand chain;
  const MTL::Texture* texInputs[TexCookCtx::kMaxTexInputs] = {nullptr, nullptr, nullptr, nullptr};
  int texInputCount = 0;
  // GRADIENT inputs (8th cook flow rail-crossing): a tex op with "Gradient" input ports
  // (GradientsToTexture) gathers its upstream host gradients through the resident drivers here, same
  // gather contract as the flat cookTexNode. Empty for every existing tex op → tc.inputGradients null.
  std::vector<SwGradient> gradientInputs;
  bool hasGradientInput = false;
  // CURVE input detection (own-tex curve ops, CurvesToTexture): there is NO Curve producer op yet, so
  // we never GATHER a wired curve here (no cookResidentCurve walker) — we only DETECT the "Curve" port
  // so the own-tex resident branch below fires for CurvesToTexture (which reads its embedded default
  // Curve internally). When a Curve producer lands, a cookResidentCurve gather mirrors the gradient one.
  bool hasCurveInput = false;
  // FLOATLIST inputs (the 5th cook flow rail-crossing, ValuesToTexture/ValuesToTexture2): a tex op with
  // a "FloatList" input port gathers its upstream host lists THROUGH the resident FloatList walker
  // (cookResidentFloatList — the resident twin of the flat cookFloatListNode), in spec port order with
  // MultiInput ports expanded into wire-declaration order. Empty for every other tex op (no FloatList
  // port → tc.inputLists null → byte-identical). UNWIRED FloatList input contributes no list (mirror of
  // the flat gather: VT2's slot-default is the empty list → no texture, faithful to ValuesToTexture2.cs).
  std::vector<std::vector<float>> floatListInputs;
  bool hasFloatListInput = false;
  for (const PortSpec& port : s->ports) {
    if (!port.isInput) continue;
    if (port.dataType == "Curve") hasCurveInput = true;
    const ResidentInput* ri = n->input(port.id);
    bool wired = ri && ri->driver == ResidentInput::Driver::Connection;
    if (port.dataType == "FloatList") {
      hasFloatListInput = true;
      if (wired) {
        std::vector<float> up;
        cookResidentFloatList(rg, ri->srcNodePath, rc, up, depth + 1);
        floatListInputs.push_back(std::move(up));
        if (port.multiInput) {
          for (const auto& ec : ri->extraConns) {
            std::vector<float> ue;
            cookResidentFloatList(rg, ec.first, rc, ue, depth + 1);
            floatListInputs.push_back(std::move(ue));
          }
        }
      }
    } else if (port.dataType == "Command") {
      if (!wired) continue;
      RenderCommand up = cookCommand(ri->srcNodePath, depth + 1);
      chain.items.insert(chain.items.end(), up.items.begin(), up.items.end());
    } else if (port.dataType == "Texture2D") {
      if (port.multiInput) {
        // MultiInput Texture2D (BlendImages/PickTexture's `Input`): ONE port gathers its N wired
        // textures into CONSECUTIVE inputTextures[] slots — primary wire (ri->srcNodePath) then
        // ri->extraConns (wire-declaration order), capped at kMaxTexInputs. Resident mirror of the
        // flat g.connections loop and of the Gradient/FloatList MultiInput branches here (which also
        // walk primary + extraConns). A FIXED numbered Texture2D port (multiInput==false) takes the
        // else branch → exactly one slot, unchanged.
        if (wired) {
          if (texInputCount < TexCookCtx::kMaxTexInputs)
            texInputs[texInputCount++] = cookTexNode(ri->srcNodePath, depth + 1, ri->srcSlotId);
          for (const auto& ec : ri->extraConns) {
            if (texInputCount >= TexCookCtx::kMaxTexInputs) break;
            texInputs[texInputCount++] = cookTexNode(ec.first, depth + 1, ec.second);
          }
        }
      } else {
        int slot = texInputCount;  // each Texture2D port occupies the next slot (wired or not)
        if (slot < TexCookCtx::kMaxTexInputs) {
          texInputs[slot] =
              wired ? cookTexNode(ri->srcNodePath, depth + 1, ri->srcSlotId) : nullptr;
          texInputCount = slot + 1;
        }
      }
    } else if (port.dataType == "Gradient") {
      hasGradientInput = true;
      if (wired) {
        const SwGradient* up = cookResidentGradient(ri->srcNodePath, depth + 1);
        gradientInputs.push_back(up ? *up : SwGradient{});
        if (port.multiInput) {
          for (const auto& ec : ri->extraConns) {
            const SwGradient* ue = cookResidentGradient(ec.first, depth + 1);
            gradientInputs.push_back(ue ? *ue : SwGradient{});
          }
        }
      }
    }
  }

  // OWN-TEXTURE fork (Slice B): a tex op that allocates its OWN data-sized, non-RGBA8 texture does
  // NOT use ensureTex (RGBA8/resolution-pinned). The op computes its dims + writes a host float
  // buffer into ownTexHost; the DRIVER then sizes the op-owned texture via ensureOwnedTex (parked in
  // texBuf). Resident mirror of the flat cookTexNode own-tex branch — makes the Gradient→texture
  // family LIVE on the production cookResident path (R-2 rule).
  //
  // GATED on (hasGradientInput || hasCurveInput || hasFloatListInput), NOT on texOpOwnsOutput alone:
  // the gate fires only for own-tex ops whose HOST CURRENCY can be gathered through a resident walker.
  //   • Gradient currency (GradientsToTexture) → cookResidentGradient
  //   • Curve currency    (CurvesToTexture)    → embedded default (no producer yet)
  //   • FloatList currency (ValuesToTexture / ValuesToTexture2) → cookResidentFloatList (now exported
  //     from resident_host_scalar_cook.cpp via resident_eval_graph.h). This LANDS the FloatList own-tex
  //     family on the production cookResident path (R-2 rule), replacing the prior flat-only behaviour.
  // An own-tex op with NONE of these inputs (none exist today) still falls through to ensureTex below
  // (its cook early-returns on null ownTexHost → unchanged), so this stays byte-identical for them.
  if (texOpOwnsOutput(n->opType) && (hasGradientInput || hasCurveInput || hasFloatListInput)) {
    std::vector<float> hostOut;
    uint32_t ow = 0, oh = 0;
    TexCookCtx tc;
    tc.dev = dev; tc.lib = lib; tc.queue = queue;
    tc.ctx = &ctx; tc.graph = nullptr; tc.reg = reg;
    tc.nodeId = 0; tc.command = &chain;
    tc.inputGradients = hasGradientInput ? &gradientInputs : nullptr;
    tc.inputLists = hasFloatListInput ? &floatListInputs : nullptr;
    // inputCurves stays null: no Curve producer exists, so CurvesToTexture uses its embedded default.
    tc.ownTexHost = &hostOut; tc.ownTexW = &ow; tc.ownTexH = &oh;
    tc.params = tp;
    tx->second(tc);
    const int fpt = texOpOwnFormat(n->opType);
    const MTL::PixelFormat fmt = fpt == 4 ? MTL::PixelFormatRGBA32Float : MTL::PixelFormatR32Float;
    if (ow == 0 || oh == 0 || hostOut.size() < (size_t)ow * oh * fpt) return nullptr;
    MTL::Texture* owned = ensureOwnedTex(path, ow, oh, fmt);
    if (owned)
      owned->replaceRegion(MTL::Region::Make2D(0, 0, ow, oh), 0, hostOut.data(),
                           (NS::UInteger)ow * fpt * sizeof(float));
    return owned;
  }

  // Size this node's own output texture. Default = the Resolution pin; WindowFollow adopts the ACTIVE
  // requestedResolution (S1 seam: window at top level, the parent RenderTarget's pushed size when
  // nested — RenderTarget.cs:53-56). A RenderTarget's own requestedResolution was just SET to this
  // same resolved size above, so re-resolving here is identical for it. A COMPUTE leaf may override
  // via its SizeFn (Crop: output = inputSize - margins) from the cooked input dims (mirror of flat).
  RenderResolution res = resolveRenderResolution(
      tp ? *tp : std::map<std::string, float>{}, requestedResolution);
  auto sizeIt = imageFilterSizeFns().find(n->opType);
  if (sizeIt != imageFilterSizeFns().end() && texInputs[0]) {
    res = sizeIt->second(tp ? *tp : std::map<std::string, float>{},
                         RenderResolution{(uint32_t)texInputs[0]->width(),
                                          (uint32_t)texInputs[0]->height()});
  }
  // Compute leaves write via RWTexture2D -> their output needs MTL::TextureUsageShaderWrite.
  bool needsWrite = imageFilterComputeTypes().count(n->opType) != 0;
  // Mipped-output ops carry a full mip pyramid (allocated by ensureTex) for downstream
  // sample(uv, level(lod)). mip-WRITE (generateMipmaps blit) happens AFTER the leaf fills level 0.
  bool needsMips = imageFilterMippedOutputTypes().count(n->opType) != 0;
  MTL::Texture* tex = ensureTex(path, res.w, res.h, needsWrite, needsMips);

  TexCookCtx tc;
  tc.dev = dev; tc.lib = lib; tc.queue = queue;
  tc.ctx = &ctx; tc.graph = nullptr; tc.reg = reg;
  tc.nodeId = 0; tc.command = &chain; tc.output = tex;
  for (int k = 0; k < texInputCount; ++k) tc.inputTextures[k] = texInputs[k];
  tc.inputTextureCount = texInputCount;
  tc.inputTexture = texInputs[0];
  // GRADIENT inputs (Gradient->t1 binding seam): resident mirror of the flat cookTexNode STANDARD
  // branch — the 4 gradient generators (LinearGradient et al.) draw into ensureTex (NOT own-output),
  // so they fall through HERE and need the gathered gradients. hasGradientInput is true ONLY for
  // specs with a "Gradient" port (every existing tex op has none → nullptr → byte-identical).
  tc.inputGradients = hasGradientInput ? &gradientInputs : nullptr;
  // ASSET texture ((E)-seam phase 2): resident mirror of flat cookTexNode — decode-and-cache once,
  // bind via tc.assetTexture. Absent type = null -> byte-identical for every existing op.
  {
    auto ai = imageFilterAssetTextures().find(n->opType);
    if (ai != imageFilterAssetTextures().end())
      tc.assetTexture = cachedAssetTexture(dev, ai->second, /*mipped=*/false);
  }
  tc.params = tp;
  tx->second(tc);
  // mip-WRITE: leaf committed+waited internally (level 0 ready) -> fill levels 1..N via a blit
  // generateMipmaps (NOT a shader). Same as flat cookTexNode (point_graph.cpp).
  if (needsMips && tex) {
    MTL::CommandBuffer* mc = queue->commandBuffer();
    MTL::BlitCommandEncoder* blit = mc->blitCommandEncoder();
    blit->generateMipmaps(tex);
    blit->endEncoding();
    mc->commit();
    mc->waitUntilCompleted();
  }
  return tex;
}

}  // namespace sw
