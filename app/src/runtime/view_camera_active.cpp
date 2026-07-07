// runtime/view_camera_active.cpp — see view_camera_active.h. The thread_local OUTPUT-CAMERA OVERRIDE scope
// + the single default-fallback replacement activeViewCameraForward. A verbatim mirror of the C1
// LiveCameraScope mechanism (point_ops_camera.cpp), one abstraction lower (it overrides the DEFAULT camera,
// not a wired Camera op). Pure CPU + view_camera / field_camera math; ZERO Metal / UI.
#include "runtime/view_camera_active.h"

#include "runtime/field_camera.h"  // defaultLayerCameraForward (the un-overridden fallback)

namespace sw {

// The live override, or nullptr = no override = the hard-wired default (production default). Mirrors
// t_liveActiveCamera in point_ops_camera.cpp — the SAME thread_local push/restore discipline. The pointer
// is BORROWED: ViewCameraScope stores the caller's ViewCamera (kept alive across the whole cook), never a copy.
static thread_local const ViewCamera* t_liveViewCameraOverride = nullptr;

ViewCameraScope::ViewCameraScope(const ViewCamera* cam)
    : prev_(t_liveViewCameraOverride), engaged_(cam != nullptr) {
  // ENGAGE only when an override is actually present. A null pointer (no override this frame) is
  // TRANSPARENT — it leaves the prior override intact (production always constructs at the cook top with the
  // enclosing state = null, so a null construct keeps liveViewCameraOverride() == null → byte-identical).
  if (engaged_) t_liveViewCameraOverride = cam;
}
ViewCameraScope::~ViewCameraScope() {
  if (engaged_) t_liveViewCameraOverride = prev_;  // pop to the enclosing override (nests; production pops to null)
}
const ViewCamera* liveViewCameraOverride() { return t_liveViewCameraOverride; }

LayerCameraForward activeViewCameraForward(float aspect) {
  // The SINGLE choke point every fallback site funnels through. Override engaged → the mutable orbit camera;
  // else the hard-wired default, byte-identical to a raw defaultLayerCameraForward(aspect) call. Because
  // toForward(makeDefaultViewCamera(),aspect) == defaultLayerCameraForward(aspect) bit-for-bit (phase-A
  // tooth), an UNMOVED override is also byte-identical — divergence appears only after an actual drag.
  if (const ViewCamera* ov = t_liveViewCameraOverride) return toForward(*ov, aspect);
  return defaultLayerCameraForward(aspect);
}

}  // namespace sw
