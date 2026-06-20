// runtime/pointlist_op_registry — the pointlist-op self-registration sinks + resolved-param accessors
// + the TiXL `new Point()` default seed. Meyers singletons (init-order safe, same as
// floatlist_op_registry.cpp / mesh_op_registry.cpp / field_node_registry.cpp).
#include "runtime/pointlist_op_registry.h"

#include "runtime/tixl_point.h"  // SwPoint full def (swPointDefault constructs one by value)

namespace sw {

SwPoint swPointDefault() {
  // = T3.Core.DataTypes.Point's default constructor (Point.cs:27-35), mapped onto SwPoint's renamed
  // fields. Raw zeros would give F1/Color/Scale/Rotation all 0 — NOT the TiXL default (a zero
  // quaternion is degenerate, a zero Scale collapses the point). Mirror `new Point()` verbatim.
  SwPoint p{};
  p.Position = {0.0f, 0.0f, 0.0f};
  p.FX1 = 1.0f;                                  // Point.F1 = 1
  p.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};         // Point.Orientation = Quaternion.Identity
  p.Color = {1.0f, 1.0f, 1.0f, 1.0f};            // Point.Color = Vector4.One
  p.Scale = {1.0f, 1.0f, 1.0f};                  // Point.Scale = Vector3.One
  p.FX2 = 1.0f;                                  // Point.F2 = 1
  return p;
}

std::vector<NodeSpec>& pointListSpecSink() {
  static std::vector<NodeSpec> v;
  return v;
}

std::map<std::string, PointListCookFn>& pointListCookFns() {
  static std::map<std::string, PointListCookFn> m;
  return m;
}

const PointListCookFn* findPointListOp(const std::string& type) {
  auto& m = pointListCookFns();
  auto it = m.find(type);
  return it != m.end() ? &it->second : nullptr;
}

bool& pointListInjectBug() {
  static bool b = false;
  return b;
}

PointListOp::PointListOp(NodeSpec spec, PointListCookFn cook) {
  pointListCookFns()[spec.type] = cook;
  pointListSpecSink().push_back(std::move(spec));
}

// Resolved-param accessors (mirror floatListParam in floatlist_op_registry.cpp). Defined here so the
// leaf ops + goldens can read params without depending on the point-graph TU.
float pointListParam(const std::map<std::string, float>* params, const char* id, float def) {
  if (!params) return def;
  auto it = params->find(id);
  return it != params->end() ? it->second : def;
}

void pointListVec3(const std::map<std::string, float>* params, const char* base, const float* fallback,
                   float* out) {
  static const char* kSuffix[3] = {".x", ".y", ".z"};
  for (int i = 0; i < 3; ++i) {
    std::string key = std::string(base) + kSuffix[i];
    out[i] = pointListParam(params, key.c_str(), fallback[i]);
  }
}

}  // namespace sw
