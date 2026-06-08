// Native node-graph data model — the source of truth for the canvas (the
// editorGraph + cook params). Per tooll3-interaction-compatibility: we borrow
// Tooll3's command vocabulary + save/load behavior, but the schema is our own
// clean native model, NOT TiXL's Symbol/Instance system.
#pragma once
#include <map>
#include <string>
#include <vector>

// EvaluationContext is defined in runtime/Particle.h (global namespace).
// Forward-declare it here so graph.h does not pull in Particle.h (which would
// conflict with tixl_point.h when both headers end up in the same TU).
struct EvaluationContext;

namespace sw {

// --- Node type definitions (NodeSpec registry, faithful to TiXL ports/params) ---
struct PortSpec {
  std::string id, name, dataType;  // dataType: "Points" | "ParticleForce" | "Float"
  bool isInput;
  float def = 0.0f, minV = 0.0f, maxV = 1.0f;  // Float input only
};
struct NodeSpec {
  std::string type, title;
  std::vector<PortSpec> ports;
  // params retired: original params are now dataType=="Float" && isInput ports.
  // Constants live in Node::params[port.id]. evaluate is used by Task 2+ value nodes.
  float (*evaluate)(const float* in, int n, const EvaluationContext& ctx) = nullptr;
};
const NodeSpec* findSpec(const std::string& type);
std::vector<std::string> specTypes();  // all registered node types (for the Add menu)

// --- Graph instances (editorGraph) ---
struct Node {
  int id = 0;
  std::string type;
  float x = 0.0f, y = 0.0f;
  std::map<std::string, float> params;  // param id -> value
};
struct Connection {
  int id = 0;
  int fromPin = 0, toPin = 0;  // absolute pin ids (see pinId())
};
struct Graph {
  std::vector<Node> nodes;
  std::vector<Connection> connections;
  int nextId = 1;

  Node* node(int id);
  const Node* node(int id) const;
  const Node* firstOfType(const std::string& type) const;
  float param(const std::string& type, const std::string& paramId, float fallback) const;

  // The connection feeding this input pin, or nullptr. Inputs are single-
  // cardinality, so there is at most one. Single source of truth for the
  // "is this input already wired?" question (reconnect / cardinality).
  const Connection* connectionToInput(int inputPin) const;
};

// Stable pin id from a node id + the port index in its spec.
inline int pinId(int nodeId, int portIndex) { return nodeId * 100 + portIndex + 1; }
// Inverse of pinId: recover the owning node id from a pin id. Single source of
// truth for the pin->node mapping (callers must not re-derive (pin-1)/100).
inline int pinNode(int pin) { return (pin - 1) / 100; }

// Default particle graph: RadialPoints -> ParticleSystem <- TurbulenceForce, ParticleSystem -> DrawPoints.
Graph defaultParticleGraph();

// Save/load (crude_json). fromJson returns false on malformed input.
std::string toJson(const Graph& g);
bool fromJson(const std::string& json, Graph& out);

// Disk I/O for project files (.swproj). saveGraphToFile writes toJson(g) to path;
// loadGraphFromFile reads a file and fromJson()s it. Both return false on failure
// (unwritable path / missing file / malformed json) — callers must not mutate
// their live graph until loadGraphFromFile returns true.
bool saveGraphToFile(const std::string& path, const Graph& g);
bool loadGraphFromFile(const std::string& path, Graph& out);

// L-save proof: default graph -> file -> graph, assert identical; AND a malformed
// file must load to false. injectBug perturbs the reloaded graph so the roundtrip
// comparison must FAIL.
int runSaveLoadSelfTest(bool injectBug);

// L6 proof: default graph -> json -> graph, assert identical. injectBug perturbs
// a param after the roundtrip so the comparison must FAIL.
int runGraphRoundtripSelfTest(bool injectBug);

// --- Value evaluation (Task 2+) ---
// Pull-evaluate the float value produced by outPin, recursing through connections.
// depth > 64 breaks cycles (returns 0). EvaluationContext supplies time/frame.
float evalFloat(const Graph& g, int outPin, const EvaluationContext& ctx, int depth = 0);
// Resolve a named Float input port on the first node of the given type: if the
// input is wired, evalFloat the upstream; otherwise return the stored constant
// (Node::params[paramId]) or fallback if absent.
// Takes `time` (not EvaluationContext&) so callers that already include
// tixl_point.h (main.cpp) need not pull Particle.h — both define `struct Particle`.
float evalParam(const Graph& g, const std::string& type, const std::string& paramId,
                float time, float fallback);
// Headless RED->GREEN proof for the value-cook engine. injectBug flips the result.
int runValueCookSelfTest(bool injectBug);

}  // namespace sw
