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
//     as a flat 16-float array into vec4Inputs. NAMED FORK `floatstobuffer-vec4-from-nodeparams`: there is
//     NO Vector4[] producer op in sw yet, so the spike reads matrices from Node::params under the
//     convention "Vec4Params.<m>.<i>" (m = matrix index 0.., i = component 0..15). The FILL ORDER (matrices
//     first, then floats) is faithful; when a Vector4[] producer lands, this becomes a cookBufferNode-style
//     recursion (Buffer→Vector4-producer gather).
#include "runtime/point_graph_internal.h"

#include <array>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/buffer_op_registry.h"  // BufferCookCtx/BufferCookFn/findBufferOp
#include "runtime/graph.h"               // Graph/Node/NodeSpec/PortSpec/Connection/pinId/pinNode/findSpec/evalFloat
#include "runtime/sw_buffer.h"           // SwBuffer (full def)

namespace sw {

using pgdetail::flatKey;

const SwBuffer* PointGraph::Impl::cookFlatBuffer(
    const Graph& g, const EvaluationContext& ctx, const NodeParamsFn& nodeParams,
    const std::function<const SwBuffer*(int)>& cookBufferNode, int id) {
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
      // Host-fed matrix stand-in (fork floatstobuffer-vec4-from-nodeparams): read consecutive matrices
      // from Node::params "Vec4Params.<m>.<i>" until a matrix is wholly absent. 16 floats (.X.Y.Z.W per
      // row) each; a missing component defaults 0.
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
