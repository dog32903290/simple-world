// runtime/point_graph_internal — PRIVATE seam between point_graph.cpp (flat cook) and
// point_graph_resident.cpp (resident cook). Only those two TUs include it; shares PointGraph::Impl + the op
// registries. Resource keys are STRINGS (slice 2b): the resident path-qualified id ("5/2") is the frame-
// stable key; the flat cook prefixes its int node id ("#7") so the two key spaces never collide.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <simd/simd.h>            // simd::float4 (colorListBuf element type)

#include "runtime/graph.h"        // PortSpec (isBufferInput) + NodeSpec (fillPointCamera)
#include "runtime/field_camera.h" // pointCameraMatrices (camera-matrix-into-points seam)
#include "runtime/point_ops_camera_scope.h"  // C1: liveActiveCamera / activeCameraMatrices (Camera-op leg)
#include "runtime/point_graph.h"  // PointGraph + op fn types
#include "runtime/sw_mesh.h"      // SwVertex (80B) + SwTriIndex (12B) — the Mesh flow's elements
#include "runtime/mesh_op_registry.h"  // SwMeshView (cookResidentMesh return type)
#include "runtime/string_op_registry.h"  // StringState (flat stringState cross-frame store)
#include "runtime/floatlist_op_registry.h"  // FloatListState (floatListState store)
#include "runtime/sw_gradient.h"  // SwGradient — the 8th flow's host value (gradientBuf)
#include "runtime/sw_buffer.h"    // SwBuffer — the Seam-1 GPU "Buffer" currency (bufferMeta stores by value)
#include "runtime/tixl_point.h"   // SwPoint (64B) — output buffers are SwPoint bags

namespace sw {

// Resident-graph types (full defs in resident_eval_graph.h; forward-declared here so Impl can declare
// cookResidentMesh without internal.h pulling the whole resident header in).
struct ResidentEvalGraph;
struct ResidentEvalCtx;

// The flat cook's per-node resolved-Float-param memo (point_graph.cpp cook()-local nodeParams). Passed by-ref
// to the extracted flat-mesh-cook methods so they share the same single-resolve-per-node memo.
using NodeParamsFn = std::function<const std::map<std::string, float>*(int)>;
// Cook-stack slot aliases (flat = int id, resident = path string); pure type aliases, no behaviour.
using FlatNodeFn = std::function<MTL::Buffer*(int)>;
using FlatCmdFn = std::function<RenderCommand(int)>;
using FlatTexFn = std::function<MTL::Texture*(int, int)>;
using ResidentParamsFn = std::function<const std::map<std::string, float>*(const std::string&)>;
using ResidentCmdFn = std::function<RenderCommand(const std::string&, int)>;
using ResidentTexFn = std::function<MTL::Texture*(const std::string&, int, const std::string&)>;

namespace pgdetail {

// --- Operator registries (Metal-side; separate from NodeSpec.evaluate float path) ---
struct OpReg {
  PointCookFn cook = nullptr;
  PointStateNewFn stateNew = nullptr;
  PointStateFreeFn stateFree = nullptr;
  // Optional: map the natural count (sum of Points inputs / Count param) to the count the node's output +
  // state are sized to. ParticleSystem grows a particle POOL larger than its emit ring (particle_params.h:
  // pool > emit lets the cycle buffer recycle). null = identity. Both cooks, right before ensureOut/State.
  PointCountFn countTransform = nullptr;
  // Count policy for ops with >1 Points input. Default false = output count is the SUM of all wired Points
  // inputs (CombineBuffers concatenates). true = FIRST Points input's count only (the op transforms Points1
  // using the rest as references — SnapToPoints: out = Points1, Points2 is a snap target). Both cooks.
  bool countFromFirstPointsInput = false;
  // Count policy for the mesh-into-points fork (MeshVerticesToPoints): output count is the gathered Mesh
  // input's VERTEX count (one Point per vertex). Default false = byte-identical (count from Count param /
  // Points inputs). Applied in BOTH cooks after the Mesh gather, before ensureOut.
  bool countFromMeshVtx = false;
};
std::map<std::string, OpReg>& cookReg();
std::map<std::string, PointDrawFn>& drawReg();
std::map<std::string, PointCmdFn>& cmdReg();
std::map<std::string, PointTexFn>& texReg();

inline bool isBufferInput(const PortSpec& p) {
  return p.isInput && (p.dataType == "Points" || p.dataType == "ParticleForce");
}

// Flat cook's resource key for node id (see header comment on key spaces).
inline std::string flatKey(int id) { return "#" + std::to_string(id); }

// CAMERA gather (camera-matrix-into-points seam): a "Camera" marker INPUT port → fill cc.objectToCamera/
// ...World. SHARED by both cooks. No port → byte-identical. C1 (CAMERA3D_BLUEPRINT §1): a wired Camera op
// sets a thread_local ACTIVE camera (point_ops_camera.cpp LiveCameraScope) around the SubGraph cook → build
// from THAT camera (the draws respect it too); else the DEFAULT at `aspect`. Closes the default-vs-pushed
// point-rail black-hole (S2c/S3); the flat↔resident mirror lives at the scope-SET site (both Cmd branches).
inline void fillPointCamera(PointCookCtx& cc, const NodeSpec& spec, float aspect) {
  bool wantsCamera = false;
  for (const PortSpec& port : spec.ports)
    if (port.isInput && port.dataType == "Camera") { wantsCamera = true; break; }
  if (!wantsCamera) return;  // every existing Points op → byte-identical
  if (const ActiveCamera* live = liveActiveCamera())
    activeCameraMatrices(*live, cc.objectToCamera, cc.cameraToWorld);  // wired Camera in scope
  else
    pointCameraMatrices(aspect, cc.objectToCamera, cc.cameraToWorld);  // default (off-scope = byte-identical)
  cc.hasCamera = true;
}

// Multi-tex-output helper (feedback seam): ABSOLUTE output port idx → ordinal among Texture2D outputs in
// point_graph.cpp (0 for single-output ops); both cooks share it.
int texOutputOrdinal(const NodeSpec& spec, int absPortIndex);

// Resident cook-depth safe-fail warn (one stderr line per process; defined in point_graph_resident.cpp).
// Shared so the extracted resident-command TU calls the SAME single g_warnedCookDepth flag — the depth-cap
// branch's verbatim behaviour (warn-once + safe empty) is preserved across the split.
void warnCookDepthOnce();

}  // namespace pgdetail

constexpr MTL::PixelFormat kPointTargetFormat = MTL::PixelFormatRGBA8Unorm;

struct PointGraph::Impl {
  MTL::Device* dev = nullptr;
  MTL::Library* lib = nullptr;
  MTL::CommandQueue* queue = nullptr;
  MTL::Texture* target = nullptr;
  uint32_t width = 0, height = 0;

  // S1 RES SEAM: requestedResolution = per-cook stack-top (CPU; doc point_ops_setrequestedresolution.cpp).
  // frameResOverride = FRAME OVERRIDE (export>selector>Fill; doc _debug.cpp; unset == Fill == today byte-id).
  RenderResolution requestedResolution;
  std::optional<RenderResolution> frameResOverride;

  // Per-node persistent resources (reused across frames; RESOURCE_LIFETIME golden: allocate → reuse (count
  // unchanged) → reallocate (count grew)). Keyed by resident path or "#"-prefixed flat id (pgdetail::flatKey).
  std::map<std::string, MTL::Buffer*> outBuf;    // key -> output point buffer
  std::map<std::string, uint32_t> outCap;        // key -> allocated capacity (points)
  std::map<std::string, uint32_t> outCount;      // key -> last cooked count (points)
  std::map<std::string, void*> state;            // key -> stateful-op memory
  std::map<std::string, PointStateFreeFn> stateFree;
  std::map<std::string, uint32_t> stateCap;      // key -> count the state was created for

  // Per-node RenderTarget textures (Texture2D stream resources; realloc on resolution change —
  // RESOURCE_LIFETIME). displayTex = the texture target() shows this frame: a tex terminal's own
  // resolution-sized texture, or null -> fall back to the window-sized `target`.
  std::map<std::string, MTL::Texture*> texBuf;
  std::map<std::string, uint32_t> texW, texH;
  std::map<std::string, bool> texMipped;  // realloc key: a false->true change MUST reallocate
  std::map<std::string, uint32_t> texFmt;  // OWN-TEXTURE realloc key (ensureOwnedTex): format change reallocs
  MTL::Texture* displayTex = nullptr;

  // Per-node MESH output (the 4th cook flow = TiXL MeshBuffers). A mesh node owns a PAIR of buffers —
  // vertices (SwVertex 80B) + indices (SwTriIndex 12B) — that travel together. Same count-change-reuse
  // lifetime as outBuf (reuse when counts unchanged; reallocate when a count grows). Keyed by flat id /
  // resident path (parallel to outBuf).
  std::map<std::string, MTL::Buffer*> meshVtxBuf;   // key -> vertex buffer
  std::map<std::string, MTL::Buffer*> meshIdxBuf;   // key -> index buffer
  std::map<std::string, uint32_t> meshVtxCap;       // key -> allocated vertex capacity
  std::map<std::string, uint32_t> meshIdxCap;       // key -> allocated index capacity
  std::map<std::string, uint32_t> meshVtxCount;     // key -> last cooked vertex count
  std::map<std::string, uint32_t> meshIdxCount;     // key -> last cooked index (face) count

  // HOST-side value-channel outputs (the non-GPU cook flows): each rides between same-typed ports as a
  // self-sizing host container — NO Metal allocation, NO pre-sizing (the op clears + fills it). All keyed by
  // flat id or resident path (parallel to outBuf/meshVtxBuf). Full per-flow doc in the per-flow cook leaves.
  std::map<std::string, std::vector<float>> floatListBuf;          // key -> host float list
  std::map<std::string, std::vector<simd::float4>> colorListBuf;   // key -> host color (float4) list
  std::map<std::string, std::string> stringBuf;                    // key -> host string
  std::map<std::string, std::vector<std::string>> stringListBuf;   // key -> host string list
  std::map<std::string, std::vector<SwPoint>> pointListBuf;        // key -> host SwPoint list
  std::map<std::string, SwGradient> gradientBuf;                   // key -> host gradient

  // GPU-resident "Buffer" currency (Seam-1, TiXL BufferWithViews → one MTL::Buffer). UNLIKE the host
  // value flows above, a Buffer op produces a REAL StorageModeShared MTL::Buffer (the marshal ops memcpy
  // host floats into it). Same allocate→reuse→reallocate-on-grow lifetime as outBuf (rawCap = allocated
  // bytes; reuse while rawCap >= byteSize, realloc when it grows). bufferMeta = the SwBuffer VIEW each
  // node produced last cook (stride/count + a borrowed ptr into rawBuf), keyed flatKey(id)/resident path.
  std::map<std::string, MTL::Buffer*> rawBuf;     // key -> raw byte buffer (driver-owned)
  std::map<std::string, uint32_t> rawCap;         // key -> allocated capacity (BYTES)
  std::map<std::string, SwBuffer> bufferMeta;     // key -> cooked SwBuffer view (stride/count + bytes ptr)

  // CROSS-FRAME value state (the only host maps that PERSIST between flat cooks; keyed flatKey(id); resident
  // keeps the SAME per resident path). colorListState = KeepColors's `_list`; stringState = HasStringChanged's
  // `_lastString`; floatListState = AmplifyValues's _averaged/_last/_output. A stateless op never touches its
  // map; all value-typed → cleaned up with the map (no ~ release).
  std::map<std::string, std::vector<simd::float4>> colorListState;  // key -> persistent accumulator
  std::map<std::string, StringState> stringState;                  // key -> persistent string state
  std::map<std::string, FloatListState> floatListState;            // key -> persistent FloatList state

  // Per-node CROSS-FRAME texture PAIR (the feedback / ping-pong flow = TiXL KeepPreviousFrame). A feedback
  // op owns TWO textures (texA+texB) that PERSIST across frames + a toggle bit (flips each frame) so it hands
  // back the PREVIOUS frame while writing the current into the other buffer. Realloc-keyed like ensureOwnedTex
  // (w/h/fmt → release the OLD pair; both released in ~PointGraph → NO leak/UAF). Keyed flat id / resident
  // path. feedbackToggle defaults false per node (TiXL `_bufferToggle`, KeepPreviousFrame.cs:80).
  struct FeedbackPair { MTL::Texture* a = nullptr; MTL::Texture* b = nullptr; };
  std::map<std::string, FeedbackPair> feedbackTexBuf;   // key -> {texA, texB}
  std::map<std::string, uint32_t> feedbackW, feedbackH; // realloc key (w/h)
  std::map<std::string, uint32_t> feedbackFmt;          // realloc key (op-chosen pixel format)
  std::map<std::string, bool> feedbackToggle;           // key -> _bufferToggle (flips each cook)
  // Last-resolved OUTPUT textures by feedback node, indexed by Texture2D-output ordinal. Borrowed pointers
  // into feedbackTexBuf's pair / an upstream input (SwapTextures), NOT owned. Both cooks write it at a
  // feedback cook's end so debugCookedFeedbackOutput can read Current/PreviousFrame.
  static constexpr int kMaxFeedbackOut = 4;
  std::map<std::string, std::array<MTL::Texture*, kMaxFeedbackOut>> feedbackOut;

  MTL::Buffer* ensureOut(const std::string& key, uint32_t count) {
    MTL::Buffer*& b = outBuf[key];
    if (!b || outCap[key] < count) {
      if (b) b->release();
      uint32_t cap = count > 0 ? count : 1;  // never alloc zero
      b = dev->newBuffer((NS::UInteger)cap * sizeof(SwPoint), MTL::ResourceStorageModeShared);
      outCap[key] = cap;
    }
    outCount[key] = count;
    return b;
  }

  // The mesh node's PAIR of output buffers (the 4th cook flow). Sizes the vertex buffer to
  // vtxCount SwVertex and the index buffer to idxCount SwTriIndex, reusing both across frames and
  // reallocating only when a count GROWS (the RESOURCE_LIFETIME rule, same as ensureOut). Never
  // allocs zero (a degenerate mesh with 0 faces still gets a 1-element index buffer). StorageMode
  // Shared so the CPU-readback golden can memcpy contents() (NO GPU draw in batch 1).
  void ensureMesh(const std::string& key, uint32_t vtxCount, uint32_t idxCount, MTL::Buffer*& outVtx,
                  MTL::Buffer*& outIdx) {
    MTL::Buffer*& vb = meshVtxBuf[key];
    if (!vb || meshVtxCap[key] < vtxCount) {
      if (vb) vb->release();
      uint32_t cap = vtxCount > 0 ? vtxCount : 1;
      vb = dev->newBuffer((NS::UInteger)cap * sizeof(SwVertex), MTL::ResourceStorageModeShared);
      meshVtxCap[key] = cap;
    }
    MTL::Buffer*& ib = meshIdxBuf[key];
    if (!ib || meshIdxCap[key] < idxCount) {
      if (ib) ib->release();
      uint32_t cap = idxCount > 0 ? idxCount : 1;
      ib = dev->newBuffer((NS::UInteger)cap * sizeof(SwTriIndex), MTL::ResourceStorageModeShared);
      meshIdxCap[key] = cap;
    }
    meshVtxCount[key] = vtxCount;
    meshIdxCount[key] = idxCount;
    outVtx = vb;
    outIdx = ib;
  }

  // The Buffer node's raw output (Seam-1 "Buffer" currency). Sizes the buffer to byteSize bytes (the
  // marshal op computed it from its float payload — a StructuredBuffer fill, NOT a SwPoint-count buffer),
  // reusing it across frames and reallocating only when byteSize GROWS past rawCap (RESOURCE_LIFETIME,
  // same as ensureOut/ensureMesh). Never allocs zero (an empty FloatsToBuffer → a 1-byte buffer, the
  // engine's "empty" convention). StorageModeShared so the marshal op memcpys host floats into contents()
  // AND a CPU-readback golden can read them (byte-parity). Returns the buffer; the caller reads contents().
  MTL::Buffer* ensureRawBuffer(const std::string& key, uint32_t byteSize) {
    MTL::Buffer*& b = rawBuf[key];
    if (!b || rawCap[key] < byteSize) {
      if (b) b->release();
      uint32_t cap = byteSize > 0 ? byteSize : 1;  // never alloc zero
      b = dev->newBuffer((NS::UInteger)cap, MTL::ResourceStorageModeShared);
      rawCap[key] = cap;
    }
    return b;
  }

  // The RenderTarget node's own output texture, sized to its resolved resolution. Reused across frames;
  // reallocated only when w/h/mipped change (RESOURCE_LIFETIME). Owned (newTexture) → released on realloc +
  // in the destructor. shaderWrite: a COMPUTE leaf (image_filter_op_registry, -cs.hlsl) writes via a
  // RWTexture2D → adds MTL::TextureUsageShaderWrite (pixel ops leave it false → byte-identical descriptor).
  // mipped: a MIPPED-OUTPUT op (imageFilterMippedOutputTypes) carries a full mip pyramid (level count =
  // floor(log2(max(w,h)))+1, TiXL RenderTarget.cs:289); we only ALLOCATE the chain here (the COOK blits
  // generateMipmaps after the leaf fills level 0). Default false → byte-identical descriptor; the flag is
  // part of the realloc key (texMipped) since a false→true change MUST reallocate.
  MTL::Texture* ensureTex(const std::string& key, uint32_t w, uint32_t h, bool shaderWrite = false,
                          bool mipped = false) {
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    MTL::Texture*& t = texBuf[key];
    if (!t || texW[key] != w || texH[key] != h || texMipped[key] != mipped) {
      if (t) t->release();
      MTL::TextureDescriptor* td =
          MTL::TextureDescriptor::texture2DDescriptor(kPointTargetFormat, w, h, mipped);
      if (mipped) {
        uint32_t mx = w > h ? w : h;
        uint32_t levels = (uint32_t)std::floor(std::log2((double)mx)) + 1u;
        td->setMipmapLevelCount(levels);
      }
      MTL::TextureUsage usage = MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead;
      if (shaderWrite) usage |= MTL::TextureUsageShaderWrite;
      td->setUsage(usage);
      td->setStorageMode(MTL::StorageModeShared);
      t = dev->newTexture(td);
      texW[key] = w;
      texH[key] = h;
      texMipped[key] = mipped;
    }
    return t;
  }

  // OWN-TEXTURE op output (Slice B tex-output fork): an op (ValuesToTexture) that allocates its OWN
  // data-sized, OP-CHOSEN-format texture instead of the resolution-pinned ensureTex one. Parked in the SAME
  // texBuf lifetime map (keyed flatKey(id)) → released on realloc here AND in ~PointGraph (NO per-cook
  // leak). Reallocates on w/h/fmt change (RESOURCE_LIFETIME; the realloc key adds `fmt`). NON-mipped,
  // ShaderRead, StorageMode=Shared (a getBytes golden can read it — the runtime-side restatement of
  // platform textureFromCpuBuffer, which runtime cannot call: runtime→platform is a forbidden upward dep).
  MTL::Texture* ensureOwnedTex(const std::string& key, uint32_t w, uint32_t h, MTL::PixelFormat fmt) {
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    MTL::Texture*& t = texBuf[key];
    if (!t || texW[key] != w || texH[key] != h || texFmt[key] != (uint32_t)fmt) {
      if (t) t->release();
      MTL::TextureDescriptor* td =
          MTL::TextureDescriptor::texture2DDescriptor(fmt, w, h, /*mipped=*/false);
      td->setUsage(MTL::TextureUsageShaderRead);
      td->setStorageMode(MTL::StorageModeShared);
      t = dev->newTexture(td);
      texW[key] = w;
      texH[key] = h;
      texFmt[key] = (uint32_t)fmt;
      texMipped[key] = false;  // keep the parallel realloc-key maps consistent for this key
    }
    return t;
  }

  // CROSS-FRAME texture PAIR for a feedback op (KeepPreviousFrame). Sizes BOTH textures (texA + texB) to
  // (w,h,fmt), reallocating BOTH only on a description change (RESOURCE_LIFETIME on a PAIR; TiXL
  // KeepPreviousFrame.cs:46-54 disposes+recreates both on formatChanged). Usage = RenderTarget|ShaderRead
  // (blit copyFromTexture writes + a downstream op/readback shader-reads); StorageMode Shared (getBytes
  // golden). Parked in feedbackTexBuf → released here on realloc AND in ~PointGraph (NO leak, NO UAF).
  // Returns false if either alloc failed.
  bool ensureFeedbackPair(const std::string& key, uint32_t w, uint32_t h, MTL::PixelFormat fmt,
                          MTL::Texture*& outA, MTL::Texture*& outB) {
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    FeedbackPair& fp = feedbackTexBuf[key];
    const bool changed = !fp.a || !fp.b || feedbackW[key] != w || feedbackH[key] != h ||
                         feedbackFmt[key] != (uint32_t)fmt;
    if (changed) {
      if (fp.a) { fp.a->release(); fp.a = nullptr; }
      if (fp.b) { fp.b->release(); fp.b = nullptr; }
      auto makeTex = [&]() -> MTL::Texture* {
        MTL::TextureDescriptor* td =
            MTL::TextureDescriptor::texture2DDescriptor(fmt, w, h, /*mipped=*/false);
        td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
        td->setStorageMode(MTL::StorageModeShared);
        return dev->newTexture(td);
      };
      fp.a = makeTex();
      fp.b = makeTex();
      feedbackW[key] = w;
      feedbackH[key] = h;
      feedbackFmt[key] = (uint32_t)fmt;
    }
    outA = fp.a;
    outB = fp.b;
    return fp.a && fp.b;
  }

  // Per-node stateful-op memory. Re-created (free + new) when `count` GROWS past what the state was sized
  // for — stateNew sizes internal buffers (e.g. the sim's particle buffer) to the creation-time count, so
  // dispatching a larger count over stale state is a GPU out-of-bounds write (refuter 2b finding 1; mirror
  // of ensureOut's grow rule). Growing resets sim continuity — correctness over continuity, like a realloc.
  void* ensureState(const std::string& key, const std::string& type, uint32_t count) {
    auto it = state.find(key);
    if (it != state.end()) {
      if (!it->second || count <= stateCap[key]) return it->second;
      if (stateFree.count(key) && stateFree[key]) stateFree[key](it->second);  // grew: rebuild
      state.erase(it);
    }
    auto r = pgdetail::cookReg().find(type);
    if (r != pgdetail::cookReg().end() && r->second.stateNew) {
      void* st = r->second.stateNew(dev, lib, count);
      state[key] = st;
      stateFree[key] = r->second.stateFree;
      stateCap[key] = count;
      return st;
    }
    state[key] = nullptr;
    return nullptr;
  }

  void clearTarget() {
    MTL::RenderPassDescriptor* rpd = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* ca = rpd->colorAttachments()->object(0);
    ca->setTexture(target);
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
    ca->setStoreAction(MTL::StoreActionStore);
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    cmd->renderCommandEncoder(rpd)->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
  }

  // The resident MESH cook (4th flow), from cookResidentMesh (resident_mesh_cook.cpp); wrapped by a lambda.
  SwMeshView cookResidentMesh(const ResidentEvalGraph& rg, const std::string& path,
                              const ResidentEvalCtx& rc, const EvaluationContext& ctx, int depth);

  // The FLAT MESH cook (point_graph_mesh_cook.cpp, Cut-6; full doc in leaf). debugCookedMeshInline = inlined twin of PointGraph::debugCookedMesh.
  bool debugCookedMeshInline(int nodeId, const MTL::Buffer*& vtx, uint32_t& vtxCount,
                             const MTL::Buffer*& idx, uint32_t& idxCount);
  bool cookFlatMeshNode(const Graph& g, const EvaluationContext& ctx, const NodeParamsFn& nodeParams,
                        int id);
  bool cookFlatMeshInto(const Graph& g, const EvaluationContext& ctx, const NodeParamsFn& nodeParams,
                        int id, const MTL::Buffer*& vtx, uint32_t& vtxCount, const MTL::Buffer*& idx,
                        uint32_t& faceCount);
  // The FLAT HOST-VALUE cooks (FloatList/ColorList/Gradient/PointList), point_graph_hostvalue_cook.cpp (Cut-4;
  // full doc in leaf). FloatList + ColorList cross a per-frame memo (by-ref) so a stateful op cooks once/frame.
  const std::vector<float>* cookFlatFloatList(const Graph& g, const EvaluationContext& ctx,
                                              const NodeParamsFn& nodeParams,
                                              std::map<int, const std::vector<float>*>& floatListCooked, int id);
  const std::vector<simd::float4>* cookFlatColorList(
      const Graph& g, const EvaluationContext& ctx, const NodeParamsFn& nodeParams,
      const std::function<MTL::Buffer*(int)>& cookNode,
      std::map<int, const std::vector<simd::float4>*>& colorListCooked, int id);
  const SwGradient* cookFlatGradient(const Graph& g, const EvaluationContext& ctx,
                                     const NodeParamsFn& nodeParams, int id);
  const std::vector<SwPoint>* cookFlatPointList(const Graph& g, const EvaluationContext& ctx, const NodeParamsFn& nodeParams,
                                                const std::function<MTL::Buffer*(int)>& cookNode, const std::function<const std::string*(int)>& cookStringNode, int id);
  // The FLAT BUFFER cook (Seam-1 = GPU "Buffer" currency), point_graph_buffer_cook.cpp (full doc in leaf).
  // cookBufferNode rides in by-ref (Buffer→Buffer self-recursion). Returns the cooked SwBuffer (bufferMeta).
  const SwBuffer* cookFlatBuffer(const Graph& g, const EvaluationContext& ctx, const NodeParamsFn& nodeParams,
                                 const std::function<const SwBuffer*(int)>& cookBufferNode, int id);
  // The FLAT STRING cooks + host-scalar, point_graph_string_cook.cpp / point_graph_hostscalar_cook.cpp (full doc in leaves). gatherStringInputs = the SHARED wire-OR-const String gather (both cooks call it).
  std::vector<std::string> gatherStringInputs(
      const Graph& g, int id, const NodeSpec& s,
      const std::function<const std::string*(int)>& cookStringNode);
  const std::string* cookFlatStringNode(
      const Graph& g, const EvaluationContext& ctx, const NodeParamsFn& nodeParams,
      const std::function<const std::vector<float>*(int)>& cookFloatListNode,
      const std::function<const std::vector<std::string>*(int)>& cookStringListNode,
      const std::function<const std::string*(int)>& cookStringNode, int id);
  const std::vector<std::string>* cookFlatStringListNode(
      const Graph& g, const EvaluationContext& ctx, const NodeParamsFn& nodeParams,
      const std::function<const std::string*(int)>& cookStringNode,
      const std::function<const std::vector<std::string>*(int)>& cookStringListNode, int id);
  void cookFlatStringLength(const Graph& g, int id,
                            const std::function<const std::string*(int)>& cookStringNode);
  void cookFlatHostScalar(
      const Graph& g, const EvaluationContext& ctx, const NodeParamsFn& nodeParams,
      const std::function<const std::vector<float>*(int)>& cookFloatListNode,
      const std::function<const std::string*(int)>& cookStringNode, int id);
  // The FLAT TEXTURE cook (Texture2D stream), point_graph_tex_cook.cpp (Cut-5; full doc in leaf). MOST cross-wired flow — cookCommand/cookTexNode/cookFloatListNode/cookGradientNode/feedbackCooked all by-ref.
  MTL::Texture* cookFlatTexNode(
      const Graph& g, const EvaluationContext& ctx, const SourceRegistry* reg,
      const NodeParamsFn& nodeParams, const FlatCmdFn& cookCommand, const FlatTexFn& cookTexNode,
      const std::function<const std::vector<float>*(int)>& cookFloatListNode,
      const std::function<const SwGradient*(int)>& cookGradientNode, std::set<int>& texVisiting,
      std::map<int, std::array<MTL::Texture*, FeedbackCookCtx::kMaxTexOutputs>>& feedbackCooked, int id,
      int outAbsPort);
  // FLAT + RESIDENT COMMAND cooks (point_graph_command_cook.cpp / point_graph_resident_command_cook.cpp;
  // full doc in leaves). cookCommand walkers extracted VERBATIM from each driver; near-mirror TWINS but NOT
  // a shared path (flat int-id/g.connections vs resident path/ResidentInput genuinely diverge). By-ref slots.
  RenderCommand cookFlatCommand(
      const Graph& g, const EvaluationContext& ctx, const SourceRegistry* reg,
      const NodeParamsFn& nodeParams, ContextVarMap* ctxVars, std::set<int>& cmdVisiting,
      const FlatNodeFn& cookNode,
      const std::function<bool(int, const MTL::Buffer*&, uint32_t&, const MTL::Buffer*&, uint32_t&)>&
          cookMeshInto,
      const FlatTexFn& cookTexNode, const FlatCmdFn& cookCommand, int id);
  RenderCommand cookResidentCommand(
      const ResidentEvalGraph& rg, const EvaluationContext& ctx, const SourceRegistry* reg,
      int depthCap, ContextVarMap* ctxVars, const ResidentParamsFn& nodeParams,
      const std::function<MTL::Buffer*(const std::string&, int)>& cookNode,
      const std::function<SwMeshView(const std::string&, int)>& cookResidentMesh,
      const ResidentTexFn& cookTexNode, const ResidentCmdFn& cookCommand,
      const std::string& path, int depth);
  // The RESIDENT TEXTURE cook (point_graph_resident_tex_cook.cpp; full doc in leaf). Resident twin of cookFlatTexNode; cookResident wraps it in a forwarding lambda. depthCap = the file-local kCookDepthCap.
  MTL::Texture* cookResidentTexNode(
      const ResidentEvalGraph& rg, const EvaluationContext& ctx, const SourceRegistry* reg,
      const ResidentEvalCtx& rc, int depthCap, const ResidentParamsFn& nodeParams,
      const ResidentCmdFn& cookCommand, const ResidentTexFn& cookTexNode,
      const std::function<const SwGradient*(const std::string&, int)>& cookResidentGradient,
      std::map<std::string, std::array<MTL::Texture*, FeedbackCookCtx::kMaxTexOutputs>>& feedbackCooked,
      const std::string& path, int depth, const std::string& outSlotId);
};

}  // namespace sw
