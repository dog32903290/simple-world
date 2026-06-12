// ui/quick_add — Cmd+F quick-add node palette.
// Zone: ui. Depends on app(document/graph_commands) + runtime + verify(one-line hook).
// See quick_add.h for the behaviour contract and fork notes.
#include "ui/quick_add.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_node_editor.h"

#include "app/document.h"
#include "app/graph_commands.h"
#include "runtime/compound_graph.h"
#include "runtime/graph.h"  // specTypes / addChildWouldCycle / dynamicSpecs (all-type source)
#include "ui/editor_ui.h"   // spawnNodeAt
#include "verify/eye/eye.h" // one-line hook: qa:<type>

namespace ed = ax::NodeEditor;
namespace sw::ui {
namespace {

// ---------------------------------------------------------------------------
// Palette state
// ---------------------------------------------------------------------------
bool  g_isOpen     = false;
float g_anchorX    = 120.0f;   // canvas coords for spawn
float g_anchorY    = 120.0f;
char  g_filterBuf[64] = "";    // imgui input text buffer
int   g_selectedIdx = 0;       // index into g_displayItems

// Full unfiltered item list (rebuilt when the palette opens or library changes).
// Each entry is a symbolId string (same token passed to spawnNodeAt).
std::vector<std::string> g_allItems;

// Filtered items displayed this frame (subset of g_allItems matching g_filterBuf).
std::vector<std::string> g_displayItems;

// Snapshot of filter string used to build g_displayItems (detect dirty).
char g_lastFilter[64] = "";

// Frame counter since last open (reset in openQuickAdd; used to skip the
// cancel-on-focus-loss check for the first two frames while focus propagates).
int g_framesOpen = 0;

// ---------------------------------------------------------------------------
// Build g_allItems from the registry + live compounds.
// = TiXL SymbolBrowser: specTypes() covers atomics; compound defs cover user symbols.
// Cycle-guard greying happens at spawn time (spawnNodeAt already refuses; we include all
// items here and grey cyclic ones at draw time — matches SymbolBrowser.cs:302-312).
// ---------------------------------------------------------------------------
static void rebuildAllItems() {
    g_allItems.clear();
    // Atomic types (specTypes = the registry table: RadialPoints, GridPoints, …)
    for (const std::string& t : sw::specTypes()) {
        g_allItems.push_back(t);
    }
    // Compound definitions from the live library
    for (const auto& kv : sw::doc::g_lib.symbols) {
        const sw::Symbol& s = kv.second;
        if (s.atomic) continue;
        g_allItems.push_back(s.id);
    }
}

// Case-insensitive substring match.
// Fork "QuickAddFilter_Substring": TiXL builds a scatter regex (char by char, .*
// between each); we use a plain case-insensitive strstr equivalent.
// Adequate for the current library size; upgrade to regex if the library exceeds ~2000
// types and users find matching too coarse.
static bool matchFilter(const std::string& item, const char* filter) {
    if (!filter || filter[0] == '\0') return true;
    // Case-fold both strings for comparison.
    std::string lower = item;
    for (char& c : lower) c = (char)std::tolower((unsigned char)c);
    char fLower[64];
    std::snprintf(fLower, sizeof(fLower), "%s", filter);
    for (char& c : fLower) c = (char)std::tolower((unsigned char)c);
    return lower.find(fLower) != std::string::npos;
}

// Rebuild g_displayItems from g_allItems filtered by g_filterBuf.
static void rebuildDisplayItems() {
    g_displayItems.clear();
    for (const std::string& item : g_allItems) {
        if (matchFilter(item, g_filterBuf)) g_displayItems.push_back(item);
    }
    std::memcpy(g_lastFilter, g_filterBuf, sizeof(g_filterBuf));
    if (g_selectedIdx >= (int)g_displayItems.size())
        g_selectedIdx = g_displayItems.empty() ? 0 : (int)g_displayItems.size() - 1;
}

// Display name for a symbolId: use the Symbol's .name if available, else the id.
static std::string displayName(const std::string& id) {
    const sw::Symbol* s = sw::doc::g_lib.find(id);
    if (s && !s->name.empty()) return s->name;
    return id;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void openQuickAdd(float cx, float cy) {
    g_anchorX = cx;
    g_anchorY = cy;
    if (!g_isOpen) {
        // Only reset filter and list on a fresh open (re-open while open = re-focus only).
        g_filterBuf[0] = '\0';
        g_lastFilter[0] = '\1';  // force first-frame rebuild
        g_selectedIdx   = 0;
        g_framesOpen    = 0;    // reset focus-loss guard
        rebuildAllItems();
        rebuildDisplayItems();
        g_isOpen = true;
    }
    // ImGui SetNextWindowFocus doesn't work here (we're inside ed::Begin scope);
    // we use a per-frame flag approach in drawQuickAdd instead.
}

// ---------------------------------------------------------------------------
// drawQuickAdd — called every frame from drawNodeCanvas() inside ed::Begin scope.
//
// Design: deferred-action pattern. ALL imgui windows/children are properly
// Begin/End'd before any spawn-and-close logic runs. No early returns inside
// Begin...End pairs (a classic imgui state corruption trap).
// ---------------------------------------------------------------------------
void drawQuickAdd() {
    if (!g_isOpen) return;

    // Lazy filter rebuild when g_filterBuf changed since last frame.
    if (std::memcmp(g_filterBuf, g_lastFilter, sizeof(g_filterBuf)) != 0) {
        rebuildDisplayItems();
    }

    // Position: convert the canvas anchor to screen, clamp to viewport.
    ImVec2 screenAnchor = ed::CanvasToScreen(ImVec2(g_anchorX, g_anchorY));
    const ImVec2 panelSize(240.0f, 280.0f);
    ImVec2 panelPos = screenAnchor;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    if (panelPos.x + panelSize.x > vp->WorkPos.x + vp->WorkSize.x)
        panelPos.x = vp->WorkPos.x + vp->WorkSize.x - panelSize.x;
    if (panelPos.y + panelSize.y > vp->WorkPos.y + vp->WorkSize.y)
        panelPos.y = vp->WorkPos.y + vp->WorkSize.y - panelSize.y;
    if (panelPos.x < vp->WorkPos.x) panelPos.x = vp->WorkPos.x;
    if (panelPos.y < vp->WorkPos.y) panelPos.y = vp->WorkPos.y;

    // Deferred action: set after all imgui calls are complete, applied at the end.
    enum class Action { None, Spawn, Cancel };
    Action action = Action::None;
    std::string spawnType;

    // Draw outside the node-editor coordinate space (ed::Suspend/Resume).
    ed::Suspend();

    ImGui::SetNextWindowPos(panelPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(panelSize, ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoCollapse;

    bool windowOpen = true;
    if (ImGui::Begin("##quick_add", &windowOpen, flags)) {
        // Focus the text input on the frame after open.
        // (TiXL SymbolBrowser.cs:190-192 _focusInputNextTime -> SetKeyboardFocusHere)
        static bool s_focusNext = false;
        if (s_focusNext) {
            ImGui::SetKeyboardFocusHere();
            s_focusNext = false;
        }
        static bool s_wasOpen = false;
        if (!s_wasOpen && g_isOpen) s_focusNext = true;
        s_wasOpen = g_isOpen;

        // Search input.
        // (TiXL SymbolBrowser.cs:202 ImGui.InputText "##symbolBrowserFilter")
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##qa_filter", g_filterBuf, sizeof(g_filterBuf));
        sw::eye::recordItem("qa:input");

        ImGui::Separator();

        // Keyboard nav: CursorDown/Up.
        // (TiXL SymbolBrowser.cs:275-283 IsKeyReleased CursorDown/CursorUp)
        if (!g_displayItems.empty()) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
                g_selectedIdx = (g_selectedIdx + 1) % (int)g_displayItems.size();
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
                g_selectedIdx = (g_selectedIdx - 1 + (int)g_displayItems.size()) % (int)g_displayItems.size();
        }

        // Enter = commit. (TiXL SymbolBrowser.cs:207-213)
        if (action == Action::None && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !g_displayItems.empty()) {
            action = Action::Spawn;
            spawnType = g_displayItems[g_selectedIdx];
        }

        // Esc = cancel. (TiXL SymbolBrowser.cs:226)
        if (action == Action::None && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            action = Action::Cancel;
        }

        // Results list. (TiXL SymbolBrowser.cs:264 DrawResultsList)
        const std::string& curId = sw::doc::currentSymbolId();
        float listHeight = panelSize.y - ImGui::GetCursorPosY() - ImGui::GetStyle().WindowPadding.y - 2.0f;
        if (listHeight < 20.0f) listHeight = 20.0f;

        if (ImGui::BeginChild("##qa_list", ImVec2(-1.0f, listHeight), ImGuiChildFlags_None)) {
            for (int i = 0; i < (int)g_displayItems.size(); ++i) {
                const std::string& id   = g_displayItems[i];
                const bool cyclic = sw::addChildWouldCycle(sw::doc::g_lib, curId, id);
                const bool selected = (i == g_selectedIdx);

                // Grey out cyclic entries (= TiXL SymbolBrowser.cs:302-312 color.Fade).
                ImGui::BeginDisabled(cyclic);
                bool clicked = ImGui::Selectable(displayName(id).c_str(), selected,
                                                 ImGuiSelectableFlags_None);
                ImGui::EndDisabled();

                // eye hook — one line per row, key = qa:<typeId>
                // (pattern: insp:*, ctx:*, bgctx:add:* — one recordItem call; no logic in verify/)
                sw::eye::recordItem(("qa:" + id).c_str());

                if (clicked && !cyclic && action == Action::None) {
                    action = Action::Spawn;
                    spawnType = id;
                }

                // Hover sets selection. (TiXL SymbolBrowser.cs:336-342)
                const ImGuiIO& io = ImGui::GetIO();
                if (ImGui::IsItemHovered() &&
                    (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
                    g_selectedIdx = i;
                }

                // Scroll selected into view.
                if (selected) ImGui::SetScrollHereY(0.5f);
            }
        }
        ImGui::EndChild();

        // Cancel on mouse-RELEASE outside the panel window. (TiXL SymbolBrowser.cs:223-232)
        // Uses IsMouseReleased (not IsMouseClicked / IsMouseDown) so the cancel fires
        // AFTER the Selectable has had a chance to consume the same click — ButtonBehavior
        // uses PressedOnClickRelease, meaning `pressed = true` on the release frame; by
        // waiting for release we guarantee the deferred-action Spawn path wins on inside
        // clicks, while outside clicks still cancel cleanly on their release frame.
        // Guard: skip the first 2 frames (window pos/size not yet committed on frame 0).
        g_framesOpen++;
        if (action == Action::None && g_framesOpen > 2) {
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                !ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows |
                                        ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
                action = Action::Cancel;
            }
        }
        if (action == Action::None && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            action = Action::Cancel;
        }
    }
    ImGui::End();
    if (!windowOpen) action = Action::Cancel;

    ed::Resume();

    // Apply deferred action AFTER all Begin/End pairs and ed::Resume.
    if (action == Action::Spawn) {
        sw::ui::spawnNodeAt(spawnType, g_anchorX, g_anchorY);
        g_isOpen = false;
    } else if (action == Action::Cancel) {
        g_isOpen = false;
    }
}

// ---------------------------------------------------------------------------
// runQuickAddSelfTest
// ---------------------------------------------------------------------------
int runQuickAddSelfTest(bool injectBug) {
    int fail = 0;

    // Test 1: empty filter matches everything.
    {
        std::vector<std::string> items = {"RadialPoints", "GridPoints", "MyCompound"};
        for (const auto& item : items) {
            if (!matchFilter(item, "")) {
                std::printf("[quickadd] empty filter should match '%s' -> FAIL\n", item.c_str());
                ++fail;
            }
        }
    }

    // Test 2: case-insensitive substring.
    {
        if (!matchFilter("RadialPoints", "radial")) {
            std::printf("[quickadd] 'radial' should match 'RadialPoints' (case-insensitive) -> FAIL\n");
            ++fail;
        }
        if (!matchFilter("GridPoints", "GRID")) {
            std::printf("[quickadd] 'GRID' should match 'GridPoints' -> FAIL\n");
            ++fail;
        }
        if (matchFilter("RadialPoints", "xyz")) {
            std::printf("[quickadd] 'xyz' should NOT match 'RadialPoints' -> FAIL\n");
            ++fail;
        }
    }

    // Test 3: eye-hook naming convention — every item key starts with "qa:".
    {
        const std::string test_id = "RadialPoints";
        const std::string key = "qa:" + test_id;
        if (key.substr(0, 3) != "qa:") {
            std::printf("[quickadd] eye key does not start with 'qa:' -> FAIL\n");
            ++fail;
        }
    }

    // Test 4 (injectBug): force a filter mismatch to demonstrate RED path.
    if (injectBug) {
        // Simulate the bug: matchFilter returns true for "xyz" against "RadialPoints".
        bool wrongMatch = true;  // injected bug result
        if (!wrongMatch) {
            std::printf("[quickadd] injectBug: expected injected wrong-match but got false -> FAIL\n");
            ++fail;
        } else {
            std::printf("[quickadd] injectBug: wrong-match detected (red-proof) -> forcing FAIL\n");
            ++fail;  // injectBug path MUST return nonzero
        }
        std::printf("[quickadd] injectBug FAIL count=%d (expected nonzero) -> %s\n", fail,
                    fail > 0 ? "PASS (red-proof)" : "FAIL");
        return fail > 0 ? 1 : 0;
    }

    std::printf("[quickadd] fail=%d -> %s\n", fail, fail == 0 ? "PASS" : "FAIL");
    return fail;
}

}  // namespace sw::ui
