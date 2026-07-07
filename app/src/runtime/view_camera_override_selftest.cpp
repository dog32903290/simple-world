// view_camera_override_selftest — --selftest-view-camera-override. The phase-B PARITY tooth for the
// OUTPUT-camera override seam (view_camera_active.{h,cpp} + the 8 default-fallback swaps). PURE MATH, NO GPU,
// NO UI. Bites BOTH faces the parity命脈 fears, through the EXACT helper all 8 injection sites call
// (activeViewCameraForward) so the tooth tracks the real code path, not a copy:
//
//   (A) OVERRIDE TAKES EFFECT: engage a NON-default ViewCamera (orbited 90° off +z) via ViewCameraScope →
//       activeViewCameraForward(aspect) must DIFFER from defaultLayerCameraForward(aspect) (the WorldToCamera
//       actually moved). This proves an engaged override reaches every site that funnels through the helper —
//       i.e. the orbit is not a no-op. Representative of all 8 fallbacks (they are one shared codepath).
//
//   (B) ABSENT / CLEARED DOES NOT LEAK (the byte-identical floor): with NO scope engaged,
//       activeViewCameraForward(aspect) == defaultLayerCameraForward(aspect) BIT-FOR-BIT (both matrices). And
//       after a scope block EXITS (~ViewCameraScope pops), the next call is byte-identical AGAIN — the RAII
//       pop restored null. This is the property that keeps every camera/layer2d/rendertarget golden green
//       under override-absent. A DEFAULT-valued override (makeDefaultViewCamera) engaged is ALSO byte-
//       identical (phase-A parity tooth) — divergence appears ONLY after an actual drag, so an idle Output
//       window (an engaged-but-unmoved camera, a plausible phase-C state) never perturbs a golden.
//
// injectBug = LEAK A STALE OVERRIDE PAST CLEAR: instead of letting the scope pop, keep a second scope engaged
// so activeViewCameraForward stays diverged when the test expects the cleared/default. This is the EXACT
// regression the命脈 fears (clear没清乾淨 → every default-camera golden silently renders through a stale
// orbit → RED). Tooth (B)'s "back to default after the block" flips to DIFF → RED. One boolean, one mistake.
#include "runtime/view_camera_active.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "runtime/field_camera.h"  // defaultLayerCameraForward (the parity reference)
#include "runtime/view_camera.h"   // makeDefaultViewCamera / applyOrbitDrag

namespace sw {
namespace {
bool matEq(const Mat4& a, const Mat4& b) { return std::memcmp(a.m, b.m, 16 * sizeof(float)) == 0; }
bool fwdEq(const LayerCameraForward& a, const LayerCameraForward& b) {
  return matEq(a.worldToCamera, b.worldToCamera) && matEq(a.cameraToClipSpace, b.cameraToClipSpace);
}
}  // namespace

int runViewCameraOverrideSelfTest(bool injectBug) {
  const float aspect = 16.0f / 9.0f;
  int rc = 0;
  const LayerCameraForward def = defaultLayerCameraForward(aspect);

  // Pre-condition: with NO scope, the helper is the plain default (the un-overridden production path).
  {
    LayerCameraForward absent = activeViewCameraForward(aspect);
    bool ok = fwdEq(absent, def);
    if (!ok) rc = 1;
    std::printf("[selftest-view-camera-override] (pre) no-scope == default: %s\n", ok ? "OK" : "RED");
  }

  // Build the NON-default orbited camera used by tooth (A) — 90° about Up off the default +z pose.
  ViewCamera orbited = makeDefaultViewCamera();
  {
    const float kOrbitSensitivity = -0.1f / 60.0f;      // mirror view_camera.cpp (drives the 90° drag)
    const float dx = (3.14159265358979323846f * 0.5f) / kOrbitSensitivity;  // exactly +90° about Up
    applyOrbitDrag(orbited, dx, 0.0f);
  }

  // (A) OVERRIDE TAKES EFFECT — inside the scope the helper must DIVERGE from the default.
  bool overrodeInScope = false;
  {
    ViewCameraScope scope(&orbited);
    LayerCameraForward got = activeViewCameraForward(aspect);
    overrodeInScope = !fwdEq(got, def);  // moved off the default → the orbit reached the helper
    if (!overrodeInScope) rc = 1;
    // Cross-check it equals toForward(orbited) exactly (the helper builds it via toForward — same math).
    LayerCameraForward want = toForward(orbited, aspect);
    bool exact = fwdEq(got, want);
    if (!exact) rc = 1;
    std::printf("[selftest-view-camera-override] (A) in-scope override diverges from default=%d "
                "== toForward(orbited)=%d %s\n",
                overrodeInScope ? 1 : 0, exact ? 1 : 0,
                (overrodeInScope && exact) ? "OK" : "RED");
  }  // ~ViewCameraScope(scope) pops → override is null again (the correct "clear" — production semantics)

  // injectBug = LEAK A STALE OVERRIDE PAST CLEAR: engage a NEVER-destroyed scope AFTER the block popped, so
  // the thread_local stays diverged when tooth (B) expects the cleared/default. This models "clear没清乾淨"
  // (the runtime forgot to reset the override). The heap ViewCamera + escaped guard are intentionally leaked
  // (a one-shot selftest process, --selftest exits via _Exit) — the point is the pop that never happens.
  if (injectBug) {
    ViewCamera* leaked = new ViewCamera(orbited);            // the stale override the runtime failed to clear
    new ViewCameraScope(leaked);  // escaped guard, never destroyed → thread_local sticks diverged → (B) RED
  }

  // (B) CLEARED / ABSENT DOES NOT LEAK — after the block the helper is byte-identical to default AGAIN.
  {
    LayerCameraForward after = activeViewCameraForward(aspect);
    bool ok = fwdEq(after, def);  // injectBug's leaked scope keeps this DIVERGED → RED
    if (!ok) rc = 1;
    std::printf("[selftest-view-camera-override] (B) after-scope back to default (no leak): %s\n",
                ok ? "OK" : "RED");
  }

  // (B2) A DEFAULT-valued override engaged is byte-identical (idle Output window never perturbs a golden).
  {
    ViewCamera idle = makeDefaultViewCamera();
    ViewCameraScope scope(&idle);
    LayerCameraForward got = activeViewCameraForward(aspect);
    bool ok = fwdEq(got, def);
    if (!ok) rc = 1;
    std::printf("[selftest-view-camera-override] (B2) engaged-but-unmoved == default (byte-id): %s\n",
                ok ? "OK" : "RED");
  }

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-view-camera-override] FAIL: injectBug tripped no tooth\n");
      return 1;
    }
    std::printf("[selftest-view-camera-override] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-view-camera-override] PASS\n");
  return rc;
}

}  // namespace sw
