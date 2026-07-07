// ui/output_window_orbit — see header. The Output window's 3D orbit gesture: routes a left-drag / wheel to
// either a session Viewer ViewCamera (phase-B seam) or a selected Camera/OrbitCamera node's params (undoable
// SetOverrideCommand). Zone: ui. Orbit/zoom MATH is runtime/view_camera.h (parity-toothed); this file ROUTES.
#include "ui/output_window_orbit.h"

#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include "imgui.h"

#include "app/command.h"          // g_commands (undo stack) + SetOverrideCommand via graph_commands.h
#include "app/document.h"         // currentSymbol (mutable child for the live drag write) + bumpLibRevision + g_lib
#include "app/graph_commands.h"   // SetOverrideCommand (one drag = one undo, exactly like the inspector jog)
#include "runtime/compound_graph.h"  // SymbolChild / childById / effectiveInput
#include "runtime/view_camera.h"     // ViewCamera + applyOrbitDrag / applyZoom (the ported orbit math)

// The phase-B override seam (main.cpp): the Viewer camera drives the cook fallback through these.
namespace sw {
void setOutputViewCamera(const ViewCamera& cam);
void clearOutputViewCamera();
}  // namespace sw

namespace sw::ui {

// ── Viewer-mode session camera ──────────────────────────────────────────────────────────────────
// Holds the live ViewCamera the Viewer-mode orbit mutates. A single Output window today → one instance.
struct ViewerOrbitState {
  sw::ViewCamera cam = sw::makeDefaultViewCamera();
  bool engaged = false;  // has the Viewer override been pushed to the cook this session?
};

ViewerOrbitState& viewerOrbitState() {
  static ViewerOrbitState s;
  return s;
}

void captureViewerCamera(float eye[3], float target[3], float& roll) {
  const sw::ViewCamera& c = viewerOrbitState().cam;
  for (int i = 0; i < 3; ++i) { eye[i] = c.eye[i]; target[i] = c.target[i]; }
  roll = c.roll;
}

void restoreViewerCamera(const float eye[3], const float target[3], float roll) {
  ViewerOrbitState& s = viewerOrbitState();
  for (int i = 0; i < 3; ++i) { s.cam.eye[i] = eye[i]; s.cam.target[i] = target[i]; }
  s.cam.roll = roll;
  // Push the restored pose to the cook so a reopened project resumes the same view. A DEFAULT pose is
  // byte-identical to no-override (phase-A parity tooth) — restoring a moved camera engages the override.
  sw::setOutputViewCamera(s.cam);
  s.engaged = true;
}

bool isCameraOpNode(const sw::SymbolChild* viewNode) {
  return viewNode && (viewNode->symbolId == "Camera" || viewNode->symbolId == "OrbitCamera");
}

namespace {
// ── Locked-to-Op gesture state (one drag = one undo, mirroring inspector.cpp's activation/deactivation
// snapshot). A single orbit gesture at a time (one Output window). On drag START we snapshot each param we
// might touch (before value + whether it had an override); each frame we live-write the override + bump the
// resident projection; on drag END we push ONE SetOverrideCommand per param that actually moved. ─────────
constexpr float kOrbitDegPerPixel = 0.4f;   // Locked-to-Op sensitivity: 0.4°/px drag (NAMED FORK — TiXL's
                                            // OrbitCamera has no drag-to-param path; this is a sw affordance).
constexpr float kDistPerWheel = 0.2f;       // OrbitCamera DistanceToTarget wheel step (world units/notch).

// One tracked scalar param for the merge. `before` = value at drag start; `hadOverride` = did an override
// exist then (so undo erases vs. restores). `slotId` = the port id (e.g. "SpinAngleAndWobble.x").
struct TrackedParam {
  std::string slotId;
  bool hadOverride = false;
  float before = 0.0f;
};

struct LockedGesture {
  bool active = false;        // a drag is in flight
  int childId = 0;            // the node being manipulated (guards mid-drag selection change)
  std::string symbolId;       // its type (Camera vs OrbitCamera) — decides the write mapping
  std::vector<TrackedParam> tracked;  // the params snapshotted at drag start
};
LockedGesture g_locked;

// The full set of scalar port ids each camera type's orbit gesture may write — snapshotted at drag start so
// undo restores exactly, even for a param the drag only grazes. Camera: eye Position (orbit moves the eye).
// OrbitCamera: the static azimuth/elevation angle + the radius.
const std::array<const char*, 3> kCameraParams = {"Position.x", "Position.y", "Position.z"};
const std::array<const char*, 3> kOrbitParams = {"SpinAngleAndWobble.x", "OrbitAngleAndWobble.x",
                                                 "DistanceToTarget"};

float paramDef(const sw::SymbolChild& c, const char* id, float fallback) {
  return sw::effectiveInput(sw::doc::g_lib(), c, id, fallback);
}

// Snapshot the tracked params for a fresh drag (drag START).
void beginLockedGesture(const sw::SymbolChild& child) {
  g_locked.active = true;
  g_locked.childId = child.id;
  g_locked.symbolId = child.symbolId;
  g_locked.tracked.clear();
  const bool isCam = child.symbolId == "Camera";
  const auto& ids = isCam ? kCameraParams : kOrbitParams;
  for (const char* id : ids) {
    TrackedParam t;
    t.slotId = id;
    t.hadOverride = child.overrides.count(id) > 0;
    t.before = paramDef(child, id, 0.0f);
    g_locked.tracked.push_back(std::move(t));
  }
}

// Push ONE SetOverrideCommand per param that actually changed (drag END). If a param returned to its start
// value and had no prior override, erase the live-write residue so the definition default re-emerges (the
// zero-residue contract, exactly like inspector.cpp's IsItemDeactivatedAfterEdit branch). The command's
// doIt re-applies the final value (harmless — already live-written), so the undo stack owns the whole drag.
void endLockedGesture(sw::Symbol& parent, sw::SymbolChild& child) {
  for (const TrackedParam& t : g_locked.tracked) {
    const float now = paramDef(child, t.slotId.c_str(), t.before);
    if (now != t.before) {
      sw::g_commands.push(std::make_unique<sw::SetOverrideCommand>(
          sw::doc::g_lib(), parent.id, child.id, t.slotId, t.hadOverride, t.before, now));
    } else if (!t.hadOverride) {
      if (child.overrides.erase(t.slotId)) sw::doc::bumpLibRevision();
    }
  }
  g_locked = LockedGesture{};
}

// Live-write a scalar override during the drag (+ bump the resident projection so the preview moves NOW).
void liveWrite(sw::SymbolChild& child, const char* id, float value) {
  child.overrides[id] = value;
  sw::doc::bumpLibRevision();
}

// ── Locked-to-Op per-frame mutation ─────────────────────────────────────────────────────────────
// Apply this frame's drag delta (dx,dy pixels) + wheel to the node's params. Returns true if anything moved.
bool applyLockedFrame(sw::SymbolChild& child, float dx, float dy, float wheel) {
  bool moved = false;
  if (child.symbolId == "Camera") {
    // Camera op: Position = eye, Target = target (point_ops_camera cookCamera stamps them raw). Run the SAME
    // ported orbit math (view_camera applyOrbitDrag) on an eye/target pair read from the node's live params,
    // then write the rotated eye back to Position.x/y/z. Orbit holds |eye-target| and never moves Target —
    // so Target's override is untouched (faithful to a 3rd-person orbit around the look-at point).
    if (dx != 0.0f || dy != 0.0f) {
      sw::ViewCamera cam;
      cam.eye[0] = paramDef(child, "Position.x", 0.0f);
      cam.eye[1] = paramDef(child, "Position.y", 0.0f);
      cam.eye[2] = paramDef(child, "Position.z", 2.4142135f);
      cam.target[0] = paramDef(child, "Target.x", 0.0f);
      cam.target[1] = paramDef(child, "Target.y", 0.0f);
      cam.target[2] = paramDef(child, "Target.z", 0.0f);
      sw::applyOrbitDrag(cam, dx, dy);  // pixel drag → orbit (distance held), CameraInteraction.cs:158-163
      liveWrite(child, "Position.x", cam.eye[0]);
      liveWrite(child, "Position.y", cam.eye[1]);
      liveWrite(child, "Position.z", cam.eye[2]);
      moved = true;
    }
    if (wheel != 0.0f) {
      // Zoom: move Position toward/away from Target (same viewDistance scale as applyZoom, but written to the
      // node's Position). Read eye/target, applyZoom, write eye back. wheel>0 = closer (view_camera semantics).
      sw::ViewCamera cam;
      cam.eye[0] = paramDef(child, "Position.x", 0.0f);
      cam.eye[1] = paramDef(child, "Position.y", 0.0f);
      cam.eye[2] = paramDef(child, "Position.z", 2.4142135f);
      cam.target[0] = paramDef(child, "Target.x", 0.0f);
      cam.target[1] = paramDef(child, "Target.y", 0.0f);
      cam.target[2] = paramDef(child, "Target.z", 0.0f);
      sw::applyZoom(cam, wheel);
      liveWrite(child, "Position.x", cam.eye[0]);
      liveWrite(child, "Position.y", cam.eye[1]);
      liveWrite(child, "Position.z", cam.eye[2]);
      moved = true;
    }
    return moved;
  }
  // OrbitCamera: the orbit is driven by angle-in-DEGREES params, not a Position vector (point_ops_orbitcamera:
  //   orbitYaw   = SpinAngleAndWobble.x + spin·time + …   (line 91-94)
  //   orbitPitch = -OrbitAngleAndWobble.x                 (line 95-96, NOTE the negation)
  // so horizontal drag adds to SpinAngle (azimuth); vertical drag adds to OrbitAngle, but because orbitPitch
  // negates it, a downward drag (dy>0) should INCREASE OrbitAngle to tilt the view down consistently with the
  // Camera/Viewer branch (there dy>0 orbits the eye down). Radius = DistanceToTarget; wheel shrinks it.
  if (dx != 0.0f) {
    const float cur = paramDef(child, "SpinAngleAndWobble.x", 0.0f);
    liveWrite(child, "SpinAngleAndWobble.x", cur + dx * kOrbitDegPerPixel);
    moved = true;
  }
  if (dy != 0.0f) {
    const float cur = paramDef(child, "OrbitAngleAndWobble.x", 30.0f);
    liveWrite(child, "OrbitAngleAndWobble.x", cur + dy * kOrbitDegPerPixel);
    moved = true;
  }
  if (wheel != 0.0f) {
    const float cur = paramDef(child, "DistanceToTarget", 3.0f);
    float d = cur - wheel * kDistPerWheel;   // wheel>0 (scroll up) = zoom in = closer = smaller radius
    if (d < 0.0001f) d = 0.0001f;            // point_ops_orbitcamera clamps 0 radius to 0.0001 anyway
    liveWrite(child, "DistanceToTarget", d);
    moved = true;
  }
  return moved;
}
}  // namespace

// ── The Viewer-mode branch (no Camera op selected): drive the session ViewCamera → the phase-B seam. ──
namespace {
bool applyViewerFrame(float dx, float dy, float wheel) {
  ViewerOrbitState& s = viewerOrbitState();
  bool moved = false;
  if (dx != 0.0f || dy != 0.0f) { sw::applyOrbitDrag(s.cam, dx, dy); moved = true; }
  if (wheel != 0.0f) { sw::applyZoom(s.cam, wheel); moved = true; }
  if (moved) {
    sw::setOutputViewCamera(s.cam);  // push the moved camera to the cook fallback (both cook legs read it)
    s.engaged = true;
  }
  return moved;
}
}  // namespace

bool handleOutputOrbit(const sw::Symbol* /*parent*/, const sw::SymbolChild* viewNode, bool active,
                       bool hovered) {
  const ImGuiIO& io = ImGui::GetIO();
  const float dx = active ? io.MouseDelta.x : 0.0f;
  const float dy = active ? io.MouseDelta.y : 0.0f;
  const float wheel = hovered ? io.MouseWheel : 0.0f;

  const bool lockedTarget = isCameraOpNode(viewNode);

  if (!lockedTarget) {
    // Viewer mode. If a Locked gesture was mid-flight (selection changed away from a camera), close it out
    // against a still-resolvable child before switching rails.
    if (g_locked.active) {
      if (sw::Symbol* cur = sw::doc::currentSymbol())
        if (sw::SymbolChild* c = sw::childById(*cur, g_locked.childId)) endLockedGesture(*cur, *c);
      g_locked = LockedGesture{};
    }
    return applyViewerFrame(dx, dy, wheel);
  }

  // Locked-to-Op mode. Resolve the MUTABLE child (the live drag writes overrides on it; the const `parent`
  // passed in is only for the type check). currentSymbol() is the same parent the coordinator resolved.
  sw::Symbol* cur = sw::doc::currentSymbol();
  sw::SymbolChild* child = cur ? sw::childById(*cur, viewNode->id) : nullptr;
  if (!cur || !child) return false;

  // Drag lifecycle: START on the first active frame, END when active drops (release). The wheel outside a
  // drag is a one-shot: snapshot → apply → push, so a lone zoom is still one undo.
  const bool wheelOnly = !active && wheel != 0.0f;
  bool moved = false;

  if (active && !g_locked.active) beginLockedGesture(*child);
  if (wheelOnly && !g_locked.active) beginLockedGesture(*child);

  if (g_locked.active && g_locked.childId == child->id)
    moved = applyLockedFrame(*child, dx, dy, wheel);
  else if (g_locked.active)
    moved = false;  // selection changed mid-drag → ignore this frame's delta (gesture closes below)

  // END the gesture when the left drag releases (or, for a wheel-only shot, immediately after applying it).
  if (g_locked.active && (!active) ) {
    endLockedGesture(*cur, *child);
  }
  return moved;
}

void resetOutputViewToDefault() {
  ViewerOrbitState& s = viewerOrbitState();
  s.cam = sw::makeDefaultViewCamera();  // CameraInteraction.ResetCamera (cs:413-418): default eye/target/roll
  s.engaged = false;
  sw::clearOutputViewCamera();          // back to the hard-wired default (byte-identical to no override)
}

}  // namespace sw::ui
