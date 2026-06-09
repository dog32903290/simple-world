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

float evalTime(const float*, int, const EvaluationContext& ctx) { return ctx.time; }
// AudioReaction: the live-audio reaction value this frame. Symmetric with evalTime —
// both are "animated" sources (recomputed every frame) that just read the per-frame
// ctx. The DSP (envelope/attack) lives in the capture/analyzer; this node only surfaces
// the current value into the graph (TiXL Operators/Lib/io/audio/AudioReaction.cs).
float evalAudioReaction(const float*, int, const EvaluationContext& ctx) { return ctx.audioLevel; }
float evalConst(const float* in, int n, const EvaluationContext&) { return n > 0 ? in[0] : 0.0f; }
float evalMultiply(const float* in, int n, const EvaluationContext&) {
  return n >= 2 ? in[0] * in[1] : 0.0f;
}
float evalSine(const float* in, int n, const EvaluationContext&) {
  return n > 0 ? std::sin(in[0]) : 0.0f;
}
float evalRemap(const float* in, int n, const EvaluationContext&) {
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
      // Live-audio reaction value (TiXL AudioReaction, v1): one Float output, recomputed
      // every frame from ctx.audioLevel. Wire it into any param; scale with Multiply/Remap.
      {"AudioReaction", "AudioReaction", {{"level", "level", "Float", false}}, evalAudioReaction},
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

// --- Value evaluation (Task 2) ---

float evalFloat(const Graph& g, int outPin, const EvaluationContext& ctx, int depth) {
  if (depth > 64) return 0.0f;  // cycle guard
  int nodeId = pinNode(outPin);
  const Node* n = g.node(nodeId);
  if (!n) return 0.0f;
  const NodeSpec* s = findSpec(n->type);
  if (!s || !s->evaluate) return 0.0f;
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
  return s->evaluate(in, ni, ctx);
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

int runValueCookSelfTest(bool injectBug) {
  Graph g;
  // Helper: add a node seeded from spec defaults.
  auto add = [&](const char* type) {
    Node n;
    n.id = g.nextId++;
    n.type = type;
    if (const NodeSpec* s = findSpec(type))
      for (auto& p : s->ports)
        if (p.isInput && p.dataType == "Float") n.params[p.id] = p.def;
    g.nodes.push_back(n);
    return g.nodes.back().id;
  };
  // Helper: port index by id within a spec.
  auto portIdx = [&](const char* type, const char* portId) {
    const NodeSpec* s = findSpec(type);
    for (size_t i = 0; i < s->ports.size(); ++i)
      if (s->ports[i].id == portId) return (int)i;
    return -1;
  };

  // Test 1: Const(3) * Const(4) == 12
  int c3 = add("Const");
  g.node(c3)->params["value"] = 3.0f;
  int c4 = add("Const");
  g.node(c4)->params["value"] = 4.0f;
  int mul = add("Multiply");
  int c3out = pinId(c3, portIdx("Const", "out"));
  int c4out = pinId(c4, portIdx("Const", "out"));
  int mulA = pinId(mul, portIdx("Multiply", "a"));
  int mulB = pinId(mul, portIdx("Multiply", "b"));
  g.connections.push_back({g.nextId++, c3out, mulA});
  g.connections.push_back({g.nextId++, c4out, mulB});

  EvaluationContext ctx{};
  ctx.time = 2.0f;
  float mulOut = evalFloat(g, pinId(mul, portIdx("Multiply", "out")), ctx, 0);
  bool ok = (mulOut == 12.0f);

  // Test 2: Time -> Sine ; result == sin(ctx.time)
  int t = add("Time");
  int sn = add("Sine");
  g.connections.push_back(
      {g.nextId++, pinId(t, portIdx("Time", "out")), pinId(sn, portIdx("Sine", "x"))});
  float sineOut = evalFloat(g, pinId(sn, portIdx("Sine", "out")), ctx, 0);
  ok = ok && (std::fabs(sineOut - std::sin(2.0f)) < 1e-5f);

  // Test 3: AudioReaction -> reads ctx.audioLevel (the "animated" live-audio source,
  // symmetric with Time reading ctx.time).
  ctx.audioLevel = 0.42f;
  int ar = add("AudioReaction");
  float audioOut = evalFloat(g, pinId(ar, portIdx("AudioReaction", "level")), ctx, 0);
  ok = ok && (std::fabs(audioOut - 0.42f) < 1e-5f);

  if (injectBug) ok = !ok;
  printf("[selftest-valuecook] mul=%.3f sine=%.3f audio=%.3f -> %s\n", mulOut, sineOut, audioOut,
         ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

int runResolveSelfTest(bool injectBug) {
  // Build a graph with one ParticleSystem and resolve its "Speed" Float input — the
  // same param S2 drives from audio, so this doubles as the binding-contract doc.
  Graph g;
  Node ps;
  ps.id = g.nextId++;
  ps.type = "ParticleSystem";
  ps.params["Speed"] = 5.0f;  // stored constant (distinct from the 1.0 spec default)
  ps.params["Drag"]  = 0.5f;  // a second un-wired param for the malformed-binding cases
  g.nodes.push_back(ps);
  const int psId = g.nodes.back().id;

  // Port index by id (the test's only graph-shape helper).
  auto idx = [&](const char* type, const char* port) {
    const NodeSpec* sp = findSpec(type);
    for (size_t i = 0; sp && i < sp->ports.size(); ++i)
      if (sp->ports[i].id == port) return (int)i;
    return -1;
  };

  // A live source that reads through `self` (mirrors S2: self=AudioInput*, value=
  // latest sample) — proves the value is read live each call, not snapshotted.
  float liveVal = 7.0f;
  LiveSource live;
  live.id = "test.live";
  live.value = [](void* self, const EvaluationContext&) -> float {
    return *static_cast<float*>(self);
  };
  live.self = &liveVal;
  SourceRegistry reg;
  reg.registerSource(live);

  EvaluationContext ctx{};  // resolution test is time/audio-agnostic; a zero ctx suffices

  // Resolve "Speed" against an arbitrary registry pointer (g captured by ref, so it
  // sees any wire added later).
  auto speed = [&](const SourceRegistry* r) {
    return evalParam(g, "ParticleSystem", "Speed", ctx, /*fallback=*/-1.0f, r);
  };

  bool ok = true;

  // 1. constant: empty reg AND reg==nullptr both return the stored constant 5.0.
  ok = ok && (speed(&reg) == 5.0f);
  ok = ok && (speed(nullptr) == 5.0f);

  // 2. live-source: bind → returns the source value (7.0), beating the stored constant…
  reg.bind(psId, "Speed", {BindingKind::LiveSource, "test.live"});
  ok = ok && (speed(&reg) == 7.0f);
  //    …and it is read live: change the backing value, resolution follows.
  liveVal = 4.0f;
  ok = ok && (speed(&reg) == 4.0f);
  liveVal = 7.0f;

  // 3. override beats the binding (sticky live touch).
  reg.setOverride(psId, "Speed", 9.0f);
  ok = ok && (speed(&reg) == 9.0f);

  // 4. global re-enable clears the override → falls back to the binding (7.0).
  reg.reEnableAll();
  ok = ok && (speed(&reg) == 7.0f);

  // 5. one parameter, one binding: re-binding replaces (now point at a 3.0 source).
  float liveVal2 = 3.0f;
  LiveSource live2;
  live2.id = "test.live2";
  live2.value = [](void* self, const EvaluationContext&) -> float {
    return *static_cast<float*>(self);
  };
  live2.self = &liveVal2;
  reg.registerSource(live2);
  reg.bind(psId, "Speed", {BindingKind::LiveSource, "test.live2"});
  ok = ok && (speed(&reg) == 3.0f);

  // 6. connection regression + mutual exclusivity: wire Const(8.0) → ParticleSystem.Speed.
  Node c;
  c.id = g.nextId++;
  c.type = "Const";
  c.params["value"] = 8.0f;
  g.nodes.push_back(c);
  const int cId = g.nodes.back().id;
  g.connections.push_back({g.nextId++, pinId(cId, idx("Const", "out")),
                                       pinId(psId, idx("ParticleSystem", "Speed"))});
  SourceRegistry fresh;  // no binding/override: the graph connection drives.
  ok = ok && (speed(&fresh) == 8.0f);    // resolves through the wire (binding=Connection)
  ok = ok && (speed(nullptr) == 8.0f);   // value-spine path unchanged by the registry
  ok = ok && (speed(&reg) == 3.0f);      // explicit live binding still beats the wire

  // --- Adversarial-review coverage (S1 Step 5). All on the un-wired "Drag" param so
  //     the graceful fallback target is its stored constant (0.5), not a wire. ---
  auto drag = [&](const SourceRegistry* r) {
    return evalParam(g, "ParticleSystem", "Drag", ctx, /*fallback=*/-1.0f, r);
  };

  // 7. override on a param with NO binding → override value; re-enable → constant.
  reg.setOverride(psId, "Drag", 9.0f);
  ok = ok && (drag(&reg) == 9.0f);
  reg.reEnableAll();
  ok = ok && (drag(&reg) == 0.5f);   // no binding to fall back to → stored constant

  // 8. dangling LiveSource binding (sourceId never registered) → graceful constant,
  //    not a crash: a half-configured binding must not break the render loop.
  reg.bind(psId, "Drag", {BindingKind::LiveSource, "no.such.source"});
  ok = ok && (drag(&reg) == 0.5f);

  // 9. LiveSource registered but with a null value fn → graceful constant (the other
  //    half of the `src && src->value` guard).
  LiveSource nullSrc;
  nullSrc.id = "test.null";  // value/self stay nullptr (defaults)
  reg.registerSource(nullSrc);
  reg.bind(psId, "Drag", {BindingKind::LiveSource, "test.null"});
  ok = ok && (drag(&reg) == 0.5f);

  // 10. Automation binding (S4 placeholder) → falls through to constant until S4 wires
  //     it to a scoreGraph curve. Locks the interim seam contract.
  reg.bind(psId, "Drag", {BindingKind::Automation, ""});
  ok = ok && (drag(&reg) == 0.5f);

  // 11. unknown paramId (typo'd / absent port) → fallback.
  ok = ok && (evalParam(g, "ParticleSystem", "NoSuchParam", ctx, -1.0f, &reg) == -1.0f);

  if (injectBug) ok = !ok;
  printf("[selftest-resolve] const/live/override/re-enable/replace/wire + "
         "ovr-nobind/dangling/nullfn/automation/badparam -> %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
