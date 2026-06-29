// runtime/point_graph — the point-buffer cook (Metal). Parallel to graph.cpp's
// evalFloat (which walks "Float" ports), but for "Points"/"ParticleForce" ports:
// a connection carries a bag of points = an MTL::Buffer of SwPoint (TiXL Point,
// 64B, tixl_point.h). Each frame PointGraph walks the Points connections back from
// the draw node, cooks each operator into its (PointGraph-owned, reused) output
// buffer, and renders the final bag into target().
//
// Lives in its OWN module so the pure data model (graph.h) stays Metal-free
// (ARCHITECTURE.md: runtime leaf, no upward deps). The operator interface allows
// STATEFUL ops (a sim keeps persistent buffers across frames via `state`) without
// forcing state onto stateless generators/transforms.
//
// Faithful to TiXL: operators consume/produce StructuredBuffer<Point>
// (external/tixl .../point/**). The buffer is the universal currency, exactly like
// TiXL's BufferWithViews flowing between point operators.
#pragma once
#include <cstdint>
#include <map>
#include <memory>  // std::shared_ptr (PointCookCtx::inputFieldTree, PF-0 field-into-force seam)
#include <string>
#include <vector>

#include <simd/simd.h>  // simd::float4 (debugCookedColorList readback type)

#include "runtime/render_command.h"  // RenderCommand (the Command stream's currency)

namespace MTL {
class Device;
class Library;
class CommandQueue;
class Buffer;
class Texture;
}  // namespace MTL

struct EvaluationContext;  // runtime/eval_context.h (full def in point_graph.cpp)
struct SwPoint;            // runtime/tixl_point.h (64B host point; full def where the cook includes it)
namespace sw { struct ContextVarMap; }  // stateful_value_ops.h (host per-frame var map; S3a)
namespace sw { struct SwGradient; struct SwBuffer; }  // sw_gradient.h (host Gradient) / sw_buffer.h (Seam-1 GPU "Buffer" currency); full defs where the ops include them
namespace sw { class Curve; struct FieldNode; }  // curve.h (host Curve) / field_graph.h (FieldNode tree); full defs in the builder + PF-a cook TU

// The FOUR cook-context structs (PointCookCtx / CmdCookCtx / TexCookCtx / FeedbackCookCtx) and their cook
// function-pointer typedefs (PointCookFn / PointDrawFn / PointCmdFn / PointTexFn / PointFeedbackFn) live in
// their own leaf header so the cook-ctx ABI the ~249 includers depend on has room to grow (a new cook input
// field) without touching this busy file. Included HERE — the exact GLOBAL spot those definitions used to
// sit (the new header opens its OWN `namespace sw` for the structs + declares MTL at global, so it must be
// included OUTSIDE `namespace sw` below). Every includer sees the identical symbols (ABI-preserving).
#include "runtime/point_graph_cook_ctx.h"

namespace sw {

struct Graph;
struct Node;
struct SymbolLibrary;  // runtime/compound_graph.h (lib-native canvas, 批次 3)
class SourceRegistry;

// A CPU-side render resolution (NOT the 16-byte GPU EvaluationContext). WindowFollow resolves to this.
struct RenderResolution {
  uint32_t w = 512;
  uint32_t h = 512;
};

// Per-node persistent state lifetime for stateful ops (e.g. a sim's particle buffer).
// `count` is the node's point count at creation. Return nullptr for stateless ops.
using PointStateNewFn = void* (*)(MTL::Device*, MTL::Library*, uint32_t count);
using PointStateFreeFn = void (*)(void*);

// Optional count remap: natural count (Points-input sum / Count param) -> the count the
// node's output + persistent state are sized to. Lets a stateful op (ParticleSystem) own a
// pool larger than its emit ring. null = identity.
using PointCountFn = uint32_t (*)(uint32_t);

// Register a node type's cook/draw fn (Metal-side; separate from NodeSpec.evaluate,
// which is the pure-float path). registerPointOp's state fns are optional (stateless
// ops omit them). Registration is explicit (call registerBuiltinPointOps() at app
// init) — NOT a static initializer — so --selftest-* runs see a clean table.
// countFromMeshVtx: the node's output bag is sized to its gathered Mesh input's VERTEX count (the
// mesh-into-points fork — MeshVerticesToPoints emits one Point per vertex). Default false → byte-identical.
void registerPointOp(const std::string& type, PointCookFn,
                     PointStateNewFn = nullptr, PointStateFreeFn = nullptr,
                     PointCountFn countTransform = nullptr,
                     bool countFromFirstPointsInput = false,
                     bool countFromMeshVtx = false);
void registerDrawOp(const std::string& type, PointDrawFn);
// Register a command op (the Command stream). Separate table from cook/draw — exactly
// as TiXL keeps Slot<Command> distinct from Slot<BufferWithViews>.
void registerCmdOp(const std::string& type, PointCmdFn);
// Register a texture op (the Texture2D stream — RenderTarget). Third table.
void registerTexOp(const std::string& type, PointTexFn);
// Register a FEEDBACK / multi-tex-output op (KeepPreviousFrame / SwapTextures). `needsCrossFramePair`
// = true for an op that carries a persistent texture pair sized to its first Texture2D input
// (KeepPreviousFrame); false for a stateless routing op (SwapTextures just swaps its two inputs).
// `pairFormat` is the MTL::PixelFormat raw value used when needsCrossFramePair (RGBA8Unorm = the
// engine's kPointTargetFormat for a frame texture); ignored when needsCrossFramePair is false. A
// feedback type is NOT in texReg() — the cook driver routes Texture2D inputs/outputs through the
// feedback path instead of the single-output tex path.
void registerFeedbackOp(const std::string& type, PointFeedbackFn fn, bool needsCrossFramePair,
                        uint32_t pairFormat = 0);
bool isFeedbackOp(const std::string& type);
bool feedbackNeedsPair(const std::string& type);
uint32_t feedbackPairFormat(const std::string& type);  // MTL::PixelFormat raw; 0 if unregistered
PointFeedbackFn findFeedbackOp(const std::string& type);  // null if not a feedback op
// Mark a tex op type as OWN-TEXTURE (Slice B tex-output fork): the cook driver does NOT pre-size its
// output via ensureTex (RGBA8/resolution-pinned). Instead it hands the op TexCookCtx::ownTexHost/W/H,
// then allocates the op-owned texture (R32Float, data-sized) parked in Impl::texBuf. ValuesToTexture
// is the first/only member. Every other tex op is untouched (drains into `output` as before).
void registerTexOpOwnsOutput(const std::string& type);
bool texOpOwnsOutput(const std::string& type);
// Own-texture FORMAT selector: an OWN-TEXTURE tex op declares how many FLOATS it writes per texel so
// the driver allocates the matching MTL::PixelFormat + uploads with the right rowPitch. 1 → R32Float
// (ValuesToTexture, the implicit default); 4 → R32G32B32A32_Float (GradientsToTexture, sampled RGBA).
// Unregistered own-tex types default to 1 (byte-identical for ValuesToTexture). Idempotent.
void registerTexOpOwnFormat(const std::string& type, int floatsPerTexel);
int texOpOwnFormat(const std::string& type);  // floats/texel for an own-tex type (default 1)

// Resolve a RenderTarget node's output resolution from its Resolution enum param (+ CustomW/H);
// WindowFollow (default) returns `windowSize`. Defined in point_ops_rendertarget.cpp; the cook driver
// sizes the node's own texture (the RESOLUTION PIN) with it. The map overload is the core (flat AND
// resident resolved params); the Node* overload wraps it for flat callers.
RenderResolution resolveRenderResolution(const std::map<std::string, float>& params,
                                         RenderResolution windowSize);
RenderResolution resolveRenderResolution(const Node* n, RenderResolution windowSize);

// S1 EXPLICIT-OVERRIDE resolve (SetRequestedResolution): the resolution this op PUSHES around its Command
// subtree = (Width>0?Width:current.w)*Multiply × likewise H, clamped [1,16384] (Set...cs:18-28; W/H==0 =
// scale the ambient size). The cook driver calls it to push BEFORE cooking the subtree.
RenderResolution resolveSetRequestedResolution(const std::map<std::string, float>& params,
                                               RenderResolution current);

// --- resolved-param accessors (slice 2b seam; defined in point_graph_params.cpp) ---
// Read a Float param from the ctx's RESOLVED map; falls back to `def` when the driver supplied
// no map (ops invoked outside a cook driver, e.g. hand-built ctx in op selftests).
float cookParam(const PointCookCtx& c, const char* id, float def);
float cookParam(const CmdCookCtx& c, const char* id, float def);
float cookParam(const TexCookCtx& c, const char* id, float def);
float cookParam(const FeedbackCookCtx& c, const char* id, float def);
// Vector params (mirrors graph.h readVecN: components "<base>.x"/".y"/".z"/".w").
// Missing component -> fallback[i].
void cookVecN(const PointCookCtx& c, const char* base, const float* fallback, int n, float* out);
void cookVecN(const TexCookCtx& c, const char* base, const float* fallback, int n, float* out);
void cookVecN(const CmdCookCtx& c, const char* base, const float* fallback, int n, float* out);
// A Float param of the node feeding buffer input `input` (force ops); def when unwired/missing.
float cookInputParam(const PointCookCtx& c, int input, const char* id, float def);
// Registers all built-in point operators (A.1+: RadialPoints/TransformPoints/
// ParticleSystem/DrawPoints/…). Called once from the app (Renderer) at startup.
void registerBuiltinPointOps();

// The point-graph runtime. Owns device/lib/queue refs, per-node output buffers and
// per-node state (both persist across frames; output buffers reuse, reallocating
// only when a node's count changes — the RESOURCE_LIFETIME golden), and the target
// texture the final draw renders into. Replaces the hardcoded ParticleSystem pipeline.
class PointGraph {
 public:
  PointGraph(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* queue, uint32_t width,
             uint32_t height);
  ~PointGraph();
  PointGraph(const PointGraph&) = delete;
  PointGraph& operator=(const PointGraph&) = delete;

  bool valid() const;
  // Cook the sub-graph feeding `targetNodeId` this frame and realize it into target(): a draw node
  // (PointDrawFn) renders its Points input; else a Points-producing op's bag is cooked but not yet shown
  // (raw Points preview needs a typed wrapper — future). `targetNodeId` = what the viewport shows (live loop
  // passes defaultDrawTarget(); a future "pin any node" another). S3a: `ctxVars` (trailing, defaulted) = the
  // LIVE host context-var map threaded into every command cook (SetVar scopes a var). nullptr = byte-identical.
  void cook(const Graph& g, const EvaluationContext& ctx, const SourceRegistry* reg,
            int targetNodeId, ContextVarMap* ctxVars = nullptr);
  // Resident-graph cook (slice 2 walk + 2b parity): walk a ResidentEvalGraph by path-qualified id, realize
  // `targetPath` into target() with the SAME three-flow terminal as cook() (tex/cmd/preview), per-path
  // persistent buffers + stateful state, driver-resolved Float params (2b seam, via evalResidentFloat).
  // Proven by --selftest-residentcook + --selftest-residentparity. S5: localTime/localFxTime (BARS) from
  // the Transport — localTime = playhead (automation samples), localFxTime = wall clock; `lib` lets Automation
  // resolve curves via definition-layer Animators (S3). S3a: `ctxVars` = resident mirror of cook()'s context-
  // var param (SetVar push/restore reaches BOTH legs). All trailing args default to pre-S5/null (byte-unchanged).
  void cookResident(const struct ResidentEvalGraph& rg, const EvaluationContext& ctx,
                    const SourceRegistry* reg, const std::string& targetPath,
                    float localTimeBars = -1.0f, float localFxTimeBars = -1.0f,
                    const SymbolLibrary* lib = nullptr, ContextVarMap* ctxVars = nullptr);
  // Default viewport target = the first draw node (today's wired terminal). 0 if none.
  int defaultDrawTarget(const Graph& g) const;
  // Same, inside ONE symbol's subgraph (批次 3): first realizing child (RenderTarget>DrawPoints>draw); 0 none.
  int defaultDrawTarget(const SymbolLibrary& lib, const std::string& symbolId) const;
  MTL::Texture* target() const;
  RenderResolution windowResolution() const;  // value-output-rail P1: window size → RequestedResolution emit
  // S1 frame-level override hook (TiXL OutputWindow.cs:411-414 export>selector>Fill; doc in _debug.cpp).
  void setFrameResolutionOverride(RenderResolution res);  // Output/export requests a frame render size
  void clearFrameResolutionOverride();                    // back to Fill (window) — the default
  RenderResolution frameResolution() const;  // override if set, else windowResolution(); UNSET==today.
  // Test-support: the count a flat-cooked node's output bag was sized to last cook (0 if never
  // cooked). Lets goldens assert the count-policy driver (e.g. SnapToPoints out == Points1, not sum).
  uint32_t debugCookedCount(int nodeId) const;
  // Test-support: the GPU output buffer a flat-cooked Points-producing node holds last cook (nullptr if
  // never cooked). StorageModeShared → a golden can read contents() for byte-parity (the ListToBuffer
  // upload bridge proves the host→GPU memcpy + 64B SwPoint stride this way). Borrowed; do not release.
  const MTL::Buffer* debugCookedBuffer(int nodeId) const;
  // Test-support for the Mesh flow (4th cook): the vertex+index buffers a flat-cooked mesh node
  // produced last cook, for CPU-readback goldens (contents()+memcpy, NO GPU draw). Returns false if
  // the node never cooked a mesh. Buffers are PointGraph-owned (borrowed; do not release).
  bool debugCookedMesh(int nodeId, const MTL::Buffer*& vtx, uint32_t& vtxCount,
                       const MTL::Buffer*& idx, uint32_t& idxCount) const;
  // Per-flow HOST-transport test-support readbacks (impls in point_graph_debug.cpp). Each returns the
  // value the node produced on its LAST cook, keyed by flatKey(id) into the matching Impl buffer; all are
  // nullptr when the node never cooked that flow, borrowed (valid until next cook). Used by goldens.
  const std::vector<float>* debugCookedFloatList(int nodeId) const;            // 5th cook: floatListBuf
  const std::vector<simd::float4>* debugCookedColorList(int nodeId) const;     // vec4-list: colorListBuf
  const std::string* debugCookedString(int nodeId) const;                      // 6th cook: stringBuf (MAIN)
  // MULTI-OUTPUT (Sub-seam B): an EXTRA String output keyed by spec output-port index (stringBuf[flatKey
  // (id)+":"+portIdx]); portIdx==0 == debugCookedString. Scalar extras ride Node::outCache[portIdx].
  const std::string* debugCookedStringPort(int nodeId, int portIdx) const;
  const std::vector<std::string>* debugCookedStringList(int nodeId) const;     // Sub-seam A: stringListBuf
  const std::vector<::SwPoint>* debugCookedPointList(int nodeId) const;        // 7th cook: pointListBuf
  const SwGradient* debugCookedGradient(int nodeId) const;                     // 8th cook: gradientBuf (flat key)
  const SwGradient* residentGradientFor(const std::string& path) const;        // 8th cook: gradientBuf (resident path, UI face)
  const SwBuffer* debugCookedSwBuffer(int nodeId) const;                        // Seam-1: bufferMeta (flat key); GPU buffer + stride/count for byte-parity goldens
  // value-output-rail Phase 4: the cooked Shared point buffer a RESIDENT Points node produced last cook,
  // keyed by resident PATH (cookResident's ensureOut key in p_->outBuf); count ← p_->outCount[path]. Shared
  // → contents() is a `const SwPoint*` (zero blit); nullptr+0 when the path cooked no points this frame.
  const ::SwPoint* residentCookedPoints(const std::string& path, uint32_t& count) const;

  // Cross-frame FEEDBACK output (KeepPreviousFrame / SwapTextures): the texture this node routed to its
  // `ordinal`-th Texture2D OUTPUT last cook (0 = PreviousFrame/TextureA, 1 = CurrentFrame/TextureB).
  // `resident` selects the key space (flat "#id" vs resident path "id"). Borrowed; nullptr off-range. Goldens.
  MTL::Texture* debugCookedFeedbackOutput(int nodeId, int ordinal, bool resident = false) const;

  // Texture a TEXTURE-flow node cooked last frame (Impl::texBuf). debugCookedTexture: FLAT key (test);
  // residentTexFor: RESIDENT path key (cookResident's ensureTex key), node-thumbnail face. Both borrowed.
  MTL::Texture* debugCookedTexture(int nodeId) const;
  MTL::Texture* residentTexFor(const std::string& path) const;

 private:
  struct Impl;
  Impl* p_;
};

}  // namespace sw

// The point-op RED→GREEN selftest/golden declarations (runPointGraphSelfTest, the image-filter goldens,
// the seam-consumer goldens, …) were moved to their own leaf header to keep this file under the ≤400 /
// ratchet line law. It opens its OWN `namespace sw`, so it is included AFTER the close above (NOT nested).
// Behavior-preserving: every declaration is identical, reached through the same include path.
#include "runtime/point_graph_selftests.h"
