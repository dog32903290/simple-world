# FrameScheduler Contract

FrameScheduler answers:

```text
who owns the WALL PULSE for this graph frame? (not the playhead — see Transport)
```

This is the runtime law for the wall pulse. It is **one of two clocks** (L8); the scrubable
composition playhead is `Transport`, defined in `MY_WORLD_RUNTIME_CONTRACT.md` (Clock
Contract), and is never merged into the frame pulse. Vuo is dropped (L7); the old
`my_MainClock` body-layer adapter is void.

## Boundary

```text
FrameScheduler := graph-level owner of frameIndex, time, deltaTime, and cook order;
                  the WALL PULSE — monotonic, never pauses (runs even when Transport stops)
Transport      := SEPARATE owner of the composition playhead (position/play/scrub/loop),
                  driven by FrameScheduler.deltaTime; its paused-but-alive sibling time is
                  Transport.fxTime (cf. TiXL FxTimeInBars) — see MY_WORLD_RUNTIME_CONTRACT.md
renderTick     := generated/internal frame pressure, not creator-facing meaning
```

Nodes do not own time. They read the frame context supplied by the scheduler:

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
- creator graph data should not hand-author per-node clock edges; live cooking routes
  through the scheduler, not scattered per-node display-refresh wiring

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
