#include "runtime/graph_bridge.h"

namespace sw {

NodeSpec specFromSymbol(const Symbol& s) {
  NodeSpec spec;
  spec.type = s.id;
  spec.title = s.name.empty() ? s.id : s.name;
  spec.evaluate = nullptr;  // compounds evaluate by INLINE (resident graph), never atomically
  for (const SlotDef& d : s.inputDefs) {
    PortSpec p;
    p.id = d.id;
    p.name = d.name.empty() ? d.id : d.name;
    p.dataType = d.dataType;
    p.isInput = true;
    p.def = d.def;
    p.minV = -10.0f;  // placeholder slider range until the per-input Min/Max sidecar (S19)
    p.maxV = 10.0f;
    spec.ports.push_back(p);
  }
  for (const SlotDef& d : s.outputDefs) {
    PortSpec p;
    p.id = d.id;
    p.name = d.name.empty() ? d.id : d.name;
    p.dataType = d.dataType;
    p.isInput = false;
    spec.ports.push_back(p);
  }
  return spec;
}

void refreshCompoundSpecs(const SymbolLibrary& lib) {
  std::map<std::string, NodeSpec> dyn;
  for (const auto& kv : lib.symbols)
    if (!kv.second.atomic) dyn[kv.first] = specFromSymbol(kv.second);
  setDynamicSpecs(std::move(dyn));
}

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
  // Carry the flat graph's monotonic counter over as the child-id floor (v1 nextId was
  // serialized for exactly this never-reuse guarantee; ≥ every node id by construction).
  root.nextChildId = g.nextId;

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

bool graphFromLib(const SymbolLibrary& lib, Graph& out, std::vector<std::string>* warnings) {
  const Symbol* root = lib.find(lib.rootId);
  if (!root || root->atomic) return false;

  auto warn = [&](const std::string& m) {
    if (warnings) warnings->push_back(m);
  };

  out = Graph{};
  int maxId = 0;
  for (const SymbolChild& c : root->children) {
    const Symbol* ref = lib.find(c.symbolId);
    if (ref && !ref->atomic) return false;  // compound child: a flat graph cannot hold it (批次 3)
    if (!findSpec(c.symbolId)) {
      warn("child " + std::to_string(c.id) + " references unknown operator '" + c.symbolId +
           "' — dropped");
      continue;
    }
    Node n;
    n.id = c.id;
    n.type = c.symbolId;
    n.params = c.overrides;
    n.x = c.x;
    n.y = c.y;
    out.nodes.push_back(n);
    if (c.id > maxId) maxId = c.id;
  }

  // (childId, slotId) -> absolute pin id, via the spec's port index. -1 when unresolvable.
  auto pinOf = [&](int childId, const std::string& slot) -> int {
    const Node* n = out.node(childId);
    const NodeSpec* s = n ? findSpec(n->type) : nullptr;
    if (!s) return -1;
    for (size_t i = 0; i < s->ports.size(); ++i)
      if (s->ports[i].id == slot) return pinId(childId, (int)i);
    return -1;
  };
  int nextConnId = ((maxId / 100) + 1) * 100 + 1;  // past every node's pin range (pinId scheme)
  for (const SymbolConnection& w : root->connections) {
    if (w.srcChild == kSymbolBoundary || w.dstChild == kSymbolBoundary) {
      warn("boundary wire (" + w.srcSlot + " -> " + w.dstSlot + ") has no flat equivalent — skipped");
      continue;
    }
    int from = pinOf(w.srcChild, w.srcSlot);
    int to = pinOf(w.dstChild, w.dstSlot);
    if (from < 0 || to < 0) {
      warn("wire " + std::to_string(w.srcChild) + ":" + w.srcSlot + " -> " +
           std::to_string(w.dstChild) + ":" + w.dstSlot + " unresolvable — dropped");
      continue;
    }
    out.connections.push_back({nextConnId++, from, to});
  }
  out.nextId = nextConnId;
  return true;
}

}  // namespace sw
