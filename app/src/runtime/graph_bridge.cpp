#include "runtime/graph_bridge.h"

namespace sw {

Symbol atomicSymbolFromSpec(const NodeSpec& s) {
  Symbol sym;
  sym.id = s.type;
  sym.name = s.title;
  sym.atomic = true;
  for (const PortSpec& p : s.ports) {
    SlotDef d{p.id, p.name, p.dataType, p.def};
    (p.isInput ? sym.inputDefs : sym.outputDefs).push_back(d);
  }
  return sym;
}

SymbolLibrary libFromGraph(const Graph& g, const std::string& rootId) {
  SymbolLibrary lib;
  lib.rootId = rootId;

  Symbol root;
  root.id = rootId;
  root.name = rootId;
  root.atomic = false;

  for (const Node& n : g.nodes) {
    const NodeSpec* s = findSpec(n.type);
    if (!s) continue;  // unknown type: skip (same tolerance as the flat cook)
    if (!lib.symbols.count(n.type)) lib.symbols[n.type] = atomicSymbolFromSpec(*s);
    SymbolChild c;
    c.id = n.id;                 // child id == node id -> resident path == id-as-string
    c.symbolId = n.type;
    c.overrides = n.params;      // stored constants ARE the instance overrides
    c.x = n.x;
    c.y = n.y;
    root.children.push_back(c);
  }

  // Wires: decode each flat pin (node id + port INDEX) to (child id, slot ID). A pin whose
  // node/spec/port doesn't resolve drops the wire — never a crash, matches cook tolerance.
  auto slotOf = [&](int pin, std::string& slotId) -> bool {
    const Node* n = g.node(pinNode(pin));
    const NodeSpec* s = n ? findSpec(n->type) : nullptr;
    if (!s) return false;
    int idx = (pin - 1) - pinNode(pin) * 100;
    if (idx < 0 || idx >= (int)s->ports.size()) return false;
    slotId = s->ports[idx].id;
    return true;
  };
  for (const Connection& c : g.connections) {
    SymbolConnection w;
    std::string srcSlot, dstSlot;
    if (!slotOf(c.fromPin, srcSlot) || !slotOf(c.toPin, dstSlot)) continue;
    w.srcChild = pinNode(c.fromPin);
    w.srcSlot = srcSlot;
    w.dstChild = pinNode(c.toPin);
    w.dstSlot = dstSlot;
    root.connections.push_back(w);
  }

  lib.symbols[rootId] = root;
  return lib;
}

}  // namespace sw
