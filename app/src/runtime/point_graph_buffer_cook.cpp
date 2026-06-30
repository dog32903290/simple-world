// runtime/point_graph_buffer_cook — the FLAT "Buffer" cook (Seam-1 = GPU "Buffer" currency = TiXL
// Slot<BufferWithViews>). Extracted from point_graph.cpp (the cook driver) to keep that file under its
// line-count cap (ARCHITECTURE.md rule 4 ratchet), the SAME extraction pattern as point_graph_*_cook.cpp
// (the host-value / mesh / string / tex cooks). point_graph.cpp keeps a THIN forwarding lambda; this
// method takes the minimal shared cook-stack state by reference (g / ctx / nodeParams + the cookBufferNode
// slot it self-recurses through).
//
// The currency is a SwBuffer — a GPU StorageModeShared MTL::Buffer (driver-owned, Impl::rawBuf) + its
// element stride/count. UNLIKE the host-value flows, the marshal ops produce a REAL GPU buffer (memcpy
// host floats in), so the driver owns the allocation via the requestBytes callback (Impl::ensureRawBuffer),
// exactly as it owns a Points op's output. A Buffer op has NO cross-frame state, so there is no per-frame
// memo (FloatsToBuffer is a pure function of its inputs; a passthrough is idempotent).
//
// The gather mirrors the host-value walkers (recurse upstream Buffer inputs in spec port order, MultiInput-
// expanded into wire-declaration order) + the marshal PAYLOAD gather:
//   • a scalar "Float" MultiInput port (FloatsToBuffer.Params, FloatsToBuffer.cs:81-82) → all wired scalars
//     via evalFloat, wire-declaration order, into floatInputs;
//   • a "Vec4Params" port (FloatsToBuffer.Vec4Params = MultiInputSlot<Vector4[]>, .cs:78-79) → each matrix
//     as a flat 16-float array into vec4Inputs. VEC4-CURRENCY BRIDGE (SEAM1_VEC4_CURRENCY_BUILD_PLAN): a
//     wired matrix/Vector4[] producer (TransformMatrix, …) rides sw's ColorList channel — a 4×4 matrix IS a
//     4-element Vector4[] (fork-matrix-as-4-vec4-on-extColorOut) — so each WIRE into Vec4Params is cooked via
//     cookColorListNode and its first 4 rows become one 16-float matrix. FALLBACK (no wire) = the host-fed
//     stand-in `floatstobuffer-vec4-from-nodeparams`: matrices from Node::params "Vec4Params.<m>.<i>"
//     (m = matrix index 0.., i = component 0..15), kept for the host-fed flat golden. The FILL ORDER
//     (matrices first, then floats) is faithful either way.
#include "runtime/point_graph_internal.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <simd/simd.h>

#include "runtime/buffer_op_registry.h"  // BufferCookCtx/BufferCookFn/findBufferOp
#include "runtime/field_camera.h"        // Mat4, mat4Identity, defaultLayerCameraForward (camera bridge)
#include "runtime/graph.h"               // Graph/Node/NodeSpec/PortSpec/Connection/pinId/pinNode/findSpec/evalFloat
#include "runtime/sw_buffer.h"           // SwBuffer (full def)

namespace sw {

using pgdetail::flatKey;

namespace {
// CAMERA gather (camera-matrix-into-buffer seam, for TransformsConstBuffer) — mirror of
// pgdetail::fillPointCamera (point_graph_internal.h:87-97). FORK `transformsconstbuffer-camera-from-
// default`: fill the 3 SOURCE matrices (CameraToClipSpace / WorldToCamera / ObjectToWorld) from the
// DEFAULT camera at the output aspect + IDENTITY ObjectToWorld (the same v1 fork as pointCameraMatrices;
// a wired Camera op is a later seam). Only TransformsConstBuffer reads them; every other Buffer op
// ignores cam* → byte-identical. The op itself derives the other 7 matrices + transposes.
void fillBufferCamera(BufferCookCtx& bc, const NodeSpec& spec, float aspect) {
  if (spec.type != "TransformsConstBuffer") return;  // every other Buffer op → byte-identical
  LayerCameraForward fwd = defaultLayerCameraForward(aspect);  // worldToCamera + cameraToClipSpace
  Mat4 objectToWorld = mat4Identity();                          // v1 fork: identity O2W
  std::memcpy(bc.camCameraToClipSpace, fwd.cameraToClipSpace.m, sizeof(float) * 16);
  std::memcpy(bc.camWorldToCamera, fwd.worldToCamera.m, sizeof(float) * 16);
  std::memcpy(bc.camObjectToWorld, objectToWorld.m, sizeof(float) * 16);
  bc.hasCamera = true;
}
}  // namespace

const SwBuffer* PointGraph::Impl::cookFlatBuffer(
    const Graph& g, const EvaluationContext& ctx, const NodeParamsFn& nodeParams,
    const std::function<const SwBuffer*(int)>& cookBufferNode,
    const std::function<const std::vector<simd::float4>*(int)>& cookColorListNode, int id) {
  const Node* n = g.node(id);
  if (!n) return nullptr;
  const NodeSpec* s = findSpec(n->type);
  if (!s) return nullptr;
  const BufferCookFn* fn = findBufferOp(n->type);
  if (!fn || !*fn) return nullptr;

  std::vector<const SwBuffer*> inputBuffers;
  std::vector<float> floatInputs;
  std::vector<std::array<float, 16>> vec4Inputs;
  for (size_t i = 0; i < s->ports.size(); ++i) {
    const PortSpec& port = s->ports[i];
    if (!port.isInput) continue;
    const int inPin = pinId(id, (int)i);
    if (port.dataType == "Buffer") {
      for (const Connection& c : g.connections) {
        if (c.toPin != inPin) continue;
        const SwBuffer* up = cookBufferNode(pinNode(c.fromPin));
        if (up) inputBuffers.push_back(up);
        if (!port.multiInput) break;  // single-input: first wire only
      }
    } else if (port.dataType == "Float" && port.multiInput) {
      // FloatsToBuffer.Params: aggregate all wired scalar Float sources (wire-declaration order).
      for (const Connection& c : g.connections)
        if (c.toPin == inPin) floatInputs.push_back(evalFloat(g, c.fromPin, ctx));
    } else if (port.dataType == "Vec4Params") {
      // VEC4-CURRENCY BRIDGE (fork cookresidentbuffer-vec4-from-extcolorout's flat twin): a wired
      // matrix/Vector4[] producer (TransformMatrix, …) rides sw's ColorList channel — a 4×4 matrix IS a
      // 4-element Vector4[] (fork-matrix-as-4-vec4-on-extColorOut). For EACH wire into this Vec4Params pin,
      // cook the upstream ColorList and take its first 4 rows as ONE 16-float matrix (.X.Y.Z.W per row);
      // matrices-first into the buffer (FloatsToBuffer.cs:38-48). One wire = one matrix (TiXL's *4*4 assumes
      // a 4-row entry); >4 rows clamp, <4 zero-pad.
      bool gotWired = false;
      for (const Connection& c : g.connections) {
        if (c.toPin != inPin) continue;
        std::array<float, 16> mat{};
        const std::vector<simd::float4>* rows =
            cookColorListNode ? cookColorListNode(pinNode(c.fromPin)) : nullptr;
        const int rn = rows ? (int)std::min<size_t>(4, rows->size()) : 0;
        for (int r = 0; r < rn; ++r) {
          mat[r * 4 + 0] = (*rows)[r].x; mat[r * 4 + 1] = (*rows)[r].y;
          mat[r * 4 + 2] = (*rows)[r].z; mat[r * 4 + 3] = (*rows)[r].w;
        }
        // UNRESOLVED-MATRIX-SOURCE GATE (mirror of the resident matrixFromColorOut check): a WIRED source
        // sw can't resolve to rows → NON-silent (++counter + warn-once), not an invisible zero matrix block.
        if (rn == 0) noteUnresolvedMatrixSource();
        vec4Inputs.push_back(mat);
        gotWired = true;
      }
      // FALLBACK — host-fed matrix stand-in (fork floatstobuffer-vec4-from-nodeparams), ONLY when no wire
      // is present: read consecutive matrices from Node::params "Vec4Params.<m>.<i>" until one is wholly
      // absent. 16 floats each; a missing component defaults 0. Keeps the host-fed flat golden valid.
      if (!gotWired) {
        for (int m = 0;; ++m) {
          bool any = false;
          std::array<float, 16> mat{};
          for (int k = 0; k < 16; ++k) {
            std::string key = "Vec4Params." + std::to_string(m) + "." + std::to_string(k);
            auto it = n->params.find(key);
            if (it != n->params.end()) { mat[k] = it->second; any = true; }
          }
          if (!any) break;
          vec4Inputs.push_back(mat);
        }
      }
    }
  }

  SwBuffer& out = bufferMeta[flatKey(id)];
  out = SwBuffer{};  // reset the view each cook (the op fills it; default = invalid/empty)
  BufferCookCtx bc;
  bc.dev = dev; bc.lib = lib; bc.queue = queue;
  bc.ctx = &ctx; bc.nodeId = id;
  bc.inputBuffers = &inputBuffers;
  bc.output = &out;
  bc.params = nodeParams(id);
  bc.floatInputs = &floatInputs;
  bc.vec4Inputs = &vec4Inputs;
  // Camera bridge (TransformsConstBuffer): default camera at the ACTIVE RequestedResolution aspect,
  // mirroring point_graph.cpp:364-366's fillPointCamera call site. Every other Buffer op → byte-identical.
  fillBufferCamera(bc, *s, (requestedResolution.h > 0)
                               ? (float)requestedResolution.w / (float)requestedResolution.h
                               : 1.0f);
  const std::string key = flatKey(id);
  bc.requestBytes = [this, key, &out](uint32_t byteSize) -> void* {
    MTL::Buffer* b = ensureRawBuffer(key, byteSize);
    out.bytes = b;
    return b ? b->contents() : nullptr;
  };
  (*fn)(bc);
  return &out;
}

}  // namespace sw
