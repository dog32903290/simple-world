// buffer_ops_assemblemeshbuffers — _AssembleMeshBuffers (wrap buffer views back into a MeshBuffers).
//
// TiXL authority: external/tixl/Operators/Lib/mesh/_/_AssembleMeshBuffers.cs (Guid e0849edd…).
//   if (PrepareCommand.HasInputConnections) PrepareCommand.GetValue(context);   // :17-19 (execute, ordering)
//   var vertices = Vertices.GetValue(context);                                  // :26
//   _result.VertexBuffer  = vertices;                                           // :36
//   _result.IndicesBuffer = indices;  _result.ChunkDefsBuffer = chunks;         // :37-38
//   NewMeshBuffers.Value  = _result;                                            // :39
//   Inputs : Vertices(BA53B274) / Indices(892838C5) / ChunkDefs(87116A9A) / PrepareCommand(5E82E351).
//   Output : NewMeshBuffers = Slot<MeshBuffers> (.cs:6-7).
//
// A nested COMPOUND in TiXL, but on sw the MeshBuffers wrapper collapses to ONE MTL::Buffer (fork
// bufferwithviews-collapse-to-mtlbuffer). Through the mesh compute rail (TransformMesh) the load-bearing
// currency is the VERTEX buffer — here it is ExecuteBufferUpdate.Output2 (the transformed vertices) wired
// into the Vertices input. So on sw this is a single-buffer PASSTHROUGH: forward the VERTICES input to
// NewMeshBuffers. NAMED FORK `assemblemeshbuffers-collapse-to-vertex-buffer`: Indices/ChunkDefs are
// carried on the mesh-op rail, not here; PrepareCommand's ordering side-effect is already honoured by the
// buffer cook driver (it recurses/executes wired Command-shaped Buffer inputs BEFORE this op, same as
// ExecuteBufferUpdate's executebufferupdate-command-is-driver-executed). The importer maps e0849edd to
// this atom so ExecuteBufferUpdate.Output2 → Result output imports NATIVELY — see t3_import.cpp TABLE ③.
#include <string>
#include <vector>

#include "runtime/buffer_op_registry.h"  // BufferCookCtx, BufferOp
#include "runtime/graph.h"               // NodeSpec, PortSpec
#include "runtime/sw_buffer.h"           // SwBuffer

namespace sw {
namespace {

void cookAssembleMeshBuffers(BufferCookCtx& c) {
  if (!c.output) return;
  // Forward the VERTICES input specifically (the transformed vertex buffer). Prefer the port-tagged
  // "Vertices" gather; fall back to the first present buffer for a single-wire hookup.
  const SwBuffer* vtx = nullptr;
  if (c.inputBuffers && c.inputBufferPorts &&
      c.inputBuffers->size() == c.inputBufferPorts->size()) {
    for (size_t i = 0; i < c.inputBuffers->size(); ++i)
      if ((*c.inputBufferPorts)[i] == "Vertices" && (*c.inputBuffers)[i]) { vtx = (*c.inputBuffers)[i]; break; }
  }
  if (!vtx && c.inputBuffers && !c.inputBuffers->empty()) vtx = c.inputBuffers->front();
  if (!vtx || !vtx->bytes) return;  // no vertices → null MeshBuffers (.cs:30-33)
  *c.output = *vtx;
}

NodeSpec makeSpec() {
  NodeSpec spec;
  spec.type = "_AssembleMeshBuffers";
  spec.title = "_AssembleMeshBuffers";
  spec.category = "mesh/_";
  spec.ports = {
      {"NewMeshBuffers", "Buffer", "Buffer", false},  // = forwarded vertex buffer (.cs:39)
      {"Vertices", "Buffer", "Buffer", true},         // vertex buffer input (.cs:45-46) — the load-bearing feed
      {"Indices", "Buffer", "Buffer", true},          // secondary (mesh-op rail carries it) — accepted, unused
      {"ChunkDefs", "Buffer", "Buffer", true},        // secondary — accepted, unused
      {"PrepareCommand", "Buffer", "Buffer", true},   // Command→Buffer; ordering honoured by the driver
  };
  spec.evaluate = nullptr;
  return spec;
}

const BufferOp _reg_assemblemeshbuffers(makeSpec(), cookAssembleMeshBuffers);

}  // namespace
}  // namespace sw
