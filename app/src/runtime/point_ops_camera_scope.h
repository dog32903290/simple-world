// runtime/point_ops_camera_scope — C1 ACTIVE-CAMERA host scope: the bridge that closes the point-camera
// hole (CAMERA3D_BLUEPRINT §1). The Command-rail Camera op already stamps each RenderDrawItem's camera so
// DRAWS respect it; but the POINT-rail (fillPointCamera, point_graph_internal.h) always filled the DEFAULT
// camera, so SamplePointsByCameraDistance / TransformPointsFromClipspace sorted/unprojected against the
// default regardless of an upstream Camera op (the S2c/S3-class default-vs-pushed black-hole).
//
// MECHANISM — a verbatim copy of the S3a LiveCtxVarScope pattern (point_ops_setvarcmd.h): a thread_local
// "active camera" the Camera op's cook-driver branch SETS around its SubGraph cook, that fillPointCamera
// READS. The point ops are cooked (cookNode) inside the Camera-wrapped subtree → they see the live camera
// for exactly the SubGraph's cook lifetime, then it pops (innermost Camera wins = TiXL push/pop). nullptr
// outside any Camera scope → fillPointCamera falls back to the DEFAULT camera (faithful: behaviour
// unchanged off-scope, byte-identical to every pre-C1 graph).
//
// ★FLAT-RESIDENT MIRROR HARD GATE (the S2c blood lesson): the scope MUST be set on BOTH cook legs'
// Command branch (point_graph.cpp flat + point_graph_resident.cpp resident=production). A resident-only
// miss = resident point ops read the default = a prod-only black-hole. fillPointCamera is SHARED by both
// cooks — it is NOT forked; only the scope-set site is mirrored.
//
// TiXL ground truth (Camera.cs:36-45 push/pop + GraphicsMath LookAtRH/PerspectiveFovRH): the matrices a
// point op consumes are ObjectToCamera (= WorldToCamera at identity ObjectToWorld, the v1 fork) and
// CameraToWorld (= inverse(WorldToCamera)). The default-camera builder is field_camera's
// pointCameraMatrices; this scope builds the SAME pair from an ARBITRARY (eye/target/up/fov/clip/aspect)
// — the only difference from the default is the camera params, the convention (row-major, RH) is identical.
//
// runtime leaf: pure CPU + the field_camera math. No UI, no upward deps. Tiny own-header (NOT the at-cap
// point_ops.h, which is on the line-count ratchet) so the two cook drivers can call it without the god-header.
#pragma once
#include <map>
#include <string>

namespace sw {

// The raw camera params the Camera op resolves (the same v1 scope cookCamera stamps onto items). aspect<=0
// → the consumer uses the output aspect (Camera.cs:53-55 RequestedResolution fallback). active=false → an
// inactive scope (a non-Camera Command op) leaves the prior live camera intact (TiXL: a non-push node is
// transparent to the ambient context).
struct ActiveCamera {
  bool active = false;
  float eye[3] = {0.0f, 0.0f, 0.0f};
  float target[3] = {0.0f, 0.0f, 0.0f};
  float up[3] = {0.0f, 1.0f, 0.0f};
  float fovDeg = 45.0f;
  float nearClip = 0.01f;
  float farClip = 1000.0f;
  float aspect = 0.0f;  // <=0 → consumer's output aspect
};

// True iff opType is the Command-rail Camera op (the point-camera scope's writer). The cook drivers'
// Command branch consults this to decide whether to set the active-camera scope around the SubGraph cook.
bool isCameraScopeWriter(const std::string& opType);

// Resolve a Camera op's RESOLVED Float params (the SAME map cookCamera reads via cookVecN/cookParam) into an
// ActiveCamera (active=true). The vec3/vec2 params use the .x/.y/.z suffix convention (Position/Target/Up,
// ClipPlanes). Same defaults as cookCamera (Position=(0,0,DefaultCameraDistance), Target=0, Up=(0,1,0),
// FieldOfView=45°, ClipPlanes=(0.01,1000), AspectRatio=0). The cook drivers' Camera branch calls this; one
// codepath so flat + resident resolve identically (the flat Node::params and resident ResidentNode params
// both reach here as the same resolved float map).
ActiveCamera resolveActiveCamera(const std::map<std::string, float>& params);

// RAII guard — construct around the SubGraph cook (the SAME `{}` block the LiveCtxVarScope wraps), destroy
// after. Nests correctly (saves+restores the prior live camera) so an OUTER Camera layers under an INNER
// one = the inner camera wins for the inner subtree, the outer for the rest (TiXL innermost-wins push/pop).
// An INACTIVE camera (cam.active==false) leaves the prior live camera (a non-Camera Command is transparent).
struct LiveCameraScope {
  explicit LiveCameraScope(const ActiveCamera& cam);
  ~LiveCameraScope();
  LiveCameraScope(const LiveCameraScope&) = delete;
  LiveCameraScope& operator=(const LiveCameraScope&) = delete;
 private:
  const ActiveCamera* prev_;
  bool engaged_;
};

// The live ACTIVE camera, or nullptr when no Camera scope is active. Consulted by fillPointCamera
// (point_graph_internal.h) to build the point-rail matrices from the wired Camera instead of the default.
const ActiveCamera* liveActiveCamera();

// Build the point-rail camera matrices (ObjectToCamera + CameraToWorld, row-major) from an ActiveCamera.
// The SAME convention as field_camera's pointCameraMatrices, just with the Camera op's eye/target/up instead
// of the SetDefaultCamera defaults. These two matrices are aspect-INDEPENDENT (LookAtRH + inverse, no
// projection) — the fov/clip/aspect fields drive the DRAW rail's CameraToClipSpace, not the point rail.
// Defined alongside the scope (point_ops_camera.cpp) so the field_camera math include stays out of the cook
// headers.
void activeCameraMatrices(const ActiveCamera& cam, float outObjectToCamera[16],
                          float outCameraToWorld[16]);

// C1 -bug DRIVER flag (mirror of setVarBugSkipWrite): when true BOTH cook legs SKIP the active-camera scope
// set → the point ops read the DEFAULT camera even under a wired Camera → the C1 golden's camera-move
// selection change disappears → RED on both flat + resident. OFF in production.
bool& cameraScopeBugSkipPush();

// --selftest-camera-scope (the C1 HARD-GATE golden; BOTH legs). A Camera(eye)→point-op graph: a wired
// Camera changes which point SamplePointsByCameraDistance weights vs the default camera; injectBug skips the
// scope set → the change disappears → RED on both flat + resident.
int runCameraScopeSelfTest(bool injectBug);

// --selftest-camera-resident (C0 resident-ratify golden): Camera→Layer2d→RenderTarget cooked through the
// RESIDENT terminal (cookResident's cookCommand dispatch of cookCamera), proving the Command-rail camera
// survives on the production leg (NOT the by-hand runCameraSelfTest harness). injectBug RED.
int runCameraResidentSelfTest(bool injectBug);

}  // namespace sw
