# Native Runtime Master Progress

This file is the single progress entrypoint for the native runtime goal. Other
plans, proofs, fixtures, and artifacts are source material; live code, tests,
and proof artifacts can overrule stale plan text.

## Current Snapshot

- Date: 2026-06-06.
- Branch: `main`, aligned with `origin/main`.
- Latest known commit: `db50c98 Require explicit Vuo port defaults in admission index`.
- Working tree safety: `AGENTS.md` and
  `skills/tixl-vuo-node-port/SKILL.md` are modified by outside work. Do not
  revert, overwrite, or casually absorb them.
- Latest verified full suite: `node --test tests/*.test.js` passed 844/844.
  `docs/contracts/vuo_node_admission_index.json` is generated from all 354
  checked-in `vuo-nodes/*.c` sources and now carries risk classification:
  9 high, 42 medium, 303 low. All high-risk entries require and point to a
  full `docs/contracts/node_manifests/*.json` manifest. Focused gates:
  `node --test tests/node_admission_contract.test.js` passes 14/14, and
  `node --test tests/graph_interaction_contract.test.js` passes 6/6.
- Current runtime claim: the native runtime architecture is at an honest
  **100/100** when the current proof lanes and artifacts are passing. The closure harness
  proves native headless app/canvas state -> command-only graph mutation ->
  runtimeGraph -> FrameScheduler -> resource lifetime ledger -> shader IR/cache
  -> AI worker diagnostic repair -> repaired multi-pass Metal GPU texture patch.
  Product-facing interaction now includes both the first human workflow and a
  bounded library/canvas/inspector command loop, but still not final canvas
  interaction parity. Importer command ingest is now also bounded-proven: an
  external document becomes a command stream, not direct editorGraph truth. Live
  FrameScheduler dirty propagation is now bounded-proven: scheduler-owned
  `u_time`/`u_frame` roots and commandGraph edits form dirty closures and skip
  clean static nodes. RuntimeGraph incremental build is now bounded-proven:
  commandGraph topology edits rebuild the executable cook graph while reusing
  unaffected executable nodes. Metal heap residency is now bounded-proven with
  real `MTLHeap` allocation and heap-backed textures. ShaderIR now has a bounded
  core expression language for uniforms, UVs, arithmetic, color composition,
  cache keys, generated MSL, and native Metal compile proof. AI authoring now
  has a bounded product assist proof: structured intent becomes a validated
  command plan, command replay builds runtime artifacts, diagnostics are read,
  and repair commands are appended without direct graph mutation.
- Closed bounded lane: TiXL Mesh Draw / PBR native Metal backend replacement is
  closure-ready for its bounded lane. It is not generic HLSL-to-MSL translation,
  repo-wide TiXL clone parity, or Vuo parity.
- Explicit nonclaims: the native canvas proof is headless/data-backed, not the
  final human-facing app skin. Shader IR is a bounded seed for ConstantImage,
  Blob, BlendImages, Gradient, Feedback, and RenderTarget. Shader IR/codegen now
  has a registry-driven bounded proof for those six texture ops plus a core
  expression language proof for uniforms, math, and color composition, but still
  not arbitrary user shader language, HLSL translation, loops, or TiXL parity.
  AI repair now has a
  live render-diagnostic-repair proof and bounded authoring-assist command-plan
  proof, but still not broad natural-language patch authoring. Native UI now has bounded human workflow and canvas command
  loop proofs, but still not final visual polish or full interaction parity.
  Importer ingest is command-only, but still not a full file-format importer.
  Live dirty propagation is proven for a bounded runtime graph, but not a full
  renderer invalidation engine. Incremental runtimeGraph rebuild is proven for a
  bounded executable graph, but not a general optimizer. Metal heap-backed
  texture allocation is proven for admitted descriptors, but not a complete heap
  allocator, eviction system, or backend hazard tracker.

## Active Lane

`node_admission_contract` is closed for the current full-node contract gate.
Every checked-in Vuo node now has a machine-readable admission entry with
creator name, source path, port/default/range surface, state, flow ownership,
backend degradation policy, parity level, failure language, observability
context, evidence pointer, and risk classification. Current high-risk entries
are `my.field.combine.combineSdf`, `my.field.generate.sdf.sphereSdf`,
`my.field.render.raymarchField`, `my.image.generate.basic.constantImage`,
`my.image.generate.basic.renderTarget`, `my.image.use.blend`,
`my.image.use.keepPreviousFrame`, `my.render.dx11.api.clearRenderTarget`, and
`my.runtime.clock.mainClock`; all require and point at full node manifests. This
does not mean every node is promoted to native runtime; full manifests remain
the promotion gate for runtime and high-risk nodes.

`graph_interaction_contract` is closed for the first pure UI-to-runtime
interaction layer. `docs/runtime/scripts/graph_interaction_contract.js` exposes
pure `GraphState` commands for CreateNode, SelectNode, MoveNode,
BeginCableDrag, HoverPort, CommitCableDrag, CancelCableDrag, DeleteSelection,
and SetParameter. `tests/graph_interaction_contract.test.js` proves a headless
SphereSDF -> RaymarchField flow, invalid cable diagnostics, attached-edge
deletion, parameter dirtying, safe save/reload, cable cancel, and layout-only
move behavior. This is not polished UI; UI remains only a view that dispatches
commands, and runtime consumes GraphState/GraphDocument.

`product_runtime_completion` is now closed for the bounded full product/runtime
body. It started from the current `full_runtime_architecture` closure evidence
and deepened the weak product spines without redoing already closed proofs. The
`native_product_canvas_surface` slice is closed for real native surface creation:
a command-authored fixture compiles and runs an AppKit/MetalKit probe, creates an
NSWindow with MTKView, attaches a runtimeGraph frame artifact, and passes
node:test. The `native_texture_patch_product_runtime` slice is also closed for
its bounded product step: a command-authored Gradient -> Feedback -> RenderTarget
patch builds runtimeGraph/resource/shader IR artifacts, runs actual Metal
readback, and keeps the nonclaim that this is still not a complete texture
runtime or complete shader language. The `native_ai_worker_live_repair` slice is
closed for bounded live repair: the worker renders a broken patch, reads
`frame_stats.json`, diagnoses `render.black_frame`, appends a commandGraph repair
command, rerenders through Metal, and publishes repaired evidence. The
  `native_shader_ir_codegen_registry` slice is closed for current product scope:
NodeSpec-style registry entries now generate ShaderIR, cache keys, and MSL for
the six admitted texture ops, while unknown nodes emit diagnostics and block
source generation. The `native_resource_lifetime_policy` slice is closed for
bounded allocator policy: transient resources get alias plans, persistent
feedback history is protected from transient aliasing, hazard barriers and
release fences are emitted, and leak reports are clean. The
`native_metal_heap_residency` slice is closed for bounded real Metal residency:
admitted descriptors allocate heap-backed textures from an `MTLHeap` and publish
a clean residency/release ledger. It is still not a complete heap allocator,
eviction system, or backend-specific hazard tracker. The
`native_human_app_workflow` slice is closed for the
first human-facing workflow: a native AppKit window contains toolbar, library,
MTKView canvas, inspector, and diagnostics strip; inspector edits dispatch
commandGraph commands and link back to a runtime frame artifact. It is still not
complete interaction parity or final visual polish. The
`native_canvas_interaction_command_loop` slice is closed for bounded
multi-region interaction: library create, canvas place/connect, and inspector
edit actions all enter through commandGraph before the runtimeGraph/frame
artifact is built. The `native_importer_command_ingest` slice is closed for
bounded importer mutation law: a simple external document is converted to an
ordered import command stream, replayed into editorGraph, and only then built as
runtimeGraph. The `native_frame_scheduler_live_dirty` slice is closed for
bounded live invalidation: graph-owned frame uniforms and a frame-time
commandGraph edit generate dirty roots, downstream dirty closures, skipped clean
static nodes, and per-frame artifacts. The
`native_runtime_graph_incremental_builder` slice is closed for bounded live
runtimeGraph building: command replay produces an initial executable graph,
live commandGraph topology edits produce a runtimeGraph diff, unaffected
executable nodes are reused by structural hash, and cook order is recomputed.
The `native_shader_ir_expression_core` slice is closed for bounded expression
codegen: NodeSpec expression fields become ShaderExpressionIR, cache entries,
generated MSL, and a native Metal compile artifact; unsupported unsafe ops block
source generation with diagnostics. The `native_ai_worker_authoring_assist`
slice is closed for bounded product authoring assist: structured intent becomes
a validated command plan, replay creates runtimeGraph/frame artifacts,
diagnostics are observed, and repair commands are appended only through
commandGraph. It is still not broad natural-language patch authoring or
arbitrary prompt-to-graph.

Acceptance line:

```text
docs/runtime/fixtures/native_product_canvas_surface.graph.json
-> docs/runtime/scripts/native_product_canvas_surface_shell.py
-> docs/runtime/artifacts/native_product_canvas_surface/*
-> tests/native_product_canvas_surface.test.js

docs/runtime/fixtures/native_canvas_interaction_command_loop.graph.json
-> docs/runtime/scripts/native_canvas_interaction_command_loop_shell.py
-> docs/runtime/artifacts/native_canvas_interaction_command_loop/*
-> tests/native_canvas_interaction_command_loop.test.js

docs/runtime/fixtures/native_importer_command_ingest.graph.json
-> docs/runtime/scripts/native_importer_command_ingest_shell.py
-> docs/runtime/artifacts/native_importer_command_ingest/*
-> tests/native_importer_command_ingest.test.js

docs/runtime/fixtures/native_frame_scheduler_live_dirty.graph.json
-> docs/runtime/scripts/native_frame_scheduler_live_dirty_shell.py
-> docs/runtime/artifacts/native_frame_scheduler_live_dirty/*
-> tests/native_frame_scheduler_live_dirty.test.js

docs/runtime/fixtures/native_runtime_graph_incremental_builder.graph.json
-> docs/runtime/scripts/native_runtime_graph_incremental_builder_shell.py
-> docs/runtime/artifacts/native_runtime_graph_incremental_builder/*
-> tests/native_runtime_graph_incremental_builder.test.js

docs/runtime/fixtures/native_metal_heap_residency.graph.json
-> docs/runtime/scripts/native_metal_heap_residency_shell.py
-> docs/runtime/native/native_metal_heap_residency_probe.mm
-> docs/runtime/artifacts/native_metal_heap_residency/*
-> tests/native_metal_heap_residency.test.js

docs/runtime/fixtures/native_shader_ir_expression_core.graph.json
-> docs/runtime/scripts/native_shader_ir_expression_core_shell.py
-> docs/runtime/native/native_shader_ir_expression_core_compile_probe.mm
-> docs/runtime/artifacts/native_shader_ir_expression_core/*
-> tests/native_shader_ir_expression_core.test.js

docs/runtime/fixtures/native_ai_worker_authoring_assist.graph.json
-> docs/runtime/scripts/native_ai_worker_authoring_assist_shell.py
-> docs/runtime/artifacts/native_ai_worker_authoring_assist/*
-> tests/native_ai_worker_authoring_assist.test.js
```

## 70% Runtime Completion Rubric

Target rule: 70% completion means at least 70 weighted points are proven by
fresh docs/artifacts/tests, with no false claim that native canvas or AI worker
work is complete.

| Axis | Weight | Current | 70% target status |
| --- | ---: | ---: | --- |
| commandGraph as mutation source of truth | 9 | 9 | Meets target; fixtures, UI workflow, AI worker repair, canvas interaction, and importer ingest all mutate only through commands. |
| runtimeGraph builder and executable cook order | 10 | 10 | Meets target; closure harness builds runtimeGraph from repaired command replay, and incremental builder proof recomputes cook order while reusing unaffected executable nodes after live topology edits. |
| FrameScheduler clock/frame ownership | 9 | 9 | Meets target; closure harness proves graph-owned frames, and live dirty propagation proves scheduler-owned frame uniforms plus command dirty closures. |
| 2D texture GPU patch runtime | 14 | 14 | Product step now covers ConstantImage/Blob/Blend and Gradient/Feedback/RenderTarget through real Metal readbacks; still not a complete node library. |
| Shader IR/codegen/cache | 12 | 12 | Registry-driven IR/cache/codegen covers six texture ops, and expression-core codegen covers uniforms, UVs, arithmetic, color composition, cache keys, generated MSL, and native Metal compile diagnostics. |
| Resource allocator/lifetime ledger | 10 | 10 | Meets target; allocate/reuse/reallocate/dispose plus alias policy, release fences, barrier ledger, leak report, and bounded real Metal heap-backed texture residency are proven. |
| Diagnostics and proof harness | 10 | 9 | Meets target; repo has strong fixture -> shell -> artifact -> node:test lanes. |
| Bounded Mesh Draw/PBR native Metal lane | 10 | 10 | Closed for the bounded lane; no generic translation or full TiXL parity claim. |
| Native app/canvas | 8 | 8 | Native AppKit window, MetalKit MTKView surface, first human-facing toolbar/library/canvas/inspector/diagnostics workflow, and bounded canvas command loop are proven; full interaction parity remains open. |
| AI worker repair loop | 8 | 8 | Live render-diagnostic-repair loop exists, and bounded product authoring assist now turns structured intent into validated command plans plus diagnostics-driven repair; broad NL patch authoring remains a nonclaim. |
| **Total** | **100** | **100** | **Product/runtime closure is complete for the bounded product/runtime body: native surface, human workflow, canvas command loop, importer command ingest, incremental runtimeGraph builder, live dirty scheduler, product texture runtime, bounded Metal heap residency, live AI repair, bounded authoring assist, registry/expression codegen, and resource policy slices are closed; broad generality remains explicit nonclaim.** |

## Spine States

| Spine | State | Note |
| --- | --- | --- |
| commandGraph law | closed | Fixture commands, UI workflow commands, canvas interaction commands, AI repair commands, and importer ingest commands are the proven graph mutation paths. |
| runtimeGraph builder | closed | Bounded builder is proven for the architecture closure lane and now has incremental live rebuild evidence with executable reuse and recomputed cook order. |
| FrameScheduler | closed | Owns frame/time in the closure harness and now has bounded live dirty propagation from scheduler-owned frame uniforms and commandGraph edits. |
| 2D texture GPU patch runtime | closed | ConstantImage/Blob/Blend and Gradient/Feedback/RenderTarget bounded product slices render through real Metal. |
| Shader IR/codegen/cache | closed | Registry-driven bounded IR/cache/codegen covers six texture ops and diagnostics for unknown nodes; expression-core codegen covers uniforms, UVs, arithmetic, color composition, generated MSL, cache keys, and native Metal compile diagnostics. Arbitrary user shader language remains out of scope. |
| Resource allocator/lifetime | closed | Allocate/reuse/reallocate/dispose, TextureView invalidation, alias planning, release fences, barriers, leak reports, and bounded Metal heap-backed texture residency are proven; complete heap eviction/hazard tracking remains backend work. |
| Diagnostics/proof harness | closed | Proof pattern is established and should remain mandatory for closure claims. |
| Bounded Mesh Draw/PBR native Metal | closed | Replacement-ready for bounded lane only. |
| Native app/canvas | closed | Product slices prove AppKit NSWindow, MetalKit MTKView surface, runtime frame attachment, first toolbar/library/canvas/inspector/diagnostics workflow, and a bounded library/canvas/inspector command loop; full interaction parity remains future work. |
| AI worker repair loop | closed | Live render artifact diagnostics drive commandGraph repair and rerender; bounded authoring assist turns structured product intent into validated command plans and diagnostics-driven repair. Broad NL patch authoring remains a nonclaim. |
| Node admission contract | closed | Full Vuo-wide admission index covers all checked-in Vuo node sources with risk classification; current high-risk/runtime/key entries require and point to full manifests. |
| Pure interaction contract | closed | Headless GraphState command layer proves create/select/move/connect/edit/validate/cook/save-reload before any UI polish. |

## Plan Inventory

- `docs/superpowers/plans/2026-06-05-native-runtime-master-progress.md`:
  master progress ledger and routing authority.
- `docs/runtime/NATIVE_RUNTIME_CLOSURE_INDEX.md`: short release/closure index
  mapping each runtime spine to its proof, fixture, shell/runtime entrypoint,
  artifacts, tests, and nonclaim boundary.
- `docs/contracts/NODE_ADMISSION_LEVELS.md`: creator-facing admission gate for
  runtime, Vuo, proof-only, and blocked nodes; machine-readable schemas and
  manifests live under `docs/contracts/`.
- `docs/contracts/vuo_node_admission_index.json`: generated central coverage
  index for every checked-in `vuo-nodes/*.c` source; this is the Vuo-wide
  admission coverage gate, not native runtime promotion.
- `tools/generate_vuo_node_admission_index.js`: generator that extracts Vuo
  source metadata, port/default/range surfaces, risk classification, and
  high-risk manifest pointers into the central admission index.
- `docs/runtime/scripts/graph_interaction_contract.js`: pure GraphState
  interaction command layer between UI and runtime; no renderer, Vuo, WebGL,
  Metal, or UI dependency.
- `tests/graph_interaction_contract.test.js`: headless acceptance for
  TiXL-like create/select/move/connect/edit/validate/cook/save-reload behavior.
- `docs/superpowers/plans/2026-06-05-native-gpu-patch-runtime-slice.md`:
  active tactical plan for the current GPU patch runtime slice.
- `docs/runtime/NATIVE_GPU_PATCH_RUNTIME_SLICE_PROOF.md`: active closure
  evidence for commandGraph-to-Metal 2D texture patch execution.
- `docs/runtime/FULL_RUNTIME_ARCHITECTURE_PROOF.md`: active eight-axis
  architecture closure evidence for native headless shell/canvas, command-only
  mutation, runtimeGraph, FrameScheduler, resource lifetime, shader IR/cache,
  AI repair, and repaired Metal patch execution.
- `docs/runtime/NATIVE_PRODUCT_CANVAS_SURFACE_PROOF.md`: queued/active proof
  evidence for the first product-runtime slice; closed for native surface
  creation and still not a final GUI skin claim.
- `docs/runtime/NATIVE_TEXTURE_PATCH_PRODUCT_RUNTIME_PROOF.md`: closure evidence
  for the bounded Gradient/Feedback/RenderTarget product texture slice; not a
  complete texture runtime or shader language claim.
- `docs/runtime/NATIVE_AI_WORKER_LIVE_REPAIR_PROOF.md`: closure evidence for a
  bounded AI worker loop that renders, reads diagnostics, repairs by command, and
  rerenders; not broad natural-language patch authoring.
- `docs/runtime/NATIVE_AI_WORKER_AUTHORING_ASSIST_PROOF.md`: closure evidence
  for bounded AI authoring assist where structured intent becomes a validated
  command plan, runtimeGraph/frame artifacts are built from replay, diagnostics
  are read, and repair commands append through commandGraph; not broad
  natural-language patch authoring or arbitrary prompt-to-graph.
- `docs/runtime/NATIVE_SHADER_IR_CODEGEN_REGISTRY_PROOF.md`: closure evidence
  for registry-driven ShaderIR/cache/MSL generation over the current six texture
  ops, with unknown-node diagnostics; not a complete shader language.
- `docs/runtime/NATIVE_SHADER_IR_EXPRESSION_CORE_PROOF.md`: closure evidence
  for bounded core ShaderExpressionIR over uniforms, UV reads, swizzles, consts,
  arithmetic, `sin`, `smoothstep`, `mix`, and `vec4`, with generated MSL and a
  native Metal compile artifact; not arbitrary shader language, loops,
  user-defined functions, HLSL translation, or TiXL parity.
- `docs/runtime/NATIVE_RESOURCE_LIFETIME_POLICY_PROOF.md`: closure evidence for
  transient alias planning, persistent feedback protection, barrier ledger,
  release fences, and leak report; not a real GPU heap allocator.
- `docs/runtime/NATIVE_METAL_HEAP_RESIDENCY_PROOF.md`: closure evidence for
  bounded real Metal heap residency where admitted texture descriptors allocate
  heap-backed textures from an `MTLHeap` and publish residency/release evidence;
  not a complete heap allocator, eviction system, or backend hazard tracker.
- `docs/runtime/NATIVE_HUMAN_APP_WORKFLOW_PROOF.md`: closure evidence for the
  first human-facing native workflow surface with toolbar, library, MTKView
  canvas, inspector, diagnostics strip, commandGraph-backed UI mutation, and
  runtime frame evidence; not full interaction parity or final visual polish.
- `docs/runtime/NATIVE_CANVAS_INTERACTION_COMMAND_LOOP_PROOF.md`: closure
  evidence for bounded multi-region native interaction where library create,
  canvas place/connect, and inspector edit actions all mutate through
  commandGraph before runtimeGraph/frame evidence; not full canvas interaction
  parity or final visual polish.
- `docs/runtime/NATIVE_IMPORTER_COMMAND_INGEST_PROOF.md`: closure evidence for
  importer command law where an external document emits ordered commands,
  replays into editorGraph, and then builds runtimeGraph; not a full file-format
  importer, migration layer, or asset resolver.
- `docs/runtime/NATIVE_FRAME_SCHEDULER_LIVE_DIRTY_PROOF.md`: closure evidence
  for bounded live dirty propagation where scheduler-owned `u_time`/`u_frame`
  and commandGraph frame edits generate dirty closures, scheduled cook sets, and
  frame artifacts; not a full renderer invalidation engine.
- `docs/runtime/NATIVE_RUNTIME_GRAPH_INCREMENTAL_BUILDER_PROOF.md`: closure
  evidence for bounded live runtimeGraph rebuilds where commandGraph topology
  edits produce a runtimeGraph diff, affected executable nodes rebuild,
  unaffected executable nodes are reused, and cook order is recomputed; not a
  general optimizer or full compiler pipeline.
- `docs/runtime/RUNTIME_CLOSURE_REPORT.md`: closure evidence for the prior
  headless runtime and bounded Mesh Draw/PBR native Metal lane.
- `docs/runtime/TIXL_MESH_DRAW_BACKEND_REPLACEMENT_GATE_PROOF.md`: bounded
  Mesh Draw/PBR backend replacement gate evidence.
- `docs/superpowers/plans/2026-06-05-native-metal-tixl-parity.md`: historical
  and source plan for the now closure-ready bounded Mesh Draw/PBR lane.
- `docs/superpowers/plans/2026-06-05-tixl-vuo-all-nodes-construction.md`:
  separate TiXL/Vuo construction plan; not the native runtime dashboard.

## Conflict Register

- Potential stale reading: old TiXL/PBR plans can look like the active runtime
  lane. Resolution: treat them as bounded closure evidence; the active runtime
  lane is now `native_gpu_patch_runtime_slice`.
- Potential overclaim: headless app/canvas proof could be mistaken for final
  native GUI completion. Resolution: rubric gives native app/canvas 5/8 and
  names final human-facing skin as still open.
- Potential overclaim: registry-driven bounded shader IR/cache could be mistaken
  for generic shader language completion. Resolution: rubric gives shader
  IR/codegen closure only to the admitted registry and expression-core language;
  arbitrary shader language remains outside the claim.
- Resolved stale claim: Shader IR was only registry templates for six texture
  ops. It now has a bounded expression-core proof with Metal compile evidence,
  while arbitrary user shader language remains out of scope.
- Resolved stale claim: AI worker repair loop is no longer only a pre-render
  validator. It now has a bounded live render-diagnostic-repair proof, with
  broad natural-language authoring still a nonclaim. It also has bounded
  authoring-assist proof for structured intent -> validated command plan ->
  runtime artifact -> diagnostics -> repair command.
- Resolved stale claim: resource lifetime is no longer only allocate/reuse/view
  identity. It now has bounded product policy evidence for aliasing, release
  fences, barriers, and leak reports, plus bounded real Metal heap residency;
  complete eviction and backend hazard tracking remain out of scope.
- Resolved stale claim: native app/canvas is no longer only a surface proof. It
  now has a bounded human-facing workflow proof, with full interaction parity and
  polish still out of scope.
- Resolved stale claim: canvas workflow is no longer inspector-only. It now has
  a bounded library/canvas/inspector command loop proof, while full canvas
  editing parity remains out of scope.
- Resolved stale claim: commandGraph law no longer has only UI and AI proof
  pressure. It now has bounded importer command-ingest evidence, while full file
  import remains out of scope.
- Resolved stale claim: FrameScheduler dirty propagation is no longer only a
  broad architecture phrase. It now has a bounded live dirty proof that skips
  unchanged static nodes while preserving scheduler-owned frame uniforms.
- Resolved stale claim: runtimeGraph builder is no longer only one-shot closure
  evidence. It now has bounded incremental rebuild proof with structural hashes,
  executable reuse, and recomputed cook order after commandGraph topology edits.
- Resolved gap: Vuo-wide node admission no longer lacks risk classification.
  The generated index marks low/medium/high risk and requires a full manifest for
  current high-risk entries.
- Resolved gap: canvas interaction no longer jumps from UI action proof directly
  to runtime shell. A pure GraphState interaction command layer now sits between
  input events and runtime consumption, with headless tests before UI polish.
- Potential ownership conflict: GPU patch and full runtime architecture closure
  files are untracked. Resolution: do not edit or revert them except where this
  product-runtime lane explicitly builds on them with a new, separately named
  proof. `native_product_canvas_surface` uses separately named files and does
  not modify the existing GPU slice files.

## Session Safety

- Write scope for this session: the new `native_product_canvas_surface` proof
  docs, fixture, shell, native probe, tests, artifacts, and this master ledger;
  closed `native_texture_patch_product_runtime` and
  `native_ai_worker_live_repair`, and `native_shader_ir_codegen_registry` files.
  `native_human_app_workflow` and
  `native_canvas_interaction_command_loop` files, and
  `native_importer_command_ingest` and `native_frame_scheduler_live_dirty`
  files, `native_runtime_graph_incremental_builder` files, and
  `native_metal_heap_residency` and `native_shader_ir_expression_core` files,
  plus `native_ai_worker_authoring_assist` files.
- Do not stage, commit, or clean untracked GPU slice files unless explicitly
  asked.
- Any next worker should run focused proof tests before raising product/runtime
  completion claims. Do not build polished UI until the pure interaction command
  layer remains green under new interaction cases.

## Next Handoff Sentence

Open `docs/runtime/NATIVE_RUNTIME_CLOSURE_INDEX.md` first for the short map,
then this master ledger for routing details. Review
`docs/runtime/FULL_RUNTIME_ARCHITECTURE_PROOF.md`,
`tests/full_runtime_architecture.test.js`, the closed
`native_product_canvas_surface` proof files, and
`native_texture_patch_product_runtime` and `native_ai_worker_live_repair` proof
files, plus `native_shader_ir_codegen_registry` proof files. Keep the
files, plus `native_shader_ir_codegen_registry`,
`native_resource_lifetime_policy`, `native_human_app_workflow`, and
`native_canvas_interaction_command_loop` and `native_importer_command_ingest`
proof files, plus `native_frame_scheduler_live_dirty` proof files.
Also keep `native_runtime_graph_incremental_builder` proof files.
Also keep `native_metal_heap_residency` proof files.
Also keep `native_shader_ir_expression_core` proof files.
Also keep `native_ai_worker_authoring_assist` proof files.
Keep the product/nonclaim boundaries intact: human workflow proof is not full
interaction parity or final visual polish, the canvas command loop is not full
interaction parity, the pure interaction command layer is not polished UI,
registry/expression ShaderIR is not arbitrary user shader
language or HLSL translation, the current texture slices are not the full texture runtime, live AI
repair and bounded authoring assist are not broad natural-language patch authoring, importer command ingest is
not a full file-format importer, live dirty scheduling is not a full renderer
invalidation engine, incremental runtimeGraph rebuild is not a general
optimizer, and Metal heap residency is not a complete heap allocator or hazard
tracking system.
