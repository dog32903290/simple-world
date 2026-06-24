// hand.cpp — see hand.h. Pure C++: file read + ImGui IO, no ObjC.
// (The headless self-test lives in hand_selftest.cpp, driven through feedLine/clearPending.)
#include "verify/hand/hand.h"

#include <cfloat>  // FLT_MAX sentinel from ed::GetNodePosition for the selectnode bad-id guard
#include <cstdio>
#include <cstdlib>  // atoi (f1..f12 key-name parsing)
#include <deque>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_node_editor.h"  // ed::SelectNode for `selectnode <childId>` (gap 2)

#ifndef SW_EYE_DIR
#define SW_EYE_DIR "/tmp/sw_eye"
#endif

namespace ed = ax::NodeEditor;

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

// App-owned MIDI-inject hook (set via setMidiInjectHook). The `midi` directive forwards to it; null
// means the directive is a no-op (verify stays a leaf — no app dependency).
void (*g_midiInjectHook)(int, int, int) = nullptr;
// App-owned MIDI-learn-arm hook (set via setLearnArmHook). The `learn` directive forwards to it.
void (*g_learnArmHook)(int, const char*) = nullptr;
// Gap 2: `selectnode <childId>` requests, applied by the canvas (editor current) rather
// than expanded into mouse frames — a direct selection that skips coordinate hit-tests.
// Separate queue from g_pending: these don't consume IO frames, they consume one drain
// call inside the canvas draw scope. clearPending() empties this too, so a self-test that
// resets between legs starts clean.
std::deque<int> g_selectNodes;

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
  if (n == "period" || n == ".") return ImGuiKey_Period;
  if (n == "comma" || n == ",") return ImGuiKey_Comma;
  if (n == "home") return ImGuiKey_Home;
  // Function keys f1..f12 (P6 演出/Focus modes bind F12; F11 OS-fullscreen is on the backlog).
  if ((n.size() == 2 || n.size() == 3) && (n[0] == 'f' || n[0] == 'F')) {
    int num = std::atoi(n.c_str() + 1);
    if (num >= 1 && num <= 12) return (ImGuiKey)(ImGuiKey_F1 + (num - 1));
  }
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
      // Same gap-2 fix as enqueueClick: a pure MOVE frame settles HoveredId at the start
      // point BEFORE the press. Without it, a drag teleporting from far presses while the
      // PREVIOUS frame's hover owner still holds HoveredId — under stacked AllowOverlap
      // widgets (timeline lane bg vs key diamonds) the stale owner steals the press and the
      // gesture lands on the wrong widget (key drag became a rubber-band fence, 批次8).
      Step move;
      move.setPos = true; move.x = x0; move.y = y0;
      g_pending.push_back(move);
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
  } else if (op == "learn") {
    // learn <child> <slot> — arm P3 MIDI-learn for graph param (child, slot). Immediate (side map).
    int child;
    std::string slot;
    if ((is >> child >> slot) && g_learnArmHook) g_learnArmHook(child, slot.c_str());
  } else if (op == "midi") {
    // midi <ch> <ctrl> <val> — inject a decoded MIDI ControllerChange into the app's live binding
    // table through the app-owned hook (P3 learn + cook-side wire). Immediate (not frame-queued):
    // the binding table is a side map, not ImGui IO, so it applies the moment the line is parsed.
    int ch, ctrl, val;
    if ((is >> ch >> ctrl >> val) && g_midiInjectHook) g_midiInjectHook(ch, ctrl, val);
  } else if (op == "selectnode") {
    // selectnode <childId> — queue a DIRECT node-editor selection (gap 2). The childId is
    // the operator node's ed node id (ui/node_draw.cpp ed::BeginNode(child.id)); identity
    // map, no lookup. Drained by applyPendingSelectNodes() while the editor is current.
    int id;
    if (is >> id) g_selectNodes.push_back(id);
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

// Includes the selectnode queue: a queued selection still needs one canvas frame to land,
// so a driver waiting on map.json "hand_pending" must see it as in-flight until drained.
bool hasPending() { return !g_pending.empty() || !g_selectNodes.empty(); }

void setMidiInjectHook(void (*hook)(int, int, int)) { g_midiInjectHook = hook; }
void setLearnArmHook(void (*hook)(int, const char*)) { g_learnArmHook = hook; }

void feedLine(const char* line) { parseLine(line); }

void clearPending() { g_pending.clear(); g_selectNodes.clear(); }

int applyPendingSelectNodes() {
  if (g_selectNodes.empty()) return 0;
  // Editor must be current here (caller's contract): ed::SelectNode acts on the editor set
  // via ed::SetCurrentEditor, and ed::GetNodePosition resolves against the SAME set. The hook
  // is drained AFTER drawChild (editor_ui.cpp), so every live node is registered this frame.
  //
  // Bad-id GUARD (the live-use contract): a non-existent id must be a TRUE no-op — leave the
  // current selection untouched, not wipe it. ed::SelectNode silently does nothing for an
  // unknown id (api: FindNode fails), so a naive ClearSelection()+SelectNode wiped the
  // selection to 0 whenever ANY queued id was stale (e.g. the agent named an id from a graph
  // it had already navigated away from). We therefore VALIDATE the whole batch first
  // (ed::GetNodePosition returns FLT_MAX for an unknown node) and only mutate the selection
  // when every id resolves. Drop the batch on any miss: an addressing typo can't clobber what
  // the user had selected. Drained ids are consumed regardless (one drain = one batch).
  std::vector<int> ids(g_selectNodes.begin(), g_selectNodes.end());
  g_selectNodes.clear();
  for (int id : ids) {
    if (ed::GetNodePosition(ed::NodeId(id)).x == FLT_MAX) return 0;  // unknown id -> no change
  }
  // All ids exist: ClearSelection once, then append each, so a batch of selectnode lines
  // reads as one multi-select (mirrors selectConnected's append).
  ed::ClearSelection();
  for (int id : ids) ed::SelectNode(ed::NodeId(id), /*append=*/true);
  return (int)ids.size();
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

}  // namespace sw::hand
