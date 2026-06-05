# Native FrameScheduler Live Dirty Proof

NativeFrameSchedulerLiveDirtyProof answers:

Can the live scheduler combine graph-owned frame uniforms with commandGraph edits
and cook only the dirty runtime subgraph for a product frame?

Acceptance line:

```text
commandGraph frame edit -> dirty closure -> scheduled cook set -> frame artifact
```

## Required Claims

- schedulerOwnsFrameUniforms: true
- commandDirtyPropagation: true
- staticUnchangedNodeSkipped: true
- runtimeFrameLinked: true

## Boundary

u_time and u_frame are scheduler-owned uniforms. Nodes may read them, but nodes
do not own or increment time. CommandGraph edits mark the changed node dirty,
then the scheduler walks downstream runtimeGraph edges to find the scheduled cook
set. Clean static nodes stay cached.

This is not a renderer, GPU command submission proof, animation language, or
complete invalidation engine. It is the live dirty propagation spine needed by
the native product runtime.
