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
namespace sw { struct SwGradient; }  // runtime/sw_gradient.h (host Gradient; full def where the op includes it)
namespace sw { class Curve; }        // runtime/curve.h (host Curve currency; full def where the op includes it)

namespace sw {

struct Graph;
struct Node;
struct SymbolLibrary;  // runtime/compound_graph.h (lib-native canvas, 批次 3)
class SourceRegistry;

// Everything an operator gets to cook one node this frame.
//   inputs[i]  = the already-cooked output buffer of the i-th wired Points/Force
//                input port (in spec port order); nullptr if that port is unwired.
//   output     = this node's PointGraph-owned output buffer, pre-sized to `count`
//                SwPoints. A cook op WRITES its result here (it does not allocate).
//   state      = per-node persistent memory for stateful ops (sim); nullptr if the
//                op registered no state factory. Stateless ops ignore it.
struct PointCookCtx {
  MTL::Device* dev = nullptr;
  MTL::Library* lib = nullptr;
  MTL::CommandQueue* queue = nullptr;
  const EvaluationContext* ctx = nullptr;  // time / frameIndex / deltaTime
  const Graph* graph = nullptr;            // flat-cook only; ops must NOT read it (params below)
  const SourceRegistry* reg = nullptr;     // live-source/override for Float params
  int nodeId = 0;
  uint32_t count = 0;                      // this node's point count
  const MTL::Buffer* const* inputs = nullptr;
  const uint32_t* inputCounts = nullptr;   // per-input point count (parallel to inputs[]; combine ops
                                           // concat by these offsets; unwired input = 0)
  int inputCount = 0;
  MTL::Buffer* output = nullptr;           // PointGraph-owned; cook writes here
  void* state = nullptr;                   // per-node persistent state (stateful ops)
  // TEXTURE2D inputs (the texture-into-points seam): a Points op that SAMPLES an upstream cooked Texture2D
  // (SamplePointColorAttributes first) gets each wired Texture2D port (spec order) cooked here.
  // inputTextures[i] = the i-th port's cooked upstream texture, or null if unwired. SAME lifetime as
  // inputs[] (PointGraph-owned, single-frame, borrowed — never retained); mirrors TexCookCtx::inputTextures
  // on the Points flow. null/0 for every existing Points op (no Texture2D port → byte-identical).
  static constexpr int kMaxTexInputs = 4;
  const MTL::Texture* inputTextures[kMaxTexInputs] = {nullptr, nullptr, nullptr, nullptr};
  int inputTextureCount = 0;  // number of Texture2D INPUT ports (wired or not), capped at kMaxTexInputs
  // GRADIENT + CURVE host inputs (the bake-into-point seam): a Points op that BAKES host Curve/Gradient
  // values into scratch textures then samples per point (MapPointAttributes) gets each wired Gradient/Curve
  // port (spec order) gathered here, SAME borrowed-single-frame lifetime as inputTextures[] (never
  // retained); mirrors TexCookCtx::inputGradients/inputCurves on the Points flow. In
  // PRODUCTION these are EMPTY/null today (MapPointAttributes' Curve/Gradient ports are host-value inputs
  // with .t3 defaults, no producer wired) → the op bakes its embedded defaults (NOT the flat-only
  // string-rail trap: no real wire is dropped). A golden injects custom values via these to bite the bake.
  // null/empty for every existing Points op (no Gradient/Curve input port → byte-identical).
  const std::vector<SwGradient>* inputGradients = nullptr;
  const std::vector<Curve>* inputCurves = nullptr;
  // MESH input (the mesh-into-points seam): a Points op that READS an upstream cooked Mesh
  // (MeshVerticesToPoints first) gets the FIRST wired Mesh port's vertex+index buffers + counts here
  // (single Mesh input — mirrors CmdCookCtx::meshVtx, which DrawMeshUnlit consumes). meshVtxCount = the
  // per-vertex count a countFromMeshVtx op sizes to. SAME borrowed-single-frame lifetime as inputs[]
  // (PointGraph meshVtxBuf/meshIdxBuf, never retained). null/0 for every existing op → byte-identical.
  const MTL::Buffer* meshVtx = nullptr;   // upstream SwVertex buffer (null when no Mesh input wired)
  uint32_t meshVtxCount = 0;              // upstream VERTEX count (countFromMeshVtx sizes the bag to it)
  const MTL::Buffer* meshIdx = nullptr;   // upstream SwTriIndex buffer (unused by the per-vertex op)
  uint32_t meshFaceCount = 0;             // upstream FACE count (== SwTriIndex count)
  // CAMERA matrices (the camera-matrix-into-points seam): a "Camera" marker INPUT port → the driver
  // fills these from the DEFAULT camera at the output aspect (fillPointCamera → pointCameraMatrices; v1
  // fork: default camera + identity ObjectToWorld). ROW-MAJOR float[16] (shader rebuilds a float4x4 so
  // mul(rowVec,M)==v·M_rowmajor; no packed_float3 after → 16-align safe). hasCamera=false → byte-identical.
  bool hasCamera = false;
  float objectToCamera[16] = {0};  // == WorldToCamera (identity O2W); SamplePointsByCameraDistance reads .z
  float cameraToWorld[16] = {0};   // inverse(WorldToCamera); TransformPointsFromClipspace unproject+rot
  // RESOLVED Float params of THIS node (slice 2b seam): the cook DRIVER resolves every Float input port
  // through the full value spine (override → binding → wire → stored → spec default, flat; or the resident
  // input's driver) and hands the result here. Ops read via cookParam/cookVecN and stay graph-model-
  // agnostic (= TiXL: the slot system resolves inputs, the op body never walks the graph; 7d4b34e).
  const std::map<std::string, float>* params = nullptr;
  // Resolved params of the node feeding buffer input i (parallel to inputs[]; null if unwired). Replaces
  // the legacy read-by-TYPE evalParam for force ops (firstOfType breaks under reuse): a force op's params
  // travel WITH the wire, like TiXL's force buffer carrying its params.
  const std::map<std::string, float>* const* inputParams = nullptr;
};

// A point operator: read inputs (+ Float params via evalParam) → write `output`.
using PointCookFn = void (*)(PointCookCtx&);
// A draw operator: render the (final) `points` bag into `target`. No buffer output.
// LEGACY: the folded "buffer → pixels directly" model. Being replaced by the Command
// stream (cmd op → RenderCommand → RenderTarget). Retired in batch 4.
using PointDrawFn = void (*)(PointCookCtx&, MTL::Texture* target, const MTL::Buffer* points);

// --- Command stream (TiXL's Slot<Command>): the SECOND cook flow, parallel to the
// buffer flow above. A command op (DrawPoints) reads its upstream point bag and emits
// a RenderCommand; it does NOT touch a texture. The buffer flow (PointCookFn, 9 ops)
// is untouched — port dataType decides which flow a connection walks. ---
//
// Everything a command op gets to cook one node this frame. `points`/`count` are the
// already-cooked upstream Points bag (the cook driver resolves the buffer flow first,
// then hands it here). A command op reads its own Float params via evalParam(graph,...).
struct CmdCookCtx {
  const EvaluationContext* ctx = nullptr;
  const Graph* graph = nullptr;
  const SourceRegistry* reg = nullptr;
  int nodeId = 0;
  const MTL::Buffer* points = nullptr;  // upstream point bag (buffer flow already cooked)
  uint32_t count = 0;                   // upstream point count
  // First wired Texture2D input (FORK#1, the Command-path texture gather): the cook driver cooks
  // the upstream tex op (RenderTarget/Blur) into ITS own texture and hands the result here. A
  // command op that draws a texture (DrawScreenQuad) borrows this ptr into its RenderDrawItem;
  // point-only command ops (DrawPoints) ignore it. null when no Texture2D input is wired. Borrowed
  // (PointGraph-owned, single-frame) — NEVER retained, same lifetime contract as `points`. Mirrors
  // TexCookCtx::inputTexture (the Texture2D flow's gather) but on the Command flow.
  const MTL::Texture* inputTexture = nullptr;
  // First wired COMMAND input subtree (Camera op, Cut 3): a command op that wraps a Command subtree
  // (TiXL Camera.Command input) gets the upstream chain cooked here. The cook driver gathers the
  // node's Command input port by recursing into the upstream command node (mirrors inputTexture for
  // the Texture2D-input command ops). null when no Command input is wired. Borrowed pointer into a
  // driver-local RenderCommand (single-frame); the op may COPY its items (Camera stamps + re-emits).
  // Point/Texture command ops (DrawPoints/DrawScreenQuad) leave it null → byte-identical path.
  const RenderCommand* inputCommand = nullptr;
  // First wired MESH input (DrawMeshUnlit, the 4th cook flow as a draw consumer): the cook driver cooks
  // the upstream mesh node (NGonMesh/QuadMesh) and hands its vertex+index buffers + face count here. A
  // command op that draws a mesh (DrawMeshUnlit) borrows these into its DrawKind::Mesh item; every other
  // command op leaves them null/0 → byte-identical path. Borrowed (PointGraph meshVtxBuf/meshIdxBuf,
  // single-frame) — NEVER retained, same lifetime contract as `points`. Mirrors inputTexture/inputCommand.
  const MTL::Buffer* meshVtx = nullptr;   // upstream SwVertex buffer (null when no Mesh input wired)
  const MTL::Buffer* meshIdx = nullptr;   // upstream SwTriIndex buffer
  uint32_t meshFaceCount = 0;             // upstream FACE count (== SwTriIndex count); VS draws ×3
  const std::map<std::string, float>* params = nullptr;  // resolved Float params (see PointCookCtx)
};
// A command operator: read the upstream point bag (+ Float params) → return a RenderCommand.
using PointCmdFn = RenderCommand (*)(CmdCookCtx&);

// --- Texture stream (TiXL's Slot<Texture2D>): the THIRD cook flow. A texture op (RenderTarget) executes
// an upstream RenderCommand into a sized texture — the RESOLUTION PIN. Output texture is PointGraph-owned
// (pre-sized, like a buffer op's output bag); the op draws into it, does not allocate. ---
// A CPU-side render resolution (NOT the 16-byte GPU EvaluationContext). WindowFollow resolves to this.
struct RenderResolution {
  uint32_t w = 512;
  uint32_t h = 512;
};
// Everything a texture op gets to cook one node this frame. `command` is the upstream
// chain (already concatenated across multi-inputs by the cook driver). `output` is the
// PointGraph-owned texture, pre-sized to the resolved resolution.
struct TexCookCtx {
  MTL::Device* dev = nullptr;
  MTL::Library* lib = nullptr;
  MTL::CommandQueue* queue = nullptr;
  const EvaluationContext* ctx = nullptr;
  const Graph* graph = nullptr;
  const SourceRegistry* reg = nullptr;
  int nodeId = 0;
  const RenderCommand* command = nullptr;  // upstream chain (concatenated); may be null/empty
  // First wired Texture2D input (the gather direct-through, lane I): the cook driver cooks the
  // upstream texture op (RenderTarget/Blur) into ITS own ensureTex texture and hands the result
  // here. An image-filter op (Blur) samples this; a RenderTarget executor ignores it. null when
  // no Texture2D input is wired. This is the Texture2D flow's "input bag" — parallel to how
  // PointCookCtx::inputs carries the already-cooked upstream Points buffer.
  const MTL::Texture* inputTexture = nullptr;
  // ALL wired Texture2D inputs in spec port order (lane D2: Displace = Image + DisplaceMap, the
  // first op with two Texture2D inputs). inputTextures[i] = the cooked upstream texture of the
  // i-th Texture2D input PORT (in spec order), or null if that port is unwired. inputTexture above
  // mirrors inputTextures[0] (single-input ops like Blur stay untouched). Parallel to how
  // PointCookCtx::inputs carries each Points input by port order.
  static constexpr int kMaxTexInputs = 4;
  const MTL::Texture* inputTextures[kMaxTexInputs] = {nullptr, nullptr, nullptr, nullptr};
  int inputTextureCount = 0;  // number of Texture2D INPUT ports (wired or not), capped at kMaxTexInputs
  // ASSET texture ((E)-seam phase 2): a decoded asset bound at the slot AFTER the wired inputs (t1
  // for a single-input op like RgbTV's noise). null unless the op's type registered an asset key in
  // imageFilterAssetTextures(); cache-owned (NOT released by the leaf). See image_filter_op_registry.h.
  const MTL::Texture* assetTexture = nullptr;
  // FLOATLIST inputs (Slice B, the rail-crossing): the already-cooked upstream FloatList inputs of a
  // tex op that has "FloatList" input ports (in spec port order, MultiInput-expanded into wire-
  // declaration order — same gather contract as cookFloatListNode). null for every existing tex op
  // (RenderTarget/Blur/Displace have no FloatList input → byte-identical path). ValuesToTexture reads
  // inputLists->at(i) for its i-th gathered Values source. Borrowed (driver-owned); never retained.
  const std::vector<std::vector<float>>* inputLists = nullptr;
  // GRADIENT inputs (the 8th cook flow rail-crossing): the already-cooked upstream SwGradient inputs of
  // a tex op that has "Gradient" input ports (GradientsToTexture), in spec port order with MultiInput
  // ports expanded into wire-declaration order — same gather contract as inputLists/cookGradientNode.
  // null for every existing tex op (no Gradient input → byte-identical path). Borrowed (driver-owned).
  // Forward-declared as a vector of SwGradient (full def via sw_gradient.h where the op includes it).
  const std::vector<SwGradient>* inputGradients = nullptr;
  // CURVE inputs (the animation-curve cook flow rail-crossing): the already-cooked upstream Curve inputs
  // of a tex op that has "Curve" input ports (CurvesToTexture), in spec port order with MultiInput ports
  // expanded into wire-declaration order — same gather contract as inputGradients. null for every existing
  // tex op (no Curve input → byte-identical path). Borrowed (driver-owned); never retained. There is NO
  // Curve PRODUCER op yet, so the cook driver does not populate this in production (CurvesToTexture falls
  // back to its embedded default Curve); the field exists so (a) a golden can inject curves and (b) a
  // future Curve producer can wire in without a TexCookCtx ABI change. (Curve = the host currency, curve.h.)
  const std::vector<Curve>* inputCurves = nullptr;
  // OWN-TEXTURE staging (Slice B, the tex-output fork): an op that allocates its OWN data-sized,
  // non-RGBA8 texture (ValuesToTexture: R32Float) does NOT use `output` (which is the ensureTex
  // RGBA8/resolution-pinned texture). Instead it computes its dims + writes its host float buffer here;
  // the cook DRIVER then allocates the op-owned texture (parked in Impl::texBuf, realloc on dim/format
  // change → no leak) and uploads `ownTexHost`. Set non-null by the walker ONLY for types in
  // texOpOwnsOutput(); every other tex op leaves these null and draws into `output` as before.
  std::vector<float>* ownTexHost = nullptr;  // op fills (clear+resize+write); driver uploads to texture
  uint32_t* ownTexW = nullptr;               // op writes the texture width
  uint32_t* ownTexH = nullptr;               // op writes the texture height
  MTL::Texture* output = nullptr;          // PointGraph-owned, pre-sized; op draws here
  const std::map<std::string, float>* params = nullptr;  // resolved Float params (see PointCookCtx)
};
// A texture operator: execute `command` into `output`. No buffer/command return.
using PointTexFn = void (*)(TexCookCtx&);

// FEEDBACK / multi-tex-output cook context (cross-frame ping-pong = TiXL KeepPreviousFrame/SwapTextures).
// A feedback op differs from a normal tex op TWO ways: (1) it can have MULTIPLE Texture2D OUTPUTS that
// return DIFFERENT textures (PreviousFrame vs CurrentFrame), and (2) it may carry CROSS-FRAME state (a
// persistent texture pair + a per-frame-flipping toggle). The driver runs the op ONCE per node per frame
// (a per-frame memo guards a double toggle when both outputs are pulled), hands it the pair + toggle, and
// the op fills `outputs[]` by OUTPUT-port-relative index (0 = first Texture2D output, 1 = second); the
// driver returns the wired output's texture. The op does NOT allocate the pair (the driver does via
// Impl::ensureFeedbackPair — cross-frame lifetime in one place); the op only blits + routes.
struct FeedbackCookCtx {
  MTL::Device* dev = nullptr;
  MTL::Library* lib = nullptr;
  MTL::CommandQueue* queue = nullptr;
  const std::map<std::string, float>* params = nullptr;  // resolved Float params (Keep / EnableSwap)
  // Wired Texture2D inputs in spec port order (same gather as TexCookCtx::inputTextures). For
  // KeepPreviousFrame: inputTextures[0] = ImageA. For SwapTextures: [0]=TextureA, [1]=TextureB.
  static constexpr int kMaxTexInputs = 4;
  const MTL::Texture* inputTextures[kMaxTexInputs] = {nullptr, nullptr, nullptr, nullptr};
  int inputTextureCount = 0;
  // CROSS-FRAME persistent pair (driver-owned; sized to the input's description via ensureFeedbackPair).
  // null for a stateless feedback op (SwapTextures needs no pair). The op blits into the buffer chosen
  // by `*toggle` and routes the other one out (KeepPreviousFrame.cs:56-64).
  MTL::Texture* pairA = nullptr;
  MTL::Texture* pairB = nullptr;
  bool* toggle = nullptr;  // the persistent _bufferToggle (the op reads it, then FLIPS it once per frame)
  // OUTPUTS the op fills, by OUTPUT-port-relative index (0 = first Texture2D output, 1 = second). The
  // driver returns outputs[requestedOutputIdx]. Unset entries stay null (a black sink, no crash).
  static constexpr int kMaxTexOutputs = 4;
  MTL::Texture* outputs[kMaxTexOutputs] = {nullptr, nullptr, nullptr, nullptr};
};
// A feedback op: route inputs + the persistent pair into `outputs[]` (and flip `*toggle`). The driver
// has already sized the pair (if any) to the live input's description, so the op only blits + routes.
using PointFeedbackFn = void (*)(FeedbackCookCtx&);
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
  // (a raw Points preview needs a typed wrapper — future). `targetNodeId` = what the viewport shows; the
  // live loop passes defaultDrawTarget(), a future "pin any node" (view ⊥ graph, TiXL OutputWindow) another.
  void cook(const Graph& g, const EvaluationContext& ctx, const SourceRegistry* reg,
            int targetNodeId);
  // Resident-graph cook (slice 2 walk + 2b parity): walk a ResidentEvalGraph by path-qualified id and
  // realize `targetPath` into target() with the SAME three-flow terminal as cook() (tex/cmd/preview),
  // per-path persistent buffers + stateful state, and driver-resolved Float params (the 2b seam). Float
  // reads go through evalResidentFloat. Proven by --selftest-residentcook + --selftest-residentparity.
  // S5: localTime/localFxTime (BARS) from the Transport (frame_cook) — localTime = playhead (automation
  // samples it), localFxTime = wall clock; `lib` lets Automation drivers resolve curves through the
  // definition-layer Animators (S3). All three default to the pre-S5 placeholder (ctx.time/no lib).
  void cookResident(const struct ResidentEvalGraph& rg, const EvaluationContext& ctx,
                    const SourceRegistry* reg, const std::string& targetPath,
                    float localTimeBars = -1.0f, float localFxTimeBars = -1.0f,
                    const SymbolLibrary* lib = nullptr);
  // Default viewport target = the first draw node (today's wired terminal). 0 if none.
  int defaultDrawTarget(const Graph& g) const;
  // Same, inside ONE symbol's subgraph (the lib-native canvas, 批次 3): the first child
  // whose op realizes (RenderTarget tex > DrawPoints cmd > legacy draw). 0 if none.
  int defaultDrawTarget(const SymbolLibrary& lib, const std::string& symbolId) const;
  MTL::Texture* target() const;
  // Test-support: the count a flat-cooked node's output bag was sized to last cook (0 if never
  // cooked). Lets goldens assert the count-policy driver (e.g. SnapToPoints out == Points1, not
  // sum). flatKey(nodeId) internally.
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
  // value the node produced on its LAST cook, keyed by flatKey(id) into the matching Impl buffer; all
  // are nullptr when the node never cooked that flow, and all are borrowed (PointGraph-owned; valid until
  // the node's next cook). Used by the goldens/selftests (production consumers read the cook channels).
  const std::vector<float>* debugCookedFloatList(int nodeId) const;            // 5th cook: floatListBuf
  const std::vector<simd::float4>* debugCookedColorList(int nodeId) const;     // vec4-list: colorListBuf
  const std::string* debugCookedString(int nodeId) const;                      // 6th cook: stringBuf (MAIN)
  // MULTI-OUTPUT (Sub-seam B): an EXTRA String output keyed by spec output-port index
  // (stringBuf[flatKey(id)+":"+portIdx]); portIdx==0 == debugCookedString. Scalar extras (TotalCount/
  // FileExists) ride Node::outCache[portIdx] (the host-scalar bridge), not here.
  const std::string* debugCookedStringPort(int nodeId, int portIdx) const;
  const std::vector<std::string>* debugCookedStringList(int nodeId) const;     // Sub-seam A: stringListBuf
  const std::vector<::SwPoint>* debugCookedPointList(int nodeId) const;        // 7th cook: pointListBuf
  const SwGradient* debugCookedGradient(int nodeId) const;                     // 8th cook: gradientBuf

  // Cross-frame FEEDBACK output (KeepPreviousFrame / SwapTextures): the texture this node routed to its
  // `ordinal`-th Texture2D OUTPUT last cook (0 = first output = PreviousFrame/TextureA, 1 = second =
  // CurrentFrame/TextureB). `resident` selects the key space (flat "#id" vs resident path "id"). Borrowed
  // (PointGraph-owned, points into the persistent pair or an upstream input); valid until the next cook
  // of that node. nullptr if the node never cooked as a feedback op / ordinal out of range. For goldens.
  MTL::Texture* debugCookedFeedbackOutput(int nodeId, int ordinal, bool resident = false) const;

  // Test-support: the resolution-sized texture a TEXTURE-flow node cooked last (Impl::texBuf, flatKey(id)).
  // Lets the S1 golden assert a NESTED RenderTarget inherited the pushed RequestedResolution, not the
  // window. nullptr if never cooked. Borrowed (do not release).
  MTL::Texture* debugCookedTexture(int nodeId) const;

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
