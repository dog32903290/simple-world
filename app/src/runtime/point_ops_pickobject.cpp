// runtime/point_ops_pickobject — PickObject: the data.object PICK-BY-INDEX Command-collector op + the
// shared Mod-selection helper both cook drivers call, + the --selftest-pickobject golden (flat AND
// resident legs — production runs the resident leg, the S2c blood lesson).
//
// TiXL authority: external/tixl/Operators/Lib/data/object/PickObject.cs
//   Update (:16-38):
//     var connections = Input.GetCollectedTypedInputs();       // MultiInput<object>, wire order
//     if (connections == null || connections.Count == 0) return;
//     var index = Index.GetValue(context).Mod(connections.Count);   // :23 — Mod, NOT C# %
//     Selected.Value = connections[index].GetValue(context);        // :24 — evaluate ONLY the picked wire
//   Mod (T3.Core Utils/MathUtils.cs:273-284): repeat==0 → 0; x = val % repeat; x<0 → x = repeat + x.
//   So EVERY index picks a wire (negative wraps): -1 → count-1. Unlike flow/Switch there is NO -1=none /
//   -2=all sentinel — that contrast is the discriminating tooth of this golden.
//
// ★NAMED FORK (currency): TiXL's MultiInputSlot<object> accepts ANY runtime object (C# System.Object).
//   sw has typed rails only (Points/Command/Texture2D/Mesh/...). This clone types the pick on the COMMAND
//   rail (the data registry's command-rail posture, node_registry_draw_data.cpp) — the dominant usage
//   (e.g. Lib/render/postfx/ProjectLight.t3 picks Command-producing objects). When an Object rail lands,
//   widen; the selection math below is currency-agnostic and stays the single source of truth.
//
// ★COOK-CORE HOOK (the Switch precedent, point_ops_switch.cpp): the SELECTION lives in the drivers'
//   MultiInput Command collector branch, NOT in the op cook. The op cook (cookPickObject) is THIN — it
//   forwards cc.inputCommand (the chain the driver already picked). pickObjectSelectIndex() is the ONE
//   function both the flat (point_graph_command_cook.cpp) and resident (point_graph_resident_command_
//   cook.cpp) collectors call, so the Mod wrap math can never diverge between the legs.
//
// runtime leaf: pure CPU + Metal (the golden cooks through PointGraph); no UI, no upward deps.
#include "runtime/point_ops.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph / Node / NodeSpec / PortSpec / pinId / setDynamicSpecs / findSpec
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph → SymbolLibrary, paths == ids)
#include "runtime/point_graph.h"          // CmdCookCtx / registerCmdOp / PointGraph / registerBuiltinPointOps
#include "runtime/render_command.h"       // RenderCommand + pickObjectSelectIndex / pickObjectIgnoreIndexForTest
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// ───────────────────────── selection math (shared by both cook legs) ─────────────────────────
// PickObject.cs:23 `Index.GetValue(context).Mod(connections.Count)` with Mod = MathUtils.cs:273-284:
//   count<=0 → -1 (cook nothing; PickObject.cs:19-20 returns before picking when no wires)
//   else: x = rawIndex % count; x<0 → x += count. EVERY in-range result picks exactly ONE wire —
//   negatives WRAP (no Switch-style -1=none / -2=all sentinels; that difference is a golden tooth).
int pickObjectSelectIndex(int rawIndex, int count) {
  if (count <= 0) return -1;             // no wired inputs → nothing to pick (PickObject.cs:19-20)
  int x = rawIndex % count;              // C# % == C++ % on the truncated-int domain
  if (x < 0) x += count;                 // MathUtils.cs:281-282 (x = repeat + x)
  return x;                              // ALWAYS a single selection in [0, count)
}

// Test-only DRIVER flag (the pick tooth): true → the collectors IGNORE the selection and concat ALL
// wires (== Execute), so the golden's -bug legs draw the WRONG branch on top → center-pixel RED. OFF in
// production. Parallel to switchIgnoreIndexForTest() (CPU driver flag, no shader test seam).
bool& pickObjectIgnoreIndexForTest() {
  static bool v = false;
  return v;
}

// ─────────────────────── the PickObject op (forwards the driver-picked chain) ───────────────────────
// THIN: the driver already picked (cc.inputCommand = the chosen wire's chain, or null for no wires).
// Forward it — exactly like Switch/Execute forward their collected chain.
RenderCommand cookPickObject(CmdCookCtx& c) {
  RenderCommand rc;
  if (c.inputCommand) rc.items = c.inputCommand->items;
  return rc;
}

void registerPickObjectOp() { registerCmdOp("PickObject", cookPickObject); }

// ───────────────────────────────────────── GOLDEN ─────────────────────────────────────────
// --selftest-pickobject (BOTH legs). Harness = the --selftest-switch template: three full-frame Layer2d
// color layers (RED[wire0], GREEN[wire1], BLUE[wire2]) → PickObject.Input, PickObject → RenderTarget,
// center-pixel readback. Closed-form per PickObject.cs:23 Mod (count==3):
//   Index =  1 → 1            → GREEN
//   Index =  4 → 4 % 3 = 1    → GREEN  (positive WRAP tooth)
//   Index = -1 → -1%3=-1 → +3 → 2 → BLUE  (negative WRAP — the anti-Switch tooth: Switch cooks NOTHING
//                                          at -1; PickObject MUST pick the last wire. A Switch-semantics
//                                          regression turns this center BLACK → RED.)
//   Index = -2 → -2%3=-2 → +3 → 1 → GREEN (negative wrap ≠ Switch's cook-ALL)
// -bug: pickObjectIgnoreIndexForTest() forces concat-ALL on every leg → BLUE (topmost wire) on top:
//   legs wanting GREEN (Index 1 / 4 / -2) read a non-green center → RED. Index=-1 (want BLUE) would pass
//   by luck under cook-all → skipped in the -bug run (the Switch golden's runUnderBug precedent).
namespace {
void cookSolidImagePk(TexCookCtx& c) {
  if (!c.output) return;
  int sel = 0;  // 0=RED, 1=GREEN, 2=BLUE (the three wires' witness colors)
  if (c.params) {
    auto it = c.params->find("ColorSel");
    if (it != c.params->end()) sel = (int)(it->second + 0.5f);
  }
  float r = (sel == 0) ? 1.0f : 0.0f;
  float g = (sel == 1) ? 1.0f : 0.0f;
  float b = (sel == 2) ? 1.0f : 0.0f;
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(r, g, b, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->endEncoding();  // clear-only pass → the whole texture is the solid color
  cmd->commit();
  cmd->waitUntilCompleted();
}

NodeSpec atomicSpecPk(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}

// Specs: SolidImage/Layer2d/RenderTarget verbatim from the Switch golden harness. PickObject itself uses
// the PRODUCTION row shape (node_registry_draw_data.cpp): Input(MultiInput Command) + Index + Selected.
void installPickObjectSpecs() {
  std::map<std::string, NodeSpec> dyn;
  dyn["SolidImage"] = atomicSpecPk(
      "SolidImage",
      {{"ColorSel", "ColorSel", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Slider, {}, true},
       {"out", "out", "Texture2D", false}});
  dyn["Layer2d"] = atomicSpecPk(
      "Layer2d",
      {{"Image", "Image", "Texture2D", true},
       {"out", "out", "Command", false},
       {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
       {"ScaleMode", "ScaleMode", "Float", true, 0.0f, 0.0f, 5.0f, Widget::Enum, {}, true},
       {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {}, true}});
  dyn["RenderTarget"] = atomicSpecPk(
      "RenderTarget",
      {{"command", "command", "Command", true},
       {"out", "out", "Texture2D", false},
       {"Resolution", "Resolution", "Float", true, 4.0f, 0.0f, 4.0f, Widget::Enum, {}, true},
       {"CustomW", "CustomW", "Float", true, 256.0f, 1.0f, 4096.0f, Widget::Slider, {}, true},
       {"CustomH", "CustomH", "Float", true, 256.0f, 1.0f, 4096.0f, Widget::Slider, {}, true}});
  setDynamicSpecs(std::move(dyn));
}

int outPortIdxPk(const char* type) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (!s->ports[i].isInput) return (int)i;
  return -1;
}
int inPortIdxPk(const char* type, const char* dataType) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (s->ports[i].isInput && s->ports[i].dataType == dataType) return (int)i;
  return -1;
}

// 3 SolidImage (R/G/B) → 3 Layer2d → PickObject.Input (wire0=R, wire1=G, wire2=B) → RenderTarget.
// Node ids: 1/2/3=Solid R/G/B, 4/5/6=Layer R/G/B, 7=PickObject, 8=RenderTarget (terminal).
Graph buildPickObjectGraph(float index, uint32_t W, uint32_t H) {
  Graph g;
  for (int k = 0; k < 3; ++k) {
    Node s; s.id = 1 + k; s.type = "SolidImage"; s.params["ColorSel"] = (float)k; g.nodes.push_back(s);
  }
  auto mkLayer = [&](int id) {
    Node l; l.id = id; l.type = "Layer2d";
    l.params["Scale"] = 1.0f;       // unit quad → full-frame at the default camera (NDC [-1,1])
    l.params["ScaleMode"] = 3.0f;   // Stretch (square target → scaleX·=1)
    l.params["BlendMode"] = 0.0f;   // Normal
    return l;
  };
  g.nodes.push_back(mkLayer(4));  // Layer R (wraps Solid 1)
  g.nodes.push_back(mkLayer(5));  // Layer G (wraps Solid 2)
  g.nodes.push_back(mkLayer(6));  // Layer B (wraps Solid 3)
  Node pk; pk.id = 7; pk.type = "PickObject"; pk.params["Index"] = index; g.nodes.push_back(pk);
  Node rt; rt.id = 8; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
  g.nodes.push_back(rt);

  const int solidOut = outPortIdxPk("SolidImage");
  const int layerTexIn = inPortIdxPk("Layer2d", "Texture2D");
  const int layerOut = outPortIdxPk("Layer2d");
  const int pkCmdIn = inPortIdxPk("PickObject", "Command");   // the PRODUCTION data-registry row
  const int pkOut = outPortIdxPk("PickObject");
  const int rtCmdIn = inPortIdxPk("RenderTarget", "Command");

  g.connections.push_back({101, pinId(1, solidOut), pinId(4, layerTexIn)});  // R
  g.connections.push_back({102, pinId(2, solidOut), pinId(5, layerTexIn)});  // G
  g.connections.push_back({103, pinId(3, solidOut), pinId(6, layerTexIn)});  // B
  // Layer2d → PickObject.Input in WIRE ORDER: wire0=R, wire1=G, wire2=B (the pick witnesses).
  g.connections.push_back({104, pinId(4, layerOut), pinId(7, pkCmdIn)});  // wire0 = RED
  g.connections.push_back({105, pinId(5, layerOut), pinId(7, pkCmdIn)});  // wire1 = GREEN
  g.connections.push_back({106, pinId(6, layerOut), pinId(7, pkCmdIn)});  // wire2 = BLUE
  g.connections.push_back({107, pinId(7, pkOut), pinId(8, rtCmdIn)});     // PickObject → RenderTarget
  return g;
}

bool readTargetRGBPk(PointGraph& pg, uint32_t W, uint32_t H, int& r, int& g, int& b) {
  MTL::Texture* tex = pg.target();
  if (!tex || (uint32_t)tex->width() != W || (uint32_t)tex->height() != H) return false;
  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  size_t i = (((size_t)H / 2) * W + W / 2) * 4;  // deep center (the picked full-frame layer covers it)
  r = px[i]; g = px[i + 1]; b = px[i + 2];
  return true;
}

// Cook a PickObject graph through whichPath (0=flat, 1=resident); return the CENTER RGB.
bool cookPickObjectGraph(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, float index,
                         int whichPath, uint32_t W, uint32_t H, int& cR, int& cG, int& cB) {
  Graph g = buildPickObjectGraph(index, W, H);
  PointGraph pg(dev, lib, q, W, H);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  if (whichPath == 0) {
    pg.cook(g, ctx, nullptr, /*terminal RenderTarget=*/8);
  } else {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    pg.cookResident(rg, ctx, nullptr, /*RenderTarget path=*/"8");
  }
  return readTargetRGBPk(pg, W, H, cR, cG, cB);
}

bool isGreenPk(int r, int g, int b) { return r < 40 && g > 200 && b < 40; }
bool isBluePk(int r, int g, int b)  { return r < 40 && g < 40 && b > 200; }
}  // namespace

int runPickObjectSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;  // SQUARE → Stretch maps NDC 1:1

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-pickobject] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();                       // Layer2d + RenderTarget + PickObject (REAL builtins)
  registerTexOp("SolidImage", cookSolidImagePk);   // the test source op
  installPickObjectSpecs();

  pickObjectIgnoreIndexForTest() = injectBug;  // ★the pick -bug (concat-all collapse)

  bool allFaithful = true;
  const char* pathName[2] = {"flat", "resident"};

  // The leg table (Mod closed-form above). runUnderBug = its want ≠ BLUE (the concat-all topmost wire),
  // so cook-all produces a center the assertion rejects → a genuine RED tooth.
  struct Leg { float index; bool wantBlue; const char* wantName; bool runUnderBug; };
  const Leg legs[] = {
      {1.0f,  false, "GREEN", true},   // 1 → wire1
      {4.0f,  false, "GREEN", true},   // 4 Mod 3 = 1 (positive wrap)
      {-1.0f, true,  "BLUE",  false},  // -1 Mod 3 = 2 (negative wrap ≠ Switch's none; == top → skip under bug)
      {-2.0f, false, "GREEN", true},   // -2 Mod 3 = 1 (negative wrap ≠ Switch's all)
  };

  for (int path = 0; path < 2; ++path) {
    for (const Leg& L : legs) {
      if (injectBug && !L.runUnderBug) continue;  // skip legs cook-all matches by luck
      int r, g, b;
      bool ok = cookPickObjectGraph(dev, lib, q, L.index, path, W, H, r, g, b);
      bool match = ok && (L.wantBlue ? isBluePk(r, g, b) : isGreenPk(r, g, b));
      allFaithful = allFaithful && match;
      std::printf("[selftest-pickobject] %s Index=%g: center=(%d,%d,%d) want %s -> %s\n", pathName[path],
                  (double)L.index, r, g, b, L.wantName, match ? "faithful-ok" : "tripped");
    }
  }

  pickObjectIgnoreIndexForTest() = false;  // reset the global (process hygiene)
  setDynamicSpecs({});                     // drop the injected test specs
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-pickobject] injectBug did not trip (concat-all collapse changed no center)\n");
      return 0;  // dead tooth → exit 0 so --bite's NO-BITE list catches it (GOLDEN_STANDARD P1)
    }
    std::printf("[selftest-pickobject] injectBug correctly RED (driver ignored Index → concatenated ALL "
                "wires → BLUE on top where GREEN was picked, both legs)\n");
    return 1;
  }
  std::printf("[selftest-pickobject] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/380, {"pickobject", runPickObjectSelfTest});

}  // namespace sw
