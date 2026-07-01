// buffer_ops_meshbuffercomponents — _MeshBufferComponents (unwrap a MeshBuffers into its buffer views).
//
// TiXL authority: external/tixl/Operators/Lib/mesh/_/_MeshBufferComponents.cs (Guid 5b9f1d97…).
//   var mesh = MeshBuffers.GetValue(context);
//   Vertices.Value = mesh.VertexBuffer;   // :52  (BufferWithViews)
//   Indices.Value  = mesh.IndicesBuffer;  // :53
//   ChunkDefs.Value= mesh.ChunkDefsBuffer;// :54
//   Input : MeshBuffers = InputSlot<MeshBuffers> (.cs:61-62).
//
// This is a nested COMPOUND in TiXL (a two-op structural passthrough), but on sw the MeshBuffers wrapper,
// its VertexBuffer, and the DX11 BufferWithViews all COLLAPSE to ONE MTL::Buffer (fork
// bufferwithviews-collapse-to-mtlbuffer, sw_buffer.h) — the mesh-family compute rail (TransformMesh) only
// ever moves the VERTEX buffer through this seam. So on sw it is a pure single-buffer PASSTHROUGH: forward
// the input SwBuffer to every output view. NAMED FORK `meshbuffercomponents-collapse-to-vertex-buffer`:
// the Indices / ChunkDefs outputs ALIAS the same forwarded vertex SwBuffer (sw carries no separate
// Indices/ChunkDefs currency through the buffer rail; the mesh-op rail owns the SwTriIndex pair). This
// mirrors GetBufferComponents' SRV/UAV alias-the-buffer fork. The importer maps 5b9f1d97 to this atom so
// the Mesh boundary → vertex feed imports NATIVELY (no scaffold rewire) — see t3_import.cpp TABLE ③.
#include <vector>

#include "runtime/buffer_op_registry.h"  // BufferCookCtx, BufferOp
#include "runtime/graph.h"               // NodeSpec, PortSpec
#include "runtime/sw_buffer.h"           // SwBuffer

namespace sw {
namespace {

void cookMeshBufferComponents(BufferCookCtx& c) {
  if (!c.output) return;
  // The single wired MeshBuffers input (the vertex SwBuffer on sw's collapsed currency).
  const SwBuffer* in =
      (c.inputBuffers && !c.inputBuffers->empty()) ? c.inputBuffers->front() : nullptr;
  if (!in || !in->bytes) return;  // absent input → output stays default-invalid (mesh==null branch)
  *c.output = *in;  // forward the vertex buffer to every output view (fork: Indices/ChunkDefs alias it)
}

NodeSpec makeSpec() {
  NodeSpec spec;
  spec.type = "_MeshBufferComponents";
  spec.title = "_MeshBufferComponents";
  spec.category = "mesh/_";
  spec.ports = {
      {"MeshBuffers", "Buffer", "Buffer", true},  // input (.cs:61-62) — port index 0 (single Buffer input)
      {"Vertices", "Buffer", "Buffer", false},    // = forwarded buffer (.cs:52)
      {"Indices", "Buffer", "Buffer", false},     // = forwarded buffer, aliased (.cs:53) — fork
      {"ChunkDefs", "Buffer", "Buffer", false},   // = forwarded buffer, aliased (.cs:54) — fork
  };
  spec.evaluate = nullptr;
  return spec;
}

const BufferOp _reg_meshbuffercomponents(makeSpec(), cookMeshBufferComponents);

}  // namespace
}  // namespace sw
