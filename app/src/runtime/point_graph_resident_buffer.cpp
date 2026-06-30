// runtime/point_graph_resident_buffer — the RESIDENT "Buffer" cook (Seam-1 = GPU "Buffer" currency = TiXL
// Slot<BufferWithViews>), the PRODUCTION leg (WO-E). The resident TWIN of point_graph_buffer_cook.cpp's
// cookFlatBuffer. Extracted to its own leaf — the SAME extraction pattern as the flat cook + the resident
// mesh/tex/command cooks — so point_graph_resident.cpp stays under its line-count cap (ARCHITECTURE.md rule
// 4 ratchet). cookResident keeps a THIN forwarding lambda; this method takes the shared cook-stack state by
// reference (rg / ctx / rc / nodeParams + the cookResidentBuffer slot it self-recurses through, for a
// Buffer→Buffer consumer like GetBufferComponents).
//
// CONTEXT (the bug this leaf fixes): today the live app cooks the RESIDENT leg. Before WO-E a Buffer terminal
// fell through cookResident's terminal → cookNode → dataType!="Points" → clearTarget = a graceful zero-no-op
// (refuter-confirmed), so buffer ops produced NOTHING in the running app. This walker wires resident Buffer
// cooking so they actually work, byte-identical to the flat leg (the cook_ctx.h both-legs rule).
//
// ★THE PARAM-PATH DIVERGENCE — fork `cookresidentbuffer-vec4-from-resident-params` (the subtlest WO-E risk,
// SEAM1_FANOUT_BUILD_PLAN §WO-E / §最高風險). The flat cookFlatBuffer reads FloatsToBuffer's matrices from
// Node::params["Vec4Params.<m>.<k>"] (a FLAT-TEST-ONLY stand-in — there is NO Vector4[] producer op). The
// resident leg has NO Node::params, and resolveResidentFloatInputs only projects DECLARED Float input ports,
// so a synthetic "Vec4Params.0.k" key NEVER surfaces here. So this leaf sources each port-gather from the
// RESIDENT drivers, NOT Node::params:
//   • Float MultiInput (FloatsToBuffer/IntsToBuffer.Params): gather over the ResidentInput::Connection drivers
//     (primary + extraConns, wire-declaration order) via evalResidentFloat — the SAME pattern the resident
//     host-scalar / PointList walkers use. This is the PRODUCTION path (real wired float connections), and it
//     gathers byte-identical to the flat leg's evalFloat-over-g.connections.
//   • Vec4Params (matrix MultiInput): the VEC4-CURRENCY BRIDGE (fork `cookresidentbuffer-vec4-from-extcolorout`,
//     SEAM1_VEC4_CURRENCY_BUILD_PLAN). A wired matrix/Vector4[] producer (TransformMatrix, …) rides the resident
//     ColorList channel — a 4×4 matrix IS a 4-element Vector4[] (fork-matrix-as-4-vec4-on-extColorOut), settled
//     onto the upstream node's extColorOut by cookMatrixOutputNodes/cookColorListNodes (cook_host_values.cpp,
//     BEFORE pg.cookResident). matrixFromColorOut (below) reads each wired source's 4 rows = one 16-float matrix.
//     This is the PRODUCTION path the 17 TiXL compounds depend on; byte-identical to the flat leg's
//     cookColorListNode gather (selftests_buffer_vec4.cpp's flat==resident gate proves it).
//   • TransformsConstBuffer (WO-D) sources its 3 camera matrices via the RESIDENT camera path (fillBufferCamera
//     below = the resident mirror of the flat fillBufferCamera / pgdetail::fillPointCamera), NOT Vec4Params.
// So the byte-parity holds BY CONSTRUCTION whenever the matrices are absent (the only production case today),
// and the WO-E selftest (selftests_buffer_resident.cpp) drives BOTH legs via real wired connections so they
// gather identically — the flat==resident gate proves it.
#include "runtime/point_graph_internal.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/buffer_op_registry.h"   // BufferCookCtx/BufferCookFn/findBufferOp
#include "runtime/field_camera.h"         // Mat4, mat4Identity, defaultLayerCameraForward (camera bridge)
#include "runtime/graph.h"                // NodeSpec/PortSpec/findSpec
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph/ResidentNode/ResidentInput/evalResidentFloat
#include "runtime/sw_buffer.h"            // SwBuffer (full def)

namespace sw {
namespace {
// CAMERA gather, RESIDENT mirror of point_graph_buffer_cook.cpp's fillBufferCamera (for TransformsConstBuffer,
// WO-D). Identical body — the SOURCE matrices are the DEFAULT camera at the output aspect + IDENTITY
// ObjectToWorld (fork `transformsconstbuffer-camera-from-default`), which does NOT depend on the cook leg (no
// Node::params, no wire) → byte-identical to the flat camera fill. Every other Buffer op ignores cam* →
// byte-identical. The op derives the other 7 matrices + transposes (fork transformsconstbuffer-hlsl-rowmajor-bytes).
void fillBufferCamera(BufferCookCtx& bc, const NodeSpec& spec, float aspect) {
  if (spec.type != "TransformsConstBuffer") return;  // every other Buffer op → byte-identical
  LayerCameraForward fwd = defaultLayerCameraForward(aspect);  // worldToCamera + cameraToClipSpace
  Mat4 objectToWorld = mat4Identity();                          // v1 fork: identity O2W
  std::memcpy(bc.camCameraToClipSpace, fwd.cameraToClipSpace.m, sizeof(float) * 16);
  std::memcpy(bc.camWorldToCamera, fwd.worldToCamera.m, sizeof(float) * 16);
  std::memcpy(bc.camObjectToWorld, objectToWorld.m, sizeof(float) * 16);
  bc.hasCamera = true;
}

// VEC4-CURRENCY BRIDGE helper (fork cookresidentbuffer-vec4-from-extcolorout): read ONE matrix (16 floats,
// .X.Y.Z.W per row) off the upstream resident node's ColorList output. A 4×4 matrix IS a 4-element Vector4[]
// (fork-matrix-as-4-vec4-on-extColorOut); cookMatrixOutputNodes/cookColorListNodes (cook_host_values.cpp,
// BEFORE pg.cookResident) already settled the rows onto extColorOut. Maps srcSlotId → the source spec's
// matching output PORT INDEX (extColorOut's key). Missing source / >4 / <4 rows → clamp + zero-pad (the
// faithful contract is exactly 4 rows = one matrix; TiXL's *4*4 assumes it).
std::array<float, 16> matrixFromColorOut(const ResidentEvalGraph& rg, const std::string& srcPath,
                                         const std::string& srcSlotId) {
  std::array<float, 16> mat{};
  int rn = 0;  // rows actually read (0 across EVERY no-rows path → unresolved gate fires below)
  const ResidentNode* src = rg.node(srcPath);
  const NodeSpec* ss = src ? findSpec(src->opType) : nullptr;
  int outIdx = -1;
  if (ss)
    for (size_t i = 0; i < ss->ports.size(); ++i)
      if (!ss->ports[i].isInput && ss->ports[i].id == srcSlotId) { outIdx = (int)i; break; }
  if (src && outIdx >= 0) {
    auto it = src->extColorOut.find(outIdx);
    if (it != src->extColorOut.end()) {
      const std::vector<simd::float4>& rows = it->second;
      rn = (int)std::min<size_t>(4, rows.size());
      for (int r = 0; r < rn; ++r) {
        mat[r * 4 + 0] = rows[r].x; mat[r * 4 + 1] = rows[r].y;
        mat[r * 4 + 2] = rows[r].z; mat[r * 4 + 3] = rows[r].w;
      }
    }
  }
  // UNRESOLVED-MATRIX-SOURCE GATE: a WIRED Vec4Params source sw can't resolve to rows (absent producer op/
  // output — e.g. TransformMatrix.ResultInverted / GetMatrixVar) → NON-silent (++counter + warn-once) instead
  // of an invisible zero matrix block. SEAM1_VEC4_UNRESOLVED_SOURCE_GATE.md.
  if (rn == 0) noteUnresolvedMatrixSource();
  return mat;
}
}  // namespace

const SwBuffer* PointGraph::Impl::cookResidentBuffer(
    const ResidentEvalGraph& rg, const EvaluationContext& ctx, const ResidentEvalCtx& rc,
    const ResidentParamsFn& nodeParams,
    const std::function<const SwBuffer*(const std::string&, int)>& cookResidentBuffer,
    const std::string& path, int depth) {
  if (depth > 64) { pgdetail::warnCookDepthOnce(); return nullptr; }  // SAME cycle guard as cookResident
  const ResidentNode* n = rg.node(path);
  if (!n) return nullptr;
  const NodeSpec* s = findSpec(n->opType);
  if (!s) return nullptr;
  const BufferCookFn* fn = findBufferOp(n->opType);
  if (!fn || !*fn) return nullptr;

  // Gather inputs in spec port order — the resident MIRROR of cookFlatBuffer's gather loop, driven by the
  // ResidentInput::Connection drivers (the flatten projects them onto Buffer/Float slots, exactly like Points/
  // PointList/FloatList). Buffer inputs self-recurse via cookResidentBuffer; the Float MultiInput payload
  // gathers each wired scalar via evalResidentFloat (primary + extraConns, wire order). Vec4Params → empty
  // (no resident source; see fork in the header).
  std::vector<const SwBuffer*> inputBuffers;
  std::vector<float> floatInputs;
  std::vector<std::array<float, 16>> vec4Inputs;  // stays empty on the resident leg (fork — see header)
  for (const PortSpec& port : s->ports) {
    if (!port.isInput) continue;
    const ResidentInput* ri = n->input(port.id);
    if (port.dataType == "Buffer") {
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        const SwBuffer* up = cookResidentBuffer(ri->srcNodePath, depth + 1);
        if (up) inputBuffers.push_back(up);
        if (port.multiInput) {
          for (const auto& ec : ri->extraConns) {
            const SwBuffer* ue = cookResidentBuffer(ec.first, depth + 1);
            if (ue) inputBuffers.push_back(ue);
          }
        }
      }
    } else if (port.dataType == "Float" && port.multiInput) {
      // FloatsToBuffer/IntsToBuffer.Params: aggregate all WIRED scalar Float sources (wire-declaration order:
      // primary then extraConns). Mirror of the flat evalFloat-over-g.connections gather — both collect only
      // CONNECTED scalars (a Constant on the slot is NOT a collected payload float).
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        floatInputs.push_back(evalResidentFloat(rg, ri->srcNodePath, ri->srcSlotId, rc));
        for (const auto& ec : ri->extraConns)
          floatInputs.push_back(evalResidentFloat(rg, ec.first, ec.second, rc));
      }
    } else if (port.dataType == "Vec4Params") {
      // VEC4-CURRENCY BRIDGE (fork cookresidentbuffer-vec4-from-extcolorout): gather each WIRED matrix off
      // the upstream node's ColorList output (extColorOut, settled by cookMatrixOutputNodes/cookColorListNodes
      // before this cook). Primary + extraConns, wire-declaration order = matrices-first into the buffer. This
      // is the PRODUCTION path the 17 TiXL compounds (TransformMatrix → FloatsToBuffer.Vec4Params) depend on;
      // byte-identical to the flat leg's cookColorListNode gather (both take 4 rows = one matrix per wire).
      const ResidentInput* ri = n->input(port.id);
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        vec4Inputs.push_back(matrixFromColorOut(rg, ri->srcNodePath, ri->srcSlotId));
        for (const auto& ec : ri->extraConns)
          vec4Inputs.push_back(matrixFromColorOut(rg, ec.first, ec.second));
      }
    }
    // (Single scalar Float inputs ride the resolved params map.)
  }

  SwBuffer& out = bufferMeta[path];  // RESIDENT PATH key (NOT flatKey) — the same map the flat leg keys by #id
  out = SwBuffer{};                  // reset the view each cook (the op fills it; default = invalid/empty)
  BufferCookCtx bc;
  bc.dev = dev; bc.lib = lib; bc.queue = queue;
  bc.ctx = &ctx; bc.nodeId = 0;  // resident: no flat node id (resources key by path, not id)
  bc.inputBuffers = &inputBuffers;
  bc.output = &out;
  bc.params = nodeParams(path);  // resolved Float params (value spine; marshal ops read floatInputs, not this)
  bc.floatInputs = &floatInputs;
  bc.vec4Inputs = &vec4Inputs;
  // Camera bridge (TransformsConstBuffer): default camera at the ACTIVE RequestedResolution aspect, the
  // resident mirror of the flat fillBufferCamera call site. Every other Buffer op → byte-identical.
  fillBufferCamera(bc, *s, (requestedResolution.h > 0)
                               ? (float)requestedResolution.w / (float)requestedResolution.h
                               : 1.0f);
  bc.requestBytes = [this, path, &out](uint32_t byteSize) -> void* {
    MTL::Buffer* b = ensureRawBuffer(path, byteSize);  // per-PATH persistent (same rawBuf map, path key)
    out.bytes = b;
    return b ? b->contents() : nullptr;
  };
  (*fn)(bc);
  return &out;
}

}  // namespace sw
