// runtime/point_ops_actioncamera — ActionCamera: the stateful fly-camera CameraDefinition provider.
// TiXL authority: Operators/Lib/render/camera/ActionCamera.cs (+ ActionCamera.t3 defaults).
//
// SHAPE — ActionCamera has NO Command flow (its only output is the Reference Slot<object>,
// ActionCamera.cs:10-11): it is a CAMERA-REFERENCE PROVIDER whose definition INTEGRATES across frames
// (a WASD-style fly camera): each frame it blends toward its ReferenceCamera's definition
// (BlendToReferenceCamera·dt·60, cs:44-52), then integrates Position/Target from the
// Forward/Sideways/UpDown/Yaw/Pitch inputs scaled by Speed/RotationSpeed·dt (cs:54-79). A consumer
// (BlendCameras) resolves it through resolveCameraRefDefinition below.
//
// STATE — _cameraDefinition/_lastUpdateTime/_initialized/_triggerReset persist per NODE across frames.
// sw home: a process-lifetime store keyed by CmdCameraRef::nodePath (the residentFloatListState
// precedent), integrated ONCE per ctx->frameIndex (a second consumer in the same frame reads the
// cached definition — TiXL's slot dirty-flag equivalent).
//
// FORKS (named):
//   fork-actioncamera-clock-ctx-time — TiXL's clock is Playback.RunTimeInSecs (the wall run clock);
//     sw integrates on EvaluationContext.time (seconds since start). Both are monotonic seconds while
//     the app runs, so the deltas match in steady playback; scrub/pause semantics differ (TiXL's run
//     clock never pauses — neither does ctx.time).
//   fork-actioncamera-no-input-writeback — TiXL clears its TriggerReset input after a reset
//     (cs:47 SetTypedInputValue(false)); sw cannot write inputs, so the reset is EDGE-detected
//     (MathUtils.WasTriggered semantics: rising edge fires once). Equivalent for wired triggers.
//   fork-actioncamera-drawrail-only — the VALUE rail (CamPosition under a BlendCameras that
//     references an ActionCamera) does NOT resolve it (falls back to the default camera): the value
//     pass runs on the bars clock BEFORE the draw cook — integrating there would double-step the
//     state with mismatched units. The draw rail owns the integration.
//
// runtime leaf: pure math + a process-lifetime state store. No UI, no upward deps.
#pragma once

#include "runtime/point_graph.h"           // CmdCookCtx (CmdCameraRef)
#include "runtime/point_ops_blendcameras.h"  // SwCameraDefinition

struct EvaluationContext;  // runtime/eval_context.h (time/frameIndex)

namespace sw {

// Resolve an ActionCamera reference to its CURRENT CameraDefinition, integrating one step when this
// is the first resolution of ctx->frameIndex (ActionCamera.cs:19-85 transcription). Missing/
// unsupported ReferenceCamera = TiXL's warning leg (cs:27-38): NO integration, the state's current
// definition (ctor default before first init) is returned. Always fills `out`.
void resolveActionCameraDefinition(const CmdCookCtx::CmdCameraRef& ref, const EvaluationContext* ctx,
                                   SwCameraDefinition& out);

// Dispatch ANY CmdCameraRef to its definition: ActionCamera → the stateful resolver above; everything
// else → cameraDefinitionFromParams. Returns false for an unsupported (non-camera) type.
bool resolveCameraRefDefinition(const CmdCookCtx::CmdCameraRef& ref, const EvaluationContext* ctx,
                                SwCameraDefinition& out);

// Test-only: clear the process-lifetime ActionCamera state store (a golden runs several independent
// trajectories in one process). No production caller.
void resetActionCameraStateForTest();

// -bug seam (true cook-path corrosion): when true the integrator SKIPS the yaw/pitch view-direction
// rotation (cs:66-71) — newViewDirection stays the un-rotated viewDirection, so the yaw probe's
// Target flips → RED. OFF in production.
bool& actionCameraBugDropViewRotation();

}  // namespace sw
