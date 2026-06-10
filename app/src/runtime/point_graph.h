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
#include <string>

#include "runtime/render_command.h"  // RenderCommand (the Command stream's currency)

namespace MTL {
class Device;
class Library;
class CommandQueue;
class Buffer;
class Texture;
}  // namespace MTL

struct EvaluationContext;  // runtime/eval_context.h (full def in point_graph.cpp)

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
  const Graph* graph = nullptr;            // to resolve Float params via evalParam
  const SourceRegistry* reg = nullptr;     // live-source/override for Float params
  int nodeId = 0;
  uint32_t count = 0;                      // this node's point count
  const MTL::Buffer* const* inputs = nullptr;
  const uint32_t* inputCounts = nullptr;   // per-input point count (parallel to inputs[]; combine ops
                                           // concat by these offsets; unwired input = 0)
  int inputCount = 0;
  MTL::Buffer* output = nullptr;           // PointGraph-owned; cook writes here
  void* state = nullptr;                   // per-node persistent state (stateful ops)
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
  MTL::Texture* output = nullptr;          // PointGraph-owned, pre-sized; op draws here
};
// A texture operator: execute `command` into `output`. No buffer/command return.
using PointTexFn = void (*)(TexCookCtx&);
// Per-node persistent state lifetime for stateful ops (e.g. a sim's particle buffer).
// `count` is the node's point count at creation. Return nullptr for stateless ops.
using PointStateNewFn = void* (*)(MTL::Device*, MTL::Library*, uint32_t count);
using PointStateFreeFn = void (*)(void*);

// Register a node type's cook/draw fn (Metal-side; separate from NodeSpec.evaluate,
// which is the pure-float path). registerPointOp's state fns are optional (stateless
// ops omit them). Registration is explicit (call registerBuiltinPointOps() at app
// init) — NOT a static initializer — so --selftest-* runs see a clean table.
void registerPointOp(const std::string& type, PointCookFn,
                     PointStateNewFn = nullptr, PointStateFreeFn = nullptr);
void registerDrawOp(const std::string& type, PointDrawFn);
// Register a command op (the Command stream). Separate table from cook/draw — exactly
// as TiXL keeps Slot<Command> distinct from Slot<BufferWithViews>.
void registerCmdOp(const std::string& type, PointCmdFn);
// Register a texture op (the Texture2D stream — RenderTarget). Third table.
void registerTexOp(const std::string& type, PointTexFn);
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
  // Default viewport target = the first draw node (today's wired terminal). 0 if none.
  int defaultDrawTarget(const Graph& g) const;
  MTL::Texture* target() const;

 private:
  struct Impl;
  Impl* p_;
};

// Headless RED→GREEN proof of the COOK MACHINERY (not any real kernel): registers
// CPU-fill stub ops under real type names, builds RadialPoints→ParticleSystem→
// DrawPoints, cooks, and asserts the generated bag threaded through the middle op to
// the draw. injectBug makes the middle op ignore its input so the assertion FAILS.
int runPointGraphSelfTest(bool injectBug);

// Headless proof of the COMMAND STREAM machinery (batch 0 of the render-target pivot):
// registers a stub command op, cooks it via a CmdCookCtx, and asserts it returns a
// 1-item RenderCommand carrying the upstream count. Proves the cmd registry + call +
// record return work WITHOUT touching cook() or the buffer flow. injectBug drops the
// item so the size assertion FAILS (RED) before we trust GREEN.
int runCommandStreamSelfTest(bool injectBug);

}  // namespace sw
