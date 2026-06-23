#pragma once
// point_ops_forcetemplates.h — cook-core internal API for the PF field-into-force COMPUTE template strings.
// Implemented in point_ops_forcetemplates.cpp; consumed only by point_ops.cpp (cookParticleSim's
// runFieldForce). PEELED from point_ops.cpp when FieldVolumeForce pushed it past the 400-line cap
// (ARCHITECTURE.md rule 4): the four function-static template loaders (VFF/FieldDistance/RandomJump/
// FieldVolume) are pure compile-time-asset file reads with no SimState dependency, so they move here.
// Each returns a process-lifetime cached string (read at most once); empty if the SW_*_TEMPLATE define is
// unset/unreadable (-> the cook falls back to the baked path, byte-identical for every fieldless graph).
#include <string>

namespace sw {
const std::string& vffTemplate();             // SW_VFF_TEMPLATE (vector_field_force_template.metal)
const std::string& fieldDistanceTemplate();   // SW_FIELD_DISTANCE_TEMPLATE
const std::string& randomJumpTemplate();       // SW_RANDOM_JUMP_TEMPLATE
const std::string& fieldVolumeTemplate();      // SW_FIELD_VOLUME_TEMPLATE
}  // namespace sw
