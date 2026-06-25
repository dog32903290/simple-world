// ui/quick_add — Cmd+F quick-add node palette.
// Zone: ui. Depends on app(document/graph_commands) + runtime + verify(one-line hook).
// See quick_add.h for the behaviour contract and fork notes.
#include "ui/quick_add.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <map>
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
// Focus the filter InputText on the next drawQuickAdd frame. Set UNCONDITIONALLY by every
// openQuickAdd (fresh open AND re-open): the old in-Begin static edge-detection only fired
// once per process — after the first close, s_wasOpen stayed true (early return skipped the
// update), the input never re-focused, WantTextInput stayed false, and Space/global keys
// pierced the open panel straight into the transport (refuter-R-P BROKEN, live-proven).
bool  g_focusNextFrame = false;
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
    for (const auto& kv : sw::doc::g_lib().symbols) {
        const sw::Symbol& s = kv.second;
        if (s.atomic) continue;
        g_allItems.push_back(s.id);
    }
}

static std::string toLower(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = (char)std::tolower((unsigned char)c);
    return r;
}

// Display name (Symbol.name else id) = the string TiXL ranks on (symbol.Name).
static std::string displayName(const std::string& id) {
    const sw::Symbol* s = sw::doc::g_lib().find(id);
    if (s && !s->name.empty()) return s->name;
    return id;
}

// Category/namespace (atomic specs carry it; default empty) = TiXL Symbol.Namespace.
static std::string categoryOf(const std::string& id) {
    const sw::NodeSpec* spec = sw::findSpec(id);
    return spec ? spec->category : std::string();
}

// scatterMatch + computeRelevancy are defined below in the sw::ui namespace (declared in
// quick_add.h) so the isolated self-test can exercise the real production primitives.

// ---------------------------------------------------------------------------
// Namespace tree (= TiXL NamespaceTreeNode / SymbolTreeMenu): when the query is EMPTY the
// palette shows a collapsible tree grouped by Symbol.Namespace (e.g. "point" -> "point.draw"
// -> DrawPoints). buildNamespaceTree (defined below, in sw::ui for the self-test) does the
// pure grouping; drawNamespaceFolder walks the result with imgui.

// Scatter-filter g_allItems by g_filterBuf (name OR category), then sort by relevancy desc.
// = TiXL SymbolFilter: match Name||Namespace, `.OrderBy(ComputeRelevancy).Reverse()`. Fork
// "QuickAddRank_StableTies": STABLE sort keeps equal-score rows in registry order (vs Reverse).
static void rebuildDisplayItems() {
    g_displayItems.clear();
    const std::string query = g_filterBuf;
    const std::string qLower = toLower(query);
    for (const std::string& item : g_allItems) {
        if (scatterMatch(toLower(displayName(item)), qLower) ||
            scatterMatch(toLower(categoryOf(item)), qLower)) {
            g_displayItems.push_back(item);
        }
    }
    // Rank: stable sort by relevancy descending (ties preserve registry order).
    std::stable_sort(g_displayItems.begin(), g_displayItems.end(),
                     [&](const std::string& a, const std::string& b) {
                         return computeRelevancy(displayName(a), query) >
                                computeRelevancy(displayName(b), query);
                     });
    std::memcpy(g_lastFilter, g_filterBuf, sizeof(g_filterBuf));
    g_selectedIdx = 0;  // top match selected after every refilter (TiXL resets to best)
}

// Cached namespace tree (built when the palette opens; empty-query view walks it).
NamespaceNode g_tree;

static void rebuildTree() {
    g_tree = buildNamespaceTree(g_allItems, [](const std::string& id) { return categoryOf(id); });
}

}  // namespace

// ---------------------------------------------------------------------------
// Search primitives (header-exposed for the isolated self-test).
// ---------------------------------------------------------------------------

// Scatter / subsequence match: query chars appear in order in `hay` (gaps allowed).
// = TiXL SymbolFilter.cs:90 `string.Join(".*", chars)` regex: "dlp" hits "DrawLinePoints".
bool scatterMatch(const std::string& hay, const std::string& q) {
    if (q.empty()) return true;
    size_t hi = 0;
    for (char qc : q) {
        bool found = false;
        for (; hi < hay.size(); ++hi) {
            if (hay[hi] == qc) { ++hi; found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

// buildNamespaceTree + drawNamespaceTree live in quick_add_tree.cpp (ui leaf, ARCHITECTURE rule
// 4: keep this file ≤400 lines). They are declared in quick_add.h (the self-test exercises the
// pure grouping there).

// Relevancy (higher = more relevant). PORTABLE subset of TiXL ComputeRelevancy
// (SymbolFilter.cs:198-389) — name+query multipliers (exact > prefix > contains > scatter;
// PascalCase initials; _/OBSOLETE demotion). DEFERRED: namespace/package/usage boosts.
double computeRelevancy(const std::string& name, const std::string& query) {
    if (query.empty()) return 1.0;  // unfiltered: registry order (stable sort keeps it)
    const std::string n = toLower(name);
    const std::string q = toLower(query);
    double rel = 1.0;
    if (q.size() == 1 && n.rfind(q, 0) == 0) rel *= 20.0;  // :215 single-char prefix
    if (n == q) rel *= 8.6;                                // :226 equals
    if (n.rfind(q, 0) == 0)                                // :232 startsWith else :239 contains
        rel *= 8.5;
    else if (n.find(q) != std::string::npos)
        rel *= 8.4;
    bool initials = true;  // :256 PascalCase initials "ds" -> "DrawState" x4
    size_t maxIndex = 0;
    for (char c : q) {
        size_t idx = n.find(c, maxIndex);
        if (idx == std::string::npos) { initials = false; break; }
        maxIndex = idx + 1;
    }
    if (initials) rel *= 4.0;
    if (!name.empty() && name[0] == '_') rel *= 0.1;                  // :296 demote
    if (name.find("OBSOLETE") != std::string::npos) rel *= 0.01;      // :302 demote
    return rel;
}

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
        rebuildTree();
        g_isOpen = true;
    }
    g_focusNextFrame = true;  // every open/re-open re-focuses the input (refuter-R-P 修)
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
        // Focus the text input when openQuickAdd asked for it (TiXL SymbolBrowser.cs:190-192
        // _focusInputNextTime -> SetKeyboardFocusHere; module-level flag, refuter-R-P 修).
        if (g_focusNextFrame) {
            ImGui::SetKeyboardFocusHere();
            g_focusNextFrame = false;
        }

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

        // Two views (= TiXL SymbolBrowser): an EMPTY query shows the namespace TREE grouped by
        // Symbol.Namespace (SymbolTreeMenu); a non-empty query shows the flat scatter+rank list
        // (SymbolFilter). The query is the live filter buffer.
        const bool emptyQuery = (g_filterBuf[0] == '\0');

        if (ImGui::BeginChild("##qa_list", ImVec2(-1.0f, listHeight), ImGuiChildFlags_None)) {
            if (emptyQuery) {
                // --- Namespace tree (collapsible folders by category) ---
                bool spawnRequested = false;
                std::string treeSpawn;
                drawNamespaceTree(g_tree, curId, spawnRequested, treeSpawn,
                                  [](const std::string& id) { return displayName(id); });
                if (spawnRequested && action == Action::None) {
                    action = Action::Spawn;
                    spawnType = treeSpawn;
                }
            } else {
                // --- Flat ranked list (scatter + relevancy) ---
                for (int i = 0; i < (int)g_displayItems.size(); ++i) {
                    const std::string& id   = g_displayItems[i];
                    const bool cyclic = sw::addChildWouldCycle(sw::doc::g_lib(), curId, id);
                    const bool selected = (i == g_selectedIdx);

                    // Grey out cyclic entries (= TiXL SymbolBrowser.cs:302-312 color.Fade).
                    ImGui::BeginDisabled(cyclic);
                    bool clicked = ImGui::Selectable(displayName(id).c_str(), selected,
                                                     ImGuiSelectableFlags_None);
                    // Category subtitle (= TiXL SymbolBrowser namespace hint) when the spec
                    // carries one.
                    const std::string cat = categoryOf(id);
                    if (!cat.empty()) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("  %s", cat.c_str());
                    }
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

// runQuickAddSelfTest lives in quick_add_selftest.cpp (isolated leaf; ARCHITECTURE rule 4/5).

}  // namespace sw::ui
