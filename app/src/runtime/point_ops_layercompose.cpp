// S2c LAYER-COMPOSE golden — the end-to-end proof that the S2a MultiInput Command collector composites
// N Layer2d layers into ONE image, byte-faithful to TiXL's Execute + Layer2d compose semantics
// (blend order = wire order, alpha-over / additive). NO new cook-core code: S2c is a HARNESS cut. It
// drives the ALREADY-LANDED keystone (cookCommand's MultiInput Command branch, point_ops_execute.cpp +
// point_graph.cpp:449-464 flat / point_graph_resident.cpp:483-502 resident) through a REAL graph:
//
//     SolidA(Texture2D) ─┐
//                        ├→ Execute(Command MultiInput) → RenderTarget(Texture2D)
//     SolidB(Texture2D) ─┘     (Layer2d wraps each Solid into a full-frame Command)
//
// i.e. SolidA → Layer2dA → Execute.Command[wire0], SolidB → Layer2dB → Execute.Command[wire1].
//
// ── TiXL GROUND TRUTH (the compose math this golden derives expected values from) ──
//   Execute (Operators/Lib/flow/Execute.cs:14-39): three passes over CollectedInputs in WIRE order;
//     the EXECUTE pass IS the draw-command order — items append to the target in input order. So the
//     LATER wire draws ON TOP (it is encoded last → composited over the earlier wire).
//   Layer2d (Operators/Lib/render/basic/Layer2d.cs): a textured quad; psMain = clamp(Color·tex). With
//     a solid opaque source + white tint the quad is a solid full-frame color (alpha = source alpha = 1).
//   Blend factors (Core/Rendering/DefaultRenderingStates.cs, mirrored in point_ops_rendertarget.cpp
//     makeDrawPSO): the EXACT equations the executor runs — we derive expected pixels from THESE, not
//     from hardcoded magic:
//       Normal   (DefaultBlendState):   out = src·SrcA + dst·(1 − SrcA)
//       Additive (AdditiveBlendState):  out = src·SrcA + dst·1
//     The pass clears to opaque black (0,0,0,1) once (LoadActionClear), then each item composites.
//
// ── CLOSED-FORM EXPECTED (derived from the equations above; SrcA = 1 for both opaque layers) ──
//   Layer A = solid RED   (1,0,0,1), wire 0 → drawn FIRST.
//   Layer B = solid GREEN (0,1,0,1), wire 1 → drawn SECOND (on top).
//   Both quads are full-frame (Scale=1, unit quad → NDC [-1,1]², ScaleMode=Stretch on a SQUARE target →
//     scaleX·=viewAspect=1, so the quad maps NDC 1:1). So the center AND the corners are inside BOTH.
//
//   NORMAL blend, center (both overlap):
//     clear:           dst = (0,0,0)
//     draw A (SrcA=1): out = (1,0,0)·1 + (0,0,0)·(1−1) = (1,0,0)            [RED]
//     draw B (SrcA=1): out = (0,1,0)·1 + (1,0,0)·(1−1) = (0,1,0)            [GREEN]  ← B on top wins
//     ⇒ CENTER = GREEN, far corner = GREEN too (both full-frame).  This is the WIRE-ORDER witness:
//       the later wire (B) wins because Execute draws it last.
//   NORMAL blend, WIRE ORDER SWAPPED (B first, A second) ⇒ CENTER = RED  (A now drawn last).
//     The golden runs BOTH orderings in the FAITHFUL leg and asserts the center flips → it is the
//     ordering itself under test, not a single color.
//
//   ADDITIVE blend, center (both, SrcA=1, dst·1):
//     clear:    dst = (0,0,0)
//     draw A:   out = (1,0,0)·1 + (0,0,0)·1 = (1,0,0)
//     draw B:   out = (0,1,0)·1 + (1,0,0)·1 = (1,1,0)                       [YELLOW = R+G]
//     ⇒ CENTER = YELLOW (R≈255, G≈255, B≈0).  Additive is order-INDEPENDENT (commutative sum) — so it
//       proves the SECOND layer is collected AT ALL (both contribute), complementary to Normal's
//       order witness.
//
// ── injectBug (-bug leg) — corrupts the REAL collector, never the expected value ──
//   executeCollectFirstOnlyForTest() forces cookCommand's MultiInput Command branch to FIRST WIRE ONLY
//   (the `break`/skip-extraConns bug). Then:
//     Normal A-then-B:  only A collected → center = RED  (expected GREEN) → RED.
//     Additive:         only A collected → center = (1,0,0) RED, NOT yellow (G≈0) → RED.
//   The bite is in the PRODUCTION collector loop (flat AND resident), not a flipped assertion. The same
//   flag closes the blueprint's resident-NIT preemptively: the RESIDENT leg runs the identical compose
//   through cookResident, so a resident collector that dropped wire 1 (the S1-style missing-tooth) would
//   make the resident center read single-layer → caught here, not in production.
//
// ── DISCIPLINE (Cut62-63 golden rules) ──
//   Probe DEEP interior (center NDC 0,0) + a FAR corner (NDC ±0.9) — both saturated plateaus, never a
//   quad edge (no fwidth/smoothstep). Single-sample. No depth (Layer2d draws depth-disabled). Expected
//   colors are DERIVED from the blend equations above for SrcA=1, not pinned to arbitrary constants.
//
// Zone: runtime leaf. The source op (SolidImage) is a TEST-ONLY harness fixture (the StubRenderTarget /
// StubCmdA precedent in point_ops_execute.cpp) installed via setDynamicSpecs + registerTexOp for this
// process only; Layer2d / Execute / RenderTarget are the REAL builtins under test.
#include "runtime/point_ops.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"           // Graph/Node/NodeSpec/PortSpec/pinId/setDynamicSpecs/findSpec
#include "runtime/graph_bridge.h"    // libFromGraph (flat Graph → SymbolLibrary, paths == ids)
#include "runtime/point_graph.h"     // PointGraph::cook/cookResident, registerBuiltinPointOps, TexCookCtx
#include "runtime/render_command.h"  // executeCollectFirstOnlyForTest (the collector -bug flag)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (the production resident path)
#include "runtime/tixl_point.h"      // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// ───────────────────────── test-only solid-color source op (harness fixture) ─────────────────────────
// A pure Texture2D generator that fills its (driver-pre-sized) output with ONE uniform RGBA via a clear
// render pass. The color is keyed by the node's "ColorSel" param (0 = RED, 1 = GREEN) so two distinct
// SolidImage nodes feed two distinct colors — the wire-order witness needs the two layers different.
// (A clear pass, not replaceRegion, so it works regardless of the ensureTex storage mode.)
namespace {
void cookSolidImage(TexCookCtx& c) {
  if (!c.output) return;
  int sel = 0;
  if (c.params) {
    auto it = c.params->find("ColorSel");
    if (it != c.params->end()) sel = (int)(it->second + 0.5f);
  }
  // sel 0 → RED (1,0,0,1); sel 1 → GREEN (0,1,0,1). Opaque (SrcA = 1) so the blend math is the clean
  // alpha-over/add derived in the header. Defined for any other sel as BLACK (no crash).
  float r = (sel == 0) ? 1.0f : 0.0f;
  float g = (sel == 1) ? 1.0f : 0.0f;
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(r, g, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->endEncoding();  // clear-only pass (no draw) → the whole texture is the solid color
  cmd->commit();
  cmd->waitUntilCompleted();
}

NodeSpec atomicSpec(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s;
  s.type = type;
  s.title = type;
  s.ports = std::move(ports);
  s.evaluate = nullptr;
  return s;
}

// Install the dynamic specs for the whole S2c graph. Layer2d / Execute / RenderTarget are real builtins
// (cmdReg/texReg) but have no static NodeSpec in the registry (the existing Layer2d/Execute goldens
// supply specs the same way); SolidImage is the test source op. PortSpec positional init:
//   {id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless, vecArity, multiInput, strDef}
void installLayerComposeSpecs() {
  std::map<std::string, NodeSpec> dyn;
  // SolidImage: a color-selector param + a Texture2D output (the generator).
  dyn["SolidImage"] = atomicSpec(
      "SolidImage",
      {{"ColorSel", "ColorSel", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, true},
       {"out", "out", "Texture2D", false}});
  // Layer2d: Texture2D in → Command out, with the SRT/blend params the op reads (cookLayer2d). We only
  // need the few the golden sets non-default (Scale, ScaleMode, BlendMode); the rest fall to op defaults.
  dyn["Layer2d"] = atomicSpec(
      "Layer2d",
      {{"Image", "Image", "Texture2D", true},
       {"out", "out", "Command", false},
       {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
       {"ScaleMode", "ScaleMode", "Float", true, 0.0f, 0.0f, 5.0f, Widget::Enum, {}, true},
       {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {}, true}});
  // Execute: ONE MultiInput Command input + IsEnabled + Command out (verbatim shape from
  // point_ops_execute.cpp's --selftest-execute spec — the keystone under test).
  dyn["Execute"] = atomicSpec(
      "Execute",
      {{"Command", "Command", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
       {"out", "out", "Command", false},
       {"IsEnabled", "IsEnabled", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true}});
  // RenderTarget: Command in (single) → Texture2D out, Custom resolution params (the resolution pin).
  dyn["RenderTarget"] = atomicSpec(
      "RenderTarget",
      {{"command", "command", "Command", true},
       {"out", "out", "Texture2D", false},
       {"Resolution", "Resolution", "Float", true, 4.0f, 0.0f, 4.0f, Widget::Enum, {}, true},
       {"CustomW", "CustomW", "Float", true, 256.0f, 1.0f, 4096.0f, Widget::Slider, {}, true},
       {"CustomH", "CustomH", "Float", true, 256.0f, 1.0f, 4096.0f, Widget::Slider, {}, true}});
  setDynamicSpecs(std::move(dyn));
}

// Resolve a spec's first output port index, and a named input port index, from the (just-installed) spec.
int outPortIdx(const char* type) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (!s->ports[i].isInput) return (int)i;
  return -1;
}
int inPortIdx(const char* type, const char* dataType) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (s->ports[i].isInput && s->ports[i].dataType == dataType) return (int)i;
  return -1;
}

// Build the S2c graph. `bFirst` = wire B (green) into Execute BEFORE A (red): the wire-order swap leg.
// blendMode: 0 = Normal, 1 = Additive. Node ids: 1=SolidA(red) 2=SolidB(green) 3=Layer2dA 4=Layer2dB
// 5=Execute 6=RenderTarget. The terminal is RenderTarget (id 6).
Graph buildComposeGraph(bool bFirst, int blendMode, uint32_t W, uint32_t H) {
  Graph g;
  Node sa; sa.id = 1; sa.type = "SolidImage"; sa.params["ColorSel"] = 0.0f; g.nodes.push_back(sa);  // RED
  Node sb; sb.id = 2; sb.type = "SolidImage"; sb.params["ColorSel"] = 1.0f; g.nodes.push_back(sb);  // GREEN
  auto mkLayer = [&](int id) {
    Node l; l.id = id; l.type = "Layer2d";
    l.params["Scale"] = 1.0f;       // unit quad → full-frame at the default camera (NDC [-1,1])
    l.params["ScaleMode"] = 3.0f;   // Stretch (Layer2dScaleMode::Stretch; square target → scaleX·=1)
    l.params["BlendMode"] = (float)blendMode;
    return l;
  };
  g.nodes.push_back(mkLayer(3));  // Layer2dA (wraps SolidA)
  g.nodes.push_back(mkLayer(4));  // Layer2dB (wraps SolidB)
  Node ex; ex.id = 5; ex.type = "Execute"; g.nodes.push_back(ex);
  Node rt; rt.id = 6; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f;  // Custom
  rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
  g.nodes.push_back(rt);

  const int solidOut = outPortIdx("SolidImage");
  const int layerTexIn = inPortIdx("Layer2d", "Texture2D");
  const int layerOut = outPortIdx("Layer2d");
  const int execCmdIn = inPortIdx("Execute", "Command");
  const int execOut = outPortIdx("Execute");
  const int rtCmdIn = inPortIdx("RenderTarget", "Command");

  // Solid → Layer2d.Image (each layer wraps one solid color).
  g.connections.push_back({101, pinId(1, solidOut), pinId(3, layerTexIn)});  // SolidA → Layer2dA
  g.connections.push_back({102, pinId(2, solidOut), pinId(4, layerTexIn)});  // SolidB → Layer2dB
  // Layer2d → Execute.Command in WIRE ORDER. Default A-then-B (A wire0, B wire1) ⇒ B on top. bFirst swaps.
  if (!bFirst) {
    g.connections.push_back({103, pinId(3, layerOut), pinId(5, execCmdIn)});  // wire0 = A (red)
    g.connections.push_back({104, pinId(4, layerOut), pinId(5, execCmdIn)});  // wire1 = B (green, on top)
  } else {
    g.connections.push_back({103, pinId(4, layerOut), pinId(5, execCmdIn)});  // wire0 = B (green)
    g.connections.push_back({104, pinId(3, layerOut), pinId(5, execCmdIn)});  // wire1 = A (red, on top)
  }
  g.connections.push_back({105, pinId(5, execOut), pinId(6, rtCmdIn)});  // Execute → RenderTarget
  return g;
}

// Read the (R,G,B) of pg.target() at NDC (ndcX, ndcY). NDC.y=+1 → row 0 (top), per the executor's raster
// (matches point_ops_layer2d.cpp's ndcYToPx). Returns false if no/under-sized target.
bool readTargetRGB(PointGraph& pg, uint32_t W, uint32_t H, float ndcX, float ndcY, int& r, int& g,
                   int& b) {
  MTL::Texture* tex = pg.target();
  if (!tex || (uint32_t)tex->width() != W || (uint32_t)tex->height() != H) return false;
  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  int x = (int)((ndcX * 0.5f + 0.5f) * (float)(W - 1) + 0.5f);
  int y = (int)((1.0f - (ndcY * 0.5f + 0.5f)) * (float)(H - 1) + 0.5f);
  x = x < 0 ? 0 : (x >= (int)W ? (int)W - 1 : x);
  y = y < 0 ? 0 : (y >= (int)H ? (int)H - 1 : y);
  size_t i = ((size_t)y * W + x) * 4;
  r = px[i]; g = px[i + 1]; b = px[i + 2];
  return true;
}

// Cook one compose graph through BOTH the flat (cook) and resident (cookResident) drivers and report
// the center + far-corner RGB of each. `whichPath`: 0 = flat, 1 = resident. Returns false on a path error.
bool cookCompose(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, const Graph& g, int whichPath,
                 uint32_t W, uint32_t H, int& cR, int& cG, int& cB, int& fR, int& fG, int& fB) {
  PointGraph pg(dev, lib, q, W, H);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  if (whichPath == 0) {
    pg.cook(g, ctx, nullptr, /*terminal RenderTarget=*/6);
  } else {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    pg.cookResident(rg, ctx, nullptr, /*RenderTarget path=*/"6");
  }
  bool okC = readTargetRGB(pg, W, H, 0.0f, 0.0f, cR, cG, cB);          // deep center (both layers)
  bool okF = readTargetRGB(pg, W, H, 0.9f, 0.9f, fR, fG, fB);          // far corner (both full-frame)
  return okC && okF;
}

bool isGreen(int r, int g, int b) { return r < 40 && g > 200 && b < 40; }
bool isRed(int r, int g, int b)   { return r > 200 && g < 40 && b < 40; }
bool isYellow(int r, int g, int b){ return r > 200 && g > 200 && b < 40; }
}  // namespace

// ──────────────────────────────────────── THE GOLDEN ────────────────────────────────────────────────
int runLayerComposeSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;  // SQUARE → viewAspect 1 → Stretch maps NDC 1:1

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-layercompose] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();          // Layer2d + Execute + RenderTarget (the REAL builtins under test)
  registerTexOp("SolidImage", cookSolidImage);  // the test source op
  installLayerComposeSpecs();         // specs for the whole graph (Layer2d/Execute/RT have none static)

  bool allFaithful = true;
  // Run every leg through BOTH flat AND resident — the resident leg closes the blueprint's S1 resident-NIT.
  const char* pathName[2] = {"flat", "resident"};

  executeCollectFirstOnlyForTest() = injectBug;  // ★the collector -bug (first-wire-only collapse)

  for (int path = 0; path < 2; ++path) {
    int cR, cG, cB, fR, fG, fB;

    // ── LEG 1: NORMAL blend, A-then-B (B on top) → center + corner GREEN (wire-order witness). ──
    {
      Graph g = buildComposeGraph(/*bFirst=*/false, /*Normal=*/0, W, H);
      bool ok = cookCompose(dev, lib, q, g, path, W, H, cR, cG, cB, fR, fG, fB);
      bool faithful = ok && isGreen(cR, cG, cB) && isGreen(fR, fG, fB);  // B (wire1) drawn last = on top
      allFaithful = allFaithful && faithful;
      std::printf("[selftest-layercompose] %s normal A->B: center=(%d,%d,%d) corner=(%d,%d,%d) "
                  "want GREEN -> %s\n", pathName[path], cR, cG, cB, fR, fG, fB,
                  faithful ? "faithful-ok" : "tripped");
    }
    // ── LEG 2: NORMAL blend, WIRE SWAPPED (B-then-A, A on top) → center RED. Proves it is ORDER. ──
    // Skipped under injectBug: with the collector collapsed to wire 0, leg 2's wire0 is B (green) → center
    // would read green and "pass" the swapped-expectation by accident. Leg 1/3 already bite under the bug;
    // leg 2 only adds value in the FAITHFUL leg (where it proves the swap genuinely flips the result).
    if (!injectBug) {
      Graph g = buildComposeGraph(/*bFirst=*/true, /*Normal=*/0, W, H);
      bool ok = cookCompose(dev, lib, q, g, path, W, H, cR, cG, cB, fR, fG, fB);
      bool faithful = ok && isRed(cR, cG, cB);  // A (now wire1) drawn last = on top → center red
      allFaithful = allFaithful && faithful;
      std::printf("[selftest-layercompose] %s normal B->A (swap): center=(%d,%d,%d) want RED -> %s\n",
                  pathName[path], cR, cG, cB, faithful ? "faithful-ok" : "tripped");
    }
    // ── LEG 3: ADDITIVE blend, A+B → center YELLOW (R+G). Order-independent: proves BOTH collected. ──
    {
      Graph g = buildComposeGraph(/*bFirst=*/false, /*Additive=*/1, W, H);
      bool ok = cookCompose(dev, lib, q, g, path, W, H, cR, cG, cB, fR, fG, fB);
      bool faithful = ok && isYellow(cR, cG, cB) && isYellow(fR, fG, fB);  // R+G = yellow ⇒ both layers
      allFaithful = allFaithful && faithful;
      std::printf("[selftest-layercompose] %s additive A+B: center=(%d,%d,%d) corner=(%d,%d,%d) "
                  "want YELLOW -> %s\n", pathName[path], cR, cG, cB, fR, fG, fB,
                  faithful ? "faithful-ok" : "tripped");
    }
  }

  executeCollectFirstOnlyForTest() = false;  // reset the global (process hygiene)
  setDynamicSpecs({});                        // drop the injected test specs
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-layercompose] FAIL: injectBug collapsed nothing (collector still composited "
                  "both layers)\n");
      return 1;
    }
    std::printf("[selftest-layercompose] injectBug correctly RED (collector collapsed to wire 0 → only "
                "layer A composited → Normal center stayed RED not GREEN, Additive not YELLOW)\n");
    return 1;
  }
  std::printf("[selftest-layercompose] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

}  // namespace sw
