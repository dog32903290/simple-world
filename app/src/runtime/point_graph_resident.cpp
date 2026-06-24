// runtime/point_graph_resident — cookResident: the resident-graph cook driver (slice 2 walk + 2b parity).
// Walks a ResidentEvalGraph by path-qualified id and realizes `targetPath` into target() with the SAME
// three-flow terminal as the flat cook() (tex/cmd/preview), per-path persistent buffers + stateful state
// (PointGraph::Impl, shared via point_graph_internal.h — the path string IS the frame-stable resource key
// the resident era exists for), and driver-resolved Float params (PointCookCtx::params, the 2b seam).
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

#include "runtime/field_graph_builder.h"   // buildResidentFieldTree (PF-0 field-into-force seam; thin call-site)
#include "runtime/graph.h"                 // NodeSpec/PortSpec/findSpec
#include "runtime/image_filter_op_registry.h"  // imageFilterComputeTypes/imageFilterSizeFns (compute leaf seam)
#include "runtime/mesh_op_registry.h"          // MeshCookCtx/SwMeshView/findMeshOp (the 4th cook flow = MeshBuffers)
#include "runtime/pointlist_op_registry.h"     // PointListCookCtx/findPointListOp (the 7th cook flow = host SwPoint list)
#include "runtime/gradient_op_registry.h"      // GradientCookCtx/findGradientOp (the 8th cook flow = host Gradient)
#include "runtime/curve.h"                 // sw::Curve (bake-into-point seam: PointCookCtx::inputCurves complete type)
#include "runtime/point_graph_internal.h"  // PointGraph::Impl + op registries
#include "runtime/point_ops_setvarcmd.h"   // S3a: cmdVarPush/cmdVarRestore/isCmdContextVarWriter/setVarBugSkipWrite
#include "runtime/resident_eval_graph.h"   // ResidentEvalGraph / drivers / resolveResidentFloatInputs
#include "runtime/tixl_point.h"            // SwPoint + EvaluationContext

namespace sw {

// The ListToBuffer upload-bridge predicate (pointlist_ops_listtobuffer.cpp); same forward-decl as in
// point_graph.cpp (the leaf defines it once). cookResident detects ListToBuffer and takes the PointList
// gather + memcpy path so the CPU point family draws on the PRODUCTION (resident) path, not just flat.
bool isListToBufferType(const std::string& type);

using pgdetail::cmdReg;
using pgdetail::cookReg;
using pgdetail::fillPointCamera;
using pgdetail::isBufferInput;
using pgdetail::texReg;

namespace {
// Recursion cap for the cook walk (修2, 批次8): the SAME 64 every other resident-era walk uses. Before
// this only the TERMINAL bypass loop was capped — a bypass redirect CYCLE inside cookNode/cookCommand
// (A↔B both bypassed) recursed bare = ASan stack-overflow. Exceeding the cap is a SAFE FAIL: null buffer /
// empty chain + one stderr warn per process, never a crash. The cap threads through ALL cookNode/
// cookCommand recursion, so a plain non-bypass wire cycle also fail-safes (covered incidentally).
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
                              const SymbolLibrary* lib, ContextVarMap* ctxVars) {
  p_->displayTex = nullptr;  // default: target() shows the window-sized texture (cmd/preview paths)
  // S1 seam: seed RequestedResolution to the window (TiXL output-layer seeding before eval). Resident
  // mirror of cook(); RenderTarget/SetRequestedResolution push/pop it around their subtree.
  p_->requestedResolution = RenderResolution{p_->width, p_->height};

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
  // S3b LIVE-READ memo trap (resident mirror of the flat leg, production runs THIS): a node whose Float param is
  // driven by a value-rail GetFloatVar resolves to the LIVE scoped var inside a SetVarCmd SubGraph vs its fallback
  // outside. The memo keys only on path — so while a live scope is active (liveCtxVars()!=nullptr) we resolve
  // FRESH and DO NOT cache (the value is ambient-dependent, not a graph property). Off-scope = byte-identical to
  // before (the live branch is reachable only inside a SetVarCmd SubGraph cook, which no off-scope cook enters).
  std::map<std::string, std::map<std::string, float>> scopedScratch;  // per-cook owner of fresh scoped maps
  std::function<const std::map<std::string, float>*(const std::string&)> nodeParams =
      [&](const std::string& path) -> const std::map<std::string, float>* {
    const ResidentNode* n = rg.node(path);
    if (!n) return nullptr;
    if (liveCtxVars()) return &(scopedScratch[path] = resolveResidentFloatInputs(rg, *n, rc));  // fresh, uncached
    auto it = paramsMemo.find(path);
    if (it != paramsMemo.end()) return &it->second;
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

    // MESH gather (the mesh-into-points seam, RESIDENT/PRODUCTION path — R-2 iron rule): a Points op with
    // a "Mesh" input port (MeshVerticesToPoints) cooks its FIRST wired upstream Mesh op through the
    // resident Connection driver via cookResidentMesh (the existing per-path mesh walker → SwMeshView)
    // and borrows its buffers + counts into PointCookCtx. Single Mesh input (not an array) — mirror of
    // the resident cookCommand Mesh branch (DrawMeshUnlit). Empty for every existing Points op (no Mesh
    // port) → meshVtx null / counts 0 → byte-identical path.
    SwMeshView meshIn;
    for (const PortSpec& port : s->ports) {
      if (!(port.isInput && port.dataType == "Mesh")) continue;
      const ResidentInput* ri = n->input(port.id);
      if (ri && ri->driver == ResidentInput::Driver::Connection)
        meshIn = cookResidentMesh(ri->srcNodePath, depth + 1);
      break;  // single Mesh input
    }

    // GRADIENT + CURVE gather (the bake-into-point seam, RESIDENT/PRODUCTION path — R-2 iron rule): a
    // Points op with "Gradient"/"Curve" host-value input ports (MapPointAttributes) gathers each wired
    // upstream host value through the resident Connection drivers into PointCookCtx::inputGradients/
    // inputCurves, in spec port order. Mirror of the resident tex-cook Gradient branch
    // (cookResidentGradient). There is NO Curve producer op yet, so a wired Curve is never gathered (we
    // only DETECT the port). UNWIRED → empty → the op bakes its embedded .t3 defaults (flat-1.0 curve,
    // white→white gradient). Empty for every existing Points op (no Gradient/Curve port) → null →
    // byte-identical path.
    std::vector<SwGradient> gradientInputs;
    bool hasGradientInput = false;
    std::vector<Curve> curveInputs;  // stays empty: no Curve producer (see above)
    bool hasCurveInput = false;
    for (const PortSpec& port : s->ports) {
      if (!port.isInput) continue;
      if (port.dataType == "Curve") {
        hasCurveInput = true;  // DETECT only (no producer to gather)
      } else if (port.dataType == "Gradient") {
        hasGradientInput = true;
        const ResidentInput* ri = n->input(port.id);
        if (ri && ri->driver == ResidentInput::Driver::Connection) {
          const SwGradient* up = cookResidentGradient(ri->srcNodePath, depth + 1);
          gradientInputs.push_back(up ? *up : SwGradient{});
          // MapPointAttributes' Gradient is single-input; no MultiInput expansion needed here.
        }
      }
    }

    // FIELD gather (PF-0, RESIDENT/PRODUCTION path — R-2 iron rule, thin call-site): two-hop force→field
    // chase + recursion in field_graph_builder.cpp → cc.inputFieldTree. Kernel un-consumed until PF-a →
    // byte-identical. cc.graph==nullptr does NOT block this (the DRIVER holds rg, like cookResidentGradient).
    std::shared_ptr<FieldNode> fieldTree = gatherForceResidentFieldTree(rg, path, nodeParams);

    const std::map<std::string, float>* params = nodeParams(path);

    // count: a "Count" Float input (generators) resolved through its driver, else sum of Points
    // (combine concatenates), or the first Points input only for reference-transform ops
    // (SnapToPoints opts into countFromFirstPointsInput — Points2 is a target, not concatenated).
    uint32_t count = sumPointsCount;
    if (auto cr = cookReg().find(n->opType); cr != cookReg().end()) {
      if (cr->second.countFromFirstPointsInput) count = firstPointsCount;
      if (cr->second.countFromMeshVtx) count = meshIn.vtxCount;  // mesh-into-points fork (one Point/vertex)
    }
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
    cc.output = out; cc.state = st; cc.params = params; cc.inputParams = insParams.data();
    for (int k = 0; k < texInputCount; ++k) cc.inputTextures[k] = texInputs[k];
    cc.inputTextureCount = texInputCount;  // texture-into-points seam (0 for ops with no Texture2D input)
    cc.inputGradients = hasGradientInput ? &gradientInputs : nullptr;  // bake-into-point seam (null otherwise)
    cc.inputCurves = hasCurveInput ? &curveInputs : nullptr;  // empty in production (no Curve producer)
    cc.meshVtx = meshIn.vtx; cc.meshVtxCount = meshIn.vtxCount; cc.meshIdx = meshIn.idx; cc.meshFaceCount = meshIn.faceCount;  // mesh seam
    cc.inputFieldTree = fieldTree;  // PF-0 field-into-force seam (null if no wired Field; PF-a consumes it)
    // Camera aspect = ACTIVE RequestedResolution (S1 seam, EvaluationContext.cs:78,94), not raw window.
    const RenderResolution& rrR_ = p_->requestedResolution;  // camera seam (resident mirror of cook())
    fillPointCamera(cc, *s, (rrR_.h > 0) ? (float)rrR_.w / (float)rrR_.h : 1.0f);
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

  // Mesh walker body (4th cook flow): forwards to the extracted method PointGraph::Impl::cookResidentMesh
  // (resident_mesh_cook.cpp). The lambda keeps the one-arg call shape every caller uses (the cookCommand
  // Mesh branch, the Mesh terminal, the new cookNode Mesh gather) + threads the shared rg/rc/ctx in. The
  // method resolves params inline (memo-free twin of nodeParams; same pure resolver → byte-identical map).
  cookResidentMesh = [&](const std::string& path, int depth) -> SwMeshView {
    return p_->cookResidentMesh(rg, path, rc, ctx, depth);
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
    const MTL::Texture* inTex = nullptr;  // ★S2c: first wired Texture2D input (Layer2d/DrawScreenQuad)
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
      } else if (port.dataType == "Texture2D" && !inTex) {
        // ★S2c: mirror of flat cookCommand FORK#1 (point_graph.cpp:444-448) — the resident path had NO
        // Texture2D gather → Layer2d/DrawScreenQuad drew BLACK (--selftest-layercompose resident leg).
        const ResidentInput* ri = n->input(port.id);
        if (ri && ri->driver == ResidentInput::Driver::Connection)
          inTex = cookTexNode(ri->srcNodePath, depth + 1, ri->srcSlotId);
      } else if (port.dataType == "Command" && !haveInCmd) {
        // S2a KEYSTONE — resident mirror of the flat MultiInput Command collector (doc: point_ops_execute
        // .cpp). MultiInput Command (Execute) concats the primary wire (srcNodePath) + extraConns (批次25,
        // wire-ordered); single-input (Camera) has empty extraConns. S1: SetRequestedResolution pushes here.
        const RenderResolution savedReq = p_->requestedResolution;
        if (n->opType == "SetRequestedResolution")
          p_->requestedResolution = resolveSetRequestedResolution(*nodeParams(path), savedReq);
        // S3a context-var scope (resident mirror — the S2c blood-lesson leg, production runs THIS). Same
        // TiXL SetFloatVar.cs:26-45 push/restore around the SubGraph; varName off ResidentNode::strInputs.
        // Inactive no-op when ctxVars null / not a writer / -bug skips the write.
        CmdVarScope varScope;
        if (!setVarBugSkipWrite() && isCmdContextVarWriter(n->opType)) {
          std::string varName;
          auto vit = n->strInputs.find("VariableName");
          if (vit != n->strInputs.end()) varName = vit->second;
          varScope = cmdVarPush(n->opType, *nodeParams(path), varName, ctxVars);
        }
        // C1 ACTIVE-CAMERA scope (resident mirror — production runs THIS leg; CAMERA3D_BLUEPRINT §1 HARD GATE).
        // A resident-only miss = resident point ops read the default under a wired Camera = a prod-only black-
        // hole (S2c). Same resolveActiveCamera + LiveCameraScope as flat; map from resident nodeParams(path).
        ActiveCamera activeCam;
        if (!cameraScopeBugSkipPush() && isCameraScopeWriter(n->opType))
          activeCam = resolveActiveCamera(*nodeParams(path));
        {
          // S3b LIVE-READ scope (resident mirror — production runs THIS leg): ctxVars is the ambient live map
          // WHILE the SubGraph cooks, so a value-rail GetFloatVar driving a SubGraph node's param re-resolves
          // LIVE. Engages only on an active writer push; else no-op.
          LiveCtxVarScope liveScope(varScope.active ? ctxVars : nullptr);
          LiveCameraScope liveCam(activeCam);  // C1: active camera live for the SubGraph cook (point rail reads it)
          const ResidentInput* ri = n->input(port.id);
          if (n->opType == "Switch") {
            // S3b Switch SUB-SELECT (resident mirror — production runs THIS leg). ★§3 OFF-BY-ONE TRAP: build
            // srcPaths primary-FIRST (ri->srcNodePath) then extraConns to match the flat wire order.
            std::vector<std::string> srcPaths;  // wire-declaration order: [primary] + extraConns
            if (ri && ri->driver == ResidentInput::Driver::Connection) {
              srcPaths.push_back(ri->srcNodePath);                       // wire 0 = primary
              for (const auto& ec : ri->extraConns) srcPaths.push_back(ec.first);  // wires 1..N
            }
            int rawIndex = 0;  // Switch.Index value param (C# (int) cast = trunc toward 0)
            const std::map<std::string, float>* sp = nodeParams(path);
            if (sp) { auto it = sp->find("Index"); if (it != sp->end()) rawIndex = (int)it->second; }
            // -bug: switchIgnoreIndexForTest() forces cook-all (selection lost) — the §3 resident tooth.
            int sel = switchIgnoreIndexForTest() ? kSwitchSelectAll
                                                 : switchSelectIndex(rawIndex, (int)srcPaths.size());
            if (sel == kSwitchSelectAll) {
              for (const std::string& spath : srcPaths) {  // -2: cook ALL (== Execute), wire order
                RenderCommand sub = cookCommand(spath, depth + 1);
                inCmd.items.insert(inCmd.items.end(), sub.items.begin(), sub.items.end());
              }
            } else if (sel != kSwitchSelectNone) {  // -1/empty: cook NOTHING (inCmd stays empty)
              RenderCommand sub = cookCommand(srcPaths[(size_t)sel], depth + 1);  // ONLY the selected wire
              inCmd.items.insert(inCmd.items.end(), sub.items.begin(), sub.items.end());
            }
          } else if (n->opType == "Loop") {
            // S3c Loop RE-COOK (resident mirror — production runs THIS leg). Same loopRunIterations() the flat
            // leg calls: cook the single wired SubGraph `Count` times, per-iter var write + live scope + concat.
            // Wired source = primary wire (ri->srcNodePath); re-cooked each call.
            std::string subPath;
            if (ri && ri->driver == ResidentInput::Driver::Connection) subPath = ri->srcNodePath;
            int count = 0;
            if (const std::map<std::string, float>* lp = nodeParams(path)) {
              auto it = lp->find("Count"); if (it != lp->end()) count = (int)it->second;
            }
            std::string iVar, pVar;
            if (auto it = n->strInputs.find("IndexVariable"); it != n->strInputs.end()) iVar = it->second;
            if (auto it = n->strInputs.find("ProgressVariable"); it != n->strInputs.end()) pVar = it->second;
            loopRunIterations(count, iVar, pVar, ctxVars, inCmd,
                              [&]() { return subPath.empty() ? RenderCommand{}
                                                             : cookCommand(subPath, depth + 1); });
          } else if (n->opType == "ExecRepeatedly") {
            // S3c ExecRepeatedly RE-COOK (resident mirror — production runs THIS leg). Same
            // execRepeatedlyRunRepetitions() the flat leg calls: cook the MultiInput wires `RepeatCount`
            // (clamped [0,100]) times, concatenating each repetition. ★§3 wire order: primary-FIRST then
            // extraConns (same trap Switch flags). SkipFrameCount=0 path.
            std::vector<std::string> srcPaths;  // wire-declaration order: [primary] + extraConns
            if (ri && ri->driver == ResidentInput::Driver::Connection) {
              srcPaths.push_back(ri->srcNodePath);                                  // wire 0 = primary
              for (const auto& ec : ri->extraConns) srcPaths.push_back(ec.first);   // wires 1..N
            }
            int rep = 1;
            if (const std::map<std::string, float>* rp = nodeParams(path)) {
              auto it = rp->find("RepeatCount"); if (it != rp->end()) rep = (int)it->second;
            }
            rep = rep < 0 ? 0 : (rep > 100 ? 100 : rep);  // ExecRepeatedly.cs:24 Clamp(0,100)
            execRepeatedlyRunRepetitions(rep, inCmd, [&]() {
              RenderCommand all;
              for (const std::string& spath : srcPaths) {  // cook ALL wires fresh, wire order (one rep)
                RenderCommand sub = cookCommand(spath, depth + 1);
                all.items.insert(all.items.end(), sub.items.begin(), sub.items.end());
              }
              return all;
            });
          } else if (ri && ri->driver == ResidentInput::Driver::Connection) {
            RenderCommand sub = cookCommand(ri->srcNodePath, depth + 1);  // primary wire (wire 0)
            inCmd.items.insert(inCmd.items.end(), sub.items.begin(), sub.items.end());
            if (port.multiInput && !executeCollectFirstOnlyForTest())  // -bug: skip the extra wires
              for (const auto& ec : ri->extraConns) {
                RenderCommand es = cookCommand(ec.first, depth + 1);
                inCmd.items.insert(inCmd.items.end(), es.items.begin(), es.items.end());
              }
          }
        }
        cmdVarRestore(varScope, ctxVars);    // S3a restore (SetFloatVar.cs:33-40)
        p_->requestedResolution = savedReq;  // restore (SetRequestedResolution.cs:28)
        haveInCmd = true;
      }
    }
    CmdCookCtx cc;
    cc.ctx = &ctx; cc.graph = nullptr; cc.reg = reg;
    cc.nodeId = 0; cc.points = pts; cc.count = cnt;
    cc.meshVtx = inMesh.vtx; cc.meshIdx = inMesh.idx; cc.meshFaceCount = inMesh.faceCount;
    cc.inputTexture = inTex;  // ★S2c Texture2D gather (Layer2d/DrawScreenQuad)
    cc.inputCommand = haveInCmd ? &inCmd : nullptr;
    cc.ctxVars = ctxVars;  // S3a: SubGraph Command ops read the scoped var off this (resident leg)
    cc.params = nodeParams(path);
    // CAMERA bridge (resident mirror — PRODUCTION runs THIS leg; the S2c flat-resident gate): surface the
    // C1 live Camera onto cc so RotateTowards FORK#2 reads it. IDENTICAL to the flat leg's populate.
    if (const ActiveCamera* lc = liveActiveCamera()) {
      cc.hasCamera = true;
      activeCameraMatrices(*lc, cc.worldToCamera, cc.cameraToWorld);
    }
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

  // Cook a TEXTURE-flow node (RenderTarget / image filter) into its OWN resolution-sized texture (resident
  // mirror of cook()'s cookTexNode). Texture2D gather direct-through (lane I): a filter's Texture2D input
  // recurses to the upstream tex node here; `depth` shares the cook recursion cap (cycle/depth-safe).
  // `outSlotId` = the upstream OUTPUT slot id wired to (single-output ops ignore it; a feedback op returns
  // PreviousFrame vs CurrentFrame by it; empty = the node's terminal/first output). Body extracted VERBATIM
  // to Impl::cookResidentTexNode (point_graph_resident_tex_cook.cpp); this thin lambda keeps the THREE-arg
  // call shape + threads the shared cook-stack state by reference (the SELF slot, cookCommand, cookResident
  // Gradient, the feedbackCooked memo, nodeParams, rg/rc/ctx/reg, kCookDepthCap). Byte-identical.
  cookTexNode =
      [&](const std::string& path, int depth, const std::string& outSlotId) -> MTL::Texture* {
    return p_->cookResidentTexNode(rg, ctx, reg, rc, kCookDepthCap, nodeParams, cookCommand,
                                   cookTexNode, cookResidentGradient, feedbackCooked, path, depth,
                                   outSlotId);
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
