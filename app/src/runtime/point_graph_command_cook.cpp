// runtime/point_graph_command_cook — cookFlatCommand: the FLAT-path cook for the COMMAND currency (the
// RenderCommand draw-chain flow = Execute / Switch / Loop / ExecRepeatedly MultiInput collectors, the
// S3a SetVar + C1 Camera SubGraph scopes, then the per-op cmd dispatch).
//
// Extracted VERBATIM from the cookCommand lambda that lived inside PointGraph::cook (point_graph.cpp) —
// a zero-behaviour-change move that buys ratchet headroom (point_graph.cpp was at its line cap),
// following the Cut-6 mesh pilot, the Cut-4 host-value, and the Cut-5 tex extraction. This is the
// Command-branch split the DEBT_LEDGER "拆 Command 分支" item tracks.
//
// THE MECHANISM = thin-lambda → Impl-method delegation (same as cookFlatTexNode). The body moves to
// PointGraph::Impl::cookFlatCommand; cook()'s original cookCommand std::function slot stays AS a thin
// forwarding lambda so the closure web is UNTOUCHED: the terminal Command dispatch, the Camera-op's
// Command subtree recursion, and cookFlatTexNode's FORK#1 (a tex op's Command input) all keep calling
// through the slot.
//
// THE COUPLING — the Command flow crosses into every other flat flow. The method takes the shared
// cook-stack state by reference:
//  • cookNode — a Command op's Points input (DrawPoints) is gathered by cooking the upstream Points node.
//  • cookMeshInto — DrawMeshUnlit's Mesh input borrows the upstream mesh node's vtx/idx buffers.
//  • cookTexNode — FORK#1: a Command op (DrawScreenQuad) can take a Texture2D input → recurse the tex flow.
//  • cookCommand (SELF) — the Command→Command recursion (Camera wraps Command; Execute/Switch/Loop/
//    ExecRepeatedly concat or re-cook their wired Command subtrees). Threaded as a by-ref slot so there is
//    ONE closure source (lambda → method → slot → lambda), mirroring cookFlatTexNode's self threading.
//  • cmdVisiting — the per-cook Command-stack cycle guard (Camera-wraps-Command must not hang). By-ref.
//  • ctxVars — the host per-frame context-var map the S3a SetVar scope pushes/restores around the SubGraph.
//
// The flat and resident Command walkers are near-mirror TWINS in control-flow SHAPE, but they are NOT
// merged into one shared path: the graph-access primitives genuinely diverge (flat = int id + g.connections
// + pinId/pinNode + Node::strParams; resident = string path + ResidentInput::extraConns + ResidentNode::
// strInputs + the bypass branch + the depth-cap). Forcing them into one path would require an abstraction
// over graph access = a behaviour-shaped change, so the resident twin lives in its own TU
// (point_graph_resident_command_cook.cpp). Only the genuinely-identical inner logic is shared, and that
// was ALREADY extracted before this split: loopRunIterations / execRepeatedlyRunRepetitions /
// switchSelectIndex / resolveSetRequestedResolution / cmdVarPush/Restore / resolveActiveCamera / the
// Live*Scope guards (render_command.h, point_ops_setvarcmd.h, point_ops_camera_scope.h).
//
// PLACEMENT: runtime leaf (depends only on the flat Graph + the cmd registry + PointGraph::Impl + the
//   already-shared scope helpers — all runtime). Defined as a method on PointGraph::Impl; cook() wraps it
//   in a forwarding lambda.
#include "runtime/point_graph_internal.h"  // PointGraph::Impl + decl + cmdReg/fillPointCamera

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <Metal/Metal.hpp>

#include "runtime/graph.h"            // Graph/Node/NodeSpec/PortSpec/Connection/pinId/pinNode/findSpec
#include "runtime/render_command.h"  // RenderCommand + loopRunIterations/execRepeatedlyRunRepetitions/switch*
#include "runtime/stateful_value_ops.h"  // ContextVarMap (complete type for cmdVarPush)
#include "runtime/point_ops_camera_scope.h"  // C1: resolveActiveCamera/LiveCameraScope/LiveCtxVarScope/...
#include "runtime/point_ops_setvarcmd.h"  // S3a: cmdVarPush/cmdVarRestore/isCmdContextVarWriter/setVarBugSkipWrite
#include "runtime/tixl_point.h"      // EvaluationContext (CmdCookCtx::ctx)

namespace sw {

using pgdetail::cmdReg;
using pgdetail::flatKey;

namespace {
// Verbatim copy of the cook()-local mapParam helper (anon-namespace in point_graph.cpp): a NULL-safe
// param-map lookup with default. Same logic, file-local — the Command walker uses it for Switch.Index /
// Loop.Count / ExecRepeatedly.RepeatCount.
float mapParam(const std::map<std::string, float>* m, const char* id, float def) {
  if (!m) return def;
  auto it = m->find(id);
  return it != m->end() ? it->second : def;
}
}  // namespace

// Cook a COMMAND-flow node into a RenderCommand draw-chain. Body extracted VERBATIM from the cookCommand
// lambda; the captured cook()-stack slots ride in by reference (see leaf header). `cookCommand` is the SELF
// slot the Command→Command recursion calls (lambda → method → slot → lambda).
RenderCommand PointGraph::Impl::cookFlatCommand(
    const Graph& g, const EvaluationContext& ctx, const SourceRegistry* reg,
    const NodeParamsFn& nodeParams, ContextVarMap* ctxVars, std::set<int>& cmdVisiting,
    const std::function<MTL::Buffer*(int)>& cookNode,
    const std::function<bool(int, const MTL::Buffer*&, uint32_t&, const MTL::Buffer*&, uint32_t&)>&
        cookMeshInto,
    const std::function<MTL::Texture*(int, int)>& cookTexNode,
    const std::function<RenderCommand(int)>& cookCommand, int id) {
  PointGraph::Impl* p_ = this;
  RenderCommand rc;
  const Node* n = g.node(id);
  const NodeSpec* s = n ? findSpec(n->type) : nullptr;
  if (!n || !s) return rc;
  auto cm = cmdReg().find(n->type);
  if (cm == cmdReg().end() || !cm->second) return rc;
  if (!cmdVisiting.insert(id).second) return rc;  // already on the command stack → break the cycle
  MTL::Buffer* pts = nullptr;
  uint32_t cnt = 0;
  const MTL::Texture* inTex = nullptr;
  const MTL::Buffer* inMeshVtx = nullptr;  // first wired Mesh input (DrawMeshUnlit, Cut 99)
  const MTL::Buffer* inMeshIdx = nullptr;
  uint32_t inMeshFaces = 0;
  bool haveMesh = false;
  RenderCommand inCmd;          // Camera op's Command subtree (Cut 3); empty unless a Command input wired
  bool haveInCmd = false;
  bool havePts = false;
  for (size_t i = 0; i < s->ports.size(); ++i) {
    const PortSpec& port = s->ports[i];
    if (!port.isInput) continue;
    if (port.dataType == "Points" && !havePts) {
      const Connection* c = g.connectionToInput(pinId(id, (int)i));
      if (c) { pts = cookNode(pinNode(c->fromPin)); cnt = p_->outCount[flatKey(pinNode(c->fromPin))]; }
      havePts = true;
    } else if (port.dataType == "Mesh" && !haveMesh) {
      // DrawMeshUnlit's Mesh input: cook the upstream mesh generator (NGonMesh/QuadMesh) and borrow
      // its vertex+index buffers + face count (mirrors the Texture2D gather; single-frame lifetime).
      const Connection* c = g.connectionToInput(pinId(id, (int)i));
      if (c) {
        uint32_t vtxCount = 0;  // unused here (the VS draws faces×3, not vertices)
        cookMeshInto(pinNode(c->fromPin), inMeshVtx, vtxCount, inMeshIdx, inMeshFaces);
      }
      haveMesh = true;
    } else if (port.dataType == "Texture2D" && !inTex) {
      // FORK#1: a command op (DrawScreenQuad) can take a Texture2D input — recurse into the
      // upstream tex node (same cook-upstream-on-demand as Points). Borrowed, single-frame.
      const Connection* c = g.connectionToInput(pinId(id, (int)i));
      if (c) inTex = cookTexNode(pinNode(c->fromPin), (c->fromPin - 1) % 100);
    } else if (port.dataType == "Command" && !haveInCmd) {
      // S2a KEYSTONE — MultiInput Command collector (TiXL Execute.cs; full doc in point_ops_execute.cpp):
      // concat N wired chains in wire order. S1: SetRequestedResolution pushes resolution (save/restore).
      const RenderResolution savedReq = p_->requestedResolution;
      if (n->type == "SetRequestedResolution")
        p_->requestedResolution = resolveSetRequestedResolution(*nodeParams(id), savedReq);
      // S3a context-var scope (TiXL SetFloatVar.cs:26-45 SubGraph branch): push the scoped var into
      // ctxVars BEFORE cooking the subtree, restore AFTER (same shape as the savedReq guard). varName off
      // Node::strParams. Inactive no-op when ctxVars null / not a writer / -bug skips the write.
      CmdVarScope varScope;
      if (!setVarBugSkipWrite() && isCmdContextVarWriter(n->type)) {
        std::string varName;
        auto vit = n->strParams.find("VariableName");
        if (vit != n->strParams.end()) varName = vit->second;
        varScope = cmdVarPush(n->type, *nodeParams(id), varName, ctxVars);
      }
      // C1 ACTIVE-CAMERA scope (CAMERA3D_BLUEPRINT §1, mirror of the S3a var scope above): a Camera op sets
      // the thread_local active camera around its SubGraph cook so the POINT ops inside read the wired Camera
      // (fillPointCamera), not the default. Resolve the same v1 params cookCamera stamps onto items. -bug
      // (cameraScopeBugSkipPush) leaves it inactive → the point rail falls back to default → C1 golden RED.
      ActiveCamera activeCam;
      if (!cameraScopeBugSkipPush() && isCameraScopeWriter(n->type))
        activeCam = resolveActiveCamera(*nodeParams(id));
      {
        // S3b LIVE-READ scope: make ctxVars the ambient live map WHILE cooking the SubGraph, so a value-rail
        // GetFloatVar driving a param of a node inside the SubGraph re-resolves LIVE (closes S3a's hollow).
        // Engages only when varScope is active (a real writer push happened); else no-op (leaves outer scope).
        LiveCtxVarScope liveScope(varScope.active ? ctxVars : nullptr);
        LiveCameraScope liveCam(activeCam);  // C1: active camera live for the SubGraph cook (point rail reads it)
        // S3b Switch SUB-SELECT (TiXL flow/Switch.cs): gather wired sources in WIRE ORDER, then cook ONLY
        // the index-th (wrap/negative-safe; -2=all, -1/empty=none). Execute concats ALL, Switch sub-selects;
        // non-Switch ops keep the verbatim concat-all (single-input/-bug collapse via the break) below.
        if (n->type == "Switch") {
          std::vector<int> srcIds;  // wired Command sources, wire-declaration order (== Switch.cs CollectedInputs)
          for (const Connection& c : g.connections) {
            if (c.toPin != pinId(id, (int)i)) continue;
            srcIds.push_back(pinNode(c.fromPin));
          }
          const int rawIndex = (int)mapParam(nodeParams(id), "Index", 0.0f);  // C# (int) cast = trunc-toward-0
          // -bug: switchIgnoreIndexForTest() forces the cook-all path (selection lost) — the §3 resident tooth.
          int sel = switchIgnoreIndexForTest() ? kSwitchSelectAll
                                               : switchSelectIndex(rawIndex, (int)srcIds.size());
          if (sel == kSwitchSelectAll) {
            for (int sid : srcIds) {  // -2: cook ALL (== Execute), wire order
              RenderCommand sub = cookCommand(sid);
              inCmd.items.insert(inCmd.items.end(), sub.items.begin(), sub.items.end());
            }
          } else if (sel != kSwitchSelectNone) {  // -1/empty: cook NOTHING (inCmd stays empty)
            RenderCommand sub = cookCommand(srcIds[(size_t)sel]);  // cook ONLY the selected wire
            inCmd.items.insert(inCmd.items.end(), sub.items.begin(), sub.items.end());
          }
        } else if (n->type == "Loop") {
          // S3c Loop RE-COOK (TiXL flow/Loop.cs): cook the single wired SubGraph `Count` times, each iter
          // writing index/progress into ctxVars (loopRunIterations owns the for-loop + live scope + concat).
          int subId = -1;
          for (const Connection& c : g.connections)
            if (c.toPin == pinId(id, (int)i)) { subId = pinNode(c.fromPin); break; }  // single-input source
          const int count = (int)mapParam(nodeParams(id), "Count", 0.0f);
          std::string iVar, pVar;
          if (auto it = n->strParams.find("IndexVariable"); it != n->strParams.end()) iVar = it->second;
          if (auto it = n->strParams.find("ProgressVariable"); it != n->strParams.end()) pVar = it->second;
          loopRunIterations(count, iVar, pVar, ctxVars, inCmd,
                            [&]() { return subId >= 0 ? cookCommand(subId) : RenderCommand{}; });
        } else if (n->type == "ExecRepeatedly") {
          // S3c ExecRepeatedly RE-COOK (TiXL flow/ExecRepeatedly.cs): cook the MultiInput wires `RepeatCount`
          // (clamped [0,100]) times, concatenating each repetition (no var injection — the Loop sibling).
          // execRepeatedlyRunRepetitions owns the repeat loop + concat. SkipFrameCount=0 path (.t3 default).
          std::vector<int> srcIds;  // wired Command sources, wire-declaration order
          for (const Connection& c : g.connections)
            if (c.toPin == pinId(id, (int)i)) srcIds.push_back(pinNode(c.fromPin));
          int rep = (int)mapParam(nodeParams(id), "RepeatCount", 1.0f);
          rep = rep < 0 ? 0 : (rep > 100 ? 100 : rep);  // ExecRepeatedly.cs:24 Clamp(0,100)
          execRepeatedlyRunRepetitions(rep, inCmd, [&]() {
            RenderCommand all;
            for (int sid : srcIds) {  // cook ALL wires fresh, wire order (one repetition)
              RenderCommand sub = cookCommand(sid);
              all.items.insert(all.items.end(), sub.items.begin(), sub.items.end());
            }
            return all;
          });
        } else {
          for (const Connection& c : g.connections) {  // g.connections = wire order (ListToBuffer :202)
            if (c.toPin != pinId(id, (int)i)) continue;
            RenderCommand sub = cookCommand(pinNode(c.fromPin));
            inCmd.items.insert(inCmd.items.end(), sub.items.begin(), sub.items.end());
            if (!port.multiInput || executeCollectFirstOnlyForTest()) break;  // single-input / -bug collapse
          }
        }
      }
      cmdVarRestore(varScope, ctxVars);    // S3a restore (SetFloatVar.cs:33-40)
      p_->requestedResolution = savedReq;  // restore (SetRequestedResolution.cs:28)
      haveInCmd = true;
    }
  }
  CmdCookCtx cc;
  cc.ctx = &ctx; cc.graph = &g; cc.reg = reg;
  cc.nodeId = id; cc.points = pts; cc.count = cnt;
  cc.inputTexture = inTex;
  cc.inputCommand = haveInCmd ? &inCmd : nullptr;
  cc.ctxVars = ctxVars;  // S3a: a Command op cooked in a SubGraph reads the scoped var off this
  cc.meshVtx = inMeshVtx; cc.meshIdx = inMeshIdx; cc.meshFaceCount = inMeshFaces;
  cc.params = nodeParams(id);
  // CAMERA bridge: surface the C1 live Camera (set around an enclosing Camera's SubGraph cook, so a
  // Command op cooked inside it sees this) onto cc → RotateTowards FORK#2 reads it. MIRRORED on resident.
  if (const ActiveCamera* lc = liveActiveCamera()) {
    cc.hasCamera = true;
    activeCameraMatrices(*lc, cc.worldToCamera, cc.cameraToWorld);
  }
  RenderCommand out = cm->second(cc);
  cmdVisiting.erase(id);  // pop: this node is no longer on the command stack
  return out;
}

}  // namespace sw
