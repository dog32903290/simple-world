// runtime/point_graph_cook_ctx — the cook-context ABI for the point-buffer cook.
//
// Split out of point_graph.h (which was at its ratchet line cap, zero margin) so the
// cook-ctx structs the ~249 includers depend on have room to grow (e.g. a new cook input
// field) without touching the busy point_graph.h. point_graph.h #includes this in the exact
// spot these definitions used to live, so every existing includer sees the identical symbols
// (ABI-preserving — behavior unchanged).
//
// Contents: the FOUR cook-context structs (PointCookCtx / CmdCookCtx / TexCookCtx /
// FeedbackCookCtx) + their cook function-pointer typedefs (PointCookFn / PointDrawFn /
// PointCmdFn / PointTexFn / PointFeedbackFn). The PointGraph class, registration functions,
// resolution helpers and resolved-param accessors stay in point_graph.h.
//
// runtime leaf (ARCHITECTURE.md): no upward deps. Self-contained / independently compilable.
#pragma once
#include <cstdint>
#include <map>
#include <memory>  // std::shared_ptr (PointCookCtx/TexCookCtx::inputFieldTree, field-into-cook seam)
#include <string>
#include <vector>

#include "runtime/render_command.h"  // RenderCommand (CmdCookCtx returns it by value)

namespace MTL {
class Device;
class Library;
class CommandQueue;
class Buffer;
class Texture;
}  // namespace MTL

struct EvaluationContext;  // runtime/eval_context.h (full def in point_graph.cpp)
namespace sw { struct ContextVarMap; }  // stateful_value_ops.h (host per-frame var map; S3a)
namespace sw { struct SwGradient; }  // runtime/sw_gradient.h (host Gradient; full def where the op includes it)
namespace sw { class Curve; }        // runtime/curve.h (host Curve currency; full def where the op includes it)
namespace sw { struct FieldNode; }   // runtime/field_graph.h (FieldNode tree; full def in the builder + PF-a cook TU)

namespace sw {

struct Graph;
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
  // FIELD input (PF-0 field-into-force seam): a force op wired to a field op (VectorFieldForce <-
  // ToroidalVortexField.Result) gets the assembled FieldNode tree here (gatherForceFieldTree flat /
  // gatherForceResidentFieldTree resident). Borrowed-single-frame like inputGradients. null = no wired
  // Field → byte-identical (PF-a's kernel still bakes (1,1,1)). v1 single slot [fork-VFF-singlefield].
  std::shared_ptr<FieldNode> inputFieldTree = nullptr;
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
  // First wired Texture2D input (FORK#1, the Command-path texture gather): the cook driver cooks the
  // upstream tex op (RenderTarget/Blur) into ITS own texture and hands the result here. DrawScreenQuad
  // borrows it; point-only ops (DrawPoints) ignore it. null when unwired. Borrowed/single-frame, NEVER
  // retained (same contract as `points`). Mirrors TexCookCtx::inputTexture but on the Command flow.
  const MTL::Texture* inputTexture = nullptr;
  // First wired COMMAND input subtree (Camera op, Cut 3): a command op that wraps a Command subtree
  // (TiXL Camera.Command input) gets the upstream chain cooked here (the driver recurses the upstream
  // command node, mirrors inputTexture). null when unwired. Borrowed pointer into a driver-local
  // RenderCommand (single-frame); the op may COPY its items. Non-wrappers leave it null → byte-identical.
  const RenderCommand* inputCommand = nullptr;
  // S3a context-var bridge (flow seam): LIVE host var map (= TiXL EvaluationContext.Float/IntVariables),
  // threaded into EVERY command cook (mirrors inputCommand) so a Command-rail SetFloatVarCmd/SetIntVarCmd
  // WRITES a scoped var around its SubGraph; a Command op cooked inside reads cc.ctxVars->floatVars[name].
  // null for ~243 golden callers. NAMED FORK (#3): value-rail GetFloatVar reads extOut, NOT this map.
  ContextVarMap* ctxVars = nullptr;
  // First wired MESH input (DrawMeshUnlit, the 4th cook flow): the cook driver cooks the upstream mesh node
  // (NGonMesh/QuadMesh) and hands its vertex+index buffers + face count here. Borrowed/single-frame, NEVER
  // retained (same contract as `points`); every non-mesh op leaves them null/0 → byte-identical.
  const MTL::Buffer* meshVtx = nullptr;   // upstream SwVertex buffer (null when no Mesh input wired)
  const MTL::Buffer* meshIdx = nullptr;   // upstream SwTriIndex buffer
  uint32_t meshFaceCount = 0;             // upstream FACE count (== SwTriIndex count); VS draws ×3
  const std::map<std::string, float>* params = nullptr;  // resolved Float params (see PointCookCtx)
  // CAMERA bridge (camera→CmdCookCtx, camera3d-remaining #1): the cook driver consults the C1
  // LiveCameraScope (liveActiveCamera) at this cc-fill and surfaces the live Camera's matrices so a
  // Command-rail op (RotateTowards FORK#2 / GetScreenPos / GetPosition) reads WorldToCamera/CameraToWorld
  // instead of TiXL's default. ROW-MAJOR float[16] (mirrors PointCookCtx::objectToCamera/cameraToWorld).
  // hasCamera=false (no Camera in scope) → every op that ignores them is byte-identical. POPULATED ON
  // BOTH cook legs identically (the S2c flat-resident mirror gate — resident is production).
  bool hasCamera = false;
  float worldToCamera[16] = {0};  // == ObjectToCamera (identity O2W); LookAtRH(eye,target,up)
  float cameraToWorld[16] = {0};  // inverse(WorldToCamera); camera WORLD pos = transform((0,0,0), this)
};
// A command operator: read the upstream point bag (+ Float params) → return a RenderCommand.
using PointCmdFn = RenderCommand (*)(CmdCookCtx&);

// --- Texture stream (TiXL's Slot<Texture2D>): the THIRD cook flow. A texture op (RenderTarget) executes
// an upstream RenderCommand into a sized texture — the RESOLUTION PIN. Output texture is PointGraph-owned
// (pre-sized, like a buffer op's output bag); the op draws into it, does not allocate. ---
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
  // FIELD input (RaymarchField tex-op seam): cookResidentTexNode/cookFlatTexNode gather the wired Field
  // input into an assembled FieldNode tree and hand it here, then the tex-op cook body consumes it (mirrors
  // PointCookCtx::inputFieldTree, the PF-0 force-tree precedent). Borrowed-single-frame like the other
  // gathered inputs; never retained. null = no wired Field → byte-identical for every existing tex op.
  std::shared_ptr<FieldNode> inputFieldTree = nullptr;
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

}  // namespace sw
