// hand.cpp — see hand.h. Pure C++: file read + ImGui IO, no ObjC.
#include "verify/hand/hand.h"

#include <cstdio>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_node_editor.h"

namespace ed = ax::NodeEditor;

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
  bool setText = false;
  std::string text;     // UTF-8 to push via io.AddInputCharactersUTF8
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

// Press+release at (x,y), expanded over THREE frames:
//   frame 1: MOVE the cursor to (x,y) only — no button. This settles ImGui's
//            HoveredWindow + the node-editor's per-frame hover/hit state AT the
//            target before any button transition.
//   frame 2: button DOWN (cursor already there & hovered).
//   frame 3: button UP -> the click registers.
//
// Why the separate move frame (the gap-2 fix): imgui-node-editor's BuildControl
// hit-tests via ImGui::IsWindowHovered()/IsMouseHoveringRect, which read the
// HoveredWindow resolved at NewFrame from io.MousePos. When a click TELEPORTS the
// cursor and presses on the SAME frame, that frame's hover state was computed
// from the PREVIOUS (far-away) position, so the node isn't hovered when the press
// lands — ClickedNode stays null and SelectAction selects nothing. Observed live
// as a non-deterministic "click sometimes doesn't select" (selectedNode stuck).
// Moving first, pressing next, makes selection deterministic. (rclick/double reuse
// this, so right-click context + double-click open get the same settled hover.)
void enqueueClick(int btn, float x, float y) {
  Step move;
  move.setPos = true; move.x = x; move.y = y;     // settle hover at target first
  Step down;
  down.setPos = true; down.x = x; down.y = y;     // keep cursor pinned (no drift)
  down.setBtn = true; down.btn = btn; down.btnDown = true;
  Step up;
  up.setBtn = true; up.btn = btn; up.btnDown = false;
  g_pending.push_back(move);
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
  } else if (op == "drag" || op == "rdrag") {
    // drag = left-button; rdrag = right-button (first user: timeline right-drag PAN — the
    // pan gesture and the right-click context menu share the button, so verifying "pan
    // doesn't open the menu" needs a real right-drag).
    const int btn = (op == "rdrag") ? 1 : 0;
    float x0, y0, x1, y1;
    if (is >> x0 >> y0 >> x1 >> y1) {
      const int N = 12;  // interpolation steps so ImGui's drag tracking follows smoothly
      Step down;
      down.setPos = true; down.x = x0; down.y = y0;
      down.setBtn = true; down.btn = btn; down.btnDown = true;
      g_pending.push_back(down);
      for (int i = 1; i <= N; ++i) {
        float t = (float)i / (float)N;
        Step s; s.setPos = true; s.x = x0 + (x1 - x0) * t; s.y = y0 + (y1 - y0) * t;
        g_pending.push_back(s);
      }
      Step up;
      up.setBtn = true; up.btn = btn; up.btnDown = false;
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
  } else if (op == "text") {
    // text <utf8...> — push the REST of the line (after "text ") into the focused
    // InputText via io.AddInputCharactersUTF8. The remainder is taken verbatim, so
    // it keeps embedded spaces AND multibyte UTF-8 (CJK: the rename dialog's first
    // user is a Chinese node name). One Step carries the whole string; it is灌'd in
    // applyPendingStep right before NewFrame, same timing as key steps, so ImGui's
    // input queue trickles the chars into the active text widget this frame.
    std::string rest;
    std::getline(is, rest);
    // drop exactly one leading space (the delimiter after "text"); keep the rest.
    if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
    if (!rest.empty()) {
      Step s; s.setText = true; s.text = rest;
      g_pending.push_back(s);
    }
  } else if (op == "key") {
    std::string name;
    if (is >> name) {
      ImGuiKey k = keyFromName(name);
      if (k != ImGuiKey_None) { enqueueKeyStep(k, true); enqueueKeyStep(k, false); }
    }
  } else if (op == "keydown" || op == "keyup") {
    // keydown/keyup <name> — half a key press, held across subsequent lines. The first
    // user is modifier-held MOUSE gestures (shift-click add-select, cmd-click deselect in
    // the timeline): keychord wraps mods around one KEY, but a mod held over a CLICK has
    // no single-line form. Accepts modifier names (shift/cmd/...) and regular key names.
    std::string name;
    if (is >> name) {
      ImGuiKey k = modFromName(name);
      if (k == ImGuiKey_None) k = keyFromName(name);
      if (k != ImGuiKey_None) enqueueKeyStep(k, op == "keydown");
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
  if (s.setText) io.AddInputCharactersUTF8(s.text.c_str());
}

// Render one headless ImGui frame, consuming one queued hand Step before it.
// `body` runs inside the live frame (between NewFrame and Render). Render() is
// called so node-editor's deferred draw/control bookkeeping advances exactly as
// in the real app loop.
template <class Body>
static void pumpFrame(const Body& body) {
  applyPendingStep();
  ImGui::NewFrame();
  body();
  ImGui::Render();  // node-editor finalizes draw channels here; mirrors app loop
}

int runSelfTest(bool injectBug) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(400, 300);
  io.DeltaTime = 1.0f / 60.0f;
  // Minimal renderer backend so ImGui::Render() has a texture to bind to and the
  // node-editor's GetWindowDrawList path is happy in a headless run.
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
  unsigned char* pixels;
  int tw, th;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &tw, &th);  // build atlas so NewFrame won't assert
  io.Fonts->SetTexID((ImTextureID)1);

  // --- mouse: click expands to move -> press -> release (the gap-2 fix). Assert
  // the cursor is parked at the target on the move frame, the button is held on
  // the press frame, and released after.
  g_pending.clear();
  if (!injectBug) parseLine("click 123 77");  // bug case: enqueue nothing -> hand does nothing

  // Frame 1: MOVE only (settle hover). Cursor at target, button NOT yet down.
  applyPendingStep();
  ImGui::NewFrame();
  bool moveOk = io.MousePos.x == 123 && io.MousePos.y == 77 && !io.MouseDown[0];
  ImGui::EndFrame();

  // Frame 2: press. Cursor still at target, button now held.
  applyPendingStep();
  ImGui::NewFrame();
  bool downOk = io.MouseDown[0] && io.MousePos.x == 123 && io.MousePos.y == 77;
  ImGui::EndFrame();

  // Frame 3: release.
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

  // --- text: type into a real ImGui::InputText (gap 1). Click to focus it, then
  // inject UTF-8 (incl. CJK 「測試」) via the `text` command; assert the buffer
  // receives the bytes. This is the rename-dialog path (CJK node name).
  char buf[64] = {0};
  bool textOk = false;
  {
    // Click the field to focus it (InputText activates on click, needs WantTextInput).
    ImVec2 fieldMin, fieldMax;
    auto drawField = [&]() {
      ImGui::SetNextWindowPos(ImVec2(0, 0));
      ImGui::SetNextWindowSize(ImVec2(400, 300));
      ImGui::Begin("textwin", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
      ImGui::InputText("##name", buf, sizeof(buf));
      fieldMin = ImGui::GetItemRectMin();
      fieldMax = ImGui::GetItemRectMax();
      ImGui::End();
    };
    pumpFrame(drawField);  // frame 0: lay the field out so we learn its rect
    float fx = (fieldMin.x + fieldMax.x) * 0.5f;
    float fy = (fieldMin.y + fieldMax.y) * 0.5f;
    g_pending.clear();
    if (!injectBug) { char c[64]; snprintf(c, 64, "click %g %g", fx, fy); parseLine(c); }
    pumpFrame(drawField);  // frame A: position + press
    pumpFrame(drawField);  // frame B: release -> field becomes active (WantTextInput next frame)
    pumpFrame(drawField);  // frame C: settle activation
    g_pending.clear();
    if (!injectBug) parseLine("text 測試hi");  // CJK + ascii, exercises AddInputCharactersUTF8
    // drain all text steps (one step carries the whole string)
    while (!g_pending.empty()) pumpFrame(drawField);
    pumpFrame(drawField);  // let InputText commit the queued chars into buf
    textOk = (std::string(buf).find("測試") != std::string::npos) &&
             (std::string(buf).find("hi") != std::string::npos);
  }

  // --- click -> node-editor selection (gap 2). Drive the REAL ed:: selection path
  // headlessly: one node drawn, then a `click` on its body; assert GetSelectedNodes
  // reports it. This reproduces the live failure (selectedNode stuck at 0) and
  // proves the fix. injectBug enqueues nothing -> selection stays empty (RED).
  bool selOk = false;
  {
    ed::Config cfg;
    cfg.SettingsFile = nullptr;  // no .json persistence in the headless harness
    ed::EditorContext* ctx = ed::CreateEditor(&cfg);
    const int kNodeId = 7;
    const float kNodeX = 60, kNodeY = 60;
    auto drawCanvas = [&]() {
      ImGui::SetNextWindowPos(ImVec2(0, 0));
      ImGui::SetNextWindowSize(ImVec2(400, 300));
      ImGui::Begin("canvashost", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
      ed::SetCurrentEditor(ctx);
      ed::Begin("canvas");
      ed::SetNodePosition(kNodeId, ImVec2(kNodeX, kNodeY));
      ed::BeginNode(kNodeId);
      ImGui::TextUnformatted("NodeTitleAAAA");  // give the node a real, wide body to hit
      ed::EndNode();
      ed::End();
      ed::SetCurrentEditor(nullptr);
      ImGui::End();
    };
    // Warm-up: lay the node out + settle the canvas window focused, but park the
    // cursor AWAY from the node (300,250). This is the gap-2 condition: the click
    // must teleport onto the node from elsewhere. The fix's move-frame is what
    // settles hover at the target; without it the press lands on a stale hover and
    // selection misses. So this leg fails on the OLD 2-frame click and passes on
    // the new 3-frame one — a real regression guard, not a tautology.
    g_pending.clear();
    parseLine("move 300 250");  // cursor far from the node
    pumpFrame(drawCanvas);
    pumpFrame(drawCanvas);
    pumpFrame(drawCanvas);
    // Now click the node body (cursor teleports from 300,250). (bug: enqueue nothing.)
    g_pending.clear();
    if (!injectBug) parseLine("click 90 75");
    // Drain the click + a few settle frames; check selection after each.
    for (int i = 0; i < 6; ++i) {
      pumpFrame(drawCanvas);
      ed::SetCurrentEditor(ctx);
      ed::NodeId sel[4];
      int n = ed::GetSelectedNodes(sel, 4);
      ed::SetCurrentEditor(nullptr);
      if (n > 0 && (int)sel[0].Get() == kNodeId) selOk = true;
    }
    ed::DestroyEditor(ctx);
  }

  bool pass = moveOk && downOk && upOk && chordOk && textOk && selOk;
  printf("[selftest-hand] move=%d down=%d up=%d chord=%d text=%d select=%d -> %s\n",
         (int)moveOk, (int)downOk, (int)upOk, (int)chordOk, (int)textOk, (int)selOk,
         pass ? "PASS" : "FAIL");
  ImGui::DestroyContext();
  return pass ? 0 : 1;
}

}  // namespace sw::hand
