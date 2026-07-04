// runtime/point_ops_getscreenpos — GetScreenPos (TiXL render/analyze): world position → screen-space
// projection, Command-rail compute + value-rail Position latch. Tiny own-header (the Group/spread
// precedent — point_ops.h is at its ratchet cap). runtime leaf: no upward deps.
#pragma once

namespace sw {

// Register the "GetScreenPos" cmd op; called by registerDrawPointOps.
void registerGetScreenPosOp();

// Test-only op flag (the projection tooth): when true the REAL cook skips the PERSPECTIVE DIVISION
// (GetScreenPos.cs:29-33) — raw clip xyz flows through as if w were 1 — while the rest of the cook
// still runs. The golden's -bug leg sets it; the closed-form expected screen values diverge → RED.
// OFF in production. CPU op flag, NOT a shader branch (constitution rule).
bool& getScreenPosDropDivideForTest();

// --selftest entry (point_ops_getscreenpos_golden.cpp).
int runGetScreenPosSelfTest(bool injectBug);

}  // namespace sw
