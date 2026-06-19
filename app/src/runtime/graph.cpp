#include "runtime/graph.h"
#include "runtime/Particle.h"  // full EvaluationContext definition (forward-decl'd in graph.h)
#include "runtime/source_registry.h"  // BindingKind/LiveSource/SourceRegistry (L5 resolution)

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include "crude_json.h"

namespace sw {
Node* Graph::node(int id) {
  for (auto& n : nodes)
    if (n.id == id) return &n;
  return nullptr;
}
const Node* Graph::node(int id) const {
  for (const auto& n : nodes)
    if (n.id == id) return &n;
  return nullptr;
}
const Node* Graph::firstOfType(const std::string& type) const {
  for (const auto& n : nodes)
    if (n.type == type) return &n;
  return nullptr;
}
const Connection* Graph::connectionToInput(int inputPin) const {
  for (const Connection& c : connections)
    if (c.toPin == inputPin) return &c;
  return nullptr;
}
float Graph::param(const std::string& type, const std::string& paramId, float fallback) const {
  const Node* n = firstOfType(type);
  if (!n) return fallback;
  auto it = n->params.find(paramId);
  return it == n->params.end() ? fallback : it->second;
}

namespace {
Node makeNode(int id, const char* type, float x, float y) {
  Node n;
  n.id = id;
  n.type = type;
  n.x = x;
  n.y = y;
  if (const NodeSpec* s = findSpec(type))
    for (const auto& p : s->ports) {
      if (p.isInput && p.dataType == "Float") n.params[p.id] = p.def;  // seed from spec defaults
      else if (p.isInput && p.dataType == "String") n.strParams[p.id] = p.strDef;  // String sub-seam
    }
  return n;
}
}  // namespace

Graph defaultParticleGraph() {
  Graph g;
  g.nodes.push_back(makeNode(1, "RadialPoints", 20, 30));
  g.nodes.push_back(makeNode(2, "ParticleSystem", 260, 90));
  g.nodes.push_back(makeNode(6, "TurbulenceForce", 20, 160));
  g.nodes.push_back(makeNode(7, "DrawPoints", 560, 50));
  // AudioReaction present but UNWIRED: 柏為 drags level -> a knob (e.g. via Multiply) to
  // map sound to it. No hidden auto-drive — the wire is his to make and to see.
  g.nodes.push_back(makeNode(8, "AudioReaction", 20, 300));
  // RadialPoints.points(1,0) -> ParticleSystem.emit(2,0)
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  // TurbulenceForce.force(6,0) -> ParticleSystem.forces(2,1)
  g.connections.push_back({102, pinId(6, 0), pinId(2, 1)});
  // ParticleSystem.result(2,2) -> DrawPoints.points(7,0)
  g.connections.push_back({103, pinId(2, 2), pinId(7, 0)});
  g.nextId = 104;
  return g;
}

std::string toJson(const Graph& g) {
  crude_json::object root;
  root["nextId"] = (crude_json::number)g.nextId;
  crude_json::array nodes;
  for (const auto& n : g.nodes) {
    crude_json::object o;
    o["id"] = (crude_json::number)n.id;
    o["type"] = n.type;
    o["x"] = (crude_json::number)n.x;
    o["y"] = (crude_json::number)n.y;
    crude_json::object params;
    for (const auto& kv : n.params) params[kv.first] = (crude_json::number)kv.second;
    o["params"] = crude_json::value(params);
    // String sub-seam: serialize strParams only when non-empty (zero churn for float-only nodes).
    if (!n.strParams.empty()) {
      crude_json::object strParams;
      for (const auto& kv : n.strParams) strParams[kv.first] = (crude_json::string)kv.second;
      o["strParams"] = crude_json::value(strParams);
    }
    nodes.push_back(crude_json::value(o));
  }
  root["nodes"] = crude_json::value(nodes);
  crude_json::array conns;
  for (const auto& c : g.connections) {
    crude_json::object o;
    o["id"] = (crude_json::number)c.id;
    o["fromPin"] = (crude_json::number)c.fromPin;
    o["toPin"] = (crude_json::number)c.toPin;
    conns.push_back(crude_json::value(o));
  }
  root["connections"] = crude_json::value(conns);
  return crude_json::value(root).dump(2);
}

bool fromJson(const std::string& json, Graph& out) {
  crude_json::value v = crude_json::value::parse(json);
  if (!v.is_object()) return false;
  out = Graph{};
  out.nextId = (int)v["nextId"].get<crude_json::number>();
  crude_json::value& nodes = v["nodes"];
  if (!nodes.is_array()) return false;
  for (auto& nv : nodes.get<crude_json::array>()) {
    Node n;
    n.id = (int)nv["id"].get<crude_json::number>();
    n.type = nv["type"].get<crude_json::string>();
    n.x = (float)nv["x"].get<crude_json::number>();
    n.y = (float)nv["y"].get<crude_json::number>();
    crude_json::value& params = nv["params"];
    if (params.is_object())
      for (auto& kv : params.get<crude_json::object>())
        n.params[kv.first] = (float)kv.second.get<crude_json::number>();
    // String sub-seam: load strParams when present (absent for legacy/float-only files).
    crude_json::value& strParams = nv["strParams"];
    if (strParams.is_object())
      for (auto& kv : strParams.get<crude_json::object>())
        if (kv.second.is_string()) n.strParams[kv.first] = kv.second.get<crude_json::string>();
    out.nodes.push_back(n);
  }
  crude_json::value& conns = v["connections"];
  if (conns.is_array())
    for (auto& cv : conns.get<crude_json::array>())
      out.connections.push_back({(int)cv["id"].get<crude_json::number>(),
                                 (int)cv["fromPin"].get<crude_json::number>(),
                                 (int)cv["toPin"].get<crude_json::number>()});
  return true;
}


bool saveGraphToFile(const std::string& path, const Graph& g) {
  std::ofstream f(path);
  if (!f) return false;
  f << toJson(g);
  return f.good();
}

bool loadGraphFromFile(const std::string& path, Graph& out) {
  std::ifstream f(path);
  if (!f) return false;
  std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return fromJson(json, out);
}


// --- Value evaluation (Task 2) ---

float evalFloat(const Graph& g, int outPin, const EvaluationContext& ctx, int depth) {
  if (depth > 64) return 0.0f;  // cycle guard
  int nodeId = pinNode(outPin);
  const Node* n = g.node(nodeId);
  if (!n) return 0.0f;
  const NodeSpec* s = findSpec(n->type);
  if (!s) return 0.0f;
  // Stateful nodes (AudioReaction) carry no pure evaluate; their output pins read the value
  // main cooked into outCache this frame. outIdx = the output port index (inverse of pinId).
  if (s->evaluate == nullptr && !n->type.empty() && n->type == "AudioReaction") {
    const int oi = (outPin - 1) - nodeId * 100;
    return (oi >= 0 && oi < 3) ? n->outCache[oi] : 0.0f;
  }
  if (!s->evaluate) return 0.0f;
  // Gather Float input values in port order (only Float inputs contribute to in[]).
  // Cap 16 (was 8): RemapVec2 has 11 Float inputs (5 Vec2 + Mode); the resident gather already
  // expanded to 32 (batch26 MultiInput), so the legacy flat path matches that headroom additively.
  float in[16];
  int ni = 0;
  for (size_t i = 0; i < s->ports.size() && ni < 16; ++i) {
    const PortSpec& p = s->ports[i];
    if (!(p.isInput && p.dataType == "Float")) continue;
    int inPin = pinId(nodeId, (int)i);
    if (const Connection* c = g.connectionToInput(inPin)) {
      in[ni++] = evalFloat(g, c->fromPin, ctx, depth + 1);  // wired: recurse upstream
    } else {
      auto it = n->params.find(p.id);
      in[ni++] = (it != n->params.end()) ? it->second : p.def;  // stored constant or spec default
    }
  }
  // Which output port of this node is being pulled (inverse of pinId): lets multi-output
  // nodes (AudioReaction level vs hit) return the right value; single-output nodes ignore it.
  const int outIdx = (outPin - 1) - nodeId * 100;
  return s->evaluate(outIdx, in, ni, ctx);
}

namespace {
// One Float input port's value through the L5 spine. The SSOT for per-port resolution —
// evalParam (first-of-type, single param) and resolveNodeParams (per-node, all params)
// both delegate here so the resolution order can never fork:
//   1. live override (sticky)  2. explicit binding (live-source; Automation -> S4 samples
//   the curve, until then falls through — the graph wire IS the Connection binding and the
//   stored value IS the Constant, both resolved by step 3)  3. wired -> evalFloat upstream,
//   else stored constant, else spec default.
float resolvePortValue(const Graph& g, const Node& n, size_t portIdx, const PortSpec& p,
                       const EvaluationContext& ctx, const SourceRegistry* reg) {
  if (reg) {
    if (const ParamOverride* ov = reg->override_(n.id, p.id); ov && ov->active)
      return ov->value;
    if (const ParamBinding* b = reg->binding(n.id, p.id)) {
      if (b->kind == BindingKind::LiveSource) {
        if (const LiveSource* src = reg->source(b->sourceId); src && src->value)
          return src->value(src->self, ctx);  // audio / hand / MIDI …
      }
    }
  }
  if (const Connection* c = g.connectionToInput(pinId(n.id, (int)portIdx)))
    return evalFloat(g, c->fromPin, ctx, 0);  // driven by connection
  auto it = n.params.find(p.id);
  return (it != n.params.end()) ? it->second : p.def;  // stored constant
}
}  // namespace

// Seam-facing API. Takes `time` (not EvaluationContext&) so callers in translation
// units that already pull tixl_point.h — e.g. main.cpp — need not include Particle.h
// (both define `struct Particle`; they can't coexist). ctx is built here. Value
// nodes currently use only ctx.time; thread more fields here when a node needs them.
float evalParam(const Graph& g, const std::string& type, const std::string& paramId,
                const EvaluationContext& ctx, float fallback, const SourceRegistry* reg) {
  const Node* n = g.firstOfType(type);
  if (!n) return fallback;
  const NodeSpec* s = findSpec(type);
  if (!s) return fallback;
  for (size_t i = 0; i < s->ports.size(); ++i) {
    const PortSpec& p = s->ports[i];
    if (!(p.isInput && p.dataType == "Float" && p.id == paramId)) continue;
    return resolvePortValue(g, *n, i, p, ctx, reg);
  }
  return fallback;
}

std::map<std::string, float> resolveNodeParams(const Graph& g, const Node& n,
                                               const EvaluationContext& ctx,
                                               const SourceRegistry* reg) {
  std::map<std::string, float> out;
  const NodeSpec* s = findSpec(n.type);
  if (!s) return out;
  for (size_t i = 0; i < s->ports.size(); ++i) {
    const PortSpec& p = s->ports[i];
    if (!(p.isInput && p.dataType == "Float")) continue;
    out[p.id] = resolvePortValue(g, n, i, p, ctx, reg);
  }
  return out;
}

void readVecN(const Node& node, const std::string& base, const float* fallback, int n, float* out) {
  static const char* kSuffix[4] = {".x", ".y", ".z", ".w"};
  for (int i = 0; i < n && i < 4; ++i) {
    auto it = node.params.find(base + kSuffix[i]);
    out[i] = (it != node.params.end()) ? it->second : fallback[i];
  }
}

}  // namespace sw
