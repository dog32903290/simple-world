// app/device_input_cook — see device_input_cook.h. The device-family cook (mirror of
// cookAudioReactionNodes): resolve each node's Float inputs, read the DeviceInputProvider snapshot,
// run the TiXL .cs semantics, write onto ResidentNode::extOut[]. Ported from Operators/Lib/io/input.
#include "app/device_input_cook.h"

#include <cmath>
#include <cstdio>

#include "runtime/compound_graph.h"        // SymbolLibrary
#include "runtime/device_input_provider.h"  // DeviceInputProvider / DeviceInputState
#include "runtime/resident_eval_graph.h"   // ResidentEvalGraph / ResidentNode / resolveResidentFloatInputs

namespace sw {

// ── TEETH hook ──────────────────────────────────────────────────────────────────────────────────────
// 0 = production. The golden flips it around the REAL cook so a fixed expected value bites the actual
// cook path (not a want-flip). Meanings:
//   1 = KeyboardInput: DROP the _wasDown state write → the PressedThisFrame edge re-pulses every frame
//       (a held key keeps reporting justPressed). Bites the KeyboardInput edge golden.
//   2 = KeyboardInputAsInt: DROP the digit-zone offset (report the raw VK code, not i-48) → the number
//       is wrong. Bites the digit-scan golden.
//   3 = MouseInput: DROP the -0.5 recenter in SignedPosition (position no longer signed around 0).
//       Bites the mouse SignedPosition golden.
//   4 = Gamepad: FORCE IsConnected true regardless of the pad slot → the absent-controller gate breaks.
static int s_deviceInputBug = 0;
void setDeviceInputBug(int mode) { s_deviceInputBug = mode; }

namespace {

// Read a resolved Float input (truncate-to-int convention for the int-on-Float params, same as the
// AudioReaction cook's (int)(P["InputBand"]+0.5f)).
int intInput(const std::map<std::string, float>& P, const char* id) {
  auto it = P.find(id);
  return it == P.end() ? 0 : (int)std::lround(it->second);
}
float floatInput(const std::map<std::string, float>& P, const char* id, float def) {
  auto it = P.find(id);
  return it == P.end() ? def : it->second;
}

// KeyboardInput.cs:16-43 — mode-selected key state (Result). Uses + updates st.wasDown (the edge memory).
void cookKeyboardInput(ResidentNode& rn, const std::map<std::string, float>& P,
                       const DeviceInputState& dev, DeviceInputNodeState& st) {
  const int keyIndex = intInput(P, "Key");
  const int mode = intInput(P, "Mode");
  const bool isDown = (keyIndex >= 0 && keyIndex < 256) ? dev.keys[(size_t)keyIndex] : false;
  const bool justPressed = !st.wasDown && isDown;   // cs:22
  const bool justReleased = !isDown && st.wasDown;  // cs:23
  if (s_deviceInputBug != 1) st.wasDown = isDown;   // cs:24 (TEETH 1 drops this write)

  bool result = false;
  switch (mode) {              // cs:26-43
    case 0: result = false; break;          // Off
    case 1: result = justPressed; break;    // PressedThisFrame
    case 2: result = justReleased; break;   // ReleasedThisFrame
    default: result = isDown; break;        // 3 = IsDown (default)
  }
  rn.extOut[0] = result ? 1.0f : 0.0f;  // Result (Bool→Float 0/1)
}

// KeyboardInputAsInt.cs:19-71 — first held digit in the selected Zone, optionally HELD. Uses/updates
// st.lastPressedNumber (the hold latch).
void cookKeyboardInputAsInt(ResidentNode& rn, const std::map<std::string, float>& P,
                            const DeviceInputState& dev, DeviceInputNodeState& st) {
  const int zone = intInput(P, "Zone");
  const int mode = intInput(P, "Mode");
  const int base = (zone == 1) ? 96 : 48;  // cs:40 Numpad(96..105) : cs:28 NumRow(48..57)
  const int offset = (s_deviceInputBug == 2) ? 0 : base;  // TEETH 2 drops the i-base offset
  bool keyPressed = false;
  int newPressedNumber = 0;
  for (int i = base; i <= base + 9; ++i) {  // cs:29-37 / cs:41-49 (first held digit wins)
    if (i >= 0 && i < 256 && dev.keys[(size_t)i]) {
      newPressedNumber = i - offset;
      keyPressed = true;
      break;
    }
  }

  float out;
  if (mode == 1) {  // cs:60-70 Hold value — only update on a NEW key press
    if (keyPressed && newPressedNumber != st.lastPressedNumber) {
      st.lastPressedNumber = newPressedNumber;
      out = (float)newPressedNumber;
    } else if (!keyPressed) {
      out = (float)st.lastPressedNumber;
    } else {
      out = (float)st.lastPressedNumber;  // held, unchanged (cs: value stays PressedNumber.Value)
    }
  } else {  // cs:55-58 Original — value is 0 when released
    out = keyPressed ? (float)newPressedNumber : 0.0f;
    st.lastPressedNumber = (int)out;  // cs:57
  }
  rn.extOut[0] = out;  // PressedNumber (int→Float)
}

// MouseInput.cs:25-72 — Position(Vec2) / IsLeftButtonDown / Position3d(Vec3) from the normalized cursor.
void cookMouseInput(ResidentNode& rn, const std::map<std::string, float>& P, const DeviceInputState& dev,
                    uint32_t reqW, uint32_t reqH) {
  const int mode = intInput(P, "OutputMode");
  const float scale = floatInput(P, "Scale", 1.0f);
  const float aspect = (reqH > 0) ? ((float)reqW / (float)reqH) : 1.0f;  // cs:29
  const float lx = dev.mouseX, ly = dev.mouseY;  // MouseInput.LastPosition (normalized 0..1)

  float posX, posY;
  // cs:33-67. Modes 2/3 (world planes) DEFERRED (need camera matrices) → fall to SignedPosition (mode 1).
  if (mode == 0) {  // Normalized (cs:35-37)
    posX = lx;
    posY = ly;
  } else {  // SignedPosition (cs:39-41) — AND the deferred world modes fall here (named fork in the spec)
    const float recenter = (s_deviceInputBug == 3) ? 0.0f : 0.5f;  // TEETH 3 drops the -0.5 recenter
    posX = (lx - recenter) * aspect * scale;
    posY = (ly - recenter) * (-1.0f) * scale;  // *(aspect,-1): y flips, x scales by aspect
  }
  // Position3d in the flat modes (cs:37/41): (LastPos.x, LastPos.y, 0) — the raw normalized cursor.
  rn.extOut[0] = posX;                                  // Position.x
  rn.extOut[1] = posY;                                  // Position.y
  rn.extOut[2] = dev.leftButtonDown ? 1.0f : 0.0f;      // IsLeftButtonDown (cs:69)
  rn.extOut[3] = lx;                                    // Position3d.x
  rn.extOut[4] = ly;                                    // Position3d.y
  rn.extOut[5] = 0.0f;                                  // Position3d.z
}

// Gamepad.cs:21-73 — IsConnected gate (the Dict<float> State output is deferred to the dict rail).
void cookGamepad(ResidentNode& rn, const std::map<std::string, float>& P, const DeviceInputState& dev) {
  int index = intInput(P, "Index");
  if (index < 0) index = 0;
  if (index > 3) index = 3;  // cs:23 .Clamp(0,3)
  const bool connected = (s_deviceInputBug == 4) ? true : dev.pads[(size_t)index].isConnected;  // cs:31-39
  rn.extOut[0] = connected ? 1.0f : 0.0f;  // IsConnected
}

}  // namespace

void cookDeviceInputNodes(ResidentEvalGraph& g, uint32_t reqW, uint32_t reqH, const SymbolLibrary* lib,
                          std::map<std::string, DeviceInputNodeState>& state) {
  const DeviceInputState& dev = DeviceInputProvider::instance().snapshot();
  ResidentEvalCtx rctx;
  rctx.lib = lib;  // Automation drivers on the Float inputs resolve through the lib
  for (ResidentNode& rn : g.nodes) {
    const std::string& t = rn.opType;
    if (t != "KeyboardInput" && t != "KeyboardInputAsInt" && t != "MouseInput" && t != "Gamepad")
      continue;
    const std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);
    DeviceInputNodeState& st = state[rn.path];
    if (t == "KeyboardInput") cookKeyboardInput(rn, P, dev, st);
    else if (t == "KeyboardInputAsInt") cookKeyboardInputAsInt(rn, P, dev, st);
    else if (t == "MouseInput") cookMouseInput(rn, P, dev, reqW, reqH);
    else cookGamepad(rn, P, dev);
  }
}

}  // namespace sw
