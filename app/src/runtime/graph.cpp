#include "runtime/graph.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include "crude_json.h"

namespace sw {
namespace {

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

namespace {
bool graphsEqual(const Graph& a, const Graph& b) {
  if (a.nextId != b.nextId) return false;
  if (a.nodes.size() != b.nodes.size()) return false;
  if (a.connections.size() != b.connections.size()) return false;
  for (size_t i = 0; i < a.nodes.size(); ++i) {
    const Node& x = a.nodes[i];
    const Node& y = b.nodes[i];
    if (x.id != y.id || x.type != y.type) return false;
    if (std::fabs(x.x - y.x) > 1e-4f || std::fabs(x.y - y.y) > 1e-4f) return false;
    if (x.params.size() != y.params.size()) return false;
    for (const auto& kv : x.params) {
      auto it = y.params.find(kv.first);
      if (it == y.params.end() || std::fabs(it->second - kv.second) > 1e-4f) return false;
    }
  }
  for (size_t i = 0; i < a.connections.size(); ++i) {
    const Connection& x = a.connections[i];
    const Connection& y = b.connections[i];
    if (x.id != y.id || x.fromPin != y.fromPin || x.toPin != y.toPin) return false;
  }
  return true;
}
}  // namespace

int runGraphRoundtripSelfTest(bool injectBug) {
  Graph g = defaultParticleGraph();
  std::string json = toJson(g);
  Graph g2;
  if (!fromJson(json, g2)) {
    printf("[selftest-graph] FAIL: parse failed\n");
    return 1;
  }
  if (injectBug && !g2.nodes.empty()) g2.nodes[0].params["Radius"] += 1.0f;  // perturb
  bool pass = graphsEqual(g, g2);
  printf("[selftest-graph] nodes=%zu conns=%zu jsonLen=%zu roundtrip%s -> %s\n", g.nodes.size(),
         g.connections.size(), json.size(), injectBug ? "(perturbed)" : "", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
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

int runSaveLoadSelfTest(bool injectBug) {
  const char* tmp = std::getenv("TMPDIR");
  const std::string path = std::string(tmp ? tmp : "/tmp") + "/sw_saveload_selftest.swproj";
  Graph g = defaultParticleGraph();
  if (!saveGraphToFile(path, g)) {
    printf("[selftest-save] write FAILED -> %s\n", path.c_str());
    return 1;
  }
  Graph reloaded;
  if (!loadGraphFromFile(path, reloaded)) {
    printf("[selftest-save] reload FAILED <- %s\n", path.c_str());
    return 1;
  }
  if (injectBug) reloaded.nextId += 1;  // perturb so the roundtrip must mismatch
  bool roundtrip = (toJson(g) == toJson(reloaded));

  // A malformed file MUST load to false (selection-of-wrong-file safety).
  { std::ofstream bad(path); bad << "{ this is not valid json "; }
  Graph dummy;
  bool rejectedBad = !loadGraphFromFile(path, dummy);

  bool pass = roundtrip && rejectedBad;
  printf("[selftest-save] roundtrip=%s rejectBad=%s -> %s\n",
         roundtrip ? "ok" : "MISMATCH", rejectedBad ? "ok" : "ACCEPTED-BAD",
         pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

}  // namespace sw
