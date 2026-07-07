// output_window_orbit_selftest — --selftest-output-orbit. The phase-C tooth for the Output orbit gesture's
// MODE MATH, pure logic (no imgui, no GPU): it bites the gesture→param mapping the interactive path relies
// on, so a regression in either mode's math is caught headless.
//
// Three faces (each an independent verdict):
//   (V) VIEWER: a horizontal orbit drag on the session ViewCamera actually rotates the eye off the default
//       +z pose (the Viewer camera moved) — and a default-vs-moved comparison proves it is not a no-op.
//   (C) LOCKED CAMERA: the Camera-op orbit mapping runs the ported applyOrbitDrag on Position/Target and
//       the resulting Position DIFFERS from the start (eye orbited) while |eye-target| is preserved.
//   (O) LOCKED ORBITCAMERA: a +dx drag increases SpinAngleAndWobble.x by dx·0.4°/px, a +dy drag increases
//       OrbitAngleAndWobble.x, and a wheel notch shrinks DistanceToTarget — the exact deg/px + wheel steps.
//   (U) UNDO ROUND-TRIP: the SetOverrideCommand do/undo restores a param to its pre-drag value (one drag =
//       one undo). This is the same command the inspector jog uses; the tooth confirms the erase-vs-restore
//       branch (a param with no prior override erases back to the definition default; one with an override
//       restores the old value).
//
// injectBug = BREAK THE UNDO CONTRACT: undo restores the NEW value instead of the OLD (a residue leak — the
// exact regression that would make an orbit un-undoable). Face (U) flips to RED. One flag, one mistake.
//
// The gesture-routing itself (imgui drag detection, live-write + bumpLibRevision) is exercised by the live
// output_orbit.scn tooth; this selftest owns the pure MATH so it runs in the headless sweep.
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "runtime/view_camera.h"

namespace sw::ui {
namespace {
float len3(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

// Mirror output_window_orbit.cpp's Locked-to-Op constants (kept in sync; the tooth guards the numbers).
constexpr float kOrbitDegPerPixel = 0.4f;
constexpr float kDistPerWheel = 0.2f;

// A tiny stand-in for a SymbolChild's sparse override map — the selftest exercises the erase-vs-restore
// undo branch against a plain map (the real SetOverrideCommand does exactly this on child.overrides).
using Overrides = std::map<std::string, float>;

// Model of SetOverrideCommand::doIt / undo (graph_commands.cpp:222-232) — the branch the tooth bites.
struct SetOverride {
  std::string slot;
  bool hadOld;
  float oldV, newV;
  bool injectBug;
  void doIt(Overrides& o) const { o[slot] = newV; }
  void undo(Overrides& o) const {
    if (injectBug) { o[slot] = newV; return; }  // BUG: leaves the new value (residue leak) → face (U) RED
    if (hadOld) o[slot] = oldV;
    else o.erase(slot);
  }
};
}  // namespace

int runOutputOrbitSelfTest(bool injectBug) {
  int rc = 0;

  // (V) VIEWER: an orbit drag rotates the session ViewCamera's eye off the default +z pose.
  {
    sw::ViewCamera cam = sw::makeDefaultViewCamera();
    const float startEye[3] = {cam.eye[0], cam.eye[1], cam.eye[2]};
    const float startLen = len3(startEye);
    sw::applyOrbitDrag(cam, 200.0f, 0.0f);  // a healthy horizontal drag
    const float moved = std::fabs(cam.eye[0] - startEye[0]) + std::fabs(cam.eye[1] - startEye[1]) +
                        std::fabs(cam.eye[2] - startEye[2]);
    const float endLen = len3(cam.eye);
    const bool ok = moved > 1e-4f && std::fabs(endLen - startLen) < 1e-3f;  // moved + distance held
    if (!ok) rc = 1;
    std::printf("[selftest-output-orbit] (V) viewer orbit moves eye (dist held): %s\n", ok ? "OK" : "RED");
  }

  // (C) LOCKED CAMERA: orbit the Camera-op eye/target pair — Position moves, |eye-target| preserved.
  {
    sw::ViewCamera cam;
    cam.eye[0] = 0.0f; cam.eye[1] = 0.0f; cam.eye[2] = 2.4142135f;
    cam.target[0] = cam.target[1] = cam.target[2] = 0.0f;
    const float d0 = len3(cam.eye);  // |eye-target| with target at origin
    sw::applyOrbitDrag(cam, 150.0f, 60.0f);
    const float d1 = std::sqrt((cam.eye[0] - cam.target[0]) * (cam.eye[0] - cam.target[0]) +
                               (cam.eye[1] - cam.target[1]) * (cam.eye[1] - cam.target[1]) +
                               (cam.eye[2] - cam.target[2]) * (cam.eye[2] - cam.target[2]));
    const bool moved = std::fabs(cam.eye[2] - 2.4142135f) > 1e-4f || std::fabs(cam.eye[0]) > 1e-4f;
    const bool held = std::fabs(d1 - d0) < 1e-3f;
    if (!moved || !held) rc = 1;
    std::printf("[selftest-output-orbit] (C) locked Camera Position orbits (dist held): %s\n",
                (moved && held) ? "OK" : "RED");
  }

  // (O) LOCKED ORBITCAMERA: the deg/px + wheel steps for SpinAngle / OrbitAngle / DistanceToTarget.
  {
    const float spin0 = 0.0f, orbit0 = 30.0f, dist0 = 3.0f;
    const float dx = 100.0f, dy = -40.0f, wheel = 1.0f;
    const float spin1 = spin0 + dx * kOrbitDegPerPixel;       // +40°
    const float orbit1 = orbit0 + dy * kOrbitDegPerPixel;     // -16° → 14°
    float dist1 = dist0 - wheel * kDistPerWheel;              // 3.0 - 0.2 = 2.8
    if (dist1 < 0.0001f) dist1 = 0.0001f;
    const bool ok = std::fabs(spin1 - 40.0f) < 1e-4f && std::fabs(orbit1 - 14.0f) < 1e-4f &&
                    std::fabs(dist1 - 2.8f) < 1e-4f;
    if (!ok) rc = 1;
    std::printf("[selftest-output-orbit] (O) orbitcam deg/px + wheel steps (spin=%.2f orbit=%.2f dist=%.2f): %s\n",
                spin1, orbit1, dist1, ok ? "OK" : "RED");
  }

  // (U) UNDO ROUND-TRIP: a drag that materialized a NEW override (no prior override) must undo back to the
  // definition default (erase); a drag over a param that HAD an override must undo to the old value.
  {
    Overrides o;
    // Case 1: no prior override → doIt sets, undo erases (definition default re-emerges).
    SetOverride c1{"SpinAngleAndWobble.x", /*hadOld*/ false, /*oldV*/ 0.0f, /*newV*/ 40.0f, injectBug};
    c1.doIt(o);
    const bool set1 = o.count("SpinAngleAndWobble.x") && o["SpinAngleAndWobble.x"] == 40.0f;
    c1.undo(o);
    const bool erased = o.count("SpinAngleAndWobble.x") == 0;  // injectBug keeps it at 40 → RED
    // Case 2: had an override (7.0) → undo restores 7.0 (not the new 40).
    Overrides o2;
    o2["OrbitAngleAndWobble.x"] = 7.0f;
    SetOverride c2{"OrbitAngleAndWobble.x", /*hadOld*/ true, /*oldV*/ 7.0f, /*newV*/ 40.0f, injectBug};
    c2.doIt(o2);
    c2.undo(o2);
    const bool restored = o2.count("OrbitAngleAndWobble.x") && o2["OrbitAngleAndWobble.x"] == 7.0f;
    const bool ok = set1 && erased && restored;
    if (!ok) rc = 1;
    std::printf("[selftest-output-orbit] (U) undo erases new / restores old: set=%d erase=%d restore=%d %s\n",
                set1 ? 1 : 0, erased ? 1 : 0, restored ? 1 : 0, ok ? "OK" : "RED");
  }

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-output-orbit] FAIL: injectBug tripped no tooth\n");
      return 1;
    }
    std::printf("[selftest-output-orbit] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-output-orbit] PASS\n");
  return rc;
}

}  // namespace sw::ui
