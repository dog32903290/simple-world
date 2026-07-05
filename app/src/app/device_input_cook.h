// app/device_input_cook — the per-frame cook for the io/input DEVICE nodes (KeyboardInput,
// KeyboardInputAsInt, MouseInput, Gamepad). The device-family sibling of cookAudioReactionNodes
// (frame_cook.cpp): walk the resident graph, resolve each device node's Float inputs through the
// resident drivers, read the process-global DeviceInputProvider snapshot (the shell fills it from
// imgui io each frame), run the TiXL .cs semantics, and write the outputs onto ResidentNode::extOut[].
// evalResidentFloat returns extOut[outputPortIndex] via the generic no-evaluate path.
//
// Zone: app — it owns the runtime DeviceInputProvider read + drives the runtime cook (app→runtime,
// legal). No imgui / AppKit here (the SHELL translates imgui io → the provider; this cook only reads
// the provider's plain POD). Mirror of the AudioReaction cook: bespoke pass, per-node cross-frame state.
#pragma once

#include <map>
#include <string>

namespace sw {

struct ResidentEvalGraph;  // runtime/resident_eval_graph.h
class SymbolLibrary;       // runtime/compound_graph.h (Automation drivers resolve through it)

// Per-node cross-frame memory for the device family (mirror of AudioReactionState), keyed by resident
// PATH (survives projection rebuilds + per-instance inside compounds). KeyboardInput's edge modes and
// KeyboardInputAsInt's hold latch are the only stateful ops; the rest read the provider statelessly.
struct DeviceInputNodeState {
  bool wasDown = false;         // KeyboardInput._wasDown (KeyboardInput.cs:24 — edge memory)
  int lastPressedNumber = 0;    // KeyboardInputAsInt._lastPressedNumber (cs:17 — hold latch)
};

// deviceInputBug — the golden TEETH hook (0 = production; the real cook always passes 0). Non-zero
// values corrupt a SPECIFIC real cook step so a fixed (bug-independent) expected value bites on the
// actual cook path (NOT a want-flip). See device_input_cook.cpp for the per-mode meaning. Sticky module
// switch; the golden clears it back to 0 after each bug run (mirror of setAnimValueBug).
void setDeviceInputBug(int mode);

// Cook every device-input node this frame. `reqW`/`reqH` = the frame resolution (MouseInput's
// SignedPosition aspect ratio, = TiXL context.RequestedResolution). `lib` = the SymbolLibrary
// (Automation drivers on the Float inputs resolve through it). `state` = the per-path cross-frame store.
void cookDeviceInputNodes(ResidentEvalGraph& g, uint32_t reqW, uint32_t reqH, const SymbolLibrary* lib,
                          std::map<std::string, DeviceInputNodeState>& state);

// Isolated proof (--selftest-deviceinput): inject a DeviceInputProvider state, cook each device node on
// the REAL resident path, assert the TiXL .cs semantics (key edge modes, digit-zone scan, mouse
// SignedPosition math, gamepad connected gate). injectBug flips deviceInputBug so the RED leg fires on
// the real cook. Returns 0 pass / 1 fail / 0 when the injected bug did not trip (dead-tooth contract).
int runDeviceInputSelfTest(bool injectBug);

}  // namespace sw
