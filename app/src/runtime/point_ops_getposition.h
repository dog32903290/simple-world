// runtime/point_ops_getposition — GetPosition (TiXL flow/context): transform a PositionOffset through the
// eval-context transform scope (ObjectToWorld / WorldToCamera / CameraToClipSpace by Space enum), Command-
// rail compute + value-rail Position latch. Tiny own-header (the GetScreenPos precedent — point_ops.h is at
// its ratchet cap). runtime leaf: no upward deps.
#pragma once

namespace sw {

// Register the "GetPosition" cmd op; called by registerDrawPointOps.
void registerGetPositionOp();

// Test-only op flag (the transform-scope tooth): when true the REAL cook forces the Space matrix to
// IDENTITY (as if the transform scope — WorldToCamera / CameraToClipSpace — never reached the op), while
// the rest of the cook still runs. The CameraSpace/ClipSpace golden legs then read the un-transformed
// offset → diverge from the closed-form expected → RED. OFF in production. CPU op flag (constitution rule).
bool& getPositionForceIdentityForTest();

// --selftest entry (point_ops_getposition_golden.cpp).
int runGetPositionSelfTest(bool injectBug);

}  // namespace sw
