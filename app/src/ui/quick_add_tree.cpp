// ui/quick_add_tree — namespace-tree grouping + imgui walk for the Add Node palette.
// Zone: ui. Split from quick_add.cpp (ARCHITECTURE rule 4: keep that file ≤400 lines).
//
// buildNamespaceTree is the PURE grouping (no imgui) the isolated --selftest-quickadd exercises
// (the empty-query view groups ops by Symbol.Namespace = TiXL NamespaceTreeNode). drawNamespaceTree
// is the imgui walk (= TiXL SymbolTreeMenu.DrawNodesRecursively: sub-namespaces as collapsible
// folders, then the symbols that terminate at this level). Both are declared in quick_add.h.
#include "ui/quick_add.h"

#include <string>
#include <vector>

#include "imgui.h"

#include "app/document.h"          // doc::g_lib()
#include "runtime/compound_graph.h"// Symbol / SlotDef (compound outputDefs)
#include "runtime/graph.h"         // findSpec / NodeSpec / addChildWouldCycle
#include "ui/node_style.h"         // labelColor (TiXL OperatorLabel tint of the dataType base)
#include "verify/eye/eye.h"        // one-line eye hooks (qa:ns:* / qa:* / qatype:*)

namespace sw::ui {

// First OUTPUT dataType — atomic spec's first non-input port, else compound's first outputDef.
// READ-ONLY access to graph.h / compound_graph.h (no mutation, no new fields).
std::string firstOutputType(const std::string& id) {
    if (const sw::NodeSpec* spec = sw::findSpec(id)) {
        for (const auto& p : spec->ports)
            if (!p.isInput) return p.dataType;
        return "";
    }
    if (const sw::Symbol* s = sw::doc::g_lib().find(id))
        if (!s->outputDefs.empty()) return s->outputDefs.front().dataType;
    return "";
}

// Port-drag type-filter predicates (= TiXL SymbolFilter UpdateMatchingSymbols / GetInputMatchingType
// / GetOutputMatchingType, Symbol.cs:332-352). STRICT dataType equality, scanning EVERY port (TiXL
// checks all InputDefinitions/OutputDefinitions, not just the first). Dual lookup like
// firstOutputType above: atomic ops resolve through the registry spec's `ports`; live compounds
// through inputDefs/outputDefs. READ-ONLY; no mutation, no new fields. Empty dataType = no
// constraint (= TiXL's _inputType/_outputType == null short-circuit).
bool opHasInputOfType(const std::string& id, const std::string& dataType) {
    if (dataType.empty()) return true;
    if (const sw::NodeSpec* spec = sw::findSpec(id)) {
        for (const auto& p : spec->ports)
            if (p.isInput && p.dataType == dataType) return true;
        return false;
    }
    if (const sw::Symbol* s = sw::doc::g_lib().find(id))
        for (const auto& d : s->inputDefs)
            if (d.dataType == dataType) return true;
    return false;
}

bool opHasOutputOfType(const std::string& id, const std::string& dataType) {
    if (dataType.empty()) return true;
    if (const sw::NodeSpec* spec = sw::findSpec(id)) {
        for (const auto& p : spec->ports)
            if (!p.isInput && p.dataType == dataType) return true;
        return false;
    }
    if (const sw::Symbol* s = sw::doc::g_lib().find(id))
        for (const auto& d : s->outputDefs)
            if (d.dataType == dataType) return true;
    return false;
}

// Combined gate (= TiXL SymbolFilter: the _inputType block keeps ops with a matching input, the
// _outputType block keeps ops with a matching output; an unset filter imposes no constraint). When
// BOTH are set (drag onto a snapped connection, PlaceholderCreation.cs:74-76) the op must satisfy
// both directions — AND semantics, matching TiXL's two sequential `continue`s.
bool passesPortFilter(const std::string& id, const std::string& inputFilter,
                      const std::string& outputFilter) {
    return opHasInputOfType(id, inputFilter) && opHasOutputOfType(id, outputFilter);
}

// Type-hint line at the palette top (= TiXL PlaceHolderUi.cs:322-331 DrawTypeFilterHint): surfaces
// the constraining dataType(s) of an active port-drag pre-filter. No-op for the unfiltered Cmd+F
// path. Emits qa:filter:in/out eye markers so the hand/eye harness can assert the active filter.
void drawTypeFilterHint(const std::string& inputFilter, const std::string& outputFilter) {
    if (!inputFilter.empty()) {
        ImGui::TextDisabled("\xE2\x86\x92 in: %s", inputFilter.c_str());  // "→ in: <type>"
        sw::eye::recordItem(("qa:filter:in:" + inputFilter).c_str());
    }
    if (!outputFilter.empty()) {
        ImGui::TextDisabled("out: %s \xE2\x86\x92", outputFilter.c_str());  // "out: <type> →"
        sw::eye::recordItem(("qa:filter:out:" + outputFilter).c_str());
    }
}

namespace {
// Port-summary tooltip built ONLY from existing fields (no description field added). Atomic:
// list non-Vec-tail input port names + their dataType, then outputs. Compound: input/output defs.
void portSummaryTooltip(const std::string& id) {
    if (!ImGui::IsItemHovered()) return;
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(id.c_str());
    ImGui::Separator();
    if (const sw::NodeSpec* spec = sw::findSpec(id)) {
        for (const auto& p : spec->ports) {
            // Skip the trailing components of a Vec group (vecArity 1 followers) to avoid noise.
            if (p.widget == sw::Widget::Vec && p.vecArity == 1) continue;
            ImGui::TextDisabled("%s %s : %s", p.isInput ? "in " : "out", p.name.c_str(),
                                p.dataType.c_str());
        }
    } else if (const sw::Symbol* s = sw::doc::g_lib().find(id)) {
        for (const auto& d : s->inputDefs)
            ImGui::TextDisabled("in  %s : %s", d.name.c_str(), d.dataType.c_str());
        for (const auto& d : s->outputDefs)
            ImGui::TextDisabled("out %s : %s", d.name.c_str(), d.dataType.c_str());
    }
    ImGui::EndTooltip();
}
}  // namespace

// Shared result-row renderer (= TiXL DrawSymbolUiEntry): type-colored title + dim dataType suffix
// + hover port tooltip. Emits qa:<id> always and qatype:<id>:<type> when a first-output type
// resolves (the eye marker that proves the type path ran — non-tautological scenario hook).
bool drawResultRow(const std::string& id, const std::string& label, bool selected, bool cyclic,
                   bool showType) {
    const std::string outType = firstOutputType(id);
    ImGui::BeginDisabled(cyclic);
    // Title tint = TiXL ColorVariations.OperatorLabel.Apply(color) (b1.3 s0.4 a1.0,
    // PlaceHolderUi.cs:424) — the brighter/desaturated label shade, NOT the raw saturated base.
    // Pushed unconditionally (TiXL tints the title before the showType check, so the tint is
    // never gated by showType — only the trailing suffix below is).
    if (!outType.empty())
        ImGui::PushStyleColor(ImGuiCol_Text, sw::ui::labelColor(outType));
    bool clicked = ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_None);
    if (!outType.empty())
        ImGui::PopStyleColor();
    // Dim dataType suffix after the title (= TiXL trailing type label, PlaceHolderUi.cs:459):
    // gated by showType — the flat search list shows it; the namespace-tree view suppresses it
    // (TiXL passes showType=false for every tree/grouped row, SymbolBrowsing.cs:81,163).
    if (showType && !outType.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("  %s", outType.c_str());
    }
    ImGui::EndDisabled();
    portSummaryTooltip(id);
    // eye hooks: row identity + (typed rows only) the surfaced dataType suffix.
    sw::eye::recordItem(("qa:" + id).c_str());
    if (!outType.empty())
        sw::eye::recordItem(("qatype:" + id + ":" + outType).c_str());
    return clicked;
}

// = TiXL NamespaceTreeNode.PopulateCompleteTree: split each category on '.' and descend, creating
// folders on the way; the symbol terminates at the final folder. Empty category -> "(uncategorized)"
// so no symbol is ever dropped from the tree (a wrong group is worse than a parked one).
NamespaceNode buildNamespaceTree(const std::vector<std::string>& items,
                                 const std::function<std::string(const std::string&)>& catOf) {
    NamespaceNode root;
    root.name = "root";
    for (const std::string& id : items) {
        std::string cat = catOf(id);
        if (cat.empty()) cat = "(uncategorized)";
        NamespaceNode* cur = &root;
        size_t start = 0;
        while (start <= cat.size()) {
            size_t dot = cat.find('.', start);
            std::string part = (dot == std::string::npos) ? cat.substr(start)
                                                          : cat.substr(start, dot - start);
            if (!part.empty()) {
                NamespaceNode& child = cur->children[part];
                if (child.name.empty()) child.name = part;
                cur = &child;
            }
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        cur->symbols.push_back(id);
    }
    return root;
}

namespace {
// Recursive walk: prefix accumulates the dotted path so far (so each folder's eye key is its FULL
// namespace, e.g. qa:ns:point.draw). Folders drawn before symbols (TiXL order).
void drawFolder(const NamespaceNode& node, const std::string& prefix, const std::string& curId,
                bool& spawnRequested, std::string& spawnType,
                const std::function<std::string(const std::string&)>& displayName) {
    for (const auto& kv : node.children) {
        const NamespaceNode& child = kv.second;
        const std::string full = prefix.empty() ? child.name : prefix + "." + child.name;
        bool open = ImGui::TreeNode(child.name.c_str());
        // Eye hook AFTER the TreeNode so the recorded rect is the folder header itself (the
        // scenario clicks @qa:ns:<path> to expand it). Key = qa:ns:<full path>.
        sw::eye::recordItem(("qa:ns:" + full).c_str());
        if (open) {
            drawFolder(child, full, curId, spawnRequested, spawnType, displayName);
            ImGui::TreePop();
        }
    }
    for (const std::string& id : node.symbols) {
        const bool cyclic = sw::addChildWouldCycle(sw::doc::g_lib(), curId, id);
        // Type-colored title + tooltip + qa:/qatype: eye hooks. showType=false: the tree view
        // suppresses the dataType suffix (= TiXL SymbolBrowsing.cs:81,163 pass showType=false);
        // only the flat free-text list shows it. Tint + tooltip stay.
        bool clicked = drawResultRow(id, displayName(id), false, cyclic, /*showType=*/false);
        if (clicked && !cyclic && !spawnRequested) {
            spawnRequested = true;
            spawnType = id;
        }
    }
}
}  // namespace

void drawNamespaceTree(const NamespaceNode& root, const std::string& curId, bool& spawnRequested,
                       std::string& spawnType,
                       const std::function<std::string(const std::string&)>& displayName) {
    drawFolder(root, std::string(), curId, spawnRequested, spawnType, displayName);
}

}  // namespace sw::ui
