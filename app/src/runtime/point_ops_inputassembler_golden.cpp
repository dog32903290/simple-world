// runtime/point_ops_inputassembler_golden — Seam 2 INPUT-ASSEMBLER goldens (topology STAGE).
//
// Two teeth, both on-machine, NO reference frame (parity-without-reference-frame: topology is Bucket-A
// closed-form — a flat enum→enum table with an exact picture-free answer):
//
//   1. --selftest-inputassembler  (CLOSED-FORM): the D3D_PRIMITIVE_TOPOLOGY → MTL::PrimitiveType table
//      (dx11_metal_state_map.h metalPrimitiveType) equals the real MTL:: enums for every row, asserted at
//      COMPILE time (static_asserts) + a runtime sweep so --selftest surfaces a PASS row. -bug corrupts a row.
//
//   2. --selftest-inputassembler-cookthrough  (BOTH-LEG + COOK-THROUGH, blood-lesson: no繞-cook struct-stuffing):
//      a real InputAssemblerStage NODE with PrimitiveTopology=LineList set, cooked THROUGH the production
//      resident leg AND the flat leg; the captured stamped frozen.topology must equal the census value
//      cookInputAssembler READ off the node (a param-name typo → cookParam default TriangleList → RED), and the
//      two legs must be BYTE-IDENTICAL (a resident-only topology-stamp miss = a silent wrong-primitive draw).
#include "runtime/point_ops.h"

#include "runtime/dx11_metal_state_map.h"  // Dx11Topology / metalPrimitiveType under assert
#include "runtime/point_graph.h"           // PointGraph::cook/cookResident, registerBuiltinPointOps
#include "runtime/render_command.h"        // FrozenRenderState / renderStateCaptureForTest

#include <cstdint>
#include <cstdio>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph/Node/pinId
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph → SymbolLibrary)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// ───────── CLOSED-FORM: the topology table == the real MTL::PrimitiveType enums (compile-time ABI lock) ─────────
static_assert(metalPrimitiveType(Dx11Topology::PointList) == (uint32_t)MTL::PrimitiveTypePoint, "");
static_assert(metalPrimitiveType(Dx11Topology::LineList) == (uint32_t)MTL::PrimitiveTypeLine, "");
static_assert(metalPrimitiveType(Dx11Topology::LineStrip) == (uint32_t)MTL::PrimitiveTypeLineStrip, "");
static_assert(metalPrimitiveType(Dx11Topology::TriangleList) == (uint32_t)MTL::PrimitiveTypeTriangle, "");
static_assert(metalPrimitiveType(Dx11Topology::TriangleStrip) == (uint32_t)MTL::PrimitiveTypeTriangleStrip, "");

namespace {

// Build RadialPoints(1) → DrawPoints(2) → InputAssemblerStage(3, topology) → RenderTarget(4,256²). The IA op
// STAMPS the topology onto the DrawPoints item; the capture reads frozen.topology back. mode picks whether the
// IA node carries a NON-DEFAULT topology (LineList, not the default TriangleList) or is STRIPPED (cook reads the
// cookParam default = TriangleList, models a param-name typo → RED for the cook-through assertion).
enum class IAMode { kNonDefault, kStrip };
Graph buildIAGraph(IAMode mode) {
  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints"; gen.params["Count"] = 64.0f; gen.params["Radius"] = 1.5f;
  g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  Node ia; ia.id = 3; ia.type = "InputAssemblerStage";
  if (mode == IAMode::kNonDefault) ia.params["PrimitiveTopology"] = 1.0f;  // LineList (default index 3=TriangleList)
  g.nodes.push_back(ia);
  Node rt; rt.id = 4; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = 256.0f; rt.params["CustomH"] = 256.0f;
  g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points → DrawPoints.points
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // DrawPoints.out(Command) → InputAssembler.command
  g.connections.push_back({103, pinId(3, 1), pinId(4, 0)});  // InputAssembler.out(Command) → RenderTarget.command
  return g;
}

// Cook `g` through ONE leg (flat if resident=false, else production resident) and read the first item's tuple.
void cookAndCapture(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, const Graph& g, bool resident,
                    FrozenRenderState& out, bool& stamped, bool& got) {
  const uint32_t W = 256, H = 256;
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  RenderCommand cap;
  renderStateCaptureForTest() = &cap;
  if (resident) {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    PointGraph pg(dev, lib, q, W, H);
    pg.cookResident(rg, ctx, nullptr, /*RenderTarget path=*/"4");
  } else {
    PointGraph pg(dev, lib, q, W, H);
    int term = pg.defaultDrawTarget(g);
    pg.cook(g, ctx, nullptr, term);
  }
  renderStateCaptureForTest() = nullptr;
  got = !cap.items.empty();
  stamped = got && cap.items.front().hasRenderState;
  if (got) out = cap.items.front().frozen;
}

}  // namespace

// --selftest-inputassembler: the closed-form D3D→MTL primitive-type sweep (Bucket-A, no picture). -bug corrupts
// the TriangleList row so the sweep trips. (The static_asserts above already lock the table at compile time; the
// runtime sweep gives --bite a PASS row + the -bug tooth.)
int runInputAssemblerSelfTest(bool injectBug) {
  struct Row { Dx11Topology t; uint32_t mtl; };
  Row rows[] = {
      {Dx11Topology::PointList, (uint32_t)MTL::PrimitiveTypePoint},
      {Dx11Topology::LineList, (uint32_t)MTL::PrimitiveTypeLine},
      {Dx11Topology::LineStrip, (uint32_t)MTL::PrimitiveTypeLineStrip},
      {Dx11Topology::TriangleList, (uint32_t)MTL::PrimitiveTypeTriangle},
      {Dx11Topology::TriangleStrip, (uint32_t)MTL::PrimitiveTypeTriangleStrip},
  };
  bool ok = true;
  for (const Row& r : rows) { if (metalPrimitiveType(r.t) != r.mtl) ok = false; }
  if (injectBug) { ok = (metalPrimitiveType(Dx11Topology::TriangleList) == (uint32_t)MTL::PrimitiveTypePoint); }
  std::printf("[selftest-inputassembler] 5 topology rows (PointList..TriangleStrip) -> %s\n",
              ok ? (injectBug ? "faithful(BUG-SHOULD-TRIP)" : "PASS") : "tripped");
  if (injectBug) return 1;  // -bug: the corrupt row makes ok false → RED
  return ok ? 0 : 1;
}

// --selftest-inputassembler-cookthrough: real InputAssemblerStage NODE (topology=LineList) cooked THROUGH both
// legs; the stamped frozen.topology must equal the census value the op READ (typo → cookParam default → RED) AND
// the two legs must be byte-identical (resident-only stamp miss = silent wrong-primitive). -bug strips the param.
int runInputAssemblerCookThroughSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-inputassembler-cookthrough] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();

  // -bug strips the topology param → cookInputAssembler reads the cookParam default (TriangleList=3=default).
  Graph g = buildIAGraph(injectBug ? IAMode::kStrip : IAMode::kNonDefault);

  FrozenRenderState flatF, resF;
  bool flatStamp = false, flatGot = false, resStamp = false, resGot = false;
  cookAndCapture(dev, lib, q, g, /*resident=*/false, flatF, flatStamp, flatGot);
  cookAndCapture(dev, lib, q, g, /*resident=*/true, resF, resStamp, resGot);

  // The captured topology is what cookInputAssembler READ off the node and STAMPED — NOT a hand-set struct.
  // Assert the FIXED census value (LineList) in the no-bug run; -bug strips → cook reads TriangleList → the
  // LineList assertion fails → RED. LineList (not TriangleList) is a genuine non-default (the .t3 default is
  // TriangleList) so a dropped stamp is also caught. Then assert the two legs are byte-identical.
  bool cookThrough = flatGot && flatStamp && resGot && resStamp &&
                     resF.topology == (uint32_t)Dx11Topology::LineList &&
                     flatF.topology == (uint32_t)Dx11Topology::LineList;
  bool identical = flatF.topology == resF.topology;
  bool ok = cookThrough && identical;

  std::printf("[selftest-inputassembler-cookthrough] flat.topology=%u res.topology=%u (want %u=LineList) "
              "flatStamp=%d resStamp=%d identical=%d -> %s\n", flatF.topology, resF.topology,
              (uint32_t)Dx11Topology::LineList, flatStamp, resStamp, identical,
              ok ? "PASS(cook-through)" : "tripped");

  lib->release(); q->release(); dev->release(); pool->release();
  if (injectBug) return 1;  // -bug: params stripped → cook read TriangleList default → cookThrough false → RED
  return ok ? 0 : 1;
}

}  // namespace sw
