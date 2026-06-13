// graph_selftest.cpp — isolation proofs for the node graph, split out of graph.cpp so the
// production file stays under the 鐵律-4 size warning. Runtime zone; uses only graph.h's
// public API + source_registry. Reached via main's --selftest-{graph,save,valuecook,
// audionode,resolve} (the declarations live in graph.h).
#include "runtime/graph.h"
#include "runtime/eval_context.h"      // full EvaluationContext (graph.h only forward-declares)
#include "runtime/source_registry.h"   // SourceRegistry/LiveSource/BindingKind (runResolveSelfTest)

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace sw {
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

  // Test 3: AudioReaction is stateful (cooked in main into Node::outCache) — its 3 output
  // pins (Level/WasHit/HitCount) read that cache. Prove evalFloat returns it by output port.
  int ar = add("AudioReaction");
  if (Node* arn = g.node(ar)) { arn->outCache[0] = 0.42f; arn->outCache[1] = 1.0f; arn->outCache[2] = 7.0f; }
  float levelOut = evalFloat(g, pinId(ar, portIdx("AudioReaction", "Level")), ctx, 0);
  float wasHitOut = evalFloat(g, pinId(ar, portIdx("AudioReaction", "WasHit")), ctx, 0);
  float hitCountOut = evalFloat(g, pinId(ar, portIdx("AudioReaction", "HitCount")), ctx, 0);
  ok = ok && (std::fabs(levelOut - 0.42f) < 1e-5f) && (wasHitOut == 1.0f) && (hitCountOut == 7.0f);

  if (injectBug) ok = !ok;
  printf("[selftest-valuecook] mul=%.3f sine=%.3f level=%.3f wasHit=%.0f count=%.0f -> %s\n",
         mulOut, sineOut, levelOut, wasHitOut, hitCountOut, ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

int runAudioNodeSelfTest(bool injectBug) {
  // AudioReaction.Level -> ParticleSystem.Speed, driven by the node's cooked outCache.
  // Proves the wired path resolves (does NOT hang) and documents the gotcha: a raw 0..1
  // Level wired straight to Speed makes Speed 0 when silent -> particles freeze ("stuck").
  Graph g;
  Node ps; ps.id = g.nextId++; ps.type = "ParticleSystem"; g.nodes.push_back(ps);
  Node ar; ar.id = g.nextId++; ar.type = "AudioReaction"; g.nodes.push_back(ar);
  const int psId = g.nodes[0].id, arId = g.nodes[1].id;
  auto idx = [&](const char* t, const char* p) {
    const NodeSpec* s = findSpec(t);
    for (size_t i = 0; s && i < s->ports.size(); ++i)
      if (s->ports[i].id == p) return (int)i;
    return -1;
  };
  g.connections.push_back({g.nextId++, pinId(arId, idx("AudioReaction", "Level")),
                                       pinId(psId, idx("ParticleSystem", "Speed"))});
  EvaluationContext ctx{};
  bool ok = true;
  auto setLevel = [&](float v) { if (Node* n = g.node(arId)) n->outCache[0] = v; };  // main's cook, simulated
  setLevel(0.0f); ok = ok && (evalParam(g, "ParticleSystem", "Speed", ctx, 1.0f) == 0.0f);
  setLevel(0.5f); ok = ok && (evalParam(g, "ParticleSystem", "Speed", ctx, 1.0f) == 0.5f);
  setLevel(1.0f); ok = ok && (evalParam(g, "ParticleSystem", "Speed", ctx, 1.0f) == 1.0f);
  if (injectBug) ok = !ok;
  printf("[selftest-audionode] AudioReaction->Speed resolves (silent=0 -> freeze) /0.5/1 -> %s\n",
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

// [overnight-math] NOTE: runMathOpsSelfTest is defined in math_ops_selftest.cpp.
// New 7-op teeth (Sqrt/Pow/Modulo/Ceil/SmoothStep/Log/Cos) are appended there.

}  // namespace sw
