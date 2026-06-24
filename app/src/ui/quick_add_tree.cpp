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

#include "app/document.h"      // doc::g_lib
#include "runtime/graph.h"     // addChildWouldCycle
#include "verify/eye/eye.h"    // one-line eye hooks (qa:ns:* / qa:*)

namespace sw::ui {

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
        const bool cyclic = sw::addChildWouldCycle(sw::doc::g_lib, curId, id);
        ImGui::BeginDisabled(cyclic);
        bool clicked = ImGui::Selectable(displayName(id).c_str(), false, ImGuiSelectableFlags_None);
        ImGui::EndDisabled();
        // Eye hook: one line per leaf row, key = qa:<typeId> (same key the flat list emits).
        sw::eye::recordItem(("qa:" + id).c_str());
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
