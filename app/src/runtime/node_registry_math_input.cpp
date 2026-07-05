// runtime/node_registry_math_input — self-registering MATH NodeSpec leaf: the io/input device family
// (KeyboardInput, KeyboardInputAsInt, MouseInput, Gamepad). Split into its OWN leaf (NOT appended to
// node_registry_math_anim.cpp, which is at 372/400 lines) via the MathOp self-reg sink — adding these
// four rows there would blow the line-count ratchet; a new leaf is the data-driven answer (rule 7).
//
// All four are STATEFUL, externally-cooked nodes (evaluate==nullptr), the EXACT mirror of AudioReaction:
// their value comes from a per-frame DEVICE snapshot (the DeviceInputProvider singleton, populated by the
// shell from imgui io) + per-node memory (KeyboardInput's _wasDown edge, KeyboardInputAsInt's hold latch).
// So they carry no pure evaluate() — the app's cookDeviceInputNodes (app/frame_cook seam, mirror of
// cookAudioReactionNodes) resolves each node's Float inputs, reads the DeviceInputProvider, and writes the
// outputs onto ResidentNode::extOut[]; evalResidentFloat returns extOut[outputPortIndex] via the GENERIC
// no-evaluate path (resident_eval_graph.cpp:114). OUTPUT PORTS FIRST → output-port index == extOut index.
//
// TiXL authority: Operators/Lib/io/input/{KeyboardInput,KeyboardInputAsInt,MouseInput,Gamepad}.cs (+ .t3
// defaults). Category = TiXL Symbol.Namespace "Lib.io.input" → "io.input".
#include "runtime/graph.h"             // NodeSpec, PortSpec, Widget
#include "runtime/math_op_registry.h"  // MathOp / mathSpecSink

namespace sw {
namespace {

// ── KeyboardInput (KeyboardInput.cs) ──────────────────────────────────────────────────────────────
// ONE bool output Result = the mode-selected key state (KeyboardInput.cs:26-43). STATEFUL: the
// PressedThisFrame / ReleasedThisFrame modes compare against _wasDown (cs:24) — cross-frame edge memory,
// so evaluate==nullptr and cookDeviceInputNodes keeps the per-path _wasDown. Result is Bool→Float 0/1
// (Cut 32). Inputs in TiXL .cs decl order (cs:56-60): Key, then Mode. .t3 DEFAULTS (KeyboardInput.t3):
// Key=13 (VK_RETURN / Enter), Mode=3 (IsDown). Key is a Windows VK code (MappedType=Key) carried on the
// float rail (the cook does (int)(v+0.5)); Mode is a Widget::Enum selector (MappedType=Modes, cs:48-54).
static const MathOp _reg_KeyboardInput{
    {"KeyboardInput", "KeyboardInput",
     {{"Result", "Result", "Float", false},
      {"Key", "Key", "Float", true, 13.0f, 0.0f, 255.0f, Widget::Slider, {}, true},
      {"Mode", "Mode", "Float", true, 3.0f, 0.0f, 3.0f, Widget::Enum,
       {"Off", "PressedThisFrame", "ReleasedThisFrame", "IsDown"}, true}},
     nullptr,
     "io.input"}
};

// ── KeyboardInputAsInt (KeyboardInputAsInt.cs) ─────────────────────────────────────────────────────
// ONE int output PressedNumber = the first held digit in the selected Zone (cs:26-51), optionally HELD
// (Mode=1 keeps the last value while nothing new is pressed, cs:60-70) — cross-frame latch memory, so
// evaluate==nullptr and cookDeviceInputNodes keeps _lastPressedNumber per path. Result is int→Float.
// Inputs in TiXL .cs decl order (cs:85-89): Zone, then Mode. .t3 DEFAULTS: Zone=0 (NumRow), Mode=0
// (IsDown / original). Zone scans keys[48..57] (0) or keys[96..105] (1); Mode 0=live, 1=hold (cs:53-71).
// Both are Widget::Enum selectors (Zones cs:79-83 / Modes cs:74-78).
static const MathOp _reg_KeyboardInputAsInt{
    {"KeyboardInputAsInt", "KeyboardInputAsInt",
     {{"PressedNumber", "PressedNumber", "Float", false},
      {"Zone", "Zone", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"NumRow", "Numpad"}, true},
      {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"IsDown", "KeepActive"}, true}},
     nullptr,
     "io.input"}
};

// ── MouseInput (MouseInput.cs) ─────────────────────────────────────────────────────────────────────
// THREE TiXL outputs: Position(Vec2), IsLeftButtonDown(bool), Position3d(Vec3) — MouseInput.cs:9-16.
// FORK fork-vec-output-as-n-scalar-ports (same as RequestedResolution/GridPosition): Position→Position.x/y,
// Position3d→Position3d.x/y/z. OUTPUT PORTS FIRST → extOut index: [0]Position.x [1]Position.y
// [2]IsLeftButtonDown [3]Position3d.x [4]Position3d.y [5]Position3d.z. The 4 OutputModes (cs:33-67) shape
// Position from the normalized LastPosition; Position3d in the flat modes = (LastPos.x, LastPos.y, 0)
// (cs:37/41). evaluate==nullptr (needs the DeviceInputProvider snapshot + the cook-context aspect ratio,
// like RequestedResolution reads ctx.requested* — impossible in a pure evaluate). Inputs in .cs decl order
// (cs:75-82): DoUpdate(bool), OutputMode(enum), Scale(float). .t3 DEFAULTS: OutputMode=1 (SignedPosition),
// DoUpdate=false, Scale=1.0.
// DEFERRED FORK fork-mouseinput-worldplane-modes: OutputModes 2/3 (OnWorldXYPlane/OnWorldFloorPlane,
// cs:43-64) need the cook-context camera matrices (CameraToClipSpace/WorldToCamera) to ray-cast onto a
// world plane — that matrix context is not on this value-cook seam yet, so those two enum values fall back
// to the SignedPosition math (named, not silently wrong: the enum labels ship so the wire cardinality is
// stable, but modes 2/3 currently reproduce mode 1 until the camera-matrix context reaches this cook).
static const MathOp _reg_MouseInput{
    {"MouseInput", "MouseInput",
     {{"Position.x", "Position", "Float", false},
      {"Position.y", "Position.y", "Float", false},
      {"IsLeftButtonDown", "IsLeftButtonDown", "Float", false},
      {"Position3d.x", "Position3d", "Float", false},
      {"Position3d.y", "Position3d.y", "Float", false},
      {"Position3d.z", "Position3d.z", "Float", false},
      {"DoUpdate", "DoUpdate", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      {"OutputMode", "OutputMode", "Float", true, 1.0f, 0.0f, 3.0f, Widget::Enum,
       {"Normalized", "SignedPosition", "OnWorldXYPlane", "OnWorldFloorPlane"}, true},
      {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 100.0f, Widget::Slider, {}, true}},
     nullptr,
     "io.input"}
};

// ── Gamepad (Gamepad.cs) ───────────────────────────────────────────────────────────────────────────
// TiXL outputs TWO slots: State(Dict<float> — 22 named channels, cs:43-71) + IsConnected(bool, cs:13).
// PARTIAL (per lane brief): the Dict<float> State output rides the DICT currency rail, which another
// worker is building — it is NOT expressible on the scalar extOut[] rail (a Dict is a keyed map, not 8
// packed floats), so State is DEFERRED to that seam (fork-gamepad-state-dict-deferred). What ships now is
// the IsConnected bool output (cs:31-39: false when controller Index is absent, true otherwise) — on
// macOS there is no XInput and no wired GameController capture yet, so the DeviceInputProvider's pad slots
// stay zero-init (isConnected=false) → IsConnected=false in production; the golden injects a synthetic
// connected pad (deferred-hw-verify). evaluate==nullptr (reads the provider). Input: Index (cs:84-85),
// clamped [0,3] (cs:23). .t3 DEFAULT: Index=0.
static const MathOp _reg_Gamepad{
    {"Gamepad", "Gamepad",
     {{"IsConnected", "IsConnected", "Float", false},
      {"Index", "Index", "Float", true, 0.0f, 0.0f, 3.0f, Widget::Slider, {}, true}},
     nullptr,
     "io.input"}
};

}  // namespace
}  // namespace sw
