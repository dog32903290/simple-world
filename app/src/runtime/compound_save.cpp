#include "runtime/compound_save.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>

#include "crude_json.h"
#include "runtime/graph.h"         // findSpec (atomic regeneration) + fromJson (legacy)
#include "runtime/graph_bridge.h"  // atomicSymbolFromSpec + libFromGraph (legacy migration)

namespace sw {
namespace {

// 承重決策 4: fixed UUIDs for the shipped atomic operator types. Assigned once, NEVER edited
// or reused — a rename of the C++ type string only touches this table, old files keep loading.
const std::map<std::string, std::string>& atomicUuidTable() {
  static const std::map<std::string, std::string> t = {
      {"RadialPoints", "5d3a9c1e-7b42-4f0e-9a11-c2e84d6f0a01"},
      {"LinePoints", "8f21b6d4-3c5a-4e87-b390-1a7e5cd20b02"},
      {"GridPoints", "2c74e8a9-651f-4b3d-8e26-94b0f3a1cc03"},
      {"SpherePoints", "b19d4f72-08e3-4a56-bc81-7d2ea6450d04"},
      {"TransformPoints", "e6582ab0-94cd-4713-a5f8-30c1b78e9e05"},
      {"OrientPoints", "47cfd913-2ab8-4c60-9374-e8156fab2f06"},
      {"RandomizePoints", "90ab37c5-de14-4982-b6c0-52f47e91a007"},
      {"SetPointAttributes", "3e60c2f8-71a9-4d35-82eb-c49d08361108"},
      {"CombineBuffers", "ad95e041-b37f-4628-95a2-16e83c7d2209"},
      {"TurbulenceForce", "61f8ba2d-c490-47e1-8d53-a02b9e64330a"},
      {"ParticleSystem", "f04c79e6-25d1-4b98-ae37-68915cfa440b"},
      {"DrawPoints", "7a2e58c1-9f06-4374-b1d9-3ce5a0b2550c"},
      {"RenderTarget", "c8b1f3a7-46e2-4d09-92c5-eb7480d3660d"},
      {"Time", "19d672e8-83ba-4f51-a046-5d29c1ef770e"},
      {"AudioReaction", "642a90fb-1ce7-4583-bd28-09f4a36c880f"},
      {"Const", "d537c4b2-68a0-4e19-8f63-714be0952910"},
      {"Multiply", "0be8a15f-d943-4c27-96da-83f50c167a11"},
      {"Sine", "85f10dc9-3741-4ab6-805e-2c96db48fb12"},
      {"Remap", "4a9be6f0-507c-4d82-b1c4-da3871e90c13"},
  };
  return t;
}

void appendWarn(std::vector<std::string>* w, const std::string& msg) {
  if (w) w->push_back(msg);
}

crude_json::value slotDefToJson(const SlotDef& d, bool isInput) {
  crude_json::object o;
  o["id"] = d.id;
  o["name"] = d.name;
  o["dataType"] = d.dataType;
  if (isInput) o["def"] = (crude_json::number)d.def;
  return crude_json::value(o);
}

SlotDef slotDefFromJson(crude_json::value& v) {
  SlotDef d;
  if (v["id"].is_string()) d.id = v["id"].get<crude_json::string>();
  if (v["name"].is_string()) d.name = v["name"].get<crude_json::string>();
  if (v["dataType"].is_string()) d.dataType = v["dataType"].get<crude_json::string>();
  if (v["def"].is_number()) d.def = (float)v["def"].get<crude_json::number>();
  return d;
}

}  // namespace

std::string atomicUuidForType(const std::string& type) {
  if (type.empty()) return "";
  auto it = atomicUuidTable().find(type);
  return it != atomicUuidTable().end() ? it->second : "sw-type:" + type;
}

std::string typeForAtomicUuid(const std::string& uuid) {
  if (uuid.rfind("sw-type:", 0) == 0) return uuid.substr(8);
  for (const auto& kv : atomicUuidTable())
    if (kv.second == uuid) return kv.first;
  return "";
}

std::string libToJsonV2(const SymbolLibrary& lib) {
  // A child's on-disk symbolId: atomic -> fixed UUID; compound -> the raw symbol id.
  auto refOf = [&](const std::string& symbolId) -> std::string {
    const Symbol* s = lib.find(symbolId);
    if (s && !s->atomic) return symbolId;
    return atomicUuidForType(symbolId);
  };

  crude_json::object root;
  root["formatVersion"] = (crude_json::number)2;
  root["rootSymbolId"] = lib.rootId;

  // Only compound symbols, sorted by id (stable file = clean diffs, TiXL OrderBy(Id)).
  std::vector<const Symbol*> compounds;
  for (const auto& kv : lib.symbols)
    if (!kv.second.atomic) compounds.push_back(&kv.second);
  std::sort(compounds.begin(), compounds.end(),
            [](const Symbol* a, const Symbol* b) { return a->id < b->id; });

  crude_json::array symbols;
  for (const Symbol* s : compounds) {
    crude_json::object o;
    o["id"] = s->id;
    o["name"] = s->name;
    crude_json::array ins, outs;  // array order == definition order (S16)
    for (const SlotDef& d : s->inputDefs) ins.push_back(slotDefToJson(d, /*isInput=*/true));
    for (const SlotDef& d : s->outputDefs) outs.push_back(slotDefToJson(d, /*isInput=*/false));
    o["inputDefs"] = crude_json::value(ins);
    o["outputDefs"] = crude_json::value(outs);
    crude_json::array children;
    for (const SymbolChild& c : s->children) {
      crude_json::object co;
      co["id"] = (crude_json::number)c.id;
      co["symbolId"] = refOf(c.symbolId);
      crude_json::object ov;
      for (const auto& kv : c.overrides) ov[kv.first] = (crude_json::number)kv.second;
      co["overrides"] = crude_json::value(ov);
      co["x"] = (crude_json::number)c.x;
      co["y"] = (crude_json::number)c.y;
      children.push_back(crude_json::value(co));
    }
    o["children"] = crude_json::value(children);
    crude_json::array conns;  // array order == multi-input order (stable)
    for (const SymbolConnection& w : s->connections) {
      crude_json::object wo;
      wo["srcChild"] = (crude_json::number)w.srcChild;
      wo["srcSlot"] = w.srcSlot;
      wo["dstChild"] = (crude_json::number)w.dstChild;
      wo["dstSlot"] = w.dstSlot;
      conns.push_back(crude_json::value(wo));
    }
    o["connections"] = crude_json::value(conns);
    symbols.push_back(crude_json::value(o));
  }
  root["symbols"] = crude_json::value(symbols);
  return crude_json::value(root).dump(2);
}

bool libFromJsonAny(const std::string& json, SymbolLibrary& out,
                    std::vector<std::string>* warnings) {
  crude_json::value v = crude_json::value::parse(json);
  if (!v.is_object()) return false;

  // LEGACY v1 (no formatVersion): a flat editor graph. Migrate through the bridge — the
  // result is a root compound whose children mirror the nodes (auto-upgrades on next save).
  if (!v["formatVersion"].is_number()) {
    Graph g;
    if (!fromJson(json, g)) return false;
    out = libFromGraph(g);
    appendWarn(warnings, "legacy v1 .swproj migrated to v2 (flat graph -> root symbol)");
    return true;
  }

  const int fmt = (int)v["formatVersion"].get<crude_json::number>();
  if (fmt > 2)
    appendWarn(warnings, "file formatVersion " + std::to_string(fmt) +
                             " is newer than this app (2) — loading what is understood");

  out = SymbolLibrary{};

  // ---- phase 1: register every compound symbol DEFINITION (no reference resolution yet,
  // 兩階段 load 照搬 — children/connections resolve only after every definition exists). ----
  crude_json::value& symbols = v["symbols"];
  if (!symbols.is_array()) return false;
  for (auto& sv : symbols.get<crude_json::array>()) {
    if (!sv["id"].is_string()) { appendWarn(warnings, "symbol without id dropped"); continue; }
    Symbol s;
    s.id = sv["id"].get<crude_json::string>();
    s.name = sv["name"].is_string() ? sv["name"].get<crude_json::string>() : s.id;
    s.atomic = false;
    if (sv["inputDefs"].is_array())
      for (auto& dv : sv["inputDefs"].get<crude_json::array>()) s.inputDefs.push_back(slotDefFromJson(dv));
    if (sv["outputDefs"].is_array())
      for (auto& dv : sv["outputDefs"].get<crude_json::array>()) s.outputDefs.push_back(slotDefFromJson(dv));
    out.symbols[s.id] = s;
  }

  // ---- phase 2: resolve children (atomic uuid -> registry regeneration; unknown -> drop)
  // and scrub unresolvable wires (S15: local drop + warn, next save self-heals). ----
  for (auto& sv : symbols.get<crude_json::array>()) {
    if (!sv["id"].is_string()) continue;
    Symbol& s = out.symbols[sv["id"].get<crude_json::string>()];

    if (sv["children"].is_array()) {
      for (auto& cv : sv["children"].get<crude_json::array>()) {
        SymbolChild c;
        c.id = cv["id"].is_number() ? (int)cv["id"].get<crude_json::number>() : 0;
        std::string ref = cv["symbolId"].is_string() ? cv["symbolId"].get<crude_json::string>() : "";
        std::string atomicType = typeForAtomicUuid(ref);
        if (!atomicType.empty()) {
          const NodeSpec* spec = findSpec(atomicType);
          if (!spec) {
            appendWarn(warnings, "child " + std::to_string(c.id) + " in '" + s.id +
                                     "' references unknown operator '" + atomicType + "' — dropped");
            continue;
          }
          if (!out.symbols.count(atomicType)) out.symbols[atomicType] = atomicSymbolFromSpec(*spec);
          c.symbolId = atomicType;
        } else if (out.symbols.count(ref) && !out.symbols[ref].atomic) {
          c.symbolId = ref;  // compound reference
        } else {
          appendWarn(warnings, "child " + std::to_string(c.id) + " in '" + s.id +
                                   "' references missing symbol '" + ref + "' — dropped");
          continue;
        }
        if (c.id <= 0) { appendWarn(warnings, "child with invalid id in '" + s.id + "' dropped"); continue; }
        if (cv["overrides"].is_object())
          for (auto& kv : cv["overrides"].get<crude_json::object>())
            if (kv.second.is_number()) c.overrides[kv.first] = (float)kv.second.get<crude_json::number>();
        if (cv["x"].is_number()) c.x = (float)cv["x"].get<crude_json::number>();
        if (cv["y"].is_number()) c.y = (float)cv["y"].get<crude_json::number>();
        s.children.push_back(c);
      }
    }

    if (sv["connections"].is_array()) {
      auto childOf = [&](int id) -> const SymbolChild* {
        for (const SymbolChild& c : s.children)
          if (c.id == id) return &c;
        return nullptr;
      };
      auto hasSlot = [&](const std::vector<SlotDef>& defs, const std::string& slot) {
        for (const SlotDef& d : defs)
          if (d.id == slot) return true;
        return false;
      };
      for (auto& wv : sv["connections"].get<crude_json::array>()) {
        SymbolConnection w;
        w.srcChild = wv["srcChild"].is_number() ? (int)wv["srcChild"].get<crude_json::number>() : -1;
        w.dstChild = wv["dstChild"].is_number() ? (int)wv["dstChild"].get<crude_json::number>() : -1;
        if (wv["srcSlot"].is_string()) w.srcSlot = wv["srcSlot"].get<crude_json::string>();
        if (wv["dstSlot"].is_string()) w.dstSlot = wv["dstSlot"].get<crude_json::string>();
        bool ok = true;
        if (w.srcChild == kSymbolBoundary) {
          ok = hasSlot(s.inputDefs, w.srcSlot);  // sentinel: the parent's own input def
        } else if (const SymbolChild* c = childOf(w.srcChild)) {
          const Symbol* ref = out.find(c->symbolId);
          ok = ref && hasSlot(ref->outputDefs, w.srcSlot);
        } else {
          ok = false;
        }
        if (ok) {
          if (w.dstChild == kSymbolBoundary) {
            ok = hasSlot(s.outputDefs, w.dstSlot);  // sentinel: the parent's own output def
          } else if (const SymbolChild* c = childOf(w.dstChild)) {
            const Symbol* ref = out.find(c->symbolId);
            ok = ref && hasSlot(ref->inputDefs, w.dstSlot);
          } else {
            ok = false;
          }
        }
        if (!ok) {
          appendWarn(warnings, "dangling wire " + std::to_string(w.srcChild) + ":" + w.srcSlot +
                                   " -> " + std::to_string(w.dstChild) + ":" + w.dstSlot + " in '" +
                                   s.id + "' — dropped");
          continue;  // self-heal: gone from the in-memory model, gone on next save
        }
        s.connections.push_back(w);
      }
    }
  }

  // Root resolve: missing/unknown root falls back to the first compound (load 不死).
  if (v["rootSymbolId"].is_string()) out.rootId = v["rootSymbolId"].get<crude_json::string>();
  const Symbol* root = out.find(out.rootId);
  if (!root || root->atomic) {
    out.rootId.clear();
    for (const auto& kv : out.symbols)
      if (!kv.second.atomic) { out.rootId = kv.first; break; }
    if (out.rootId.empty()) return false;  // nothing usable
    appendWarn(warnings, "rootSymbolId missing/unknown — fell back to '" + out.rootId + "'");
  }
  return true;
}

bool saveLibToFile(const std::string& path, const SymbolLibrary& lib) {
  std::ofstream f(path);
  if (!f) return false;
  f << libToJsonV2(lib);
  return f.good();
}

bool loadLibFromFile(const std::string& path, SymbolLibrary& out,
                     std::vector<std::string>* warnings) {
  std::ifstream f(path);
  if (!f) return false;
  std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return libFromJsonAny(json, out, warnings);
}

}  // namespace sw
