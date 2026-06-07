// hand.cpp — see hand.h. Pure C++: file read + ImGui IO, no ObjC.
#include "verify/hand/hand.h"

#include <cstdio>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "imgui.h"

#ifndef SW_EYE_DIR
#define SW_EYE_DIR "/tmp/sw_eye"
#endif

namespace sw::hand {

namespace {

// One frame's worth of injected input. A high-level command (click/drag/...)
// expands into a sequence of these so down/up land on separate frames.
struct Step {
  bool setPos = false;
  float x = 0, y = 0;
  bool setBtn = false;
  int btn = 0;  // 0=left 1=right 2=middle
  bool btnDown = false;
  bool setWheel = false;
  float wx = 0, wy = 0;
  bool setKey = false;
  int key = 0;          // ImGuiKey value
  bool keyDown = false;
};

std::deque<Step> g_pending;

std::string cmdPath() { return std::string(SW_EYE_DIR) + "/hand"; }

// Key-name -> ImGuiKey. Single letters a-z / A-Z and digits 0-9 map directly;
// a handful of named keys cover what the agent needs to drive editing.
ImGuiKey keyFromName(const std::string& n) {
  if (n.size() == 1) {
    char c = n[0];
    if (c >= 'a' && c <= 'z') return (ImGuiKey)(ImGuiKey_A + (c - 'a'));
    if (c >= 'A' && c <= 'Z') return (ImGuiKey)(ImGuiKey_A + (c - 'A'));
    if (c >= '0' && c <= '9') return (ImGuiKey)(ImGuiKey_0 + (c - '0'));
  }
  if (n == "backspace") return ImGuiKey_Backspace;
  if (n == "delete" || n == "del") return ImGuiKey_Delete;
  if (n == "enter" || n == "return") return ImGuiKey_Enter;
  if (n == "escape" || n == "esc") return ImGuiKey_Escape;
  if (n == "tab") return ImGuiKey_Tab;
  if (n == "space") return ImGuiKey_Space;
  if (n == "left") return ImGuiKey_LeftArrow;
  if (n == "right") return ImGuiKey_RightArrow;
  if (n == "up") return ImGuiKey_UpArrow;
  if (n == "down") return ImGuiKey_DownArrow;
  return ImGuiKey_None;
}

// Modifier-name -> ImGuiMod_*. AddKeyEvent on these updates io.KeyCtrl/Super/...
ImGuiKey modFromName(const std::string& n) {
  if (n == "cmd" || n == "super" || n == "win" || n == "meta") return ImGuiMod_Super;
  if (n == "ctrl" || n == "control") return ImGuiMod_Ctrl;
  if (n == "shift") return ImGuiMod_Shift;
  if (n == "alt" || n == "option" || n == "opt") return ImGuiMod_Alt;
  return ImGuiKey_None;
}

void enqueueKeyStep(ImGuiKey k, bool down) {
  Step s; s.setKey = true; s.key = (int)k; s.keyDown = down;
  g_pending.push_back(s);
}

// Press+release at (x,y): frame 1 positions cursor AND presses (ImGui sees it
// hovered+held that frame), frame 2 releases -> the click registers.
void enqueueClick(int btn, float x, float y) {
  Step down;
  down.setPos = true; down.x = x; down.y = y;
  down.setBtn = true; down.btn = btn; down.btnDown = true;
  Step up;
  up.setBtn = true; up.btn = btn; up.btnDown = false;
  g_pending.push_back(down);
  g_pending.push_back(up);
}

void parseLine(const std::string& line) {
  std::istringstream is(line);
  std::string op;
  if (!(is >> op)) return;

  if (op == "move") {
    float x, y;
    if (is >> x >> y) { Step s; s.setPos = true; s.x = x; s.y = y; g_pending.push_back(s); }
  } else if (op == "click") {
    float x, y;
    if (is >> x >> y) enqueueClick(0, x, y);
  } else if (op == "rclick") {
    float x, y;
    if (is >> x >> y) enqueueClick(1, x, y);
  } else if (op == "double") {
    float x, y;
    if (is >> x >> y) { enqueueClick(0, x, y); enqueueClick(0, x, y); }  // two clicks within double-click time
  } else if (op == "drag") {
    float x0, y0, x1, y1;
    if (is >> x0 >> y0 >> x1 >> y1) {
      const int N = 12;  // interpolation steps so ImGui's drag tracking follows smoothly
      Step down;
      down.setPos = true; down.x = x0; down.y = y0;
      down.setBtn = true; down.btn = 0; down.btnDown = true;
      g_pending.push_back(down);
      for (int i = 1; i <= N; ++i) {
        float t = (float)i / (float)N;
        Step s; s.setPos = true; s.x = x0 + (x1 - x0) * t; s.y = y0 + (y1 - y0) * t;
        g_pending.push_back(s);
      }
      Step up;
      up.setBtn = true; up.btn = 0; up.btnDown = false;
      g_pending.push_back(up);
    }
  } else if (op == "scroll") {
    float x, y, dx, dy;
    if (is >> x >> y >> dx >> dy) {
      Step pos; pos.setPos = true; pos.x = x; pos.y = y;
      Step wheel; wheel.setWheel = true; wheel.wx = dx; wheel.wy = dy;
      g_pending.push_back(pos);
      g_pending.push_back(wheel);
    }
  } else if (op == "key") {
    std::string name;
    if (is >> name) {
      ImGuiKey k = keyFromName(name);
      if (k != ImGuiKey_None) { enqueueKeyStep(k, true); enqueueKeyStep(k, false); }
    }
  } else if (op == "keychord") {
    std::string mods, name;
    if (is >> mods >> name) {
      ImGuiKey k = keyFromName(name);
      if (k == ImGuiKey_None) return;
      std::vector<ImGuiKey> mk;
      std::stringstream ms(mods);
      std::string tok;
      while (std::getline(ms, tok, '+')) {
        ImGuiKey m = modFromName(tok);
        if (m != ImGuiKey_None) mk.push_back(m);
      }
      for (ImGuiKey m : mk) enqueueKeyStep(m, true);          // mods down
      enqueueKeyStep(k, true);                                // key down (key registers here)
      enqueueKeyStep(k, false);                               // key up
      for (auto it = mk.rbegin(); it != mk.rend(); ++it) enqueueKeyStep(*it, false);  // mods up
    }
  }
}

}  // namespace

void poll() {
  const std::string p = cmdPath();
  std::ifstream f(p);
  if (!f) return;
  std::stringstream buf;
  buf << f.rdbuf();
  f.close();
  std::remove(p.c_str());  // consume: one command file = one batch of commands

  std::istringstream lines(buf.str());
  std::string line;
  while (std::getline(lines, line)) parseLine(line);
}

void applyPendingStep() {
  if (g_pending.empty()) return;
  Step s = g_pending.front();
  g_pending.pop_front();
  ImGuiIO& io = ImGui::GetIO();
  if (s.setPos) io.AddMousePosEvent(s.x, s.y);
  if (s.setBtn) io.AddMouseButtonEvent(s.btn, s.btnDown);
  if (s.setWheel) io.AddMouseWheelEvent(s.wx, s.wy);
  if (s.setKey) io.AddKeyEvent((ImGuiKey)s.key, s.keyDown);
}

int runSelfTest(bool injectBug) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(400, 300);
  io.DeltaTime = 1.0f / 60.0f;
  unsigned char* pixels;
  int tw, th;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &tw, &th);  // build atlas so NewFrame won't assert
  io.Fonts->SetTexID((ImTextureID)1);

  // --- mouse: click press-then-release ---
  g_pending.clear();
  if (!injectBug) parseLine("click 123 77");  // bug case: enqueue nothing -> hand does nothing

  // Frame 1: position + press. NewFrame consumes the queued IO events.
  applyPendingStep();
  ImGui::NewFrame();
  bool downOk = io.MouseDown[0] && io.MousePos.x == 123 && io.MousePos.y == 77;
  ImGui::EndFrame();

  // Frame 2: release.
  applyPendingStep();
  ImGui::NewFrame();
  bool upOk = !io.MouseDown[0];
  ImGui::EndFrame();

  // --- keyboard: Cmd+Z chord; assert the modifier is held on the frame Z presses.
  // ConfigMacOSXBehaviors (default on __APPLE__) swaps Cmd->Ctrl inside
  // AddKeyEvent, so injecting "cmd" lands in io.KeyCtrl (imgui's cross-platform
  // convention: ImGuiMod_Ctrl == Cmd on Mac). The real app must therefore detect
  // Cmd+Z via Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z), NOT io.KeySuper.
  g_pending.clear();
  if (!injectBug) parseLine("keychord cmd z");
  bool chordOk = false;
  while (!g_pending.empty()) {
    applyPendingStep();
    ImGui::NewFrame();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) chordOk = true;
    ImGui::EndFrame();
  }

  bool pass = downOk && upOk && chordOk;
  printf("[selftest-hand] down=%d up=%d chord=%d -> %s\n", (int)downOk, (int)upOk, (int)chordOk,
         pass ? "PASS" : "FAIL");
  ImGui::DestroyContext();
  return pass ? 0 : 1;
}

}  // namespace sw::hand
