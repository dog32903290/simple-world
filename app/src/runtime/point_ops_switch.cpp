// runtime/point_ops_switch — S3b Switch: the Command-collector SUB-SELECT op + the shared selection-math
// helper both cook drivers call, + the --selftest-switch HARD-GATE golden (flat AND resident legs; the
// resident -bug is a distinct RED tooth = the S2c/S3a blood lesson — production runs the resident leg).
//
// TiXL ground truth: flow/Switch.cs:30-89 (the Update):
//   var commands = Commands.GetCollectedTypedInputs();   // MultiInput, wire-declaration order
//   var index = Index.GetValue(context);
//   if (commands.Count == 0 || index == -1) { Count.Value = 0; return; }   // none
//   if (index == -2) { for i: commands[i].GetValue(ctx); }                  // all (like Execute)
//   else {
//     index %= commands.Count;  if (index < 0) index += commands.Count;    // wrap, negative-safe :60-64
//     commands[index].GetValue(ctx);                                        // cook ONLY the index-th :67
//   }
//   Count.Value = commands.Count;                                          // N
// Unlike Execute (concat ALL) or SetVarCmd (push a var around the SubGraph), Switch is PURE SELECTION: it
// changes WHICH of the N collected Command subtrees the driver cooks. There is no re-cook (S3c Loop) and no
// var write (S3a) — the cheapest Command-collector variant, the de-risk step before Loop's harder re-cook.
//
// ★COOK-CORE HOOK (the seam): the SELECTION lives in the driver's MultiInput Command collector loop, NOT in
// the op cook. The op cook (cookSwitch) is THIN — like Execute it just forwards cc.inputCommand (the chain
// the driver already sub-selected). The driver, when it sees a Switch node, reads the Index param, counts the
// N wired Commands, and concatenates ONLY the selected one (or all for -2, none for -1/empty). This mirrors
// how Execute's "concat all" and SetVarCmd's "push var" both live in that same collector branch. The math is
// factored into switchSelectIndex() so the FLAT and RESIDENT legs call the IDENTICAL function — a single
// source of truth defeats the off-by-one trap the blueprint §3 flags (resident wires = primary + extraConns).
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
#include "runtime/point_graph.h"          // CmdCookCtx / registerCmdOp / PointGraph / cookParam / registerBuiltinPointOps
#include "runtime/render_command.h"       // RenderCommand + switchSelectIndex / switchIgnoreIndexForTest / kSwitch*
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// ───────────────────────── selection math (shared by both cook legs) ─────────────────────────
// Returns the index of the SINGLE wire to cook, OR a sentinel:
//   kSwitchSelectAll  (-2) → cook ALL wires (TiXL index==-2)
//   kSwitchSelectNone (-1) → cook NO wire   (TiXL index==-1 OR count==0)
//   else: the wrapped, negative-safe index in [0, count) → cook ONLY that wire (TiXL :60-67).
// `count` = number of wired Command inputs the driver gathered (flat: matching connections; resident:
// primary wire + extraConns). `rawIndex` = the Switch.Index param value (truncated to int by the caller).
// ONE function so the two legs can NEVER diverge on the wrap/negative/empty edges (the §3 off-by-one trap).
int switchSelectIndex(int rawIndex, int count) {
  if (count <= 0 || rawIndex == kSwitchSelectNone) return kSwitchSelectNone;  // none (empty / -1)
  if (rawIndex == kSwitchSelectAll) return kSwitchSelectAll;                  // all (-2)
  int idx = rawIndex % count;            // C# % matches C++ % for the truncated-int domain
  if (idx < 0) idx += count;             // negative-safe (Switch.cs:61-64)
  return idx;                            // single selection in [0, count)
}

// Test-only DRIVER flag (the Switch sub-select tooth): when true, the driver IGNORES the selection and
// concatenates ALL wires (== Execute / "cook-all"), so --selftest-switch's -bug leg draws the WRONG branch
// on top → the center-pixel assertion goes RED. OFF in production (zero behaviour change). A CPU DRIVER flag,
// NOT a shader bug-branch (no test seam in any .metal — constitution rule); parallel to
// executeCollectFirstOnlyForTest(). Read by the flat (point_graph.cpp) + resident (point_graph_resident.cpp)
// collectors. For an Index that selects the LAST/top wire, "cook-all" leaves that wire on top by luck, so the
// golden's -bug legs pick Indices whose correct branch is NOT the topmost wire (Index=1, Index=4→1, Index=-1).
bool& switchIgnoreIndexForTest() {
  static bool v = false;
  return v;
}

// ───────────────────────────── the Switch op (forwards the sub-selected chain) ─────────────────────────────
// THIN: the driver already sub-selected (cc.inputCommand = the chosen wire's chain, or empty for none).
// Forward it — exactly like Execute forwards the concatenated chain. (Count output = N is realized on the
// value rail by a future GetCollectedInputsCount reader; the Command-rail draw effect is the selection here.)
RenderCommand cookSwitch(CmdCookCtx& c) {
  RenderCommand rc;
  if (c.inputCommand) rc.items = c.inputCommand->items;
  return rc;
}

void registerSwitchOp() { registerCmdOp("Switch", cookSwitch); }

// ───────────────────────────────────────── GOLDEN ─────────────────────────────────────────
// --selftest-switch (S3b HARD GATE, BOTH legs). Wire three full-frame Layer2d color layers
// (LayerRed[wire0], LayerGreen[wire1], LayerBlue[wire2]) into a Switch, Switch → RenderTarget. Read the
// CENTER pixel of the cooked target. Closed-form per Switch.cs selection (count==3):
//   Index =  1 → wrap n/a → cook wire1 = GREEN  (center green)
//   Index =  2 →            cook wire2 = BLUE   (center blue)
//   Index =  4 → 4%3 = 1  → cook wire1 = GREEN  (the WRAP tooth)
//   Index = -1 →            cook NOTHING        (center BLACK)
//   Index = -2 →            cook ALL (R then G then B, B last = on top) = BLUE  (the all tooth)
// -bug: switchIgnoreIndexForTest() forces cook-ALL on every leg → the selection is gone:
//   Index=1 (want GREEN) → all → BLUE on top → center NOT green → RED
//   Index=2 (want BLUE)  → all → BLUE on top → center IS blue → would pass by luck → SKIPPED under bug
//   Index=4 (want GREEN) → all → BLUE on top → center NOT green → RED  (wrap tooth bites)
//   Index=-1(want BLACK) → all → BLUE on top → center NOT black → RED  (none tooth bites)
// So the -bug legs are {Index=1, Index=4, Index=-1} — each one's correct branch is NOT the topmost wire,
// so "cook-all" produces a center the assertion rejects. The resident -bug is a SEPARATE assertion (S2c
// blood lesson: a resident-only miss = a prod-only black-hole; production runs the resident leg).
//
// Harness is the --selftest-layercompose template (real Layer2d/RenderTarget builtins + a SolidImage test
// source + center-pixel readback through BOTH cook drivers). Switch is the REAL builtin under test.
namespace {
void cookSolidImage(TexCookCtx& c) {
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

NodeSpec atomicSpec(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}

// Specs: Layer2d / RenderTarget verbatim shape from the layercompose golden; Switch = ONE MultiInput
// Command input + Index + a Command out. PortSpec positional init:
//   {id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless, vecArity, multiInput, strDef}
void installSwitchSpecs() {
  std::map<std::string, NodeSpec> dyn;
  dyn["SolidImage"] = atomicSpec(
      "SolidImage",
      {{"ColorSel", "ColorSel", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Slider, {}, true},
       {"out", "out", "Texture2D", false}});
  dyn["Layer2d"] = atomicSpec(
      "Layer2d",
      {{"Image", "Image", "Texture2D", true},
       {"out", "out", "Command", false},
       {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
       {"ScaleMode", "ScaleMode", "Float", true, 0.0f, 0.0f, 5.0f, Widget::Enum, {}, true},
       {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {}, true}});
  // Switch: MultiInput Command in (the N branches) + Index (the selector) + Command out.
  dyn["Switch"] = atomicSpec(
      "Switch",
      {{"Commands", "Commands", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
       {"out", "out", "Command", false},
       {"Index", "Index", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Slider, {}, true}});
  dyn["RenderTarget"] = atomicSpec(
      "RenderTarget",
      {{"command", "command", "Command", true},
       {"out", "out", "Texture2D", false},
       {"Resolution", "Resolution", "Float", true, 4.0f, 0.0f, 4.0f, Widget::Enum, {}, true},
       {"CustomW", "CustomW", "Float", true, 256.0f, 1.0f, 4096.0f, Widget::Slider, {}, true},
       {"CustomH", "CustomH", "Float", true, 256.0f, 1.0f, 4096.0f, Widget::Slider, {}, true}});
  setDynamicSpecs(std::move(dyn));
}

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

// Build the Switch graph: 3 SolidImage (R/G/B) → 3 Layer2d → Switch.Commands (wire0=R, wire1=G, wire2=B)
// → RenderTarget. Switch.Index = `index`. Node ids: 1/2/3=Solid R/G/B, 4/5/6=Layer R/G/B, 7=Switch,
// 8=RenderTarget (terminal).
Graph buildSwitchGraph(float index, uint32_t W, uint32_t H) {
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
  Node sw; sw.id = 7; sw.type = "Switch"; sw.params["Index"] = index; g.nodes.push_back(sw);
  Node rt; rt.id = 8; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
  g.nodes.push_back(rt);

  const int solidOut = outPortIdx("SolidImage");
  const int layerTexIn = inPortIdx("Layer2d", "Texture2D");
  const int layerOut = outPortIdx("Layer2d");
  const int swCmdIn = inPortIdx("Switch", "Command");
  const int swOut = outPortIdx("Switch");
  const int rtCmdIn = inPortIdx("RenderTarget", "Command");

  // Solid → Layer2d.Image.
  g.connections.push_back({101, pinId(1, solidOut), pinId(4, layerTexIn)});  // R
  g.connections.push_back({102, pinId(2, solidOut), pinId(5, layerTexIn)});  // G
  g.connections.push_back({103, pinId(3, solidOut), pinId(6, layerTexIn)});  // B
  // Layer2d → Switch.Commands in WIRE ORDER: wire0=R, wire1=G, wire2=B (the selection witnesses).
  g.connections.push_back({104, pinId(4, layerOut), pinId(7, swCmdIn)});  // wire0 = RED
  g.connections.push_back({105, pinId(5, layerOut), pinId(7, swCmdIn)});  // wire1 = GREEN
  g.connections.push_back({106, pinId(6, layerOut), pinId(7, swCmdIn)});  // wire2 = BLUE
  g.connections.push_back({107, pinId(7, swOut), pinId(8, rtCmdIn)});     // Switch → RenderTarget
  return g;
}

bool readTargetRGB(PointGraph& pg, uint32_t W, uint32_t H, float ndcX, float ndcY, int& r, int& g, int& b) {
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

// Cook a Switch graph through whichPath (0=flat, 1=resident); return the CENTER RGB.
bool cookSwitchGraph(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, float index, int whichPath,
                     uint32_t W, uint32_t H, int& cR, int& cG, int& cB) {
  Graph g = buildSwitchGraph(index, W, H);
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
  return readTargetRGB(pg, W, H, 0.0f, 0.0f, cR, cG, cB);  // deep center (the selected layer covers it)
}

bool isRed(int r, int g, int b)   { return r > 200 && g < 40 && b < 40; }
bool isGreen(int r, int g, int b) { return r < 40 && g > 200 && b < 40; }
bool isBlue(int r, int g, int b)  { return r < 40 && g < 40 && b > 200; }
bool isBlack(int r, int g, int b) { return r < 40 && g < 40 && b < 40; }
}  // namespace

int runSwitchSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;  // SQUARE → Stretch maps NDC 1:1

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-switch] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();                     // Layer2d + RenderTarget + Switch (the REAL builtins)
  registerTexOp("SolidImage", cookSolidImage);   // the test source op
  installSwitchSpecs();

  switchIgnoreIndexForTest() = injectBug;  // ★the sub-select -bug (cook-all collapse)

  bool allFaithful = true;
  const char* pathName[2] = {"flat", "resident"};

  // The leg table. `runUnderBug` = whether this leg is exercised in the -bug run (its correct branch is
  // NOT the topmost wire, so cook-all gives a center the assertion rejects → a genuine RED tooth).
  struct Leg { float index; int want; const char* wantName; bool runUnderBug; };
  enum { WR = 0, WG = 1, WB = 2, WK = 3 };  // expected: Red / Green / Blue / blacK
  const Leg legs[] = {
      {1.0f,  WG, "GREEN",  true},   // wire1 = green
      {2.0f,  WB, "BLUE",   false},  // wire2 = blue (== topmost; cook-all also blue → no bite, skip under bug)
      {4.0f,  WG, "GREEN",  true},   // 4%3=1 = green (WRAP tooth)
      {-1.0f, WK, "BLACK",  true},   // none
      {-2.0f, WB, "BLUE",   false},  // all → blue on top (== cook-all → no bite, skip under bug)
  };

  for (int path = 0; path < 2; ++path) {
    for (const Leg& L : legs) {
      if (injectBug && !L.runUnderBug) continue;  // skip legs that cook-all matches by luck
      int r, g, b;
      bool ok = cookSwitchGraph(dev, lib, q, L.index, path, W, H, r, g, b);
      bool match = ok && ((L.want == WR && isRed(r, g, b)) || (L.want == WG && isGreen(r, g, b)) ||
                          (L.want == WB && isBlue(r, g, b)) || (L.want == WK && isBlack(r, g, b)));
      allFaithful = allFaithful && match;
      std::printf("[selftest-switch] %s Index=%g: center=(%d,%d,%d) want %s -> %s\n", pathName[path],
                  (double)L.index, r, g, b, L.wantName, match ? "faithful-ok" : "tripped");
    }
  }

  switchIgnoreIndexForTest() = false;  // reset the global (process hygiene)
  setDynamicSpecs({});                 // drop the injected test specs
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-switch] FAIL: injectBug selected nothing wrong (driver still sub-selected the "
                  "index → the cook-all collapse changed no center)\n");
      return 1;
    }
    std::printf("[selftest-switch] injectBug correctly RED (driver ignored Index → cooked ALL wires → BLUE "
                "on top for Index 1/4/-1 → center NOT the selected branch on BOTH legs)\n");
    return 1;
  }
  std::printf("[selftest-switch] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/324, {"switch", runSwitchSelfTest});

}  // namespace sw
