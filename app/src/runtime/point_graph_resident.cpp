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
#include "runtime/point_graph_internal.h"  // PointGraph::Impl + op registries
#include "runtime/resident_eval_graph.h"   // ResidentEvalGraph / drivers / resolveResidentFloatInputs
#include "runtime/tixl_point.h"            // SwPoint + EvaluationContext

namespace sw {

using pgdetail::cmdReg;
using pgdetail::cookReg;
using pgdetail::isBufferInput;
using pgdetail::texReg;

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

  std::function<MTL::Buffer*(const std::string&)> cookNode =
      [&](const std::string& path) -> MTL::Buffer* {
    auto m = cooked.find(path);
    if (m != cooked.end()) return m->second;
    const ResidentNode* n = rg.node(path);
    if (!n) return nullptr;
    const NodeSpec* s = findSpec(n->opType);
    if (!s) return nullptr;

    // Gather buffer inputs (Points + ParticleForce, spec order) via Connection drivers,
    // + the feeding node's resolved params (force ops read these via cookInputParam).
    std::vector<const MTL::Buffer*> ins;
    std::vector<uint32_t> insCounts;
    std::vector<const std::map<std::string, float>*> insParams;
    uint32_t sumPointsCount = 0;
    for (const PortSpec& port : s->ports) {
      if (!isBufferInput(port)) continue;
      const ResidentInput* ri = n->input(port.id);
      MTL::Buffer* ub = nullptr;
      uint32_t inCount = 0;
      const std::map<std::string, float>* up = nullptr;
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        ub = cookNode(ri->srcNodePath);
        inCount = ub ? p_->outCount[ri->srcNodePath] : 0u;
        up = nodeParams(ri->srcNodePath);
      }
      ins.push_back(ub);
      insCounts.push_back(inCount);
      insParams.push_back(up);
      if (port.dataType == "Points") sumPointsCount += inCount;
    }

    const std::map<std::string, float>* params = nodeParams(path);

    // count: a "Count" Float input (generators) resolved through its driver, else sum of Points.
    uint32_t count = sumPointsCount;
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
  auto cookCommand = [&](const std::string& path) -> RenderCommand {
    RenderCommand rcmd;
    const ResidentNode* n = rg.node(path);
    const NodeSpec* s = n ? findSpec(n->opType) : nullptr;
    if (!n || !s) return rcmd;
    auto cm = cmdReg().find(n->opType);
    if (cm == cmdReg().end() || !cm->second) return rcmd;
    MTL::Buffer* pts = nullptr;
    uint32_t cnt = 0;
    for (const PortSpec& port : s->ports) {
      if (!(port.isInput && port.dataType == "Points")) continue;
      const ResidentInput* ri = n->input(port.id);
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        pts = cookNode(ri->srcNodePath);
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

  // Terminal three-flow (parity with cook()): tex (RenderTarget executes its Command chain into
  // its own resolution-sized texture) / cmd (1-item chain into the window target) / preview
  // (Points-producing op -> synthesized 1-item chain). No legacy draw flow here — the resident
  // era starts after the render-target pivot. Unknown target -> black, no crash.
  const ResidentNode* tn = rg.node(targetPath);
  const NodeSpec* ts = tn ? findSpec(tn->opType) : nullptr;
  if (!tn || !ts) { p_->clearTarget(); return; }

  auto texIt = texReg().find(tn->opType);
  if (texIt != texReg().end() && texIt->second) {
    const std::map<std::string, float>* tp = nodeParams(targetPath);
    RenderResolution res = resolveRenderResolution(
        tp ? *tp : std::map<std::string, float>{}, RenderResolution{p_->width, p_->height});
    MTL::Texture* tex = p_->ensureTex(targetPath, res.w, res.h);
    RenderCommand chain;
    for (const PortSpec& port : ts->ports) {
      if (!(port.isInput && port.dataType == "Command")) continue;
      const ResidentInput* ri = tn->input(port.id);
      if (!(ri && ri->driver == ResidentInput::Driver::Connection)) continue;
      RenderCommand up = cookCommand(ri->srcNodePath);
      chain.items.insert(chain.items.end(), up.items.begin(), up.items.end());
    }
    TexCookCtx tc;
    tc.dev = p_->dev; tc.lib = p_->lib; tc.queue = p_->queue;
    tc.ctx = &ctx; tc.graph = nullptr; tc.reg = reg;
    tc.nodeId = 0; tc.command = &chain; tc.output = tex;
    tc.params = tp;
    texIt->second(tc);
    p_->displayTex = tex;  // viewport shows the resolution-sized texture
  } else if (cmdReg().find(tn->opType) != cmdReg().end()) {
    RenderCommand chain = cookCommand(targetPath);
    execIntoTarget(chain, "RenderTarget", targetPath);
  } else {
    MTL::Buffer* out = cookNode(targetPath);
    const PortSpec* outPort = nullptr;
    for (const PortSpec& port : ts->ports)
      if (!port.isInput) { outPort = &port; break; }
    if (out && outPort && outPort->dataType == "Points") {
      RenderCommand chain;
      chain.items.push_back(RenderDrawItem{out, p_->outCount[targetPath], 3.5f});
      execIntoTarget(chain, "RenderTarget", targetPath);
    } else {
      p_->clearTarget();  // no visualizer for this output type yet (§5)
    }
  }
}

}  // namespace sw
