// runtime/buffer_op_registry — self-registration seam for the "Buffer" cook flow (the Seam-1 currency).
//
// The Buffer channel is a GPU-RESIDENT raw-bytes currency — TiXL's Slot<BufferWithViews> (a DX11
// StructuredBuffer + its Srv/Uav views). UNLIKE the host-side PointList/FloatList/String flows (which
// ride a self-sizing std::vector in host memory), a Buffer op produces a REAL MTL::Buffer: the marshal
// ops (FloatsToBuffer/IntsToBuffer) memcpy host param data INTO a StorageModeShared GPU buffer, exactly
// as TiXL fills a dynamic StructuredBuffer (FloatsToBuffer.cs:62-70 GetDynamicConstantBuffer +
// WriteDynamicBufferData). So this flow's ctx mirrors the GPU Points PointCookCtx (the driver owns the
// allocation) rather than the host-vector FloatListCookCtx.
//
// Pattern: cloned from pointlist_op_registry.h (the 7th cook flow) — same two-sink self-registration
// (spec sink for the Add menu / findSpec + cook-fn sink for the driver's cookBufferNode dispatch). The
// difference is the currency: a SwBuffer (GPU MTL::Buffer + stride/count) instead of a host SwPoint
// vector. The driver (point_graph.cpp cookBufferNode) gathers upstream Buffer inputs by recursion,
// allocates THIS node's output buffer (Impl::rawBuf, via the requestBytes callback below), and dispatches.
//
// NAMED FORK `bufferwithviews-collapse-to-mtlbuffer` (defined in sw_buffer.h): every Buffer op operates
//   on a SwBuffer (one MTL::Buffer), never the DX11 Buffer/Srv/Uav triple.
//
// Init-order safety (identical to the pointlist / floatlist / value-op sinks): every registrar is a
// namespace-scope static, so all finish their dynamic-init constructors before main and before any LIVE
// sink read (node_registry's findSpec/specTypes read the sink live, never snapshot).
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "runtime/graph.h"        // NodeSpec

namespace MTL {
class Device;
class Library;
class CommandQueue;
class Buffer;
class Texture;
}  // namespace MTL

struct EvaluationContext;  // runtime/eval_context.h
struct RenderCommand;      // runtime/render_command.h (ExecuteBufferUpdate's Command input)

namespace sw {

struct SwBuffer;  // runtime/sw_buffer.h (full def); incomplete here is fine (only ptr/ref members below)

// Everything a Buffer op gets to cook one node this frame. Mirrors PointCookCtx (a GPU-resident flow)
// but the currency is a SwBuffer:
//   inputBuffers : the already-cooked upstream Buffer inputs (spec port order, MultiInput-expanded into
//                  wire-declaration order). A pure producer (FloatsToBuffer) has none → empty. A
//                  passthrough/consumer (GetBufferComponents/ExecuteBufferUpdate) reads inputBuffers->at(i).
//   inputTexture : a wired Texture2D input (SrvFromTexture2d, a later op) — null for FloatsToBuffer etc.
//   inputCommand : a wired Command input already executed by the driver (ExecuteBufferUpdate.UpdateCommand,
//                  ExecuteBufferUpdate.cs:25). Borrowed; presence means the driver already cooked it.
//   output       : THIS node's SwBuffer. The op fills it: for a PRODUCER it calls requestBytes() to get a
//                  driver-owned MTL::Buffer of the right size, memcpys into it, and sets stride/count; a
//                  PASSTHROUGH copies an inputBuffer into *output. Driver-owned `bytes` (Impl::rawBuf).
//   requestBytes : driver callback — ensureRawBuffer(byteSize) → returns the StorageModeShared MTL::Buffer
//                  (sized ≥ byteSize, reused across frames) and writes it into output->bytes. Returns
//                  contents() so the op can memcpy host data in. null byteSize-0 path → a 1-byte buffer
//                  (the "empty" convention, mirror of ensureOut's never-alloc-zero).
//   params       : RESOLVED Float params of THIS node (same value spine as PointCookCtx::params); but the
//                  marshal ops read their float payload from the FLOAT-LIST inputs, not params (see leaf).
//   floatInputs  : the wired scalar Float inputs of THIS node gathered in wire-declaration order (the
//                  marshal payload: FloatsToBuffer.Params = MultiInputSlot<float>, .cs:81-82). Borrowed.
//   vec4Inputs   : the wired Vector4[] (matrix) inputs of THIS node — each entry is a flat 16-float
//                  matrix (FloatsToBuffer.Vec4Params = MultiInputSlot<Vector4[]>, .cs:78-79). Borrowed.
struct BufferCookCtx {
  MTL::Device* dev = nullptr;
  MTL::Library* lib = nullptr;
  MTL::CommandQueue* queue = nullptr;
  const EvaluationContext* ctx = nullptr;  // time / frameIndex / deltaTime
  int nodeId = 0;
  const std::vector<const SwBuffer*>* inputBuffers = nullptr;  // cooked upstream Buffer inputs (borrowed)
  // PARALLEL to inputBuffers: the spec PORT ID each gathered SwBuffer arrived on (same index/order).
  // Lets a multi-Buffer-port op (ComputeShaderStage: ConstantBuffers / ShaderResources / Uavs) tell
  // which wired buffer is a CB vs SRV vs UAV — the flat inputBuffers vector alone loses that grouping.
  // Empty/absent for single-Buffer-input ops (they read inputBuffers->front() unchanged). Both cook
  // legs (flat + resident) fill it alongside inputBuffers.
  const std::vector<std::string>* inputBufferPorts = nullptr;
  const MTL::Texture* inputTexture = nullptr;                  // wired Texture2D input (SrvFromTexture2d)
  const RenderCommand* inputCommand = nullptr;                 // wired+executed Command (ExecuteBufferUpdate)
  SwBuffer* output = nullptr;                                  // THIS node's output (driver-owned bytes)
  // Driver allocation callback: size THIS node's output buffer to byteSize, return its contents() ptr,
  // and set output->bytes. Reuses across frames (Impl::ensureRawBuffer). The op then memcpys + sets
  // output->elementStride / output->elementCount.
  std::function<void*(uint32_t byteSize)> requestBytes;
  const std::map<std::string, float>* params = nullptr;       // resolved Float params (value spine)
  // Resolved STRING params of THIS node (slot id -> text), the string twin of `params`. Populated by
  // both cook legs from the node's string overrides else the spec's PortSpec.strDef. Used by ops that
  // name a resource by string (ComputeShaderStage's KernelName). Absent/empty for float-only ops.
  const std::map<std::string, std::string>* strParams = nullptr;
  const std::vector<float>* floatInputs = nullptr;            // wired scalar Float payload (marshal)
  const std::vector<std::array<float, 16>>* vec4Inputs = nullptr;  // wired Vector4[] matrix payload
  // CAMERA matrices (camera-matrix-into-buffer seam, for TransformsConstBuffer): the driver fills the 3
  // SOURCE matrices TiXL's TransformBufferLayout ctor takes (TransformsConstBuffer.cs:58-60
  // context.CameraToClipSpace / WorldToCamera / ObjectToWorld). Filled by fillBufferCamera (mirror of
  // fillPointCamera) from the DEFAULT camera + IDENTITY ObjectToWorld at the output aspect — fork
  // `transformsconstbuffer-camera-from-default`. ROW-MAJOR float[16] (m[r*4+c], the field_camera Mat4
  // convention; the op transposes them itself per fork transformsconstbuffer-hlsl-rowmajor-bytes).
  // hasCamera=false → no Camera-wanting op present, fields stay identity/zero → every other Buffer op is
  // byte-identical (they ignore these).
  bool hasCamera = false;
  float camCameraToClipSpace[16] = {0};
  float camWorldToCamera[16] = {0};
  float camObjectToWorld[16] = {0};
};

// A Buffer op: read inputBuffers/floatInputs/vec4Inputs → fill *output (via requestBytes for a producer,
// or copy an input for a passthrough). ONE fn.
using BufferCookFn = void (*)(BufferCookCtx&);

// Read a Float param from a BufferCookCtx's RESOLVED map (mirror of pointListParam); `def` when no map.
float bufferParam(const std::map<std::string, float>* params, const char* id, float def);
// Read a String param from a BufferCookCtx's RESOLVED string map; `def` when no map / no key.
std::string bufferStrParam(const std::map<std::string, std::string>* strs, const char* id,
                           const std::string& def);

// --- the two sinks every Buffer-op leaf registrar feeds ---
std::vector<NodeSpec>& bufferSpecSink();                  // NodeSpecs (node_registry reads live)
std::map<std::string, BufferCookFn>& bufferCookFns();     // type-name -> cook fn

// Lookup the cook fn for a type (nullptr if not a Buffer op). Used by the cook driver's dispatch.
const BufferCookFn* findBufferOp(const std::string& type);

// Test-only injection seam (goldens): when set, a Buffer op's cook corrupts its REAL output (drops the
// last float of FloatsToBuffer's payload) so the golden's RED case fires on the actual cook path (NOT by
// flipping the expected value). Off in production. A leaf reads it at the end of its cook.
bool& bufferInjectBug();

// UNRESOLVED-MATRIX-SOURCE GATE (SEAM1_VEC4_UNRESOLVED_SOURCE_GATE.md). Vec4Params can be WIRED to a source
// sw cannot resolve to matrix rows (a producer op/output sw has not ported — e.g. TransformMatrix.ResultInverted
// / GetMatrixVar). The gather then silently emits a ZERO matrix block (count is right, bytes are zero → points
// collapse to origin, byte-parity can't catch it). `noteUnresolvedMatrixSource()` makes that NON-silent: it
// bumps a test-visible counter AND warns once. Both cook legs call it when a Vec4Params wire yields no rows.
// The counter is the regression GATE: a selftest asserts it fires; remove the detection → counter 0 → RED.
uint32_t& bufferUnresolvedMatrixSources();  // test-visible; reset to 0 before a cook like bufferInjectBug()
void noteUnresolvedMatrixSource();          // ++counter + warn-once (called by both gather legs)

// RAII registrar: declare one file-scope static of this type at the end of each Buffer-op leaf.
//   BufferOp(spec, cookFn);  // pushes spec into bufferSpecSink() and cook into bufferCookFns()
struct BufferOp {
  BufferOp(NodeSpec spec, BufferCookFn cook);
};

}  // namespace sw
