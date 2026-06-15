// runtime/point_graph_resident — cookResident: the resident-graph cook driver (slice 2 walk +
// slice 2b parity). Walks a ResidentEvalGraph by path-qualified id and realizes `targetPath`
// into target() with the SAME three-flow terminal as the flat cook() (tex / cmd / preview),
// per-path persistent output buffers + stateful op state (PointGraph::Impl, shared via
// point_graph_internal.h — the path string IS the frame-stable resource key the resident era
// exists for), and driver-resolved Float params handed to ops (PointCookCtx::params, the 2b
// seam — ops never see which graph model is cooking).
//
// Float reads go through evalResidentFloat (no version-chasing cache yet — wiring
// pullResidentFloat into the production pull is the swap cut, named-deferred). The flat
// cook() in point_graph.cpp mirrors this file; flat dies at the production swap, this stays.
#include "runtime/point_graph.h"

#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                 // NodeSpec/PortSpec/findSpec
#include "runtime/image_filter_op_registry.h"  // imageFilterComputeTypes/imageFilterSizeFns (compute leaf seam)
#include "runtime/point_graph_internal.h"  // PointGraph::Impl + op registries
#include "runtime/resident_eval_graph.h"   // ResidentEvalGraph / drivers / resolveResidentFloatInputs
#include "runtime/tixl_point.h"            // SwPoint + EvaluationContext

namespace sw {

using pgdetail::cmdReg;
using pgdetail::cookReg;
using pgdetail::isBufferInput;
using pgdetail::texReg;

namespace {
// Recursion cap for the cook walk (修2, 批次8): the SAME 64 every other walk in the resident era
// uses (builder inlineSymbol depth, eval cycle guard, terminal bypass loop below). Before this,
// only the TERMINAL bypass loop was capped — a bypass redirect CYCLE inside cookNode/cookCommand
// (A↔B both bypassed) recursed bare = ASan stack-overflow. Exceeding the cap is a SAFE FAIL:
// null buffer / empty chain + one stderr warn per process, never a crash. The cap threads through
// ALL cookNode/cookCommand recursion (the bypass redirect shares the call edge with the normal
// input gather), so a plain non-bypass wire cycle now also fail-safes instead of overflowing —
// covered incidentally, not contracted (the parked normal-cycle account stays parked).
constexpr int kCookDepthCap = 64;
bool g_warnedCookDepth = false;
void warnCookDepthOnce() {
  if (g_warnedCookDepth) return;
  g_warnedCookDepth = true;
  std::fprintf(stderr,
               "[cookResident] cook depth > %d (bypass/wire cycle?) — returning safe empty\n",
               kCookDepthCap);
}
}  // namespace

void PointGraph::cookResident(const ResidentEvalGraph& rg, const EvaluationContext& ctx,
                              const SourceRegistry* reg, const std::string& targetPath,
                              float localTimeBars, float localFxTimeBars,
                              const SymbolLibrary* lib) {
  p_->displayTex = nullptr;  // default: target() shows the window-sized texture (cmd/preview paths)

  ResidentEvalCtx rc;
  rc.frameIndex = ctx.frameIndex;
  // S5: the two clocks now come from the Transport (frame_cook), in BARS. localTime = playhead
  // (automation samples THIS — a scrubbed/paused playhead freezes the sampled value); localFxTime
  // = wall clock (the Time op's evaluate reads THIS — keeps running while paused). The negative
  // sentinel (selftest callers that don't pass them) falls back to the pre-S5 placeholder so the
  // resident*/parity goldens are byte-unchanged.
  rc.localTime = localTimeBars >= 0.0f ? localTimeBars : ctx.time;
  rc.localFxTime = localFxTimeBars >= 0.0f ? localFxTimeBars : ctx.time;
  rc.lib = lib;  // S3 接通: Automation drivers resolve their curve THROUGH this (nullptr = fallback)

  std::map<std::string, MTL::Buffer*> cooked;  // this-cook memo (cook each path once)

  // Per-node resolved Float params (the 2b seam): each input resolved through its driver
  // (Constant / Connection -> evalResidentFloat / Automation stub), memoized so pointers stay
  // stable for the whole cook (ops + inputParams point into it).
  std::map<std::string, std::map<std::string, float>> paramsMemo;
  std::function<const std::map<std::string, float>*(const std::string&)> nodeParams =
      [&](const std::string& path) -> const std::map<std::string, float>* {
    auto it = paramsMemo.find(path);
    if (it != paramsMemo.end()) return &it->second;
    const ResidentNode* n = rg.node(path);
    if (!n) return nullptr;
    return &(paramsMemo[path] = resolveResidentFloatInputs(rg, *n, rc));
  };

  // `depth` counts cookNode/cookCommand call-edges from the terminal; > kCookDepthCap = safe fail
  // (修2: the bypass redirect recursion was bare before — only the terminal loop had the cap).
  std::function<MTL::Buffer*(const std::string&, int)> cookNode =
      [&](const std::string& path, int depth) -> MTL::Buffer* {
    auto m = cooked.find(path);
    if (m != cooked.end()) return m->second;
    if (depth > kCookDepthCap) { warnCookDepthOnce(); return nullptr; }
    const ResidentNode* n = rg.node(path);
    if (!n) return nullptr;

    // S2 BYPASS, buffer flow (修B; = TiXL Slot.ByPassUpdate on Slot<BufferWithViews>,
    // Slot.cs:176-179 / Instance.Connections.cs:291-303): a bypassed node's MAIN output IS its
    // MAIN input's upstream buffer — the op never cooks (its state is untouched, like TiXL
    // swapping out UpdateAction). The count rides along (outCount aliased onto this path so
    // downstream count reads stay one-lookup). Unwired main input = the input slot's default =
    // no buffer (null, count 0) — same as an unwired Points input anywhere else. The builder
    // only sets `bypassed` for whitelist-passing types (childIsBypassable), so the main I/O
    // here is buffer-shaped by construction.
    if (n->bypassed) {
      const ResidentInput* ri = n->input(n->bypassInSlot);
      MTL::Buffer* b = nullptr;
      if (ri && ri->driver == ResidentInput::Driver::Connection)
        b = cookNode(ri->srcNodePath, depth + 1);
      p_->outCount[path] = b ? p_->outCount[ri->srcNodePath] : 0u;
      cooked[path] = b;
      return b;
    }

    const NodeSpec* s = findSpec(n->opType);
    if (!s) return nullptr;

    // Gather buffer inputs (Points + ParticleForce, spec order) via Connection drivers,
    // + the feeding node's resolved params (force ops read these via cookInputParam).
    std::vector<const MTL::Buffer*> ins;
    std::vector<uint32_t> insCounts;
    std::vector<const std::map<std::string, float>*> insParams;
    uint32_t sumPointsCount = 0;
    uint32_t firstPointsCount = 0;
    bool haveFirstPoints = false;
    for (const PortSpec& port : s->ports) {
      if (!isBufferInput(port)) continue;
      const ResidentInput* ri = n->input(port.id);
      MTL::Buffer* ub = nullptr;
      uint32_t inCount = 0;
      const std::map<std::string, float>* up = nullptr;
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        ub = cookNode(ri->srcNodePath, depth + 1);
        inCount = ub ? p_->outCount[ri->srcNodePath] : 0u;
        up = nodeParams(ri->srcNodePath);
      }
      ins.push_back(ub);
      insCounts.push_back(inCount);
      insParams.push_back(up);
      if (port.dataType == "Points") {
        sumPointsCount += inCount;
        if (!haveFirstPoints) { firstPointsCount = inCount; haveFirstPoints = true; }
      }
    }

    const std::map<std::string, float>* params = nodeParams(path);

    // count: a "Count" Float input (generators) resolved through its driver, else sum of Points
    // (combine concatenates), or the first Points input only for reference-transform ops
    // (SnapToPoints opts into countFromFirstPointsInput — Points2 is a target, not concatenated).
    uint32_t count = sumPointsCount;
    if (auto cr = cookReg().find(n->opType);
        cr != cookReg().end() && cr->second.countFromFirstPointsInput)
      count = firstPointsCount;
    for (const PortSpec& port : s->ports)
      if (port.isInput && port.dataType == "Float" && port.id == "Count") {
        float v = port.def;
        if (params) {
          auto pit = params->find("Count");
          if (pit != params->end()) v = pit->second;
        }
        count = v > 0.0f ? (uint32_t)(v + 0.5f) : 0u;
        break;
      }

    // Op may remap count (ParticleSystem grows a pool > its emit ring; emit count stays
    // available to the op as inputCounts[0]). Output + state size to the remapped count.
    if (auto rr = cookReg().find(n->opType); rr != cookReg().end() && rr->second.countTransform)
      count = rr->second.countTransform(count);

    MTL::Buffer* out = p_->ensureOut(path, count);          // per-path persistent (reused across cooks)
    void* st = p_->ensureState(path, n->opType, count);     // stateful ops live on the resident path

    PointCookCtx cc;
    cc.dev = p_->dev; cc.lib = p_->lib; cc.queue = p_->queue;
    cc.ctx = &ctx; cc.graph = nullptr; cc.reg = reg;  // resident path: ops read params, never a graph
    cc.nodeId = 0; cc.count = count;
    cc.inputs = ins.data(); cc.inputCounts = insCounts.data(); cc.inputCount = (int)ins.size();
    cc.output = out; cc.state = st;
    cc.params = params; cc.inputParams = insParams.data();
    auto r = cookReg().find(n->opType);
    if (r != cookReg().end() && r->second.cook) r->second.cook(cc);
    cooked[path] = out;
    return out;
  };

  // Cook a command node: resolve its upstream Points bag, then call its cmd fn -> RenderCommand.
  // std::function (not auto) so the bypass redirect can self-recurse through chained bypasses.
  // Same depth cap as cookNode (no memo here, so a bypass cycle recursed bare before 修2).
  std::function<RenderCommand(const std::string&, int)> cookCommand =
      [&](const std::string& path, int depth) -> RenderCommand {
    RenderCommand rcmd;
    if (depth > kCookDepthCap) { warnCookDepthOnce(); return rcmd; }  // safe fail: empty chain
    const ResidentNode* n = rg.node(path);
    if (!n) return rcmd;

    // S2 BYPASS, command flow (修B; = TiXL ByPassUpdate on Slot<Command>, Slot.cs:176-179 /
    // Instance.Connections.cs:275-289): the bypassed node's MAIN output Command IS its MAIN
    // input's upstream chain — its own cmd fn never runs, so it contributes no draw items
    // (skip-self, the upstream command list passes through unchanged). Unwired main input =
    // the input slot's default = an empty chain.
    if (n->bypassed) {
      const ResidentInput* ri = n->input(n->bypassInSlot);
      if (ri && ri->driver == ResidentInput::Driver::Connection)
        return cookCommand(ri->srcNodePath, depth + 1);
      return rcmd;
    }

    const NodeSpec* s = findSpec(n->opType);
    if (!s) return rcmd;
    auto cm = cmdReg().find(n->opType);
    if (cm == cmdReg().end() || !cm->second) return rcmd;
    MTL::Buffer* pts = nullptr;
    uint32_t cnt = 0;
    for (const PortSpec& port : s->ports) {
      if (!(port.isInput && port.dataType == "Points")) continue;
      const ResidentInput* ri = n->input(port.id);
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        pts = cookNode(ri->srcNodePath, depth + 1);
        cnt = p_->outCount[ri->srcNodePath];
      }
      break;
    }
    CmdCookCtx cc;
    cc.ctx = &ctx; cc.graph = nullptr; cc.reg = reg;
    cc.nodeId = 0; cc.points = pts; cc.count = cnt;
    cc.params = nodeParams(path);
    return cm->second(cc);
  };

  // Execute a Command chain into target() via a named texture executor (mirror of flat).
  auto execIntoTarget = [&](const RenderCommand& chain, const std::string& execType,
                            const std::string& path) {
    auto tx = texReg().find(execType);
    if (tx == texReg().end() || !tx->second) { p_->clearTarget(); return; }
    TexCookCtx tc;
    tc.dev = p_->dev; tc.lib = p_->lib; tc.queue = p_->queue;
    tc.ctx = &ctx; tc.graph = nullptr; tc.reg = reg;
    tc.nodeId = 0; tc.command = &chain; tc.output = p_->target;
    tc.params = nodeParams(path);
    tx->second(tc);
  };

  // Cook a TEXTURE-flow node (RenderTarget OR an image filter like Blur) into its OWN
  // resolution-sized texture and return it (resident mirror of cook()'s cookTexNode). The
  // Texture2D gather direct-through (lane I): a filter's Texture2D input recurses to the upstream
  // tex node here. `depth` shares the cook recursion cap. Cycle/depth-safe.
  std::function<MTL::Texture*(const std::string&, int)> cookTexNode =
      [&](const std::string& path, int depth) -> MTL::Texture* {
    if (depth > kCookDepthCap) return nullptr;
    const ResidentNode* n = rg.node(path);
    const NodeSpec* s = n ? findSpec(n->opType) : nullptr;
    if (!n || !s) return nullptr;
    auto tx = texReg().find(n->opType);
    if (tx == texReg().end() || !tx->second) return nullptr;
    const std::map<std::string, float>* tp = nodeParams(path);

    // Gather inputs in spec order FIRST (mirror of flat cookTexNode, point_graph.cpp): concat
    // Command inputs, recurse EACH Texture2D input. Gathering BEFORE output sizing is harmless for
    // pixel ops (they size off the Resolution pin and ignore input dims) — done UNCONDITIONALLY so
    // a compute leaf's SizeFn can be handed the cooked INPUT size (Crop's output = input - margins).
    RenderCommand chain;
    const MTL::Texture* texInputs[TexCookCtx::kMaxTexInputs] = {nullptr, nullptr, nullptr, nullptr};
    int texInputCount = 0;
    for (const PortSpec& port : s->ports) {
      if (!port.isInput) continue;
      const ResidentInput* ri = n->input(port.id);
      bool wired = ri && ri->driver == ResidentInput::Driver::Connection;
      if (port.dataType == "Command") {
        if (!wired) continue;
        RenderCommand up = cookCommand(ri->srcNodePath, depth + 1);
        chain.items.insert(chain.items.end(), up.items.begin(), up.items.end());
      } else if (port.dataType == "Texture2D") {
        int slot = texInputCount;  // each Texture2D port occupies the next slot (wired or not)
        if (slot < TexCookCtx::kMaxTexInputs) {
          texInputs[slot] = wired ? cookTexNode(ri->srcNodePath, depth + 1) : nullptr;
          texInputCount = slot + 1;
        }
      }
    }

    // Size this node's own output texture. Default = the Resolution pin (window size fallback). A
    // COMPUTE leaf may override via its SizeFn (Crop: output = inputSize - margins) from the cooked
    // input dims — the output can be smaller/larger than the Resolution pin (mirror of flat).
    RenderResolution res = resolveRenderResolution(
        tp ? *tp : std::map<std::string, float>{}, RenderResolution{p_->width, p_->height});
    auto sizeIt = imageFilterSizeFns().find(n->opType);
    if (sizeIt != imageFilterSizeFns().end() && texInputs[0]) {
      res = sizeIt->second(tp ? *tp : std::map<std::string, float>{},
                           RenderResolution{(uint32_t)texInputs[0]->width(),
                                            (uint32_t)texInputs[0]->height()});
    }
    // Compute leaves write via RWTexture2D -> their output needs MTL::TextureUsageShaderWrite.
    bool needsWrite = imageFilterComputeTypes().count(n->opType) != 0;
    MTL::Texture* tex = p_->ensureTex(path, res.w, res.h, needsWrite);

    TexCookCtx tc;
    tc.dev = p_->dev; tc.lib = p_->lib; tc.queue = p_->queue;
    tc.ctx = &ctx; tc.graph = nullptr; tc.reg = reg;
    tc.nodeId = 0; tc.command = &chain; tc.output = tex;
    for (int k = 0; k < texInputCount; ++k) tc.inputTextures[k] = texInputs[k];
    tc.inputTextureCount = texInputCount;
    tc.inputTexture = texInputs[0];
    tc.params = tp;
    tx->second(tc);
    return tex;
  };

  // Terminal three-flow (parity with cook()): tex (RenderTarget executes its Command chain into
  // its own resolution-sized texture) / cmd (1-item chain into the window target) / preview
  // (Points-producing op -> synthesized 1-item chain). No legacy draw flow here — the resident
  // era starts after the render-target pivot. Unknown target -> black, no crash.
  //
  // S2 BYPASS at the TERMINAL (修B): a bypassed terminal realizes its MAIN input's upstream
  // producer instead (= viewing TiXL's bypassed slot shows the passed-through value). Since lane I
  // a Texture2D-input gather DOES exist mid-walk (cookTexNode recurses image filters into their
  // upstream tex producer), but the terminal bypass loop still only redirects the displayed node;
  // a bypassed Texture2D node shows the upstream texture producer's own texture, at the UPSTREAM's
  // resolution (TiXL: the value IS the upstream texture). The loop walks chained bypasses
  // (depth-capped to match the eval paths' cycle guard); the redirect target then dispatches by
  // ITS own flow below. An unwired main input = the input slot's default = nothing to show -> black.
  std::string termPath = targetPath;
  const ResidentNode* tn = rg.node(termPath);
  for (int guard = 0; tn && tn->bypassed; ++guard) {
    if (guard > 64) { p_->clearTarget(); return; }
    const ResidentInput* ri = tn->input(tn->bypassInSlot);
    if (!(ri && ri->driver == ResidentInput::Driver::Connection)) { p_->clearTarget(); return; }
    termPath = ri->srcNodePath;
    tn = rg.node(termPath);
  }
  const NodeSpec* ts = tn ? findSpec(tn->opType) : nullptr;
  if (!tn || !ts) { p_->clearTarget(); return; }

  auto texIt = texReg().find(tn->opType);
  if (texIt != texReg().end() && texIt->second) {
    // Texture terminal (RenderTarget OR an image filter like Blur): cook it + its upstream
    // tex/command chain into its own resolution-sized texture via the recursive tex walker.
    MTL::Texture* tex = cookTexNode(termPath, 0);
    if (tex) p_->displayTex = tex;  // viewport shows the resolution-sized texture
    else p_->clearTarget();
  } else if (cmdReg().find(tn->opType) != cmdReg().end()) {
    RenderCommand chain = cookCommand(termPath, 0);
    execIntoTarget(chain, "RenderTarget", termPath);
  } else {
    MTL::Buffer* out = cookNode(termPath, 0);
    const PortSpec* outPort = nullptr;
    for (const PortSpec& port : ts->ports)
      if (!port.isInput) { outPort = &port; break; }
    if (out && outPort && outPort->dataType == "Points") {
      RenderCommand chain;
      chain.items.push_back(RenderDrawItem{out, p_->outCount[termPath], 3.5f});
      execIntoTarget(chain, "RenderTarget", termPath);
    } else {
      p_->clearTarget();  // no visualizer for this output type yet (§5)
    }
  }
}

}  // namespace sw
