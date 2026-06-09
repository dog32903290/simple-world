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
namespace {

// ----- Value-node evaluate functions (pure value, no GPU). -----
// in[] is ordered by the Float input ports in the spec; n is the count.

float evalTime(int, const float*, int, const EvaluationContext& ctx) { return ctx.time; }
// AudioReaction is stateful (TiXL parity) and has no pure evaluate — it's cooked in main from
// the live spectrum into Node::outCache, which evalFloat returns directly (see below).
float evalConst(int, const float* in, int n, const EvaluationContext&) { return n > 0 ? in[0] : 0.0f; }
float evalMultiply(int, const float* in, int n, const EvaluationContext&) {
  return n >= 2 ? in[0] * in[1] : 0.0f;
}
float evalSine(int, const float* in, int n, const EvaluationContext&) {
  return n > 0 ? std::sin(in[0]) : 0.0f;
}
float evalRemap(int, const float* in, int n, const EvaluationContext&) {
  // in: [x, outMin, outMax]. x in -1..1 → outMin..outMax.
  if (n < 3) return 0.0f;
  float t = (in[0] + 1.0f) * 0.5f;         // -1..1 → 0..1
  return in[1] + (in[2] - in[1]) * t;      // → outMin..outMax
}

// NodeSpec registry — params unified into Float input ports (schema spine, Task 1).
// id kept identical to old ParamSpec.id so Node::params map + save/load stay compatible.
const std::vector<NodeSpec>& registry() {
  static const std::vector<NodeSpec> specs = {
      {"RadialPoints",
       "RadialPoints",
       {{"points", "points", "Points", false},
        {"Count", "Count", "Float", true, 2048.0f, 16.0f, 8192.0f},
        {"Radius", "Radius", "Float", true, 2.0f, 0.1f, 10.0f}},
       nullptr},
      {"TurbulenceForce",
       "TurbulenceForce",
       {{"force", "force", "ParticleForce", false},
        {"Amount", "Amount", "Float", true, 15.0f, 0.0f, 100.0f},
        {"Frequency", "Frequency", "Float", true, 1.2f, 0.0f, 5.0f},
        {"Phase", "Phase", "Float", true, 0.0f, 0.0f, 10.0f}},
       nullptr},
      {"ParticleSystem",
       "ParticleSystem",
       {{"emit", "emit", "Points", true},
        {"forces", "forces", "ParticleForce", true},
        {"result", "result", "Points", false},
        {"Speed", "Speed", "Float", true, 1.0f, 0.0f, 3.0f},
        {"Drag", "Drag", "Float", true, 0.02f, 0.0f, 0.2f},
        {"OrientTowardsVelocity", "OrientTowardsVelocity", "Float", true, 0.15f, 0.0f, 1.0f}},
       nullptr},
      {"DrawPoints", "DrawPoints", {{"points", "points", "Points", true}}, nullptr},
      // --- Value nodes (Task 2) ---
      {"Time", "Time", {{"out", "out", "Float", false}}, evalTime},
      // TiXL AudioReaction (full parity): 3 outputs + 10 params. STATEFUL — cooked in main
      // from the live spectrum (runtime/audio_reaction) because it needs the whole spectrum
      // (too big for ctx) + per-node memory; so it has no pure evaluate() and evalFloat reads
      // its outputs from Node::outCache. Params are pinless (Inspector knobs, no canvas pins).
      {"AudioReaction", "AudioReaction",
       {{"Level", "Level", "Float", false},
        {"WasHit", "WasHit", "Float", false},
        {"HitCount", "HitCount", "Float", false},
        {"Amplitude", "Amplitude", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
        {"InputBand", "InputBand", "Float", true, 2.0f, 0.0f, 4.0f, Widget::Enum,
         {"RawFft", "NormalizedFft", "FrequencyBands", "Peaks", "Attacks"}, true},
        {"WindowCenter", "WindowCenter", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, true},
        {"WindowWidth", "WindowWidth", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Slider, {}, true},
        {"WindowEdge", "WindowEdge", "Float", true, 1.0f, 0.0001f, 1.0f, Widget::Slider, {}, true},
        {"Threshold", "Threshold", "Float", true, 0.5f, 0.0f, 2.0f, Widget::Slider, {}, true},
        {"MinTimeBetweenHits", "MinTimeBetweenHits", "Float", true, 0.1f, 0.0f, 2.0f, Widget::Slider, {}, true},
        {"Output", "Output", "Float", true, 3.0f, 0.0f, 4.0f, Widget::Enum,
         {"Pulse", "TimeSinceHit", "Count", "Level", "AccumulatedLevel"}, true},
        {"Bias", "Bias", "Float", true, 1.0f, 0.0f, 4.0f, Widget::Slider, {}, true},
        {"Reset", "Reset", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr},
      {"Const", "Const",
       {{"value", "value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalConst},
      {"Multiply", "Multiply",
       {{"a", "a", "Float", true, 1.0f, -10.0f, 10.0f},
        {"b", "b", "Float", true, 1.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalMultiply},
      {"Sine", "Sine",
       {{"x", "x", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalSine},
      {"Remap", "Remap",
       {{"x", "x", "Float", true, 0.0f, -1.0f, 1.0f},
        {"outMin", "outMin", "Float", true, 0.0f, -10.0f, 10.0f},
        {"outMax", "outMax", "Float", true, 1.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalRemap},
  };
  return specs;
}

}  // namespace

const NodeSpec* findSpec(const std::string& type) {
  for (const auto& s : registry())
    if (s.type == type) return &s;
  return nullptr;
}

std::vector<std::string> specTypes() {
  std::vector<std::string> out;
  for (const auto& s : registry()) out.push_back(s.type);
  return out;
}

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
    for (const auto& p : s->ports)
      if (p.isInput && p.dataType == "Float") n.params[p.id] = p.def;  // seed from spec defaults
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
  float in[8];
  int ni = 0;
  for (size_t i = 0; i < s->ports.size() && ni < 8; ++i) {
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

  // L5 resolution: override → binding(live-source/automation) → [graph: connection
  // else constant]. reg == nullptr (value-spine callers) skips straight to the graph
  // behavior, so wiring and constants resolve exactly as before — zero regression.
  if (reg) {
    if (const ParamOverride* ov = reg->override_(n->id, paramId); ov && ov->active)
      return ov->value;                                            // 1. live override (sticky)
    if (const ParamBinding* b = reg->binding(n->id, paramId)) {    // 2. explicit binding
      if (b->kind == BindingKind::LiveSource) {
        if (const LiveSource* src = reg->source(b->sourceId); src && src->value)
          return src->value(src->self, ctx);                       //    audio / hand / MIDI …
      }
      // BindingKind::Automation -> S4 (sample the scoreGraph curve @ playhead); until
      // then it falls through. Connection / Constant are intentionally NOT consumed
      // here: the graph wire IS the Connection binding and the stored value IS the
      // Constant — both resolved by the value-spine path below.
    }
  }

  // 3. value-spine behavior = binding=Connection (wired) else Constant (stored / def).
  for (size_t i = 0; i < s->ports.size(); ++i) {
    const PortSpec& p = s->ports[i];
    if (!(p.isInput && p.dataType == "Float" && p.id == paramId)) continue;
    int inPin = pinId(n->id, (int)i);
    if (const Connection* c = g.connectionToInput(inPin))
      return evalFloat(g, c->fromPin, ctx, 0);  // driven by connection
    auto it = n->params.find(paramId);
    return (it != n->params.end()) ? it->second : p.def;  // stored constant
  }
  return fallback;
}

}  // namespace sw
