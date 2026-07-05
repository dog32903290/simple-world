// runtime/point_graph_resident_command_cook — cookResidentCommand: the RESIDENT-path cook for the COMMAND
// currency (the RenderCommand draw-chain flow on the PRODUCTION graph — frame_cook drives the resident
// graph, so THIS is the leg the running app renders).
//
// Extracted VERBATIM from the cookCommand lambda that lived inside PointGraph::cookResident
// (point_graph_resident.cpp) — a zero-behaviour-change move that buys ratchet headroom (the resident
// driver was at its line cap), mirroring the flat extraction (point_graph_command_cook.cpp). This is the
// resident half of the DEBT_LEDGER "拆 Command 分支" split.
//
// ★NOT A SHARED PATH with the flat twin. The flat (point_graph_command_cook.cpp) and resident Command
// walkers are near-mirror TWINS in control-flow SHAPE (Execute/Switch/Loop/ExecRepeatedly collectors, the
// S3a SetVar + C1 Camera SubGraph scopes, the per-op cmd dispatch), but the graph-access primitives
// genuinely DIVERGE and are NOT forced into one path:
//   • flat keys on int id + iterates g.connections + pinId/pinNode + Node::strParams;
//   • resident keys on string path + ResidentInput::extraConns + ResidentNode::strInputs, has the S2
//     BYPASS branch + the depth-cap safe-fail (kCookDepthCap), and gathers Mesh into a SwMeshView.
// Forcing a single shared function would require an abstraction layer over graph access = a behaviour-
// shaped change (the S2c blood lesson: the resident leg is production, a forced false-share = prod
// corruption). Only the genuinely-identical INNER logic is shared, and that was ALREADY extracted before
// this split: loopRunIterations / execRepeatedlyRunRepetitions / switchSelectIndex /
// resolveSetRequestedResolution / cmdVarPush/Restore / resolveActiveCamera / the Live*Scope guards.
//
// THE MECHANISM = thin-lambda → Impl-method delegation (same as cookResidentTexNode). The body moves to
// PointGraph::Impl::cookResidentCommand; cookResident's cookCommand std::function slot stays a THIN
// forwarding lambda so the closure web is UNTOUCHED. Shared cook-stack slots ride in by reference:
// cookNode (Points gather) / cookResidentMesh (Mesh gather) / cookTexNode (★S2c Texture2D gather) /
// cookCommand (SELF, the Command→Command + bypass recursion) / ctxVars (S3a scope) / depthCap (the
// recursion cap — the SAME kCookDepthCap cookResident passes cookResidentTexNode). warnCookDepthOnce is
// shared (decl in point_graph_internal.h, def de-anonymised in point_graph_resident.cpp) so the depth-cap
// branch hits the SAME process-global g_warnedCookDepth flag — warn-once preserved across the split.
//
// PLACEMENT: runtime leaf (depends only on the resident graph + the cmd registry + PointGraph::Impl + the
//   already-shared scope helpers — all runtime). Defined as a method on PointGraph::Impl; cookResident
//   wraps it in a forwarding lambda.
#include "runtime/point_graph_internal.h"  // PointGraph::Impl + decl + cmdReg/fillPointCamera/warnCookDepthOnce
#include "runtime/point_ops_logmessage.h"  // logMessageCurrentText (LogMessage Message thread, resident mirror)

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <Metal/Metal.hpp>

#include "runtime/graph.h"            // NodeSpec/PortSpec/findSpec
#include "runtime/render_command.h"  // RenderCommand + loopRunIterations/execRepeatedlyRunRepetitions/switch*
#include "runtime/mesh_op_registry.h"  // SwMeshView (resident Mesh gather element)
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / ResidentNode / ResidentInput
#include "runtime/stateful_value_ops.h"  // ContextVarMap (complete type for cmdVarPush)
#include "runtime/point_ops_camera_scope.h"  // C1: resolveActiveCamera/LiveCameraScope/LiveCtxVarScope/...
#include "runtime/point_ops_setvarcmd.h"  // S3a: cmdVarPush/cmdVarRestore/isCmdContextVarWriter/setVarBugSkipWrite
#include "runtime/point_ops_forwardbeattaps.h"  // ForwardBeatTaps: isForwardBeatTaps/forwardBeatTapsApply
#include "runtime/point_ops_settime.h"  // SetTime: LiveTimeScope/resolveSetTimeScope/isSetTimeScopeWriter
#include "runtime/tixl_point.h"      // EvaluationContext (CmdCookCtx::ctx)

namespace sw {

using pgdetail::cmdReg;
using pgdetail::warnCookDepthOnce;

// PickObject selection helpers (defined in point_ops_pickobject.cpp; render_command.h is at its line cap,
// so the decls ride locally here — same local-extern posture as the flat twin point_graph_command_cook.cpp).
int pickObjectSelectIndex(int rawIndex, int count);
bool& pickObjectIgnoreIndexForTest();

// Cook a resident COMMAND-flow node into a RenderCommand draw-chain. Body extracted VERBATIM from the
// cookResident cookCommand lambda; the captured cook-stack slots ride in by reference (see leaf header).
// `cookCommand` is the SELF slot the Command→Command + bypass recursion calls (lambda → method → slot →
// lambda). `depthCap` = cookResident's file-local kCookDepthCap.
RenderCommand PointGraph::Impl::cookResidentCommand(
    const ResidentEvalGraph& rg, const EvaluationContext& ctx, const SourceRegistry* reg, int depthCap,
    ContextVarMap* ctxVars, const ResidentParamsFn& nodeParams,
    const std::function<MTL::Buffer*(const std::string&, int)>& cookNode,
    const std::function<SwMeshView(const std::string&, int)>& cookResidentMesh,
    const ResidentTexFn& cookTexNode, const ResidentCmdFn& cookCommand, const std::string& path,
    int depth) {
  PointGraph::Impl* p_ = this;
  RenderCommand rcmd;
  if (depth > depthCap) { warnCookDepthOnce(); return rcmd; }  // safe fail: empty chain
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
  std::vector<uint32_t> inCmdWireCounts;  // per-wire item counts of the generic gather (spread seam)
  bool haveInCmd = false;
  bool havePts = false;
  std::vector<CmdCookCtx::CmdCameraRef> camRefs;  // camera-A: wired Object refs (BlendCameras/ActionCamera)
  ActiveCamera refCam;          // ReuseCamera: referenced camera off an "Object" wire (inactive = none)
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
    } else if (port.dataType == "Object") {
      // The TWO camera-reference gathers share the "Object" wire currency (merge unification,
      // camera-A × camera-B; resident mirror — PRODUCTION runs THIS leg):
      // (1) camera-B single-ref (ReuseCamera): the SAME resolveReferencedCamera the flat twin calls
      //     (S2c mirror law); source node off the FIRST Object port's primary wire.
      // (2) camera-A structural multi-ref (BlendCameras/ActionCamera): EVERY wire (primary +
      //     extraConns, wire order) resolves its UPSTREAM node to (opType, resolved params, path)
      //     with ONE nested Object level (ActionCamera.ReferenceCamera;
      //     fork-cameraref-one-level-nesting). CmdCookCtx::cameraRefs doc has the currency contract.
      auto resolveRef = [&](const std::string& upPath) {
        const ResidentNode* up = rg.node(upPath);
        if (!up) return;
        CmdCookCtx::CmdCameraRef ref;
        ref.opType = up->opType;
        ref.params = nodeParams(upPath);
        ref.nodePath = upPath;
        if (const NodeSpec* us = findSpec(up->opType))
          for (const PortSpec& p2 : us->ports) {
            if (!p2.isInput || p2.dataType != "Object") continue;
            const ResidentInput* ri2 = up->input(p2.id);
            if (!ri2 || ri2->driver != ResidentInput::Driver::Connection) continue;
            auto pushNested = [&](const std::string& p) {
              if (const ResidentNode* up2 = rg.node(p)) {
                CmdCookCtx::CmdCameraRef r2;
                r2.opType = up2->opType;
                r2.params = nodeParams(p);
                r2.nodePath = p;
                ref.upstreamRefs.push_back(std::move(r2));
              }
            };
            pushNested(ri2->srcNodePath);
            for (const auto& ec2 : ri2->extraConns) pushNested(ec2.first);
          }
        camRefs.push_back(std::move(ref));
      };
      const ResidentInput* ri = n->input(port.id);
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        if (!refCam.active) {
          const ResidentNode* src = rg.node(ri->srcNodePath);
          if (src)
            resolveReferencedCamera(src->opType, *nodeParams(ri->srcNodePath), ctx.localFxTime, refCam);
        }
        resolveRef(ri->srcNodePath);
        for (const auto& ec : ri->extraConns) resolveRef(ec.first);
      }
    } else if (port.dataType == "Command" && !haveInCmd) {
      // S2a KEYSTONE — resident mirror of the flat MultiInput Command collector (doc: point_ops_execute
      // .cpp). MultiInput Command (Execute) concats the primary wire (srcNodePath) + extraConns (批次25,
      // wire-ordered); single-input (Camera) has empty extraConns. S1: SetRequestedResolution pushes here.
      const RenderResolution savedReq = p_->requestedResolution;
      if (n->opType == "SetRequestedResolution")
        p_->requestedResolution = resolveSetRequestedResolution(*nodeParams(path), savedReq);
      else if (n->opType == "SetRequestedResolutionCmd")  // flow-rail sibling (resident mirror): +StretchResolution
        p_->requestedResolution = resolveSetRequestedResolutionCmd(*nodeParams(path), savedReq);
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
      // ForwardBeatTaps PRE-SUBTREE write (resident mirror — production runs THIS leg; ForwardBeatTaps.cs:22-38):
      // publish the edge-detected beat/resync pulses + slide-sync into the TapProvider BEFORE cooking SubTree.
      if (isForwardBeatTaps(n->opType)) forwardBeatTapsApply(*nodeParams(path));
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
        // SetTime SUBTREE-TIME scope (resident mirror — production runs THIS leg; SetTime.cs:23-43): push
        // the {Absolute/Relative, NewTime} chain node around the SubTree cook. The resident readers are the
        // transient-ec fx clock (evalResidentFloat) AND automation localTime (sampleAutomation). Same
        // -bug gate as the flat twin (a leg-split here would be the S2c anti-pattern).
        LiveTimeScope timeScope((!setTimeBugSkipPush() && isSetTimeScopeWriter(n->opType))
                                    ? resolveSetTimeScope(*nodeParams(path))
                                    : SetTimeScopeSpec{});
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
        } else if (n->opType == "PickObject") {
          // data.object PickObject SUB-SELECT (resident mirror — production runs THIS leg). PickObject.cs:23
          // Index.Mod(count): negatives WRAP; NO -1=none/-2=all sentinels (point_ops_pickobject.cpp). Same
          // §3 wire-order trap as Switch: srcPaths primary-FIRST then extraConns to match the flat order.
          std::vector<std::string> srcPaths;  // wire-declaration order: [primary] + extraConns
          if (ri && ri->driver == ResidentInput::Driver::Connection) {
            srcPaths.push_back(ri->srcNodePath);                                 // wire 0 = primary
            for (const auto& ec : ri->extraConns) srcPaths.push_back(ec.first);  // wires 1..N
          }
          int rawIndex = 0;  // PickObject.Index value param (C# (int) cast = trunc toward 0)
          const std::map<std::string, float>* sp = nodeParams(path);
          if (sp) { auto it = sp->find("Index"); if (it != sp->end()) rawIndex = (int)it->second; }
          if (pickObjectIgnoreIndexForTest()) {
            for (const std::string& spath : srcPaths) {  // -bug: concat ALL (selection lost) — the pick tooth
              RenderCommand sub = cookCommand(spath, depth + 1);
              inCmd.items.insert(inCmd.items.end(), sub.items.begin(), sub.items.end());
            }
          } else {
            const int sel = pickObjectSelectIndex(rawIndex, (int)srcPaths.size());
            if (sel >= 0) {  // no wires → nothing to cook (PickObject.cs:19-20)
              RenderCommand sub = cookCommand(srcPaths[(size_t)sel], depth + 1);  // ONLY the picked wire
              inCmd.items.insert(inCmd.items.end(), sub.items.begin(), sub.items.end());
            }
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
          inCmdWireCounts.push_back((uint32_t)sub.items.size());  // spread seam: per-wire boundary
          if (port.multiInput && !executeCollectFirstOnlyForTest())  // -bug: skip the extra wires
            for (const auto& ec : ri->extraConns) {
              RenderCommand es = cookCommand(ec.first, depth + 1);
              inCmd.items.insert(inCmd.items.end(), es.items.begin(), es.items.end());
              inCmdWireCounts.push_back((uint32_t)es.items.size());  // spread seam (extra wires)
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
  cc.inputCmdWireItemCounts = std::move(inCmdWireCounts);  // spread seam (empty for non-generic gathers)
  cc.ctxVars = ctxVars;  // S3a: SubGraph Command ops read the scoped var off this (resident leg)
  cc.cameraRefs = std::move(camRefs);  // camera-A: wired CameraRef inputs (empty for every other op)
  cc.params = nodeParams(path);
  if (refCam.active) {  // ReuseCamera: surface the referenced camera (IDENTICAL to the flat leg's fill)
    cc.hasRefCamera = true;
    for (int k = 0; k < 3; ++k) {
      cc.refCamEye[k] = refCam.eye[k]; cc.refCamTarget[k] = refCam.target[k]; cc.refCamUp[k] = refCam.up[k];
    }
    cc.refCamFovDeg = refCam.fovDeg; cc.refCamNear = refCam.nearClip; cc.refCamFar = refCam.farClip;
    cc.refCamAspect = refCam.aspect;
  }
  // CAMERA bridge (resident mirror — PRODUCTION runs THIS leg; the S2c flat-resident gate): surface the
  // C1 live Camera onto cc so RotateTowards FORK#2 reads it. IDENTICAL to the flat leg's populate.
  if (const ActiveCamera* lc = liveActiveCamera()) {
    cc.hasCamera = true;
    activeCameraMatrices(*lc, cc.worldToCamera, cc.cameraToWorld);
  }
  // LogMessage Message thread (resident mirror — production runs THIS leg; param-completion fan-out): resolve
  // the node's String "Message" off ResidentNode::strInputs into the op's process-scoped text (LogMessage.cs:26).
  // Mirrors the flat leg byte-for-byte (the S2c flat-resident gate — a resident-only miss = a prod-only miss).
  if (n->opType == "LogMessage" && !logMessageSkipMessageThread()) {
    auto mit = n->strInputs.find("Message");
    logMessageCurrentText() = (mit != n->strInputs.end()) ? mit->second : std::string();
  }
  return cm->second(cc);
}

}  // namespace sw
