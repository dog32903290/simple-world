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
  // TEXTURE2D inputs (the texture-into-points seam): a Points op that SAMPLES an upstream cooked
  // Texture2D (SamplePointColorAttributes is the first) gets each wired Texture2D INPUT port (in spec
  // port order) cooked here. inputTextures[i] = the cooked upstream texture of the i-th Texture2D
  // input port, or null if that port is unwired. SAME lifetime contract as `inputs[]` above
  // (PointGraph-owned, single-frame, borrowed — NEVER retained). Mirrors TexCookCtx::inputTextures
  // (the Texture2D flow's gather) but carried on the Points cook flow. null/0 for every existing
  // Points op (no Texture2D input port → byte-identical path).
  static constexpr int kMaxTexInputs = 4;
  const MTL::Texture* inputTextures[kMaxTexInputs] = {nullptr, nullptr, nullptr, nullptr};
  int inputTextureCount = 0;  // number of Texture2D INPUT ports (wired or not), capped at kMaxTexInputs
  // GRADIENT + CURVE host inputs (the bake-into-point seam): a Points op that BAKES host Curve/Gradient
  // values into scratch textures internally then samples per point (MapPointAttributes) gets each wired
  // Gradient/Curve INPUT port (in spec port order) gathered here, the SAME borrowed-single-frame lifetime
  // as inputTextures[] above (PointGraph-owned, never retained). Mirrors TexCookCtx::inputGradients /
  // inputCurves (the Texture2D flow's host-currency gather) but carried on the Points cook flow. In
  // PRODUCTION these are EMPTY/null today (MapPointAttributes' Curve/Gradient ports are host-value inputs
  // with .t3 defaults and have no producer op wired) → the op bakes its embedded defaults, exactly as
  // CurvesToTexture/GradientsToTexture cook their embedded defaults when unwired (NOT the flat-only
  // string-rail trap: no real wire is dropped). A golden injects custom values via these fields to bite
  // the bake. null/empty for every existing Points op (no Gradient/Curve input port → byte-identical).
  const std::vector<SwGradient>* inputGradients = nullptr;
  const std::vector<Curve>* inputCurves = nullptr;
  // RESOLVED Float params of THIS node (slice 2b seam): the cook DRIVER resolves every Float
  // input port through the full value spine — override → binding → wire → stored → spec default
  // (flat), or the resident input's driver (resident) — and hands the result here. Ops read via
  // cookParam/cookVecN and stay graph-model-agnostic (= TiXL: the slot system resolves inputs,
  // the op body never walks the graph). Also fixes wire-blind param reads: a Connection into ANY
  // Float param now drives it, same class as the Count fix (7d4b34e).
  const std::map<std::string, float>* params = nullptr;
  // Resolved params of the node feeding buffer input i (parallel to inputs[]; null if unwired).
  // Replaces the legacy read-by-TYPE evalParam for force ops (firstOfType breaks under reuse):
  // a force op's params travel WITH the wire, like TiXL's force buffer carrying its params.
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

// --- Texture stream (TiXL's Slot<Texture2D>): the THIRD cook flow. A texture op
// (RenderTarget) executes an upstream RenderCommand into a sized texture — this is the
// RESOLUTION PIN point. Output texture is PointGraph-owned (pre-sized, like a buffer op's
// output bag); the op draws into it, does not allocate. ---
//
// A CPU-side render resolution (NOT the 16-byte GPU EvaluationContext — that stays pure
// time/frame). WindowFollow resolves to this; fixed modes ignore it.
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

// FEEDBACK / multi-tex-output cook context (the cross-frame ping-pong flow = TiXL KeepPreviousFrame /
// SwapTextures). A feedback op differs from a normal tex op in TWO ways: (1) it can have MULTIPLE
// Texture2D OUTPUTS that return DIFFERENT textures (PreviousFrame vs CurrentFrame), and (2) it may
// carry CROSS-FRAME state (a persistent texture pair + a toggle that flips each frame). The driver
// runs the op ONCE per node per frame (a per-frame memo guards a double toggle when both outputs are
// pulled), hands it the persistent pair + toggle, and the op fills `outputs[]` by OUTPUT-port-relative
// index (0 = first Texture2D output port, 1 = second). The driver then returns the texture for the
// SPECIFIC output port the consumer wired to. The op does NOT allocate the pair (the driver does, via
// Impl::ensureFeedbackPair, so the cross-frame lifetime stays in one place); the op only blits + routes.
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
void registerPointOp(const std::string& type, PointCookFn,
                     PointStateNewFn = nullptr, PointStateFreeFn = nullptr,
                     PointCountFn countTransform = nullptr,
                     bool countFromFirstPointsInput = false);
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
// WindowFollow (default) returns `windowSize`. Defined in point_ops_rendertarget.cpp; declared
// here so the cook driver can size the node's own texture (the RESOLUTION PIN) before the tex op.
// The map overload is the core (works for flat AND resident resolved params); the Node* overload
// wraps it for flat callers.
RenderResolution resolveRenderResolution(const std::map<std::string, float>& params,
                                         RenderResolution windowSize);
RenderResolution resolveRenderResolution(const Node* n, RenderResolution windowSize);

// --- resolved-param accessors (slice 2b seam; defined in point_graph.cpp) ---
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
  // Cook the sub-graph feeding `targetNodeId` this frame and realize it into target():
  //  - if the target is a draw node (registered PointDrawFn) -> render its Points input;
  //  - else the target is a Points-producing op -> its output bag is cooked but not yet
  //    shown (visualizing a raw Points node needs a typed-preview wrapper — future).
  // `targetNodeId` is what the viewport shows. The single wired terminal is NOT baked in:
  // the live loop passes defaultDrawTarget(); a future "pin any node" (view ⊥ graph, like
  // TiXL's OutputWindow — pin is session state, not a graph edge) just passes another id.
  void cook(const Graph& g, const EvaluationContext& ctx, const SourceRegistry* reg,
            int targetNodeId);
  // Resident-graph cook (slice 2 walk + slice 2b parity): walk a ResidentEvalGraph by
  // path-qualified id and realize `targetPath` into target() with the SAME three-flow terminal
  // as cook() (tex / cmd / preview), per-path persistent output buffers + stateful op state,
  // and driver-resolved Float params handed to ops (the 2b seam). Production still calls
  // cook(Graph&) — the swap is the next cut. Float reads go through evalResidentFloat (no
  // version cache yet: wiring pullResidentFloat in = the swap cut, named-deferred).
  // Proven by --selftest-residentcook (slice-2 walk) + --selftest-residentparity (2b).
  // S5: localTime/localFxTime (BARS) come from the Transport (frame_cook). localTime = playhead
  // (automation samples it); localFxTime = wall clock. `lib` lets the resident eval's Automation
  // drivers resolve curves through the definition-layer Animators (S3 接通). All three default to
  // the pre-S5 placeholder (ctx.time as both clocks, no lib) so the selftest callers are unchanged.
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

 private:
  struct Impl;
  Impl* p_;
};

// Headless RED→GREEN proof of the COOK MACHINERY (not any real kernel): registers
// CPU-fill stub ops under real type names, builds RadialPoints→ParticleSystem→
// DrawPoints, cooks, and asserts the generated bag threaded through the middle op to
// the draw. injectBug makes the middle op ignore its input so the assertion FAILS.
int runPointGraphSelfTest(bool injectBug);

// Headless RED→GREEN proof that cookResident (resident-graph walk) yields the SAME point bag as
// cook (flat-graph walk) for an equivalent graph. injectBug makes the resident walk drop a driver
// so the bags diverge. (resident_cook_selftest.cpp)
int runResidentCookSelfTest(bool injectBug);

// Golden for the ensureState grow rule (state_count_selftest.cpp, refuter-2b promoted repro):
// a stateful op's persistent state must be re-created when the node's count grows past the
// capacity it was born with (else: GPU OOB write over the undersized state buffer), on BOTH
// cook paths. injectBug = the op's stateNew under-allocates -> the overrun detector fires.
int runStateCountSelfTest(bool injectBug);

// Headless RED→GREEN proof of S2 bypass on the GPU cook flows (bypass_cook_selftest.cpp, 修B):
// a bypassed node's MAIN output passes its MAIN input's upstream value through on the Points
// (buffer), Command (chain) and Texture2D (terminal) flows of cookResident — the executor half
// of the honest whitelist (compoundBypassableType). injectBug emulates a cook that ignores the
// flag so the Points passthrough assertion FAILS (teeth).
int runBypassCookSelfTest(bool injectBug);

// Headless RED→GREEN proof of slice-2b parity (resident_cook_parity_selftest.cpp): for each of
// (a) driver-resolved params (stored/override AND wire-driven), (b) stateful op state persisting
// across cooks per path, (c) force params resolved via the WIRED input (not by-type), (d) the
// tex terminal (RenderTarget executor + displayTex), (e) the preview terminal (synthesized
// 1-item chain) — resident cook == flat cook == hand-computed. injectBug makes the stateful stub
// ignore its persistent state -> the across-cooks assertion FAILS (teeth).
int runResidentCookParitySelfTest(bool injectBug);

// Headless RED→GREEN proof that the Cut 50 compute-shader cook seam works in the RESIDENT
// (production) cook path (resident_crop_selftest.cpp): cook a TexSource->Crop resident graph
// through cookResident and assert Crop's displayTex output is SIZED via imageFilterSizeFns() from
// the cooked input (input - margins, not the Resolution pin) AND fully written via its RWTexture2D
// (proving imageFilterComputeTypes()/needsWrite gave the output ShaderWrite). The pre-seam resident
// cook (ensureTex with no needsWrite/sizeFn) FAILS both. injectBug paints the marker off the kept
// rect so the shift probe fails (teeth on the readback).
int runResidentCropSelfTest(bool injectBug);

// FastBlur resident golden (resident_fastblur_selftest.cpp): the FIRST MULTI-PASS COMPUTE leaf driven
// through the RESIDENT (production) cook path. RenderTarget paints a white square on black ->
// FastBlur terminal; cookResident -> cookTexNode -> leaf N down + N up dispatches over per-level
// scratch (cachedScratchTex shaderWrite seam) -> displayTex. Asserts energy conservation (total ~=
// the square's input energy, DC gain 1) + edge softening + full ShaderWrite coverage. injectBug
// paints a SOLID field (no edge) so the edge-softened + square-energy-band probes can't fire -> RED.
int runResidentFastBlurSelfTest(bool injectBug);

// RgbTV resident golden (resident_rgbtv_selftest.cpp): the (E)-seam phase-2 leaf driven through the
// RESIDENT (production) cook path. RenderTarget paints a uniform gray field -> RgbTV terminal;
// cookResident -> cookTexNode (resolves the registered asset key, calls the registered decoder, binds
// the noise texture @t1) -> leaf generates input mips + dispatches the CRT kernel -> displayTex.
// Golden region: GlitchAmount override 0 (kills both noise sources -> deterministic), pins the
// center-row pixels to the flat-cooked GREEN reference + asserts the asset-decode seam fired.
// injectBug drops the RGB stripe (PatternAmount 0) so the pinned pattern pixels diverge -> RED.
int runResidentRgbTvSelfTest(bool injectBug);

// DistortAndShade resident golden (resident_distortandshade_selftest.cpp): the multi-image seam's
// second resident consumer (Displace was first). Two RenderTarget sources (ramp + uniform) -> the op's
// two Texture2D inputs via cookResident -> cookTexNode (recurses BOTH inputs into inputTextures[0/1]) ->
// leaf samples ImageA at the ImageB-driven uv2 -> displayTex. Golden: hand-derived ramp pins on the
// center row. injectBug OMITS the ImageB wire so the multi-image gather loses its 2nd input (ramp
// self-displaces) -> the pins diverge -> RED.
int runResidentDistortAndShadeSelfTest(bool injectBug);

// Combine3Images golden (point_ops_combine3images.cpp): the multi-image seam's FIRST 3-input consumer
// (Displace/DistortAndShade = 2 inputs). Three FLAT solids with distinct channel values -> the op packs
// out.R<-ImageA.r / out.G<-ImageB.g / out.B<-ImageC.b / out.A<-1 (closed-form d=0 plateau). injectBug
// drops ImageC so out.B reads ImageA.b (the fork) -> the B pin diverges -> RED (exercises the 3rd
// Texture2D port). The resident variant drives the same pack through cookResident -> cookTexNode (all
// three inputs into inputTextures[0/1/2]) -> displayTex; injectBug OMITS the ImageC wire.
int runCombine3ImagesSelfTest(bool injectBug);
int runResidentCombine3ImagesSelfTest(bool injectBug);

// CombineMaterialChannels2 golden (point_ops_combinematerialchannels2.cpp): the PBR twin of
// Combine3Images — SAME kernel (img-combine-3.hlsl), SAME 3-image gather. A PBR-flavored solid set
// (roughness/metallic/ao) packs to (A.r, B.g, C.b, 1); injectBug drops ImageC -> B reads ImageA.b -> RED.
// The resident variant drives it through cookResident with the ImageC wire omitted on injectBug.
int runCombineMaterialChannels2SelfTest(bool injectBug);
int runResidentCombineMaterialChannels2SelfTest(bool injectBug);

// Blur image-filter golden (point_ops_blur.cpp, lane I): the FIRST image filter (Texture2D in ->
// Texture2D out). (a) BLUR MATH: fill a source texture with a hard 1px-wide vertical white line on
// black, run Blur, assert the line SPREADS horizontally (neighbouring columns lit) — a no-op /
// passthrough leaves them black. (b) GATHER DIRECT-THROUGH: build RadialPoints->DrawPoints->
// RenderTarget->Blur through PointGraph::cook and assert the terminal texture is non-empty (the
// RenderTarget's Texture2D output really reached the Blur input). injectBug makes the blur write
// the center tap only (Size 0) so the spread assertion FAILS (teeth).
int runBlurSelfTest(bool injectBug);
int runBlurChainSelfTest(bool injectBug);

// Displace image-filter golden (point_ops_displace.cpp, lane D2): the SECOND image filter and the
// FIRST op with TWO Texture2D inputs (Image + DisplaceMap). (a) DISPLACE MATH: Image = a vertical
// edge, DisplaceMap = a horizontal ramp; with Displacement!=0 the edge MOVES vs a no-warp baseline
// (passthrough leaves it put). (b) MULTI-INPUT GATHER: build two RenderTarget legs feeding Displace's
// Image + DisplaceMap and cook through PointGraph::cook (flat + resident); assert the terminal texture
// is sized + non-empty (both RenderTargets threaded into Displace's two inputs). injectBug zeroes
// Displacement (math) / drops the Image wire (chain) so the assertion FAILS (teeth).
int runDisplaceSelfTest(bool injectBug);
int runDisplaceChainSelfTest(bool injectBug);

// Blend / BlendWithMask image-filter goldens (point_ops_blend.cpp / point_ops_blendwithmask.cpp, lane
// multi-image, image/use): the THIRD/FOURTH multi-image-seam consumers (Displace/DistortAndShade first).
// Blend composites ImageA+ImageB (two Texture2D inputs); BlendWithMask is the FIRST op with THREE
// Texture2D inputs (ImageA+ImageB+Mask). The *SelfTest fns (flat closed-form blend math on solid inputs)
// self-register via the imageFilterSelfTests() sink ("blend"/"blendwithmask"); the *ChainSelfTest fns
// (flat-chain + RESIDENT production golden, the multi-Texture2D gather承重線) are kTable rows below.
// injectBug drops the ImageB wire so the multi-image gather loses its 2nd input -> the mixed-color pins
// collapse -> RED. (runBlend*SelfTest declared here next to the goldens, same precedent as Displace.)
int runBlendSelfTest(bool injectBug);
int runBlendChainSelfTest(bool injectBug);
int runBlendWithMaskSelfTest(bool injectBug);
int runBlendWithMaskChainSelfTest(bool injectBug);

// Tint image-filter golden (point_ops_tint.cpp, lane F3-1): (a) TINT MATH: solid grey ->
// red-ramp tint (Amount=1, MapWhite=(1,0,0,1)); center R>64 & G<96. injectBug Amount=0 ->
// passthrough grey -> FAIL. (b) TINT CHAIN: RadialPoints->DrawPoints->RenderTarget->Tint through
// cook (flat + resident); Tint is terminal, texture non-black. injectBug drops RT->Tint wire -> FAIL.
int runTintSelfTest(bool injectBug);
int runTintChainSelfTest(bool injectBug);

// ChromaticAbberation image-filter golden (point_ops_chromab.cpp, lane F3-2): (a) SHIFT MATH:
// white center stripe; R and B channels inside the stripe become asymmetric (left vs right)
// due to the radial fringe offset. injectBug Size=0 (no fringe) -> symmetric -> FAIL.
int runChromaBAShiftSelfTest(bool injectBug);

// AdjustColors image-filter golden (point_ops_adjustcolors.cpp, lane F3-3): (a) HSB MATH:
// solid red input; Saturation=0 -> greyscale (R≈G≈B within 30, all >60). injectBug Sat=1 ->
// red stays red (R>>G) -> FAIL.
int runAdjustColorsSelfTest(bool injectBug);

// SamplePointColorAttributes golden (point_ops_samplepointcolorattributes.cpp): the FIRST Points op
// with a Texture2D INPUT — the proving op for the texture-into-points seam (PointCookCtx::inputTextures).
// A point's Color is BlendColors(p.Color, sample*BaseColor, Mode). Closed-form (BOTH cook paths, R-2):
// input bag Color=(0,0,0,0), a UNIFORM texture=(1,0,0,1), BaseColor=(1,1,1,1), Mode=0 Normal ->
// sample c=(1,0,0,1) at EVERY uv (uniform texture is coordinate-independent => identity-transform fork
// holds) -> BlendColors Normal: a=1, rgb=(1,0,0) -> every point Color=(1,0,0,1). The FLAT leg reads the
// cooked Points buffer back byte-for-byte (debugCookedBuffer); the RESIDENT leg drives the seam through
// cookResident -> cookNode's Texture2D gather -> the op samples -> DrawPoints->RenderTarget and reads the
// rendered RED pixels (production path; no resident Points-buffer accessor). injectBug: DROP the texture
// bind (inputTextureCount=0 / OMIT the wire) -> sample=(0,0,0,0) -> Color stays (0,0,0,0); want FIXED at
// (1,0,0,1) -> RED.
int runSamplePointColorAttributesSelfTest(bool injectBug);

// AttributesFromImageChannels — texture-into-points seam consumer that ROUTES sampled channels into
// point attributes (position/F1/F2/rotate/scale) via per-channel Factor/Offset gains. Golden: ROUTING
// direct-cook leg (R->Position_X / G->Position_Y with non-identity gains -> want pos=(0.50,0.80,0)) +
// FLAT-DRIVER gather leg (PointGraph::cook + debugCookedBuffer, Red->Scale_Uniform) + RESIDENT leg
// (cookResident, grown sprites). injectBug drops the texture bind -> passthrough -> RED.
int runAttributesFromImageChannelsSelfTest(bool injectBug);

// LinearSamplePointAttributes — texture-into-points seam consumer that samples the texture along the
// point INDEX (uv = (i/pointCount, 0.5) — a 1D LINEAR strip, NO position-derived uv) and ROUTES the
// sampled channels into point attributes (position/F1/rotate/stretch/F2) via per-channel Factor/Offset
// gains. Golden (2 legs, R-2): FLAT direct-cook leg (uniform red, Red->For_X, RedFactor=1 -> every
// point's X shifts +1, closed-form byte-read) + RESIDENT leg (cookResident, the lit ring centroid
// shifts RIGHT vs the no-texture baseline). injectBug drops the texture bind -> passthrough -> RED.
int runLinearSamplePointAttributesSelfTest(bool injectBug);

// MapPointAttributes — the bake-into-point seam consumer (PointCookCtx::inputCurves/inputGradients). A
// count-preserving MODIFIER that BAKES its host Curve (→ R32_Float CurveImage) + Gradient (→ RGBA32
// GradientImage, .t3 resolution 512) into two scratch textures during cook, then per point samples both
// at (f,0.5) — f = the InputMode→MappingMode-remapped coordinate — to write a curve value into FX1/FX2/
// Scale (WriteTo) + a gradient color into Color (WriteColor, default Multiply). Faithful to
// external/tixl .../point/modify/MapPointAttributes.{cs,hlsl,t3} (the .t3 compound bakes the host inputs
// via CurvesToTexture/GradientsToTexture + FirstValidTexture, not straight wires — read the .hlsl cbuffer
// directly). Golden (4 legs, R-2): a hand-built ctx injects a known 2-key curve + 2-step gradient and
// byte-reads FX1 (== curve.r at f) AND Color (== gradient at f, multiplied) — hand-computed; one leg
// through PointGraph::cook (flat) + one through cookResident (production, embedded defaults baked).
// injectBug omits the gradient scratch bind → Color = white passthrough (≠ the injected gradient color);
// want FIXED at the true values.
int runMapPointAttributesSelfTest(bool injectBug);

}  // namespace sw
