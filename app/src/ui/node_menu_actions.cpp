// ui/node_menu_actions — see node_menu_actions.h.
#include "ui/node_menu_actions.h"

#include <memory>
#include <set>
#include <vector>

#include "imgui_node_editor.h"

#include "app/command.h"         // g_commands
#include "app/document.h"        // currentSymbol(Const) / g_lib
#include "app/graph_commands.h"  // DeleteChildrenCommand / MoveChildrenCommand
#include "runtime/compound_graph.h"  // Symbol / SymbolChild / SymbolConnection / childById

namespace ed = ax::NodeEditor;

namespace sw::ui {

void selectConnected(const std::vector<int>& seedIds) {
  const sw::Symbol* cur = sw::doc::currentSymbolConst();
  if (!cur || seedIds.empty()) return;
  std::set<int> reached(seedIds.begin(), seedIds.end());
  bool grew = true;
  while (grew) {  // BFS to fixpoint over child<->child wires (boundary endpoints ignored)
    grew = false;
    for (const sw::SymbolConnection& w : cur->connections) {
      if (w.srcChild <= 0 || w.dstChild <= 0) continue;  // skip parent-boundary endpoints
      const bool haveSrc = reached.count(w.srcChild) != 0;
      const bool haveDst = reached.count(w.dstChild) != 0;
      if (haveSrc != haveDst) {  // wire bridges into an unreached node -> pull it in
        reached.insert(haveSrc ? w.dstChild : w.srcChild);
        grew = true;
      }
    }
  }
  ed::ClearSelection();
  for (int id : reached) ed::SelectNode(ed::NodeId(id), /*append=*/true);
}

void alignSelectionLeft(const std::vector<int>& ids) {
  sw::Symbol* cur = sw::doc::currentSymbol();
  if (!cur || ids.size() < 2) return;
  float minX = 0.0f; bool any = false;
  for (int id : ids)
    if (const sw::SymbolChild* c = sw::childById(*cur, id)) {
      if (!any || c->x < minX) minX = c->x;
      any = true;
    }
  if (!any) return;
  std::vector<sw::MoveChildrenCommand::Move> moves;
  for (int id : ids)
    if (const sw::SymbolChild* c = sw::childById(*cur, id))
      if (c->x != minX) moves.push_back({id, c->x, c->y, minX, c->y});
  if (moves.empty()) return;  // already aligned -> no dead undo entry (TiXL parity)
  sw::g_commands.push(
      std::make_unique<sw::MoveChildrenCommand>(sw::doc::g_lib(), cur->id, std::move(moves)));
}

void deleteCaptured(const std::vector<int>& ids) {
  sw::Symbol* cur = sw::doc::currentSymbol();
  if (!cur || ids.empty()) return;
  sw::g_commands.push(std::make_unique<sw::DeleteChildrenCommand>(sw::doc::g_lib(), cur->id, ids));
  ed::ClearSelection();
}

}  // namespace sw::ui
