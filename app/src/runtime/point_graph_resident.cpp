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

#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                 // NodeSpec/PortSpec/findSpec
#include "runtime/image_filter_op_registry.h"  // imageFilterComputeTypes/imageFilterSizeFns (compute leaf seam)
#include "runtime/mesh_op_registry.h"          // MeshCookCtx/SwMeshView/findMeshOp (the 4th cook flow = MeshBuffers)
#include "runtime/pointlist_op_registry.h"     // PointListCookCtx/findPointListOp (the 7th cook flow = host SwPoint list)
#include "runtime/gradient_op_registry.h"      // GradientCookCtx/findGradientOp (the 8th cook flow = host Gradient)
#include "runtime/point_graph_internal.h"  // PointGraph::Impl + op registries
#include "runtime/resident_eval_graph.h"   // ResidentEvalGraph / drivers / resolveResidentFloatInputs
#include "runtime/tixl_point.h"            // SwPoint + EvaluationContext

namespace sw {

// The ListToBuffer upload-bridge predicate (pointlist_ops_listtobuffer.cpp); same forward-decl as in
// point_graph.cpp (the leaf defines it once). cookResident detects ListToBuffer and takes the PointList
// gather + memcpy path so the CPU point family draws on the PRODUCTION (resident) path, not just flat.
bool isListToBufferType(const std::string& type);

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

  // FEEDBACK per-frame memo (cross-frame ping-pong flow = KeepPreviousFrame): resident mirror of the
  // flat feedbackCooked. A feedback op runs its blit + toggle EXACTLY ONCE per frame; if both outputs
  // are wired, the second pull reads this cache instead of re-cooking (= double toggle). Keyed by path.
  std::map<std::string, std::array<MTL::Texture*, FeedbackCookCtx::kMaxTexOutputs>> feedbackCooked;

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

  // PointList walker (7th cook flow on the RESIDENT path): cook ONE upstream PointList-producing node
  // into a host SwPoint list, gathering its PointList inputs THROUGH the resident graph (following the
  // ResidentInput Connection drivers the flatten DOES project onto PointList slots — PointList is NOT a
  // String, so flatten gives it a ResidentInput, exactly like FloatList/Points). Mirror of the flat
  // cookPointListNode + the cookResidentFloatList pattern (resident_host_scalar_cook.cpp). This is what
  // makes the CPU point family LIVE in the running app: ListToBuffer (cookNode below) calls this to
  // gather the host list it memcpys onto the GPU, on the production cookResident path (R-2 iron rule).
  std::function<const std::vector<SwPoint>*(const std::string&, int)> cookResidentPointList;

  // Gradient walker (8th cook flow on the RESIDENT/PRODUCTION path — the R-2 iron rule). Cooks ONE
  // Gradient-producing node into its per-path host gradient (p_->gradientBuf[path]), gathering its
  // Gradient inputs THROUGH the resident graph (Connection drivers projected onto Gradient slots,
  // exactly like Points/PointList). Mirror of the flat cookGradientNode + cookResidentPointList.
  // GradientsToTexture (a tex op below) calls this to gather the host gradients it samples into a
  // texture on the production path. Returns nullptr if `path` is not a gradient op.
  std::function<const SwGradient*(const std::string&, int)> cookResidentGradient;

  // Mesh walker (4th cook flow on the RESIDENT/PRODUCTION path — the R-2 iron rule). Cooks ONE mesh
  // node (generator OR consumer) into its PER-PATH owned pair (p_->meshVtxBuf/meshIdxBuf[path]) and
  // returns a borrowed SwMeshView. A CONSUMER (TransformMesh/CombineMeshes) gathers its Mesh input(s)
  // THROUGH the resident graph (following the ResidentInput Connection drivers the flatten projects
  // onto Mesh slots — Mesh is NOT a String, so it gets a ResidentInput exactly like Points/PointList).
  // This is what makes the mesh family + DrawMeshUnlit LIVE in the running app: the resident cookCommand
  // Mesh branch (below) calls this to fill cc.meshVtx/idx/faceCount. Mirror of the flat cookMeshNode +
  // cookResidentPointList. Returns an empty view if `path` is not a mesh op / produced nothing.
  std::function<SwMeshView(const std::string&, int)> cookResidentMesh;

  // Texture walker forward-decl (the texture-into-points seam, RESIDENT/PRODUCTION path — R-2 iron
  // rule). Declared HERE (above cookNode, peer to cookResidentGradient/Mesh) so cookNode's Texture2D
  // gather can recurse into the upstream tex op: a Points op with a Texture2D input
  // (SamplePointColorAttributes) cooks each wired Texture2D input via cookTexNode into
  // PointCookCtx::inputTextures. The body is assigned far below (it's mutually recursive with
  // cookCommand); std::function breaks the ordering. `outSlotId` = the upstream OUTPUT slot id the
  // consumer wired to (ResidentInput::srcSlotId); single-output ops ignore it.
  std::function<MTL::Texture*(const std::string&, int, const std::string&)> cookTexNode;

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

    // PointList → GPU UPLOAD BRIDGE (ListToBuffer) on the RESIDENT/PRODUCTION path (R-2): gather the
    // node's PointList input(s) through the resident graph (cookResidentPointList follows the Connection
    // drivers), concatenate into one host SwPoint vector, size the output buffer to the total count
    // (ensureOut, per-path persistent), and memcpy the host points into contents(). After this the
    // output is a normal GPU point bag the resident DrawPoints path consumes unchanged — so a real
    // graph RadialPointsCpu→ListToBuffer→DrawPoints→RenderTarget renders ON SCREEN in the running app,
    // not only in a flat selftest. Mirror of the flat cookNode ListToBuffer branch (point_graph.cpp).
    if (isListToBufferType(n->opType)) {
      std::vector<SwPoint> all;
      for (const PortSpec& port : s->ports) {
        if (!(port.isInput && port.dataType == "PointList")) continue;
        const ResidentInput* ri = n->input(port.id);
        if (ri && ri->driver == ResidentInput::Driver::Connection) {
          const std::vector<SwPoint>* up = cookResidentPointList(ri->srcNodePath, depth + 1);
          if (up) all.insert(all.end(), up->begin(), up->end());
          if (port.multiInput) {
            for (const auto& ec : ri->extraConns) {
              const std::vector<SwPoint>* ue = cookResidentPointList(ec.first, depth + 1);
              if (ue) all.insert(all.end(), ue->begin(), ue->end());
            }
          }
        }
      }
      uint32_t count = (uint32_t)all.size();
      MTL::Buffer* out = p_->ensureOut(path, count);  // per-path persistent; records outCount[path]=count
      if (out && count > 0)
        std::memcpy(out->contents(), all.data(), (size_t)count * sizeof(SwPoint));
      cooked[path] = out;
      return out;
    }

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

    // TEXTURE2D gather (the texture-into-points seam, RESIDENT/PRODUCTION path — R-2 iron rule): a
    // Points op with "Texture2D" input ports (SamplePointColorAttributes) cooks each wired Texture2D
    // input through the resident Connection drivers into PointCookCtx::inputTextures, in spec port
    // order. Mirror of the resident tex gather (cookTexNode's Texture2D branch below): each Texture2D
    // port occupies the next slot (wired or not); cookTexNode(srcNodePath, depth+1, srcSlotId). Empty
    // for every existing Points op (no Texture2D port) → byte-identical path. isBufferInput() skips
    // Texture2D ports above, so this loop is the ONLY consumer of them (no double-count of Points).
    const MTL::Texture* texInputs[PointCookCtx::kMaxTexInputs] = {nullptr, nullptr, nullptr, nullptr};
    int texInputCount = 0;
    for (const PortSpec& port : s->ports) {
      if (!port.isInput || port.dataType != "Texture2D") continue;
      int slot = texInputCount;  // each Texture2D port occupies the next slot (wired or not)
      if (slot < PointCookCtx::kMaxTexInputs) {
        const ResidentInput* ri = n->input(port.id);
        bool wired = ri && ri->driver == ResidentInput::Driver::Connection;
        texInputs[slot] = wired ? cookTexNode(ri->srcNodePath, depth + 1, ri->srcSlotId) : nullptr;
        texInputCount = slot + 1;
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
    for (int k = 0; k < texInputCount; ++k) cc.inputTextures[k] = texInputs[k];
    cc.inputTextureCount = texInputCount;  // texture-into-points seam (0 for ops with no Texture2D input)
    auto r = cookReg().find(n->opType);
    if (r != cookReg().end() && r->second.cook) r->second.cook(cc);
    cooked[path] = out;
    return out;
  };

  // PointList walker body (assigned now that cookNode exists; the two don't recurse into each other —
  // ListToBuffer is the only crossing, via cookNode's branch above). Cooks ONE upstream PointList node
  // into Impl::pointListBuf[path], gathering its PointList inputs through the resident Connection drivers
  // (primary + extraConns, wire-declaration order), in spec port order. Mirror of the flat
  // cookPointListNode + cookResidentFloatList. Returns nullptr if `path` is not a pointlist op.
  cookResidentPointList = [&](const std::string& path, int depth) -> const std::vector<SwPoint>* {
    if (depth > kCookDepthCap) { warnCookDepthOnce(); return nullptr; }
    const ResidentNode* n = rg.node(path);
    if (!n) return nullptr;
    const NodeSpec* s = findSpec(n->opType);
    if (!s) return nullptr;
    const PointListCookFn* fn = findPointListOp(n->opType);
    if (!fn || !*fn) return nullptr;

    std::vector<std::vector<SwPoint>> inputLists;
    for (const PortSpec& port : s->ports) {
      if (!(port.isInput && port.dataType == "PointList")) continue;
      const ResidentInput* ri = n->input(port.id);
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        const std::vector<SwPoint>* upp = cookResidentPointList(ri->srcNodePath, depth + 1);
        inputLists.push_back(upp ? *upp : std::vector<SwPoint>{});
        if (port.multiInput) {
          for (const auto& ec : ri->extraConns) {
            const std::vector<SwPoint>* uep = cookResidentPointList(ec.first, depth + 1);
            inputLists.push_back(uep ? *uep : std::vector<SwPoint>{});
          }
        }
      }
      // (An unwired / Constant PointList input contributes NO entry → empty → faithful to the flat gather.)
    }

    std::vector<SwPoint>& out = p_->pointListBuf[path];
    PointListCookCtx pc;
    pc.dev = p_->dev; pc.lib = p_->lib; pc.queue = p_->queue;
    pc.ctx = &ctx; pc.nodeId = 0;
    pc.inputLists = &inputLists;
    pc.output = &out;
    pc.params = nodeParams(path);
    (*fn)(pc);
    return &out;
  };

  // Gradient walker body (8th cook flow on the RESIDENT path; assigned now that cookNode exists — they
  // don't recurse into each other, the only crossing is GradientsToTexture's tex branch via cookTexNode).
  // VERBATIM clone of cookResidentPointList (SwPoint→SwGradient): cook ONE upstream Gradient node into
  // p_->gradientBuf[path], gathering its Gradient inputs through the resident Connection drivers (primary
  // + extraConns, wire-declaration order), in spec port order. Returns nullptr if not a gradient op.
  cookResidentGradient = [&](const std::string& path, int depth) -> const SwGradient* {
    if (depth > kCookDepthCap) { warnCookDepthOnce(); return nullptr; }
    const ResidentNode* n = rg.node(path);
    if (!n) return nullptr;
    const NodeSpec* s = findSpec(n->opType);
    if (!s) return nullptr;
    const GradientCookFn* fn = findGradientOp(n->opType);
    if (!fn || !*fn) return nullptr;

    std::vector<SwGradient> inputGradients;
    for (const PortSpec& port : s->ports) {
      if (!(port.isInput && port.dataType == "Gradient")) continue;
      const ResidentInput* ri = n->input(port.id);
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        const SwGradient* upp = cookResidentGradient(ri->srcNodePath, depth + 1);
        inputGradients.push_back(upp ? *upp : SwGradient{});
        if (port.multiInput) {
          for (const auto& ec : ri->extraConns) {
            const SwGradient* uep = cookResidentGradient(ec.first, depth + 1);
            inputGradients.push_back(uep ? *uep : SwGradient{});
          }
        }
      }
      // (An unwired / Constant Gradient input contributes NO entry → faithful to the flat gather.)
    }

    SwGradient& out = p_->gradientBuf[path];
    GradientCookCtx gc;
    gc.dev = p_->dev; gc.lib = p_->lib; gc.queue = p_->queue;
    gc.ctx = &ctx; gc.nodeId = 0;
    gc.inputGradients = &inputGradients;
    gc.output = &out;
    gc.params = nodeParams(path);
    (*fn)(gc);
    return &out;
  };

  // Mesh walker body (4th cook flow on the RESIDENT path; assigned now that cookNode exists — the two
  // don't recurse into each other, the only crossing is cookCommand's Mesh branch below). Gathers the
  // node's Mesh input(s) through the resident Connection drivers (primary + extraConns, in spec port
  // order), runs countFn(params, views, n) then cookFn, into the per-path owned pair. Mirror of the flat
  // cookMeshNode + cookResidentPointList. Returns a borrowed SwMeshView (empty if not a mesh op).
  cookResidentMesh = [&](const std::string& path, int depth) -> SwMeshView {
    SwMeshView outView;
    if (depth > kCookDepthCap) { warnCookDepthOnce(); return outView; }
    const ResidentNode* n = rg.node(path);
    if (!n) return outView;
    const NodeSpec* s = findSpec(n->opType);
    if (!s) return outView;
    const MeshOpReg* reg = findMeshOp(n->opType);
    if (!reg || !reg->cook || !reg->count) return outView;

    // Gather upstream Mesh inputs through the resident graph (Connection drivers; MultiInput → primary +
    // extraConns, wire-declaration order). cookResidentMesh fills p_->meshVtxBuf[srcPath] for each source.
    std::vector<SwMeshView> inputMeshes;
    for (const PortSpec& port : s->ports) {
      if (!(port.isInput && port.dataType == "Mesh")) continue;
      const ResidentInput* ri = n->input(port.id);
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        inputMeshes.push_back(cookResidentMesh(ri->srcNodePath, depth + 1));
        if (port.multiInput)
          for (const auto& ec : ri->extraConns)
            inputMeshes.push_back(cookResidentMesh(ec.first, depth + 1));
      }
      // (An unwired / Constant Mesh input contributes NO entry → empty → faithful to the flat gather.)
    }

    const std::map<std::string, float>* mp = nodeParams(path);
    uint32_t vtxCount = 0, idxCount = 0;
    reg->count(mp, inputMeshes.data(), (int)inputMeshes.size(), vtxCount, idxCount);  // counts FIRST

    MTL::Buffer* vb = nullptr;
    MTL::Buffer* ib = nullptr;
    p_->ensureMesh(path, vtxCount, idxCount, vb, ib);  // per-path owned pair (string key, no flat collision)

    MeshCookCtx mc;
    mc.dev = p_->dev; mc.lib = p_->lib; mc.queue = p_->queue;
    mc.ctx = &ctx; mc.nodeId = 0;
    mc.vertexCount = vtxCount; mc.indexCount = idxCount;
    mc.output_vertices = vb; mc.output_indices = ib;
    mc.inputMeshes = inputMeshes.data(); mc.inputMeshCount = (int)inputMeshes.size();
    mc.params = mp;
    reg->cook(mc);

    outView.vtx = vb; outView.vtxCount = vtxCount;
    outView.idx = ib; outView.faceCount = idxCount;
    return outView;
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
    SwMeshView inMesh;            // ★R-2: first wired Mesh input (DrawMeshUnlit) — was UNGATHERED before
    bool haveMesh = false;
    RenderCommand inCmd;          // Camera op's Command subtree (Cut 3)
    bool haveInCmd = false;
    bool havePts = false;
    for (const PortSpec& port : s->ports) {
      if (!port.isInput) continue;
      if (port.dataType == "Points" && !havePts) {
        const ResidentInput* ri = n->input(port.id);
        if (ri && ri->driver == ResidentInput::Driver::Connection) {
          pts = cookNode(ri->srcNodePath, depth + 1);
          cnt = p_->outCount[ri->srcNodePath];
        }
        havePts = true;
      } else if (port.dataType == "Mesh" && !haveMesh) {
        // ★R-2 production black-hole fix: DrawMeshUnlit's Mesh input was NEVER gathered on the resident
        // path (this branch did not exist), so cc.meshVtx stayed null → cookDrawMeshUnlit returned an
        // empty chain → the running app drew NOTHING (a Draw*Mesh in production was black). Gather the
        // upstream mesh node into a SwMeshView (mirror of the flat cookCommand Mesh branch).
        const ResidentInput* ri = n->input(port.id);
        if (ri && ri->driver == ResidentInput::Driver::Connection)
          inMesh = cookResidentMesh(ri->srcNodePath, depth + 1);
        haveMesh = true;
      } else if (port.dataType == "Command" && !haveInCmd) {
        // Cut 3: the Camera op wraps a Command subtree — recurse into the upstream command node
        // (depth-capped via kCookDepthCap) and hand the cooked chain in via cc.inputCommand.
        const ResidentInput* ri = n->input(port.id);
        if (ri && ri->driver == ResidentInput::Driver::Connection)
          inCmd = cookCommand(ri->srcNodePath, depth + 1);
        haveInCmd = true;
      }
    }
    CmdCookCtx cc;
    cc.ctx = &ctx; cc.graph = nullptr; cc.reg = reg;
    cc.nodeId = 0; cc.points = pts; cc.count = cnt;
    cc.meshVtx = inMesh.vtx; cc.meshIdx = inMesh.idx; cc.meshFaceCount = inMesh.faceCount;
    cc.inputCommand = haveInCmd ? &inCmd : nullptr;
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
  // `outSlotId` = the upstream OUTPUT slot id the consumer wired to (ResidentInput::srcSlotId);
  // single-output ops ignore it. A feedback op (KeepPreviousFrame) returns PreviousFrame vs
  // CurrentFrame by it. Empty = "the node's terminal/first output" (the cook-entry caller).
  // (Forward-declared ABOVE cookNode — assigned here so cookNode's Texture2D gather can call it.)
  cookTexNode =
      [&](const std::string& path, int depth, const std::string& outSlotId) -> MTL::Texture* {
    if (depth > kCookDepthCap) return nullptr;
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
      fc.dev = p_->dev; fc.lib = p_->lib; fc.queue = p_->queue;
      fc.params = nodeParams(path);
      for (int k = 0; k < fbInputCount; ++k) fc.inputTextures[k] = fbInputs[k];
      fc.inputTextureCount = fbInputCount;
      if (feedbackNeedsPair(n->opType) && fbInputs[0]) {
        const uint32_t w = (uint32_t)fbInputs[0]->width();
        const uint32_t h = (uint32_t)fbInputs[0]->height();
        const MTL::PixelFormat fmt = (MTL::PixelFormat)feedbackPairFormat(n->opType);
        MTL::Texture* pa = nullptr;
        MTL::Texture* pb = nullptr;
        if (p_->ensureFeedbackPair(path, w, h, fmt, pa, pb)) {
          fc.pairA = pa;
          fc.pairB = pb;
          fc.toggle = &p_->feedbackToggle[path];  // path-keyed cross-frame toggle (production R-2)
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
        p_->feedbackOut[path] = persist;
      }
      return (outOrd >= 0 && outOrd < FeedbackCookCtx::kMaxTexOutputs) ? outs[outOrd] : nullptr;
    }

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
    for (const PortSpec& port : s->ports) {
      if (!port.isInput) continue;
      if (port.dataType == "Curve") hasCurveInput = true;
      const ResidentInput* ri = n->input(port.id);
      bool wired = ri && ri->driver == ResidentInput::Driver::Connection;
      if (port.dataType == "Command") {
        if (!wired) continue;
        RenderCommand up = cookCommand(ri->srcNodePath, depth + 1);
        chain.items.insert(chain.items.end(), up.items.begin(), up.items.end());
      } else if (port.dataType == "Texture2D") {
        int slot = texInputCount;  // each Texture2D port occupies the next slot (wired or not)
        if (slot < TexCookCtx::kMaxTexInputs) {
          texInputs[slot] =
              wired ? cookTexNode(ri->srcNodePath, depth + 1, ri->srcSlotId) : nullptr;
          texInputCount = slot + 1;
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
    // GATED on (hasGradientInput || hasCurveInput), NOT on texOpOwnsOutput alone, deliberately:
    // ValuesToTexture is ALSO an own-tex op but its currency is FloatList, and this TU has no resident
    // FloatList walker (cookResidentFloatList is file-local to resident_host_scalar_cook.cpp). Gating on
    // the host-currency input keeps ValuesToTexture's PRIOR resident behaviour byte-identical (it falls
    // through to ensureTex; its cook early-returns on null ownTexHost — unchanged, zero regression).
    // GradientsToTexture (Gradient currency) + CurvesToTexture (Curve currency) take this branch → both
    // LIVE on the production cookResident path (R-2 rule). CurvesToTexture reads its embedded default
    // Curve (no producer to wire), so its inputCurves stays null here (faithful — see the op's fork note).
    if (texOpOwnsOutput(n->opType) && (hasGradientInput || hasCurveInput)) {
      std::vector<float> hostOut;
      uint32_t ow = 0, oh = 0;
      TexCookCtx tc;
      tc.dev = p_->dev; tc.lib = p_->lib; tc.queue = p_->queue;
      tc.ctx = &ctx; tc.graph = nullptr; tc.reg = reg;
      tc.nodeId = 0; tc.command = &chain;
      tc.inputGradients = hasGradientInput ? &gradientInputs : nullptr;
      // inputCurves stays null: no Curve producer exists, so CurvesToTexture uses its embedded default.
      tc.ownTexHost = &hostOut; tc.ownTexW = &ow; tc.ownTexH = &oh;
      tc.params = tp;
      tx->second(tc);
      const int fpt = texOpOwnFormat(n->opType);
      const MTL::PixelFormat fmt = fpt == 4 ? MTL::PixelFormatRGBA32Float : MTL::PixelFormatR32Float;
      if (ow == 0 || oh == 0 || hostOut.size() < (size_t)ow * oh * fpt) return nullptr;
      MTL::Texture* owned = p_->ensureOwnedTex(path, ow, oh, fmt);
      if (owned)
        owned->replaceRegion(MTL::Region::Make2D(0, 0, ow, oh), 0, hostOut.data(),
                             (NS::UInteger)ow * fpt * sizeof(float));
      return owned;
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
    // Mipped-output ops carry a full mip pyramid (allocated by ensureTex) for downstream
    // sample(uv, level(lod)). mip-WRITE (generateMipmaps blit) happens AFTER the leaf fills level 0.
    bool needsMips = imageFilterMippedOutputTypes().count(n->opType) != 0;
    MTL::Texture* tex = p_->ensureTex(path, res.w, res.h, needsWrite, needsMips);

    TexCookCtx tc;
    tc.dev = p_->dev; tc.lib = p_->lib; tc.queue = p_->queue;
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
        tc.assetTexture = cachedAssetTexture(p_->dev, ai->second, /*mipped=*/false);
    }
    tc.params = tp;
    tx->second(tc);
    // mip-WRITE: leaf committed+waited internally (level 0 ready) -> fill levels 1..N via a blit
    // generateMipmaps (NOT a shader). Same as flat cookTexNode (point_graph.cpp).
    if (needsMips && tex) {
      MTL::CommandBuffer* mc = p_->queue->commandBuffer();
      MTL::BlitCommandEncoder* blit = mc->blitCommandEncoder();
      blit->generateMipmaps(tex);
      blit->endEncoding();
      mc->commit();
      mc->waitUntilCompleted();
    }
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

  if (findMeshOp(tn->opType)) {
    // Mesh terminal (preview pin on a Mesh-producing op): cook the pair so a readback can see it
    // (no Mesh VISUALIZER — Draw*Mesh consumes it). Clear the viewport (parity with flat cook()). The
    // chain still runs through cookResidentMesh so the production mesh gather is on this path too.
    cookResidentMesh(termPath, 0);
    p_->clearTarget();
    return;
  }

  auto texIt = texReg().find(tn->opType);
  if ((texIt != texReg().end() && texIt->second) || isFeedbackOp(tn->opType)) {
    // Texture terminal (RenderTarget OR an image filter like Blur OR a feedback op as terminal):
    // cook it + its upstream tex/command chain into its own texture via the recursive tex walker.
    // A feedback terminal shows its first Texture2D output (empty outSlotId → ordinal 0).
    MTL::Texture* tex = cookTexNode(termPath, 0, std::string());
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
