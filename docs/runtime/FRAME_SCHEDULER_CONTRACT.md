# FrameScheduler Contract

FrameScheduler answers:

```text
who owns visual time for this graph frame?
```

This is the runtime law underneath `my_MainClock`.

## Boundary

```text
FrameScheduler := graph-level owner of frameIndex, time, deltaTime, and cook order
my_MainClock := Vuo body-layer adapter for host display refresh
renderTick := generated/internal frame pressure, not creator-facing meaning
```

Visual nodes do not own time. They read the frame context supplied by the
scheduler:

```text
frameIndex
time
deltaTime
```

## Synchronization Rules

- one visual frame has exactly one `frameIndex`, `time`, and `deltaTime`
- every cooked node in that frame observes the same frame context
- cook order is deterministic and derived from graph edges
- state nodes update once per frame boundary, even if several downstream ports
  observe them
- publish/output nodes publish only after upstream frame work is complete
- Vuo event cables may still exist in generated proof compositions, but creator
  graph data should not hand-author per-node clock edges

## First Proof

Fixture:

```text
docs/runtime/fixtures/frame_scheduler_constant_feedback.graph.json
```

Runner:

```text
docs/runtime/scripts/frame_scheduler_shell.py
```

Artifacts:

```text
docs/runtime/artifacts/frame_scheduler/frame_scheduler_trace.json
docs/runtime/artifacts/frame_scheduler/frame_scheduler_errors.json
docs/runtime/artifacts/frame_scheduler/node_observations.json
docs/runtime/artifacts/frame_scheduler/state_trace.json
```

This proof is not a renderer and not a Vuo replacement. It only proves the
clock ownership and state boundary that let us hide clock/event wiring later
without making every source node run on its own private pulse.
