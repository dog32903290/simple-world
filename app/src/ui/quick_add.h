// ui/quick_add — Cmd+F quick-add node palette (SearchGraph, TiXL FactoryKeyMap.cs:56).
//
// Zone: ui. Depends on app(document/graph_commands) + runtime + verify(thin hook).
// Never depends upward (runtime/platform/verify cannot include this).
//
// Behaviour contract (mirrors TiXL SymbolBrowser behaviour skeleton):
//   open  : openQuickAdd(canvasX, canvasY) — anchors spawn point on canvas coords
//   filter: real-time scatter / subsequence match (= TiXL SymbolBrowser regex `c.*c.*c`,
//           SymbolFilter.cs:90) over the row's display name OR its category, then ranked
//           by relevancy (exact > prefix > contains > scatter; PascalCase-initials bump;
//           _/OBSOLETE demotion; usage-count boost = TiXL SymbolFilter.cs:369-381). Fork
//           "QuickAddRank_StableTies": equal-score rows hold registry order (stable sort)
//           vs TiXL's Reverse(). DEFERRED: namespace/package boosts.
//   nav   : CursorDown/Up advance selection
//   commit: Enter or mouse-click spawns the selected type at the anchor
//   cancel: Esc or click-outside closes without action
//
// eye hook: each rendered row emits qa:<type> via eye::recordItem (one line per row;
//           implementation is the recordItem call itself — no logic in verify/).
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace sw::ui {

// Namespace tree node (= TiXL NamespaceTreeNode): a nested container grouping symbols by their
// dot-separated category (Symbol.Namespace, "Lib." root stripped). Exposed for the isolated
// self-test (buildNamespaceTree is a pure data transform; the imgui walk lives in quick_add.cpp).
struct NamespaceNode {
    std::string name;                              // folder name at this level ("point", "draw"…)
    std::map<std::string, NamespaceNode> children; // sub-folders keyed by name (sorted)
    std::vector<std::string> symbols;              // ids whose namespace ENDS at this node
};

// Group items into a namespace tree using catOf(id) -> dot-separated category. Empty category
// lands under "(uncategorized)" so no symbol is ever dropped. Pure (no imgui / globals).
NamespaceNode buildNamespaceTree(const std::vector<std::string>& items,
                                 const std::function<std::string(const std::string&)>& catOf);

// Draw the namespace tree with imgui (collapsible TreeNode folders, Selectable leaves). Folders
// emit qa:ns:<full.path> + leaves emit qa:<id> (eye hooks). A clicked, non-cyclic leaf sets
// spawnRequested + spawnType. displayName maps id -> shown label. (= TiXL SymbolTreeMenu walk.)
void drawNamespaceTree(const NamespaceNode& root, const std::string& curId, bool& spawnRequested,
                       std::string& spawnType,
                       const std::function<std::string(const std::string&)>& displayName);

// First OUTPUT port's dataType for an id (= TiXL Symbol first-output type), or "" if none.
// Works for atomic specs (findSpec ports) and live compounds (Symbol.outputDefs). READ-ONLY.
std::string firstOutputType(const std::string& id);

// Draw one quick-add result row (= TiXL DrawSymbolUiEntry): the title is tinted by the row's
// first-output dataType (typeColor), the dataType name trails as a dim suffix, a hover tooltip
// summarises the ports, and the row emits qa:<id> + (when typed) qatype:<id>:<dataType> eye
// markers. Returns true if the row was clicked (caller gates spawn on !cyclic). Shared by the
// flat ranked list (quick_add.cpp) and the namespace-tree leaves (quick_add_tree.cpp) so both
// views colour identically. `label` is the already-resolved display name. `showType` gates ONLY
// the trailing dim dataType suffix (= TiXL DrawSymbolUiEntry showType param, PlaceHolderUi.cs:459):
// the flat free-text search list passes true; the namespace-tree/grouped view passes false (TiXL
// SymbolBrowsing.cs:81,163). The type-color title tint + hover tooltip are NOT gated (the tint is
// pushed before the showType check in TiXL, PlaceHolderUi.cs:424).
bool drawResultRow(const std::string& id, const std::string& label, bool selected, bool cyclic,
                   bool showType = true);

// Open the palette anchored at canvas coordinates (cx, cy).
// No-op if already open (idempotent — Cmd+F pressed while open does NOT close it;
// that matches TiXL which just re-focuses the input box).
//
// Optional port-drag type pre-filter (= TiXL SymbolFilter FilterInputType / FilterOutputType,
// SymbolFilter.cs:14-34). When the palette is opened by DRAGGING off a port onto empty canvas
// (TiXL PlaceholderCreation OpenForItemOutput/OpenForItemInput → PlaceHolderUi.Open inputFilter/
// outputFilter), the dragged port's dataType pre-filters the candidate list to only ops that can
// wire to it:
//   - inputFilter  (set when dragging FROM an OUTPUT port): keep ops that have an INPUT port of
//     this dataType (the new node will RECEIVE the dragged output). = TiXL FilterInputType, set
//     from outputLine.Output.ValueType (PlaceholderCreation.cs:148-152).
//   - outputFilter (set when dragging FROM an INPUT port): keep ops that have an OUTPUT port of
//     this dataType (the new node will FEED the dragged input). = TiXL FilterOutputType
//     (PlaceholderCreation.cs:266-269).
// Empty strings = no filter (the Cmd+F path); matching is STRICT dataType equality, mirroring
// TiXL GetInputMatchingType/GetOutputMatchingType (Symbol.cs:332-352, ValueType == type).
void openQuickAdd(float cx, float cy, const std::string& inputFilter = "",
                  const std::string& outputFilter = "");

// Pure port-filter predicates (header-exposed for the isolated self-test; production calls them
// from rebuildDisplayItems). Strict dataType equality across ALL ports (= TiXL scans every
// InputDefinition/OutputDefinition, not just the first). READ-ONLY registry/library access.
//   opHasInputOfType(id, t)  : does op `id` have any INPUT port whose dataType == t?
//   opHasOutputOfType(id, t) : does op `id` have any OUTPUT port whose dataType == t?
//   passesPortFilter(id, inF, outF): combined gate — an empty filter string is "pass" (no
//     constraint), a non-empty one requires the matching-direction port to exist. = TiXL
//     SymbolFilter UpdateMatchingSymbols (_inputType/_outputType blocks, SymbolFilter.cs:117-152).
bool opHasInputOfType(const std::string& id, const std::string& dataType);
bool opHasOutputOfType(const std::string& id, const std::string& dataType);
bool passesPortFilter(const std::string& id, const std::string& inputFilter,
                      const std::string& outputFilter);

// Draw the port-drag type-hint line at the top of the palette (= TiXL PlaceHolderUi.cs:322-331
// DrawTypeFilterHint): surfaces the constraining dataType(s) so the user knows the list is
// narrowed. No-op when both filters are empty (the Cmd+F path). Emits qa:filter:in/out eye
// markers. Lives in quick_add_tree.cpp (imgui leaf) to keep quick_add.cpp ≤400 lines.
void drawTypeFilterHint(const std::string& inputFilter, const std::string& outputFilter);

// Draw the palette this frame. Call once per frame from drawNodeCanvas() while the
// node-editor is current (needed for ed::ScreenToCanvas in the spawn path).
void drawQuickAdd();

// Pure search primitives (exposed for the isolated self-test; production uses them internally).
// scatterMatch: query chars appear in order in `hay` (gaps allowed; empty query = match all).
// computeRelevancy: higher = more relevant (exact > prefix > contains > scatter; see .cpp).
bool   scatterMatch(const std::string& hay, const std::string& q);
double computeRelevancy(const std::string& name, const std::string& query);

// Self-test: 0=PASS, nonzero=FAIL; injectBug=true forces a red-path.
// Tests scatter-match, relevancy ranking, list building, and eye-hook naming.
int runQuickAddSelfTest(bool injectBug);

}  // namespace sw::ui
