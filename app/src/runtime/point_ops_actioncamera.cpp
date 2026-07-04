// runtime/point_ops_actioncamera — ActionCamera stateful integrator (header has the story + forks).
// TiXL authority: Operators/Lib/render/camera/ActionCamera.cs (line refs inline).
#include "runtime/point_ops_actioncamera.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <string>

#include "runtime/eval_context.h"  // EvaluationContext (time / frameIndex)

namespace sw {

bool& actionCameraBugDropViewRotation() {
  static bool v = false;  // OFF in production; the golden flips it around one resolve then resets
  return v;
}

namespace {

struct ActionCameraState {
  bool initialized = false;                // _initialized (cs:90)
  bool lastTrigger = false;                // _triggerReset (cs:87; the WasTriggered edge memory)
  double lastUpdateTime = 0.0;             // _lastUpdateTime (cs:88)
  uint32_t lastFrameIndex = 0xFFFFFFFFu;   // once-per-frame integrate guard (slot dirty-flag analog)
  SwCameraDefinition camDef;               // _cameraDefinition (cs:92; ctor default = CameraDefinition())
};

// Process-lifetime per-node store keyed by CmdCameraRef::nodePath (residentFloatListState precedent).
std::map<std::string, ActionCameraState>& stateStore() {
  static std::map<std::string, ActionCameraState> s;
  return s;
}

float pget(const std::map<std::string, float>* m, const char* id, float def) {
  if (!m) return def;
  auto it = m->find(id);
  return it != m->end() ? it->second : def;
}

void norm3(float v[3]) {
  const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (l > 0.0f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}
void cross3(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

// System.Numerics Matrix4x4.CreateRotationY — TransformNormal by it, row-vector (v·M).
void rotateAboutY(const float v[3], float rad, float out[3]) {
  const float c = std::cos(rad), s = std::sin(rad);
  out[0] = v[0] * c + v[2] * s;
  out[1] = v[1];
  out[2] = -v[0] * s + v[2] * c;
}

// System.Numerics Matrix4x4.CreateFromAxisAngle — VERBATIM element formula (TiXL passes the
// NON-normalized `side` axis, cs:69-70; the formula is transcribed raw, no normalize).
void rotateAboutAxis(const float v[3], const float axis[3], float rad, float out[3]) {
  const float x = axis[0], y = axis[1], z = axis[2];
  const float sa = std::sin(rad), ca = std::cos(rad);
  const float xx = x * x, yy = y * y, zz = z * z;
  const float xy = x * y, xz = x * z, yz = y * z;
  const float m[9] = {
      xx + ca * (1.0f - xx),      xy - ca * xy + sa * z,      xz - ca * xz - sa * y,
      xy - ca * xy - sa * z,      yy + ca * (1.0f - yy),      yz - ca * yz + sa * x,
      xz - ca * xz + sa * y,      yz - ca * yz - sa * x,      zz + ca * (1.0f - zz)};
  out[0] = v[0] * m[0] + v[1] * m[3] + v[2] * m[6];  // row-vector v·M (TransformNormal)
  out[1] = v[0] * m[1] + v[1] * m[4] + v[2] * m[7];
  out[2] = v[0] * m[2] + v[1] * m[5] + v[2] * m[8];
}

}  // namespace

void resetActionCameraStateForTest() { stateStore().clear(); }

void resolveActionCameraDefinition(const CmdCookCtx::CmdCameraRef& ref, const EvaluationContext* ctx,
                                   SwCameraDefinition& out) {
  ActionCameraState& st = stateStore()[ref.nodePath.empty() ? std::string("?") : ref.nodePath];
  if (!ctx) {  // no clock → no integration (a clockless caller reads the current definition)
    out = st.camDef;
    return;
  }
  if (st.lastFrameIndex == ctx->frameIndex) {  // already integrated this frame → cached definition
    out = st.camDef;
    return;
  }
  st.lastFrameIndex = ctx->frameIndex;

  // cs:21-23 — deltaTime off the run clock (fork-actioncamera-clock-ctx-time: ctx.time seconds).
  const double time = (double)ctx->time;
  float deltaTime = (float)(time - st.lastUpdateTime);
  st.lastUpdateTime = time;

  // cs:27-38 — the reference camera (one-level nested ref). Missing/unsupported → warning leg:
  // NO integration, the current definition stands (ctor default before the first init).
  SwCameraDefinition refDef;
  if (ref.upstreamRefs.empty() || !ref.upstreamRefs[0].params ||
      !cameraDefinitionFromParams(ref.upstreamRefs[0].opType, *ref.upstreamRefs[0].params, refDef)) {
    out = st.camDef;
    return;
  }

  const std::map<std::string, float>* p = ref.params;
  const float speed = pget(p, "Speed", 0.01f);                  // cs:40 (.t3 default 0.01)
  const float rotationSpeed = pget(p, "RotationSpeed", 0.2f);   // cs:41 (.t3 default 0.2)

  // cs:43-50 — reset edge + blend-to-reference. WasTriggered = rising edge (the input write-back
  // TiXL does at cs:47 is fork-actioncamera-no-input-writeback).
  const bool trigger = pget(p, "TriggerReset", 0.0f) > 0.5f;
  const bool reset = trigger && !st.lastTrigger;
  st.lastTrigger = trigger;
  float blend = pget(p, "BlendToReferenceCamera", 0.0f) * deltaTime * 60.0f;  // cs:44
  if (reset || !st.initialized) {  // cs:45-50
    st.initialized = true;
    blend = 1.0f;
  }
  blend = blend < 0.0f ? 0.0f : (blend > 1.0f ? 1.0f : blend);         // cs:52 blend.Clamp(0,1)
  st.camDef = blendCameraDefinitions(st.camDef, refDef, blend);        // cs:52

  // cs:54-79 — the fly integration.
  const float forward = pget(p, "Forward", 0.0f);
  const float sideways = pget(p, "Sideways", 0.0f);
  const float upDown = pget(p, "UpDown", 0.0f);
  const float yaw = pget(p, "Yaw", 0.0f);
  const float pitch = pget(p, "Pitch", 0.0f);
  const float roll = pget(p, "Roll", 0.0f);
  const float fov = pget(p, "FOV", 0.0f);

  float viewDirection[3] = {st.camDef.target[0] - st.camDef.position[0],
                            st.camDef.target[1] - st.camDef.position[1],
                            st.camDef.target[2] - st.camDef.position[2]};  // cs:63
  norm3(viewDirection);                                                     // cs:64

  float newViewDirection[3];
  if (actionCameraBugDropViewRotation()) {
    // ★-bug: the yaw/pitch view rotation dropped (cs:66-71 skipped) → the yaw probe's Target flips.
    newViewDirection[0] = viewDirection[0];
    newViewDirection[1] = viewDirection[1];
    newViewDirection[2] = viewDirection[2];
  } else {
    rotateAboutY(viewDirection, -yaw * rotationSpeed * deltaTime, newViewDirection);  // cs:66-67
  }
  const float unitY[3] = {0.0f, 1.0f, 0.0f};
  float side[3];
  cross3(unitY, newViewDirection, side);  // cs:69 (NOT normalized — TiXL passes it raw)
  if (!actionCameraBugDropViewRotation()) {
    float tmp[3];
    rotateAboutAxis(newViewDirection, side, pitch * rotationSpeed * deltaTime, tmp);  // cs:70-71
    newViewDirection[0] = tmp[0];
    newViewDirection[1] = tmp[1];
    newViewDirection[2] = tmp[2];
  }

  for (int i = 0; i < 3; ++i) {  // cs:73-75 (forward moves along the PRE-rotation viewDirection)
    st.camDef.position[i] += viewDirection[i] * forward * speed * deltaTime;
    st.camDef.position[i] += side[i] * sideways * speed * deltaTime;
  }
  st.camDef.position[1] += upDown * speed * deltaTime;  // UnitY term

  for (int i = 0; i < 3; ++i)  // cs:77
    st.camDef.target[i] = st.camDef.position[i] + newViewDirection[i];
  st.camDef.roll += roll * speed * deltaTime;  // cs:78
  st.camDef.fovRad += fov * deltaTime;         // cs:79 (FieldOfView is radians in the definition)

  out = st.camDef;
}

bool resolveCameraRefDefinition(const CmdCookCtx::CmdCameraRef& ref, const EvaluationContext* ctx,
                                SwCameraDefinition& out) {
  if (ref.opType == "ActionCamera") {
    resolveActionCameraDefinition(ref, ctx, out);
    return true;
  }
  return ref.params && cameraDefinitionFromParams(ref.opType, *ref.params, out);
}

}  // namespace sw
