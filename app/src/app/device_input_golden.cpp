// app/device_input_golden — --selftest-deviceinput: the io/input device family golden (KeyboardInput /
// KeyboardInputAsInt / MouseInput / Gamepad), driven through the REAL production seam cookDeviceInputNodes.
//
// TiXL authority (expected values hand-derived, line-referenced):
//   KeyboardInput.cs:22-43   — justPressed = !wasDown && isDown; mode switch (Off/Pressed/Released/IsDown).
//   KeyboardInputAsInt.cs:28-58 — NumRow scans keys[48..57], PressedNumber = i-48; Mode 0 = 0-on-release.
//   MouseInput.cs:39-41      — SignedPosition = (LastPos - 0.5) * (aspect,-1) * scale.
//   Gamepad.cs:31-39         — IsConnected = the controller-present gate.
//
// Anti-pattern guards (GOLDEN_STANDARD.md three features):
//   • diverging-middle probes: KeyboardInput's PressedThisFrame is tested ACROSS two frames (a held key
//     must fire justPressed ONCE then go false — the edge, not an identity point); the mouse probe uses a
//     non-centered cursor (0.75,0.25) with aspect=2 and scale=3 so the (x-0.5)*aspect*scale and the y-flip
//     BOTH bite (a dropped recenter / dropped aspect / wrong sign all diverge); the digit scan holds '7'
//     (VK 55) so PressedNumber=7≠0 (a dropped i-48 offset yields 55, not 7).
//   • real inject seam: setDeviceInputBug corrupts the REAL cook step (drop _wasDown write / drop the digit
//     offset / drop the -0.5 recenter / force IsConnected) — NOT a want-flip of the expected value.
//   • did-not-trip → return 0: if the injected bug leaves every assertion green, we return 0 (dead-tooth
//     contract) so --bite's NO-BITE list catches it instead of a false exit-1.
#include "app/device_input_cook.h"

#include <cmath>
#include <cstdio>

#include "runtime/device_input_provider.h"  // DeviceInputProvider (inject the device snapshot)
#include "runtime/graph.h"                   // findSpec
#include "runtime/graph_bridge.h"            // atomicSymbolFromSpec
#include "runtime/resident_eval_graph.h"     // buildEvalGraph / initResidentCache / ResidentNode

namespace sw {
namespace {

int g_fail = 0;
bool g_anyBite = false;  // did any bug-leg assertion actually flip? (dead-tooth guard)

// The comparison helper. The expectation is ALWAYS `prodWant` — the FIXED TiXL production truth,
// bug-independent (NOT a want-flip). injectBug corrupts the REAL cook (via setDeviceInputBug) so the
// corrupted output diverges from this fixed expectation → the assertion FAILS (the tooth bites). `bugWant`
// is ONLY the documented shape of the corrupted output, used for the dead-tooth check: if injectBug is on
// and this probe's bug value actually differs from production AND the observed value moved off prodWant,
// the tooth genuinely bit (g_anyBite). A probe where bugWant==prodWant is a bug-invariant sanity check
// (it should stay green on both legs — e.g. Position3d.x, unaffected by the recenter fork).
void checkEq(bool injectBug, const char* what, float observed, float prodWant, float bugWant) {
  const bool pass = std::abs(observed - prodWant) < 1e-5f;  // expectation is ALWAYS the production truth
  const bool canBite = std::abs(bugWant - prodWant) > 1e-6f;  // this probe is sensitive to the injected bug
  if (injectBug && canBite && !pass) g_anyBite = true;        // the corrupted cook diverged here → real bite
  if (!pass) { ++g_fail; printf("  [deviceinput] FAIL %s (got %.5f want %.5f)\n", what, observed, prodWant); }
  else printf("  [deviceinput] ok   %s = %.5f\n", what, observed);
}

// Build a root compound with ONE atomic child of `type` (id 1), applying the given Float overrides.
SymbolLibrary makeLib(const char* type, const std::map<std::string, float>& overrides) {
  SymbolLibrary lib;
  const NodeSpec* spec = findSpec(type);
  if (spec) lib.symbols[type] = atomicSymbolFromSpec(*spec);
  Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
  SymbolChild c; c.id = 1; c.symbolId = type;
  for (const auto& kv : overrides) c.overrides[kv.first] = kv.second;
  root.children = {c};
  root.nextChildId = 2;
  lib.symbols["R"] = root;
  lib.rootId = "R";
  return lib;
}

}  // namespace

int runDeviceInputSelfTest(bool injectBug) {
  g_fail = 0;
  g_anyBite = false;
  printf("[selftest] deviceinput (TiXL io/input family: KeyboardInput/KeyboardInputAsInt/MouseInput/Gamepad)\n");
  auto& prov = DeviceInputProvider::instance();

  // ===== KeyboardInput — PressedThisFrame edge ACROSS two frames (Mode=1). Key='A' (VK 65). =====
  // TiXL cs:22 justPressed = !wasDown && isDown. Frame 1: A goes down from released -> justPressed=true.
  // Frame 2: A still down -> !wasDown(true) is false -> justPressed=false. TEETH 1 drops the _wasDown
  // write, so frame 2 STILL sees wasDown=false -> re-fires justPressed=true (the held key re-pulses).
  {
    setDeviceInputBug(injectBug ? 1 : 0);
    SymbolLibrary lib = makeLib("KeyboardInput", {{"Key", 65.0f}, {"Mode", 1.0f}});  // A, PressedThisFrame
    ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
    initResidentCache(g);
    std::map<std::string, DeviceInputNodeState> st;
    prov.reset();
    prov.mutableState().keys[65] = true;  // A held both frames
    cookDeviceInputNodes(g, 1920, 1080, &lib, st);
    float f1 = g.node("1")->extOut[0];
    checkEq(injectBug, "KeyboardInput.PressedThisFrame frame1 (A rises)", f1, 1.0f, 1.0f);  // both fire f1
    cookDeviceInputNodes(g, 1920, 1080, &lib, st);  // A STILL held — the edge must go false
    float f2 = g.node("1")->extOut[0];
    // production: 0 (edge consumed). bug (TEETH 1): 1 (state never advanced -> re-pulses).
    checkEq(injectBug, "KeyboardInput.PressedThisFrame frame2 (A held -> edge OFF)", f2, 0.0f, 1.0f);
    setDeviceInputBug(0);
  }

  // ===== KeyboardInput — IsDown (Mode=3, the .t3 default) + Off (Mode=0) sanity. Key='A'. =====
  {
    SymbolLibrary lib = makeLib("KeyboardInput", {{"Key", 65.0f}, {"Mode", 3.0f}});  // IsDown
    ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
    initResidentCache(g);
    std::map<std::string, DeviceInputNodeState> st;
    prov.reset(); prov.mutableState().keys[65] = true;
    cookDeviceInputNodes(g, 1920, 1080, &lib, st);
    checkEq(injectBug, "KeyboardInput.IsDown(A held)", g.node("1")->extOut[0], 1.0f, 1.0f);
    prov.mutableState().keys[65] = false;  // release
    cookDeviceInputNodes(g, 1920, 1080, &lib, st);
    checkEq(injectBug, "KeyboardInput.IsDown(A released)", g.node("1")->extOut[0], 0.0f, 0.0f);
  }

  // ===== KeyboardInputAsInt — NumRow scan, hold '7' (VK 55). Mode=0. Zone=0. =====
  // cs:33 newPressedNumber = i - 48; '7' at VK 55 -> 55-48 = 7. TEETH 2 drops the offset -> reports 55.
  {
    setDeviceInputBug(injectBug ? 2 : 0);
    SymbolLibrary lib = makeLib("KeyboardInputAsInt", {{"Zone", 0.0f}, {"Mode", 0.0f}});
    ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
    initResidentCache(g);
    std::map<std::string, DeviceInputNodeState> st;
    prov.reset(); prov.mutableState().keys[55] = true;  // '7'
    cookDeviceInputNodes(g, 1920, 1080, &lib, st);
    // production: 7. bug (TEETH 2): 55 (raw VK, no i-48).
    checkEq(injectBug, "KeyboardInputAsInt NumRow '7' -> 7", g.node("1")->extOut[0], 7.0f, 55.0f);
    prov.mutableState().keys[55] = false;  // release -> Mode 0 returns to 0 (cs:56)
    cookDeviceInputNodes(g, 1920, 1080, &lib, st);
    checkEq(injectBug, "KeyboardInputAsInt release -> 0 (Mode0)", g.node("1")->extOut[0], 0.0f, 0.0f);
    setDeviceInputBug(0);
  }

  // ===== MouseInput — SignedPosition (Mode=1, the .t3 default), non-centered cursor + aspect=2, scale=3. =====
  // cs:39-41: Position = (LastPos - 0.5) * (aspect,-1) * scale. LastPos=(0.75,0.25), aspect=2, scale=3:
  //   x = (0.75-0.5)*2*3 = 0.25*6 = 1.5 ;  y = (0.25-0.5)*(-1)*3 = (-0.25)*(-3) = 0.75.
  // TEETH 3 drops the -0.5 recenter -> x = 0.75*2*3 = 4.5, y = 0.25*(-1)*3 = -0.75.
  {
    setDeviceInputBug(injectBug ? 3 : 0);
    SymbolLibrary lib = makeLib("MouseInput", {{"OutputMode", 1.0f}, {"Scale", 3.0f}});  // SignedPosition
    ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
    initResidentCache(g);
    std::map<std::string, DeviceInputNodeState> st;
    prov.reset();
    prov.mutableState().mouseX = 0.75f;
    prov.mutableState().mouseY = 0.25f;
    prov.mutableState().leftButtonDown = true;
    cookDeviceInputNodes(g, 2000, 1000, &lib, st);  // aspect = 2000/1000 = 2
    checkEq(injectBug, "MouseInput.SignedPosition.x", g.node("1")->extOut[0], 1.5f, 4.5f);
    checkEq(injectBug, "MouseInput.SignedPosition.y", g.node("1")->extOut[1], 0.75f, -0.75f);
    checkEq(injectBug, "MouseInput.IsLeftButtonDown", g.node("1")->extOut[2], 1.0f, 1.0f);
    // Position3d flat mode = raw normalized cursor (cs:41) — independent of the recenter fork.
    checkEq(injectBug, "MouseInput.Position3d.x (raw normalized)", g.node("1")->extOut[3], 0.75f, 0.75f);
    setDeviceInputBug(0);
  }

  // ===== Gamepad — IsConnected gate (Index=0). Inject a synthetic connected pad (deferred-hw-verify). =====
  // cs:31-39: absent controller -> IsConnected=false; present -> true. TEETH 4 forces true regardless.
  {
    setDeviceInputBug(injectBug ? 4 : 0);
    SymbolLibrary lib = makeLib("Gamepad", {{"Index", 0.0f}});
    ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
    initResidentCache(g);
    std::map<std::string, DeviceInputNodeState> st;
    prov.reset();  // pad[0].isConnected = false (no hardware)
    cookDeviceInputNodes(g, 1920, 1080, &lib, st);
    // production: 0 (absent). bug (TEETH 4): 1 (forced connected).
    checkEq(injectBug, "Gamepad.IsConnected(absent)", g.node("1")->extOut[0], 0.0f, 1.0f);
    prov.mutableState().pads[0].isConnected = true;  // synthetic connect
    cookDeviceInputNodes(g, 1920, 1080, &lib, st);
    checkEq(injectBug, "Gamepad.IsConnected(present)", g.node("1")->extOut[0], 1.0f, 1.0f);
    setDeviceInputBug(0);
  }

  prov.reset();  // leave the singleton clean for any later selftest in the sweep
  // did-not-trip → return 0 (dead-tooth contract): if injectBug was requested but NO assertion could
  // actually bite (every bugWant == prodWant), report 0 so --bite's NO-BITE list surfaces it.
  if (injectBug && !g_anyBite) {
    printf("[selftest] deviceinput NO-BITE (injected bug did not trip any tooth) -> 0\n");
    return 0;
  }
  printf("[selftest] deviceinput %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw
