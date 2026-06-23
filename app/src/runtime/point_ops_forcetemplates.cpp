// point_ops_forcetemplates.cpp — cook-core helper leaf: the PF field-into-force COMPUTE template strings.
// PEELED from point_ops.cpp (at the 400-line ARCHITECTURE rule-4 cap) when FieldVolumeForce landed. Each
// loader reads its compile-time SW_*_TEMPLATE asset path ONCE into a process-lifetime function-static (the
// file is read at most once per process, not per cook), then assembleFieldMSL fills its /*{...}*/ hooks.
// Empty string if the define is unset/unreadable -> the cook falls back to the baked .metal path
// (byte-identical for every fieldless graph). Behavior is identical to the formerly-inline anonymous-
// namespace loaders; only the location changed.
//
// ZONE: runtime (pure std::ifstream file reads, no Metal objects). The SW_*_TEMPLATE defines are
// target-wide (CMake target_compile_definitions), so they are visible in this TU exactly as in point_ops.cpp.
#include "runtime/point_ops_forcetemplates.h"

#include <fstream>
#include <sstream>

namespace sw {
namespace {
// Read a compile-time-asset path into a string; empty on unreadable. Shared body of the four loaders.
std::string readTemplateFile(const char* path) {
  std::ifstream f(path);
  if (!f) return std::string();
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
}  // namespace

const std::string& vffTemplate() {
  static const std::string tmpl =
#ifdef SW_VFF_TEMPLATE
      readTemplateFile(SW_VFF_TEMPLATE);
#else
      std::string();
#endif
  return tmpl;
}

const std::string& fieldDistanceTemplate() {
  static const std::string tmpl =
#ifdef SW_FIELD_DISTANCE_TEMPLATE
      readTemplateFile(SW_FIELD_DISTANCE_TEMPLATE);
#else
      std::string();
#endif
  return tmpl;
}

const std::string& randomJumpTemplate() {
  static const std::string tmpl =
#ifdef SW_RANDOM_JUMP_TEMPLATE
      readTemplateFile(SW_RANDOM_JUMP_TEMPLATE);
#else
      std::string();
#endif
  return tmpl;
}

const std::string& fieldVolumeTemplate() {
  static const std::string tmpl =
#ifdef SW_FIELD_VOLUME_TEMPLATE
      readTemplateFile(SW_FIELD_VOLUME_TEMPLATE);
#else
      std::string();
#endif
  return tmpl;
}

}  // namespace sw
