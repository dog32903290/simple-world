// hand_selftest.cpp — the headless RED->GREEN teeth for verify/hand (see hand.h).
// Split out of hand.cpp (ARCHITECTURE rule 4: one file one duty — hand.cpp is the
// injection engine, this TU is the proof). Drives the REAL queue through the public
// feedLine/clearPending/applyPendingStep seam — no private access, so the test exercises
// exactly what the live app runs.
#include "verify/hand/hand.h"

#include <cstdio>
#include <string>

#include "imgui.h"
#include "imgui_node_editor.h"

namespace ed = ax::NodeEditor;

namespace sw::hand {

namespace {

// Render one headless ImGui frame, consuming one queued hand Step before it.
// `body` runs inside the live frame (between NewFrame and Render). Render() is
// called so node-editor's deferred draw/control bookkeeping advances exactly as
// in the real app loop.
template <class Body>
void pumpFrame(const Body& body) {
  applyPendingStep();
  ImGui::NewFrame();
  body();
  ImGui::Render();  // node-editor finalizes draw channels here; mirrors app loop
}

}  // namespace

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
  clearPending();
  if (!injectBug) feedLine("click 123 77");  // bug case: enqueue nothing -> hand does nothing

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

  // --- double: two settled clicks must land inside ImGui's double-click window
  // (0.30s; at 1/60 DeltaTime the two downs are 3 frames = 0.05s apart) and at one
  // spot, so IsMouseDoubleClicked fires on the second press frame. Guards the
  // expansion staying tight enough for the double-click clock (批次9 工單 3).
  clearPending();
  if (!injectBug) feedLine("double 123 77");
  bool dblOk = false;
  while (hasPending()) {
    applyPendingStep();
    ImGui::NewFrame();
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) dblOk = true;
    ImGui::EndFrame();
  }

  // --- keyboard: Cmd+Z chord; assert the modifier is held on the frame Z presses.
  // ConfigMacOSXBehaviors (default on __APPLE__) swaps Cmd->Ctrl inside
  // AddKeyEvent, so injecting "cmd" lands in io.KeyCtrl (imgui's cross-platform
  // convention: ImGuiMod_Ctrl == Cmd on Mac). The real app must therefore detect
  // Cmd+Z via Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z), NOT io.KeySuper.
  clearPending();
  if (!injectBug) feedLine("keychord cmd z");
  bool chordOk = false;
  while (hasPending()) {
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
    clearPending();
    if (!injectBug) { char c[64]; snprintf(c, 64, "click %g %g", fx, fy); feedLine(c); }
    pumpFrame(drawField);  // frame A: position + press
    pumpFrame(drawField);  // frame B: release -> field becomes active (WantTextInput next frame)
    pumpFrame(drawField);  // frame C: settle activation
    clearPending();
    if (!injectBug) feedLine("text 測試hi");  // CJK + ascii, exercises AddInputCharactersUTF8
    // drain all text steps (one step carries the whole string)
    while (hasPending()) pumpFrame(drawField);
    pumpFrame(drawField);  // let InputText commit the queued chars into buf
    textOk = (std::string(buf).find("測試") != std::string::npos) &&
             (std::string(buf).find("hi") != std::string::npos);
  }

  // --- click -> node-editor selection (gap 2) AND rclick -> node-editor context menu
  // (批次9 工單 3: D3 named "rclick 注入不穩" — this leg pins the frame pattern down).
  // Drive the REAL ed:: paths headlessly: one node drawn; a cold `click` must select it,
  // then a cold `rclick` must fire ShowNodeContextMenu WITH that node. Both gestures
  // teleport from far away, so they pass ONLY if the expansion's leading move frame
  // settles hover before the press (node-editor latches its context/click candidate at
  // PRESS from the frame's hover). injectBug enqueues nothing -> both legs RED.
  bool selOk = false;
  bool ctxOk = false;
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
      // Context-menu probe, placed exactly like the app's drawCanvasContextMenu
      // (combine_dialog.cpp): inside the canvas scope, popups suspended.
      ed::Suspend();
      ed::NodeId cid;
      if (ed::ShowNodeContextMenu(&cid) && (int)cid.Get() == kNodeId) ctxOk = true;
      ed::Resume();
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
    clearPending();
    feedLine("move 300 250");  // cursor far from the node
    pumpFrame(drawCanvas);
    pumpFrame(drawCanvas);
    pumpFrame(drawCanvas);
    // Now click the node body (cursor teleports from 300,250). (bug: enqueue nothing.)
    clearPending();
    if (!injectBug) feedLine("click 90 75");
    // Drain the click + a few settle frames; check selection after each.
    for (int i = 0; i < 6; ++i) {
      pumpFrame(drawCanvas);
      ed::SetCurrentEditor(ctx);
      ed::NodeId sel[4];
      int n = ed::GetSelectedNodes(sel, 4);
      ed::SetCurrentEditor(nullptr);
      if (n > 0 && (int)sel[0].Get() == kNodeId) selOk = true;
    }
    // rclick leg: park far again (cold teleport), then right-click the node body.
    // ShowNodeContextMenu reports inside drawCanvas (the probe above) on the frames
    // after the release lands.
    clearPending();
    feedLine("move 300 250");
    pumpFrame(drawCanvas);
    pumpFrame(drawCanvas);
    clearPending();
    if (!injectBug) feedLine("rclick 90 75");
    for (int i = 0; i < 6; ++i) pumpFrame(drawCanvas);

    // --- selectnode <childId> -> DIRECT node-editor selection (gap 2). No mouse, no
    // coordinate hit-test: the agent names the child id and applyPendingSelectNodes()
    // (drained inside the canvas scope, editor current) calls ed::SelectNode. This is the
    // bypass for the flaky "injected click doesn't select a non-terminal node" path. The
    // canvas-scope drain mirrors the app's editor_ui hook. A drawCanvas2 that drains the
    // queue stands in for drawChild + the hook line. RED legs:
    //   bad-id : selectnode on a non-existent id must NOT leave kNodeId selected
    //   noop   : injectBug enqueues nothing -> selection stays cleared
    ed::SetCurrentEditor(ctx);
    ed::ClearSelection();  // start from a clean slate so the assert is about selectnode alone
    ed::SetCurrentEditor(nullptr);
    auto drainCanvas = [&]() {
      ImGui::SetNextWindowPos(ImVec2(0, 0));
      ImGui::SetNextWindowSize(ImVec2(400, 300));
      ImGui::Begin("canvashost2", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
      ed::SetCurrentEditor(ctx);
      ed::Begin("canvas");
      ed::SetNodePosition(kNodeId, ImVec2(kNodeX, kNodeY));
      ed::BeginNode(kNodeId);
      ImGui::TextUnformatted("NodeTitleAAAA");
      ed::EndNode();
      applyPendingSelectNodes();  // <- the hook under test (editor current, inside Begin/End)
      ed::End();
      ed::SetCurrentEditor(nullptr);
      ImGui::End();
    };
    clearPending();
    // GREEN: select the real node by id. injectBug enqueues nothing (noop RED leg).
    if (!injectBug) feedLine(std::string(std::string("selectnode ") + std::to_string(kNodeId)).c_str());
    bool selNodeOk = false;
    for (int i = 0; i < 4; ++i) {
      pumpFrame(drainCanvas);
      ed::SetCurrentEditor(ctx);
      ed::NodeId sel[4];
      int n = ed::GetSelectedNodes(sel, 4);
      ed::SetCurrentEditor(nullptr);
      if (n == 1 && (int)sel[0].Get() == kNodeId) selNodeOk = true;
    }
    // RED guard (runs in BOTH modes): a bad id must be a TRUE no-op — it must NOT disturb the
    // existing selection. Pre-seed kNodeId selected, then address an id that isn't on the
    // canvas: kNodeId must STILL be selected afterward (the bad-id guard drops the whole batch
    // rather than ClearSelection-ing the user's selection out from under them). This is the
    // live-use contract: an addressing typo can't clobber what was selected.
    ed::SetCurrentEditor(ctx);
    ed::ClearSelection();
    ed::SelectNode(ed::NodeId(kNodeId), /*append=*/true);  // pre-seed: kNodeId IS selected
    ed::SetCurrentEditor(nullptr);
    clearPending();
    feedLine("selectnode 99999");  // unknown id; applyPendingSelectNodes must leave kNodeId alone
    for (int i = 0; i < 4; ++i) pumpFrame(drainCanvas);
    ed::SetCurrentEditor(ctx);
    ed::NodeId selBad[4];
    int nBad = ed::GetSelectedNodes(selBad, 4);
    ed::SetCurrentEditor(nullptr);
    // After addressing a non-existent id: kNodeId must STILL be selected (batch dropped, no
    // ClearSelection). This proves the bad-id guard protects the live selection.
    bool badIdOk = false;
    for (int i = 0; i < nBad; ++i) if ((int)selBad[i].Get() == kNodeId) badIdOk = true;

    ed::DestroyEditor(ctx);
    selOk = selOk && selNodeOk && badIdOk;  // gap-2 now requires BOTH click-select and selectnode
  }

  bool pass = moveOk && downOk && upOk && dblOk && chordOk && textOk && selOk && ctxOk;
  printf("[selftest-hand] move=%d down=%d up=%d double=%d chord=%d text=%d select=%d rctx=%d "
         "-> %s\n",
         (int)moveOk, (int)downOk, (int)upOk, (int)dblOk, (int)chordOk, (int)textOk, (int)selOk,
         (int)ctxOk, pass ? "PASS" : "FAIL");
  ImGui::DestroyContext();
  return pass ? 0 : 1;
}

}  // namespace sw::hand
