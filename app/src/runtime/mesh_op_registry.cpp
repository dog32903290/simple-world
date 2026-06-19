// runtime/mesh_op_registry — the mesh-op self-registration sinks + resolved-param accessors.
// Meyers singletons (init-order safe, same as field_node_registry.cpp / image_filter_op_registry).
#include "runtime/mesh_op_registry.h"

namespace sw {

std::vector<NodeSpec>& meshSpecSink() {
  static std::vector<NodeSpec> v;
  return v;
}

std::map<std::string, MeshOpReg>& meshCookFns() {
  static std::map<std::string, MeshOpReg> m;
  return m;
}

const MeshOpReg* findMeshOp(const std::string& type) {
  auto& m = meshCookFns();
  auto it = m.find(type);
  return it != m.end() ? &it->second : nullptr;
}

bool& meshInjectBug() {
  static bool b = false;
  return b;
}

MeshOp::MeshOp(NodeSpec spec, MeshCountFn count, MeshCookFn cook) {
  meshCookFns()[spec.type] = MeshOpReg{count, cook};
  meshSpecSink().push_back(std::move(spec));
}

// Resolved-param accessors (mirror cookParam/cookVecN in point_graph.cpp). Defined here so the leaf
// ops + goldens can read params without depending on the point-graph TU.
float cookMeshParam(const std::map<std::string, float>* params, const char* id, float def) {
  if (!params) return def;
  auto it = params->find(id);
  return it != params->end() ? it->second : def;
}

void cookMeshVecN(const std::map<std::string, float>* params, const char* base, const float* fallback,
                 int n, float* out) {
  static const char* kSuffix[4] = {".x", ".y", ".z", ".w"};
  for (int i = 0; i < n && i < 4; ++i) {
    std::string key = std::string(base) + kSuffix[i];
    out[i] = cookMeshParam(params, key.c_str(), fallback[i]);
  }
}

}  // namespace sw
