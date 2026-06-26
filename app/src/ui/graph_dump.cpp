// ui/graph_dump — see graph_dump.h. Serializes the current compound for eye's req_graph dump.
#include "ui/graph_dump.h"

#include <cstdio>
#include <string>
#include <vector>

#include "app/document.h"            // doc::currentSymbolConst / g_compositionPath / g_lib
#include "runtime/compound_graph.h"  // Symbol / SymbolChild / SymbolConnection / SlotDef
#include "runtime/graph.h"           // findSpec / NodeSpec / PortSpec (atomic child ports)
#include "verify/eye/eye.h"          // setGraphDumpHook + writeGraphDump (the test drives the live path)

namespace sw::ui {

namespace {

// Minimal JSON string escape: ids/dataTypes are ascii identifiers, but a custom instance name may
// carry quotes/backslashes/control chars (CJK passes through as raw UTF-8, valid in JSON).
void appendJsonString(std::string& out, const std::string& s) {
  out += '"';
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((unsigned char)c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", (int)(unsigned char)c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

// One port descriptor: {id, dataType, isInput, multiInput}.
struct PortInfo {
  std::string id, dataType;
  bool isInput = false;
  bool multiInput = false;
};

// Resolve a child's ports — atomic op (findSpec) OR compound instance (the referenced Symbol's
// input/output defs). Mirrors connection_ops::portInfo's atomic-vs-compound split, but enumerates
// EVERY port (the dump lists the whole port surface, not one slot). Compound defs have no
// multiInput concept in the model, so those report false.
std::vector<PortInfo> portsOf(const SymbolLibrary& lib, const SymbolChild& ch) {
  std::vector<PortInfo> ports;
  if (const NodeSpec* sp = findSpec(ch.symbolId)) {
    for (const PortSpec& p : sp->ports)
      ports.push_back({p.id, p.dataType, p.isInput, p.multiInput});
    return ports;
  }
  if (const Symbol* def = lib.find(ch.symbolId)) {
    for (const SlotDef& d : def->inputDefs)
      ports.push_back({d.id, d.dataType, /*isInput=*/true, /*multiInput=*/false});
    for (const SlotDef& d : def->outputDefs)
      ports.push_back({d.id, d.dataType, /*isInput=*/false, /*multiInput=*/false});
  }
  return ports;
}

}  // namespace

std::string serializeCurrentCompound() {
  const Symbol* cur = sw::doc::currentSymbolConst();
  if (!cur) return "{\"compound\": null, \"children\": [], \"connections\": []}\n";
  const SymbolLibrary& lib = sw::doc::g_lib();

  std::string out;
  out += "{\n";
  out += "  \"compound\": {\"id\": ";
  appendJsonString(out, cur->id);
  out += ", \"name\": ";
  appendJsonString(out, cur->name);
  out += "},\n";

  // breadcrumb: the composition path (child ids from root to here). [] at root.
  out += "  \"breadcrumb\": [";
  for (size_t i = 0; i < sw::doc::g_compositionPath.size(); ++i)
    out += (i ? ", " : "") + std::to_string(sw::doc::g_compositionPath[i]);
  out += "],\n";

  // children[]: id, opType (the symbolId — atomic op type OR compound def id), name, ports[].
  out += "  \"children\": [\n";
  for (size_t ci = 0; ci < cur->children.size(); ++ci) {
    const SymbolChild& ch = cur->children[ci];
    out += "    {\"childId\": " + std::to_string(ch.id) + ", \"opType\": ";
    appendJsonString(out, ch.symbolId);
    out += ", \"name\": ";
    appendJsonString(out, ch.name);
    out += ", \"ports\": [";
    std::vector<PortInfo> ports = portsOf(lib, ch);
    for (size_t pi = 0; pi < ports.size(); ++pi) {
      const PortInfo& p = ports[pi];
      out += (pi ? ", " : "");
      out += "{\"id\": ";
      appendJsonString(out, p.id);
      out += ", \"dataType\": ";
      appendJsonString(out, p.dataType);
      out += ", \"isInput\": ";
      out += p.isInput ? "true" : "false";
      out += ", \"multiInput\": ";
      out += p.multiInput ? "true" : "false";
      out += "}";
    }
    out += "]}";
    out += (ci + 1 < cur->children.size()) ? ",\n" : "\n";
  }
  out += "  ],\n";

  // connections[]: the 4-tuple wires (childId==0 == the compound's own boundary slot).
  out += "  \"connections\": [\n";
  for (size_t wi = 0; wi < cur->connections.size(); ++wi) {
    const SymbolConnection& w = cur->connections[wi];
    out += "    {\"srcChild\": " + std::to_string(w.srcChild) + ", \"srcSlot\": ";
    appendJsonString(out, w.srcSlot);
    out += ", \"dstChild\": " + std::to_string(w.dstChild) + ", \"dstSlot\": ";
    appendJsonString(out, w.dstSlot);
    out += "}";
    out += (wi + 1 < cur->connections.size()) ? ",\n" : "\n";
  }
  out += "  ]\n";
  out += "}\n";
  return out;
}

void mountGraphDump() { sw::eye::setGraphDumpHook(&serializeCurrentCompound); }

}  // namespace sw::ui
