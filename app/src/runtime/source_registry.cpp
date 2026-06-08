#include "runtime/source_registry.h"

namespace sw {

void SourceRegistry::registerSource(const LiveSource& src) {
  sources_[src.id] = src;
}

void SourceRegistry::bind(int nodeId, const std::string& portId, const ParamBinding& b) {
  bindings_[{nodeId, portId}] = b;  // replaces any prior — one parameter, one binding
}

void SourceRegistry::setOverride(int nodeId, const std::string& portId, float v) {
  overrides_[{nodeId, portId}] = ParamOverride{true, v};
}

void SourceRegistry::reEnableAll() {
  overrides_.clear();
}

const ParamBinding* SourceRegistry::binding(int nodeId, const std::string& portId) const {
  auto it = bindings_.find({nodeId, portId});
  return it == bindings_.end() ? nullptr : &it->second;
}

const ParamOverride* SourceRegistry::override_(int nodeId, const std::string& portId) const {
  auto it = overrides_.find({nodeId, portId});
  return it == overrides_.end() ? nullptr : &it->second;
}

const LiveSource* SourceRegistry::source(const std::string& id) const {
  auto it = sources_.find(id);
  return it == sources_.end() ? nullptr : &it->second;
}

}  // namespace sw
