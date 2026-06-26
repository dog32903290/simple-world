// Output-slot picker golden (TiXL OutputWindow "Show Output…" submenu): when a viewed node has >1
// output port, the cook target resolves to the SELECTED output's producer, NOT always outputDefs[0].
//
// ★SEAM under test: viewProducerPath's `startSlot` param (compound_graph.cpp). A compound child with two
// outputs wires each output to a DIFFERENT inner producer; selecting output "a" vs "b" must resolve to
// the DIFFERENT producer path. Today (startSlot="") both resolve to outputDefs[0]'s producer — the bug
// the slot picker fixes.
//
// Topology:
//   root R (compound) → child 1 = compound "Two"
//   "Two" subgraph: RadialPoints(id 1) → boundary output "a";  DrawPoints(id 2) → boundary output "b".
//   viewProducerPath(lib, "", 1, "a") → dive into Two, follow boundary "a" → producer = inner child 1 → "1/1".
//   viewProducerPath(lib, "", 1, "b") → follow boundary "b"               → producer = inner child 2 → "1/2".
//   So slot "a" ≠ slot "b" at the PATH level (distinguishable cook targets), no cook-core change needed.
//   ★injectBug: resolve "b" with the OLD behaviour (startSlot="" → outputDefs[0]="a") → "b" collapses to
//     "a"'s path → the two outputs become indistinguishable → the assertion BITES (RED). Proves the slot
//     param is what lets the viewport pick a non-default output.
#include "runtime/compound_graph.h"
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS

#include <cstdio>
#include <string>

namespace sw {

int runOutputSlotViewSelfTest(bool injectBug) {
  SymbolLibrary lib;

  // atomic: RadialPoints — points(Points) out
  Symbol radial;
  radial.id = "RadialPoints"; radial.name = "RadialPoints"; radial.atomic = true;
  radial.outputDefs = {{"points", "points", "Points", 0.0f}};
  lib.symbols[radial.id] = radial;

  // atomic: DrawPoints — out(Command)
  Symbol draw;
  draw.id = "DrawPoints"; draw.name = "DrawPoints"; draw.atomic = true;
  draw.inputDefs = {{"points", "points", "Points", 0.0f}};
  draw.outputDefs = {{"out", "out", "Command", 0.0f}};
  lib.symbols[draw.id] = draw;

  // compound "Two": TWO outputs (a ← RadialPoints#1, b ← DrawPoints#2) — the multi-output op under test.
  Symbol two;
  two.id = "Two"; two.name = "Two"; two.atomic = false;
  two.outputDefs = {{"a", "a", "Points", 0.0f}, {"b", "b", "Command", 0.0f}};
  SymbolChild r1; r1.id = 1; r1.symbolId = "RadialPoints";
  SymbolChild d2; d2.id = 2; d2.symbolId = "DrawPoints";
  two.children = {r1, d2};
  two.connections = {{1, "points", kSymbolBoundary, "a"},   // RadialPoints#1.points → output "a"
                     {2, "out", kSymbolBoundary, "b"}};      // DrawPoints#2.out → output "b"
  two.nextChildId = 3;
  lib.symbols["Two"] = two;

  // root R: one "Two" child (id 1). The viewport views child 1; the slot picks WHICH output's producer.
  Symbol root;
  root.id = "R"; root.name = "R"; root.atomic = false;
  root.outputDefs = {{"image", "image", "Command", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "Two";
  root.children = {c1};
  root.connections = {{1, "b", kSymbolBoundary, "image"}};  // R views Two's "b" by default (irrelevant here)
  root.nextChildId = 2;
  lib.symbols["R"] = root; lib.rootId = "R";

  // Resolve the producer of output "a" and output "b" of the viewed child (id 1).
  const std::string pathA = viewProducerPath(lib, "", 1, "a");
  // ★injectBug: resolve "b" the OLD way (ignore the slot → outputDefs[0] = "a") → collapses onto pathA.
  const std::string slotB = injectBug ? std::string() : std::string("b");
  const std::string pathB = viewProducerPath(lib, "", 1, slotB);

  const bool aOk = (pathA == "1/1");                 // output "a" → inner RadialPoints#1
  const bool bOk = (pathB == "1/2");                 // output "b" → inner DrawPoints#2
  const bool distinct = !pathA.empty() && pathA != pathB;
  const bool pass = aOk && bOk && distinct;

  std::printf("[selftest-output-slot-view] a=\"%s\"(want 1/1) b=\"%s\"(want 1/2) distinct=%d -> %s\n",
              pathA.c_str(), pathB.c_str(), distinct ? 1 : 0, pass ? "PASS" : "FAIL");

  if (injectBug) {
    if (pass) {
      std::printf("[selftest-output-slot-view] FAIL: injectBug still passed (output \"b\" resolved to a "
                  "different producer than \"a\" even with the slot dropped — the picker is not the seam)\n");
      return 1;
    }
    std::printf("[selftest-output-slot-view] injectBug correctly RED (slot dropped → \"b\" collapses to "
                "\"a\"'s producer → the two outputs are indistinguishable)\n");
    return 1;  // -bug ALWAYS exits non-zero (the tooth bit) — the bite scanner asserts this
  }
  return pass ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/314, {"output-slot-view", runOutputSlotViewSelfTest});

}  // namespace sw
