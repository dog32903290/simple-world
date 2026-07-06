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
#include "runtime/point_ops_logmessage.h"  // logMessageCurrentText (LogMessage Message thread)

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
#include "runtime/point_ops_forwardbeattaps.h"  // ForwardBeatTaps: isForwardBeatTaps/forwardBeatTapsApply
#include "runtime/point_ops_settime.h"  // SetTime: LiveTimeScope/resolveSetTimeScope/isSetTimeScopeWriter
#include "runtime/point_ops_timeclip.h"  // TimeClip: window gate + remap (LiveTimeRemapScope) + flat test seam
#include "runtime/point_ops_sliceviewport.h"  // SliceViewPort: resolveSliceViewPortResolution (cell push)
#include "runtime/sw_pbr_scope.h"    // PBR: SetMaterial/SetPointLight/SetFog/UseMaterial/DefineMaterials scope
#include "runtime/tixl_point.h"      // EvaluationContext (CmdCookCtx::ctx)

namespace sw {

using pgdetail::cmdReg;
using pgdetail::flatKey;

// PickObject selection helpers (defined in point_ops_pickobject.cpp; render_command.h is at its line cap,
// so the decls ride locally here — the registerSwitchOp/point_ops_register_draw.cpp local-extern precedent).
int pickObjectSelectIndex(int rawIndex, int count);
bool& pickObjectIgnoreIndexForTest();

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
  std::vector<uint32_t> inCmdWireCounts;  // per-wire item counts of the generic gather (spread seam)
  bool haveInCmd = false;
  bool havePts = false;
  std::vector<CmdCookCtx::CmdCameraRef> camRefs;  // camera-A: wired Object refs (BlendCameras/ActionCamera)
  ActiveCamera refCam;          // ReuseCamera: referenced camera off an "Object" wire (inactive = none)
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
    } else if (port.dataType == "Object") {
      // The TWO camera-reference gathers share the "Object" wire currency (merge unification,
      // camera-A × camera-B — the port dataType = TiXL's Slot<Object>):
      // (1) camera-B single-ref (ReuseCamera): resolve the FIRST Object port's wired source into raw
      //     camera params through the shared resolveReferencedCamera (both legs; ReuseCamera.cs:17-29).
      if (!refCam.active) {
        const Connection* c2 = g.connectionToInput(pinId(id, (int)i));
        if (c2) {
          const Node* src = g.node(pinNode(c2->fromPin));
          if (src)
            resolveReferencedCamera(src->type, *nodeParams(pinNode(c2->fromPin)), ctx.localFxTime, refCam);
        }
      }
      // (2) camera-A structural multi-ref (BlendCameras/ActionCamera, MIRRORED on resident): EVERY
      //     wire into this Object port resolves its UPSTREAM node to (opType, resolved params,
      //     identity) in wire order (= TiXL GetCollectedTypedInputs; CmdCookCtx::cameraRefs doc).
      //     ONE nested level: the upstream op's own Object inputs (ActionCamera.ReferenceCamera)
      //     resolve into upstreamRefs (fork-cameraref-one-level-nesting).
      for (const Connection& c : g.connections) {
        if (c.toPin != pinId(id, (int)i)) continue;
        const int upId = pinNode(c.fromPin);
        const Node* up = g.node(upId);
        if (!up) continue;
        CmdCookCtx::CmdCameraRef ref;
        ref.opType = up->type;
        ref.params = nodeParams(upId);
        ref.nodePath = std::to_string(upId);
        if (const NodeSpec* us = findSpec(up->type))
          for (size_t j = 0; j < us->ports.size(); ++j) {
            const PortSpec& p2 = us->ports[j];
            if (!p2.isInput || p2.dataType != "Object") continue;
            for (const Connection& c2 : g.connections) {
              if (c2.toPin != pinId(upId, (int)j)) continue;
              const int up2Id = pinNode(c2.fromPin);
              const Node* up2 = g.node(up2Id);
              if (!up2) continue;
              CmdCookCtx::CmdCameraRef r2;
              r2.opType = up2->type;
              r2.params = nodeParams(up2Id);
              r2.nodePath = std::to_string(up2Id);
              ref.upstreamRefs.push_back(std::move(r2));
              if (!p2.multiInput) break;
            }
          }
        camRefs.push_back(std::move(ref));
        if (!port.multiInput) break;
      }
    } else if (port.dataType == "Command" && !haveInCmd) {
      // S2a KEYSTONE — MultiInput Command collector (TiXL Execute.cs; full doc in point_ops_execute.cpp):
      // concat N wired chains in wire order. S1: SetRequestedResolution pushes resolution (save/restore).
      const RenderResolution savedReq = p_->requestedResolution;
      if (n->type == "SetRequestedResolution")
        p_->requestedResolution = resolveSetRequestedResolution(*nodeParams(id), savedReq);
      else if (n->type == "SetRequestedResolutionCmd")  // flow-rail sibling: same scope, +StretchResolution factor
        p_->requestedResolution = resolveSetRequestedResolutionCmd(*nodeParams(id), savedReq);
      else if (n->type == "SliceViewPort")  // render/transform: push the CELL size (cs:103) around the subtree
        p_->requestedResolution = resolveSliceViewPortResolution(*nodeParams(id), savedReq);
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
      // ForwardBeatTaps PRE-SUBTREE write (ForwardBeatTaps.cs:22-38): publish the edge-detected beat/resync
      // pulses + slide-sync into the process-global TapProvider BEFORE cooking the SubTree — same pre-subtree
      // side-effect shape as cmdVarPush above. No restore (a per-frame publish, not a scoped push).
      if (isForwardBeatTaps(n->type)) forwardBeatTapsApply(*nodeParams(id));
      // C1 ACTIVE-CAMERA scope (CAMERA3D_BLUEPRINT §1, mirror of the S3a var scope above): a Camera op sets
      // the thread_local active camera around its SubGraph cook so the POINT ops inside read the wired Camera
      // (fillPointCamera), not the default. Resolve the same v1 params cookCamera stamps onto items. -bug
      // (cameraScopeBugSkipPush) leaves it inactive → the point rail falls back to default → C1 golden RED.
      ActiveCamera activeCam;
      if (!cameraScopeBugSkipPush() && isCameraScopeWriter(n->type))
        activeCam = resolveActiveCamera(*nodeParams(id));
      // PBR CONTEXT-STACK scope (mirror of the C1 camera scope): SetMaterial/UseMaterial push the active
      // material, SetPointLight pushes a light, SetFog pushes fog — around the SubGraph cook, so a DrawMeshPbr
      // inside reads the live scope. -bug (materialScopeBugSkipPush/…) leaves it unset → default → golden RED.
      ActiveMaterial pbrMat;
      if (!materialScopeBugSkipPush() && isMaterialScopeWriter(n->type)) {
        std::string matName;  // SetMaterial.MaterialId / UseMaterial.MaterialReference (String channel)
        auto sit = n->strParams.find(n->type == "UseMaterial" ? "MaterialReference" : "MaterialId");
        if (sit != n->strParams.end()) matName = sit->second;
        if (n->type == "UseMaterial") {
          if (!lookupDefinedMaterial(matName, pbrMat)) pbrMat.active = false;  // invalid ref → leave prior (UseMaterial.cs:19)
        } else {
          pbrMat = resolveActiveMaterial(*nodeParams(id), matName);
        }
      }
      SwPointLight pbrLight;
      bool pbrLightActive = !pointLightScopeBugSkipPush() && isPointLightScopeWriter(n->type);
      if (pbrLightActive) pbrLight = resolvePointLightFromParams(*nodeParams(id));
      ActiveFog pbrFog;
      if (!fogScopeBugSkipPush() && isFogScopeWriter(n->type)) pbrFog = resolveActiveFog(*nodeParams(id));
      // DefineMaterials: register its Materials MultiInput into the by-name library for the SubGraph (v1:
      // a SetMaterial wired into Materials surfaces its resolved material — but sw has no PbrMaterial slot
      // currency, so DefineMaterials registers the material NAME it carries via strParams "Define<i>", a
      // named fork; the UseMaterial reference resolves against it). The single-material common case rides
      // the SetMaterial scope directly; DefineMaterials' library is the multi-define surface.
      ActiveMaterial pbrDefine;
      if (n->type == "DefineMaterials") {
        auto sit = n->strParams.find("MaterialId");
        if (sit != n->strParams.end()) { pbrDefine = resolveActiveMaterial(*nodeParams(id), sit->second); }
      }
      {
        // S3b LIVE-READ scope: make ctxVars the ambient live map WHILE cooking the SubGraph, so a value-rail
        // GetFloatVar driving a param of a node inside the SubGraph re-resolves LIVE (closes S3a's hollow).
        // Engages only when varScope is active (a real writer push happened); else no-op (leaves outer scope).
        LiveCtxVarScope liveScope(varScope.active ? ctxVars : nullptr);
        LiveCameraScope liveCam(activeCam);  // C1: active camera live for the SubGraph cook (point rail reads it)
        // PBR scopes live for the SubGraph cook so a DrawMeshPbr inside reads them (mirror of liveCam above).
        LiveMaterialScope livePbrMat(pbrMat);
        LivePointLightScope livePbrLight(pbrLight, pbrLightActive);
        LiveFogScope livePbrFog(pbrFog);
        MaterialLibraryScope livePbrDefine(pbrDefine, pbrDefine.active);  // DefineMaterials by-name library
        // SetTime SUBTREE-TIME scope (SetTime.cs:23-43; point_ops_settime.h — the S3b shape): push the
        // {Absolute/Relative, NewTime} chain node around the SubTree cook so the value-rail fx clock (and
        // resident automation localTime) read the SCOPED time. -bug (setTimeBugSkipPush) leaves it inactive.
        LiveTimeScope timeScope((!setTimeBugSkipPush() && isSetTimeScopeWriter(n->type))
                                    ? resolveSetTimeScope(*nodeParams(id))
                                    : SetTimeScopeSpec{});
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
        } else if (n->type == "PickObject") {
          // data.object PickObject SUB-SELECT (PickObject.cs:23 Index.Mod(count) — negatives WRAP; NO
          // Switch-style -1=none/-2=all sentinels. Selection math + doc: point_ops_pickobject.cpp).
          std::vector<int> srcIds;  // wired Command sources, wire order (== CollectedTypedInputs)
          for (const Connection& c : g.connections) {
            if (c.toPin != pinId(id, (int)i)) continue;
            srcIds.push_back(pinNode(c.fromPin));
          }
          const int rawIndex = (int)mapParam(nodeParams(id), "Index", 0.0f);  // C# (int) trunc-toward-0
          if (pickObjectIgnoreIndexForTest()) {
            for (int sid : srcIds) {  // -bug: concat ALL (selection lost) — the pick tooth
              RenderCommand sub = cookCommand(sid);
              inCmd.items.insert(inCmd.items.end(), sub.items.begin(), sub.items.end());
            }
          } else {
            const int sel = pickObjectSelectIndex(rawIndex, (int)srcIds.size());
            if (sel >= 0) {  // no wires → nothing to cook (PickObject.cs:19-20)
              RenderCommand sub = cookCommand(srcIds[(size_t)sel]);  // cook ONLY the picked wire
              inCmd.items.insert(inCmd.items.end(), sub.items.begin(), sub.items.end());
            }
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
        } else if (n->type == "TimeClip" && !timeClipBugSkipScope() && flatTimeClipTestScope().active) {
          // TimeClip WINDOW GATE + REMAP (flat test leg; TimeClipSlot.cs:52-83). The flat Node carries no
          // authored clip, so the window comes from the flat test seam (flatTimeClipTestScope, set by the
          // golden). Gate on the AMBIENT fx clock (composed through any enclosing scope) — out-of-window →
          // cook NOTHING (the SubTree contributes no items). In-window → push the remap scope so the value-
          // rail fx clock the SubTree reads is remapped, and publish _normalizedTime for a scoped GetFloatVar.
          const TimeClipScopeSpec& clip = flatTimeClipTestScope();
          const float ambient = scopedTimeOr(ctx.localFxTime);  // fork-timeclip-flat-fxclock-gate
          if (timeClipInWindow(clip, ambient)) {
            TimeRemapScopeSpec rm; rm.active = true;
            rm.inMin = clip.timeStart; rm.inMax = clip.timeEnd;
            rm.outMin = clip.sourceStart; rm.outMax = clip.sourceEnd;
            LiveTimeRemapScope remap(rm);
            timeClipPublishNormalized(clip, ambient, ctxVars);  // TimeClip.cs:19-22 _normalizedTime var
            LiveCtxVarScope ntLive(ctxVars ? ctxVars : nullptr);
            for (const Connection& c : g.connections) {
              if (c.toPin != pinId(id, (int)i)) continue;
              RenderCommand sub = cookCommand(pinNode(c.fromPin));
              inCmd.items.insert(inCmd.items.end(), sub.items.begin(), sub.items.end());
              inCmdWireCounts.push_back((uint32_t)sub.items.size());
            }
          }  // out-of-window: inCmd stays empty (== TimeClipSlot returns before _baseUpdateAction)
        } else {
          for (const Connection& c : g.connections) {  // g.connections = wire order (ListToBuffer :202)
            if (c.toPin != pinId(id, (int)i)) continue;
            RenderCommand sub = cookCommand(pinNode(c.fromPin));
            inCmd.items.insert(inCmd.items.end(), sub.items.begin(), sub.items.end());
            inCmdWireCounts.push_back((uint32_t)sub.items.size());  // spread seam: per-wire boundary
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
  cc.inputCmdWireItemCounts = std::move(inCmdWireCounts);  // spread seam (empty for non-generic gathers)
  cc.ctxVars = ctxVars;  // S3a: a Command op cooked in a SubGraph reads the scoped var off this
  cc.meshVtx = inMeshVtx; cc.meshIdx = inMeshIdx; cc.meshFaceCount = inMeshFaces;
  cc.cameraRefs = std::move(camRefs);  // camera-A: wired CameraRef inputs (empty for every other op)
  cc.params = nodeParams(id);
  if (refCam.active) {  // ReuseCamera: surface the referenced camera's raw params (mirrored on resident)
    cc.hasRefCamera = true;
    for (int k = 0; k < 3; ++k) {
      cc.refCamEye[k] = refCam.eye[k]; cc.refCamTarget[k] = refCam.target[k]; cc.refCamUp[k] = refCam.up[k];
    }
    cc.refCamFovDeg = refCam.fovDeg; cc.refCamNear = refCam.nearClip; cc.refCamFar = refCam.farClip;
    cc.refCamAspect = refCam.aspect;
  }
  // CAMERA bridge: surface the C1 live Camera (set around an enclosing Camera's SubGraph cook, so a
  // Command op cooked inside it sees this) onto cc → RotateTowards FORK#2 reads it. MIRRORED on resident.
  if (const ActiveCamera* lc = liveActiveCamera()) {
    cc.hasCamera = true;
    activeCameraMatrices(*lc, cc.worldToCamera, cc.cameraToWorld);
  }
  // PBR bridge: surface the live SetMaterial/SetPointLight/SetFog scope onto cc so a DrawMeshPbr reads it.
  // Reads the SAME thread_local getters the resident leg reads (single source → the S2c mirror can't fork).
  fillCmdPbrFromScope(cc.pbrHasMaterial, cc.pbrMaterial, cc.pbrHasFog, cc.pbrFog, cc.pbrLightCount, cc.pbrLights);
  // LogMessage Message thread (param-completion fan-out): resolve the node's String "Message" param into the
  // process-scoped text the op emits (LogMessage.cs:26 Message.GetValue) — same shape as VariableName above.
  // Closes the previously-deferred prod string-thread wire on the flat leg (resident mirrors it).
  if (n->type == "LogMessage" && !logMessageSkipMessageThread()) {
    auto mit = n->strParams.find("Message");
    logMessageCurrentText() = (mit != n->strParams.end()) ? mit->second : std::string();
  }
  RenderCommand out = cm->second(cc);
  cmdVisiting.erase(id);  // pop: this node is no longer on the command stack
  return out;
}

}  // namespace sw
