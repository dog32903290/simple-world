// runtime/point_ops_draw_explicit_golden — Seam 2 DRAW (explicit-draw terminal) goldens.
//
// Draw is a Command SOURCE (no upstream Command, like ClearRenderTarget) that emits ONE DrawKind::Explicit item
// carrying the raw deviceContext.Draw(VertexCount, VertexStartLocation). Two teeth, both on-machine, NO reference
// frame (the count/baseVertex plumbing + the IA-topology composition are closed-form):
//
//   1. --selftest-draw-explicit  (BOTH-LEG COOK-THROUGH, blood-lesson: no繞-cook struct-stuffing): a real Draw
//      NODE with VertexCount=12 / VertexStartLocation=4, cooked THROUGH the flat AND resident legs; the emitted
//      item must be DrawKind::Explicit with explicitVertexCount/explicitBaseVertex == the census values the op
//      READ off the node (a param-name typo → cookParam default 3/0 → RED), and the two legs must be BYTE-
//      IDENTICAL (a resident-only miss = a silent wrong draw-call). -bug strips the params (cook reads 3/0).
//
//   2. --selftest-draw-explicit-topology  (COMPOSITION, resident): InputAssemblerStage(LineList) WRAPS the Draw
//      leaf; the stamped Explicit item's frozen.topology must be LineList (proves IA stamps the raw draw's
//      primitive). -bug drops the IA topology (strips the param → default TriangleList) → RED.
#include "runtime/point_ops.h"

#include "runtime/dx11_metal_state_map.h"  // Dx11Topology
#include "runtime/point_graph.h"           // PointGraph::cook/cookResident, registerBuiltinPointOps
#include "runtime/render_command.h"        // RenderCommand / RenderDrawItem / DrawKind::Explicit / renderStateCaptureForTest

#include <cstdint>
#include <cstdio>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph/Node/pinId
#include "runtime/graph_bridge.h"         // libFromGraph
#include "runtime/resident_eval_graph.h"  // buildEvalGraph
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// Draw(1) → RenderTarget(2). The Draw source emits a DrawKind::Explicit item; the capture reads it back. mode
// picks whether the Draw node carries non-default VertexCount/VertexStart or is STRIPPED (cook reads .t3 defaults
// 3/0, models a param-name typo → RED). The census values 12/4 are genuine non-defaults (defaults are 3/0).
enum class DrawMode { kNonDefault, kStrip };
Graph buildDrawGraph(DrawMode mode) {
  Graph g;
  Node dr; dr.id = 1; dr.type = "Draw";
  if (mode == DrawMode::kNonDefault) { dr.params["VertexCount"] = 12.0f; dr.params["VertexStartLocation"] = 4.0f; }
  g.nodes.push_back(dr);
  Node rt; rt.id = 2; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = 256.0f; rt.params["CustomH"] = 256.0f;
  g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // Draw.out(Command) → RenderTarget.command
  return g;
}

// Draw(1) → InputAssemblerStage(2, topology) → RenderTarget(3): the IA wraps the raw Draw and stamps topology
// onto its Explicit item. mode strip → IA reads TriangleList default (the -bug case for the topology tooth).
enum class ComposeMode { kLineList, kStrip };
Graph buildComposeGraph(ComposeMode mode) {
  Graph g;
  Node dr; dr.id = 1; dr.type = "Draw"; dr.params["VertexCount"] = 12.0f; g.nodes.push_back(dr);
  Node ia; ia.id = 2; ia.type = "InputAssemblerStage";
  if (mode == ComposeMode::kLineList) ia.params["PrimitiveTopology"] = 1.0f;  // LineList (default 3=TriangleList)
  g.nodes.push_back(ia);
  Node rt; rt.id = 3; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = 256.0f; rt.params["CustomH"] = 256.0f;
  g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // Draw.out → InputAssembler.command
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // InputAssembler.out → RenderTarget.command
  return g;
}

// Cook `g` through ONE leg and capture the first item (kind + explicit fields + frozen).
void cookAndCapture(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, const Graph& g, bool resident,
                    const char* rtPath, RenderDrawItem& out, bool& got) {
  const uint32_t W = 256, H = 256;
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  RenderCommand cap;
  renderStateCaptureForTest() = &cap;
  if (resident) {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    PointGraph pg(dev, lib, q, W, H);
    pg.cookResident(rg, ctx, nullptr, rtPath);
  } else {
    PointGraph pg(dev, lib, q, W, H);
    int term = pg.defaultDrawTarget(g);
    pg.cook(g, ctx, nullptr, term);
  }
  renderStateCaptureForTest() = nullptr;
  got = !cap.items.empty();
  if (got) out = cap.items.front();
}

}  // namespace

// --selftest-draw-explicit: real Draw NODE (VertexCount=12/Start=4) cooked THROUGH both legs; the item must be
// DrawKind::Explicit with the census count/baseVertex the op READ (typo → default 3/0 → RED) + byte-identical.
int runDrawExplicitSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-draw-explicit] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();

  Graph g = buildDrawGraph(injectBug ? DrawMode::kStrip : DrawMode::kNonDefault);
  RenderDrawItem flatIt{}, resIt{};
  bool flatGot = false, resGot = false;
  cookAndCapture(dev, lib, q, g, /*resident=*/false, "1", flatIt, flatGot);
  cookAndCapture(dev, lib, q, g, /*resident=*/true, "2", resIt, resGot);

  // The captured fields are what cookDrawExplicit READ off the node — NOT a hand-set struct. Assert the FIXED
  // census combo (12/4) in the no-bug run; -bug strips → cook reads 3/0 → the 12/4 assertion fails → RED.
  const uint32_t kCount = 12, kBase = 4;
  bool cookThrough = flatGot && resGot &&
                     resIt.kind == DrawKind::Explicit && flatIt.kind == DrawKind::Explicit &&
                     resIt.explicitVertexCount == kCount && resIt.explicitBaseVertex == kBase &&
                     flatIt.explicitVertexCount == kCount && flatIt.explicitBaseVertex == kBase;
  bool identical = flatIt.kind == resIt.kind && flatIt.explicitVertexCount == resIt.explicitVertexCount &&
                   flatIt.explicitBaseVertex == resIt.explicitBaseVertex;
  bool ok = cookThrough && identical;

  std::printf("[selftest-draw-explicit] flat{kind=%u vc=%u base=%u} res{kind=%u vc=%u base=%u} (want kind=%u "
              "vc=12 base=4) identical=%d -> %s\n", (uint32_t)flatIt.kind, flatIt.explicitVertexCount,
              flatIt.explicitBaseVertex, (uint32_t)resIt.kind, resIt.explicitVertexCount, resIt.explicitBaseVertex,
              (uint32_t)DrawKind::Explicit, identical, ok ? "PASS(cook-through)" : "tripped");

  lib->release(); q->release(); dev->release(); pool->release();
  if (injectBug) return 1;  // -bug: params stripped → cook read 3/0 → cookThrough false → RED
  return ok ? 0 : 1;
}

// --selftest-draw-explicit-topology: InputAssemblerStage(LineList) WRAPS the Draw leaf; the stamped Explicit
// item's frozen.topology must be LineList (proves IA stamps the raw draw's primitive). -bug strips the IA param
// → the cook reads TriangleList (default) → the LineList assertion fails → RED.
int runDrawExplicitTopologySelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-draw-explicit-topology] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();

  Graph g = buildComposeGraph(injectBug ? ComposeMode::kStrip : ComposeMode::kLineList);
  RenderDrawItem it{};
  bool got = false;
  cookAndCapture(dev, lib, q, g, /*resident=*/true, "3", it, got);

  // The IA op stamped topology onto the Draw leaf's Explicit item. no-bug → LineList; -bug strips → TriangleList
  // (the .t3 default) → the LineList assertion fails → RED. Also assert the item stayed DrawKind::Explicit +
  // carried its VertexCount through the IA wrapper (the stamp copies the subtree items, must not mangle them).
  bool ok = got && it.kind == DrawKind::Explicit && it.hasRenderState &&
            it.frozen.topology == (uint32_t)Dx11Topology::LineList &&
            it.explicitVertexCount == 12;

  std::printf("[selftest-draw-explicit-topology] kind=%u stamped=%d topology=%u (want %u=LineList) vc=%u -> %s\n",
              (uint32_t)it.kind, it.hasRenderState, it.frozen.topology, (uint32_t)Dx11Topology::LineList,
              it.explicitVertexCount, ok ? "PASS(compose)" : "tripped");

  lib->release(); q->release(); dev->release(); pool->release();
  if (injectBug) return 1;  // -bug: IA param stripped → cook read TriangleList → topology assertion false → RED
  return ok ? 0 : 1;
}

}  // namespace sw
