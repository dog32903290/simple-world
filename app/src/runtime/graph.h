// Native node-graph data model — the source of truth for the canvas (the
// editorGraph + cook params). Per tooll3-interaction-compatibility: we borrow
// Tooll3's command vocabulary + save/load behavior, but the schema is our own
// clean native model, NOT TiXL's Symbol/Instance system.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace sw {

// --- Node type definitions (NodeSpec registry, faithful to TiXL ports/params) ---
struct ParamSpec {
  std::string id, label;
  float def, minV, maxV;
};
struct PortSpec {
  std::string id, name, dataType;  // dataType: "Points" | "ParticleForce"
  bool isInput;
};
struct NodeSpec {
  std::string type, title;
  std::vector<PortSpec> ports;
  std::vector<ParamSpec> params;
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
};

// Stable pin id from a node id + the port index in its spec.
inline int pinId(int nodeId, int portIndex) { return nodeId * 100 + portIndex + 1; }

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

}  // namespace sw
