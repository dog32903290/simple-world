// runtime/point_ops_pbrshading — the Command-rail PBR shading ops: SetMaterial / SetPointLight / SetFog /
// UseMaterial / DefineMaterials (the context-stack WRITERS) + DrawMeshPbr (the lit-mesh READER/draw). The
// scope push/restore lives in the cook drivers (mirror of the S3a var scope / C1 camera scope); these ops
// only forward the cooked SubGraph (writers) or stamp the lit item from the surfaced cc.pbr* (DrawMeshPbr).
//
// Own tiny header (NOT the at-cap point_ops.h) so registration + the golden decl are reachable without the
// god-header. Bodies in point_ops_pbrshading.cpp (globbed as point_ops*.cpp → no CMake edit).
#pragma once

namespace sw {

// Register the 6 Command-rail PBR ops (SetMaterial/SetPointLight/SetFog/UseMaterial/DefineMaterials/DrawMeshPbr).
void registerPbrShadingOps();

// --selftest-pbr-shading (the KEYSTONE golden; render + pixel readback, both flat + resident legs). A single
// triangle facing +z, one point light, a SetMaterial → Cook-Torrance closed-form hand-computed → the center
// pixel must match. injectBug (skip the light push) → no direct lighting → the pixel goes dark → RED.
int runPbrShadingSelfTest(bool injectBug);

}  // namespace sw
