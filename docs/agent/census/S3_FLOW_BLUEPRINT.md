# S3 FLOW / CONTROL-FLOW — BUILD BLUEPRINT

> Read-only Plan pass, 2026-06-23. Spine seam **S3** per MASTER_PLAN.md (`flow / 控制流縫 = context-var + Execute/Loop`, ~35 nodes, touches the eval core). S1 (output-resolution) + S2a/b/c (render-graph collector + Group SRT + layer-compose) are DONE. file:line cited against HEAD at authoring time (14e5dd1) — re-confirm before editing.
> Mirror-structure target: `S2_RENDERGRAPH_BLUEPRINT.md`.

## 0. One-line verdict (the seam is NOT "build context-var")

**Half of S3 is already shipped.** The context-var dictionary (`ContextVarMap`, `stateful_value_ops.h:71-73`), its string-name channel (`strParams`/`strOverrides`/`strInputs`, `graph.h:90`, `compound_graph.h:101`, `resident_eval_graph.h:101`), the 2-pass writer→reader ordering and per-frame clear (`frame_cook.cpp:183-251,341-346`), and `--selftest-contextvar` all landed. Get/SetFloatVar + Get/SetIntVar live on the **value rail** (`cookStatefulValueNodes`), NOT the Command rail.

**The real, un-built S3 seam is the bridge between those two rails.** Every TiXL flow Command op (Loop, SetFloatVar's SubGraph branch, Switch, BlendScenes) mutates `EvaluationContext.*Variables` **inside the Command cook** and the children read it back **during that same cook**. But in sw the context-var map (`s_ctxVars`, frame_cook.cpp:344) lives in a pass that runs *before* and *separate from* the terminal Command cook, and **`CmdCookCtx` has no `ContextVarMap*` pointer at all** (`point_graph.h:128-160`). So today a Command op physically cannot write a var that its collected subtree will read.

The S3 seam is exactly: **thread the live `ContextVarMap` into `CmdCookCtx` and the two `cookCommand` lambdas, so a Command op can push/pop context vars around the subtree it collects — using the identical save/restore RAII pattern S1 already uses for `requestedResolution` in that very branch.** Loop additionally needs **per-iteration re-cook of the collected subtree** (the one genuinely new cook-core mechanism). That is the seam, the way S2 turned out to be "MultiInput Command collector," not "new draw pipeline."

## 1. TiXL ground truth (cited)

**EvaluationContext var dicts** (`external/tixl/Core/Operator/EvaluationContext.cs:156-168`): `BoolVariables`/`IntVariables`/`FloatVariables`/`ObjectVariables`/`StringVariables` (+ `Vec3`/`Matrix` in the Operators layer). `Reset()` (`:43-58`) clears all five once per top-level eval — sw mirrors this as the per-frame clear at the top of `cookStatefulValueNodes`.

**Loop** (`external/tixl/Operators/Lib/flow/Loop.cs:14-41`) — the keystone hard case:
```
end = Count.GetValue(ctx);
for (i = 0; i < end; i++) {
  ctx.FloatVariables[indexVar] = i;  ctx.IntVariables[indexVar] = i;
  ctx.FloatVariables[progressVar] = (end==1) ? 0 : i/(float)(end-1);
  DirtyFlag.GlobalInvalidationTick++;   // force re-eval
  Command.InvalidateGraph();            // mark subtree dirty
  Command.GetValue(ctx);                // RE-COOK the subtree with new var values
}
```
Three load-bearing facts: (a) **per-iteration var write** into both Float and Int dicts (`Loop.cs:25-26`); (b) **progress formula** `end==1 ? 0 : i/(end-1)` (`:27-35`) — closed-form golden fuel; (c) **per-iteration re-cook** via `InvalidateGraph()`+`GetValue()` (`:37-39`) — the subtree is cooked `end` times, each seeing a different `index`/`progress`. The `// TODO: may restore context variable after iterating` (`:21`) means TiXL itself **leaks** index/progress after the loop — faithful behavior is *no restore* (simplifies our mirror).

**SetFloatVar** (`external/tixl/Operators/Lib/flow/context/SetFloatVar.cs:26-45`): the **scoped** branch is `if (SubGraph.HasInputConnections)`: TryGetValue(previous) → set → `SubGraph.GetValue(ctx)` → restore previous (or Remove if it wasn't there and `!clearAfterExecution`). The no-SubGraph branch (`:43-44`) is the flat write sw already ships. **The SubGraph branch is the Command-rail context-var write S3 unlocks.**

**GetFloatVar** (`GetFloatVar.cs:14-28`): `Result = FloatVariables.TryGetValue(name) ? value : FallbackDefault`. Already shipped on value rail; the new thing is it must read the *same map* a Command-rail Loop/SetVar wrote during the Command cook.

**Switch** (`external/tixl/Operators/Lib/flow/Switch.cs:30-89`): collect N (`GetCollectedTypedInputs`), `index %= count` (wrap, negative-safe `:60-64`), cook **only** `commands[index]` (`:67`); `index==-2` => cook all; `index==-1`/empty => none. Pure selection — no re-cook, no var write. The cheapest Command-collector variant.

## 2. simple_world current state (post-S2)

**Already built (the context-var value-rail half — do NOT rebuild):**
- `ContextVarMap{ map<string,float> floatVars; map<string,long> intVars; }` (`stateful_value_ops.h:71-73`).
- 2-pass cook with per-frame clear + writer-before-reader ordering (`frame_cook.cpp:183-251`), owner = function-local `static ContextVarMap s_ctxVars` (`:344`).
- String-name channel end-to-end: `Node::strParams` (`graph.h:90`), `SymbolChild::strOverrides` (`compound_graph.h:101`), `ResidentNode::strInputs` (`resident_eval_graph.h:101`), serialized in graph.cpp toJson/fromJson + projected at flatten.
- `Get/SetFloatVar`, `Get/SetIntVar` as stateful-value ops; `isContextVarWriter` predicate; `--selftest-contextvar`.

**Already built (the Command-collector machinery S3 rides — from S2):**
- Flat `cookCommand` MultiInput Command branch with **S1 RAII save/restore already in it** (`point_graph.cpp:449-464`): `savedReq = requestedResolution` → mutate if SetRequestedResolution → cook subtree → `requestedResolution = savedReq`. **This is the exact push/pop shape S3 needs for context vars.**
- Resident mirror, same shape + same S1 guard (`point_graph_resident.cpp:490-509`).
- `CmdCookCtx` (`point_graph.h:128-160`) carries `inputCommand`, `inputTexture`, `points`, `params` — but **no `ContextVarMap*`**.
- Execute (`point_ops_execute.cpp:65-70`) + Group SRT-stamp (`point_ops_group.cpp:52-88`) shipped; per-item stamp pattern + `hasGroup`/`groupObjectToWorld` in `render_command.h`.
- Host-side per-cook scope state precedent: `Impl::requestedResolution` (`point_graph_internal.h:101`).

**The gaps S3 must close:**
1. `CmdCookCtx` gains `ContextVarMap* ctxVars` (mirrors `inputCommand`). Both `cookCommand` lambdas receive the live map (the same `s_ctxVars` instance) and pass it into `cc`.
2. A Command op (SetFloatVar-SubGraph / Loop) writes `ctxVars` **before** cooking the subtree branch and restores **after** — RAII, identical to the S1 `savedReq` block at `point_graph.cpp:453-462`.
3. **Loop only:** the subtree must be cooked **once per iteration**, each iteration writing index/progress first, concatenating the per-iteration items into the output chain. This is the one new mechanism — the collector's `cookCommand(subtree)` call moves inside a `for i<Count` loop.
4. **Lifetime/ownership:** `s_ctxVars` currently lives in `frame_cook.cpp` and is cleared/populated by `cookStatefulValueNodes` which runs as a distinct pass. S3 must make the **same** map instance visible to the terminal Command cook (pass a `ContextVarMap&` into `PointGraph::cook`/the resident run, threaded to the two `cookCommand` lambdas). Confirm pass ordering: value-rail writers populate the map, then the Command cook reads/augments it — verify the terminal Command cook runs **after** `cookStatefulValueNodes` in `run()` (`frame_cook.cpp:345` then terminal).

**Cook-core files that must change (owner-locked sequential spine):**
- `point_graph.h` — `CmdCookCtx::ctxVars` (additive shared-header field, like S2's `inputCommand`).
- `point_graph.cpp` — flat `cookCommand`: thread `ctxVars` into `cc`; Loop/SetVar-SubGraph write+RAII+re-cook branch.
- `point_graph_resident.cpp` — resident mirror (the blood-lesson leg — see §3).
- `app/src/app/frame_cook.cpp` — pass `s_ctxVars` into `PointGraph::cook`/resident run; confirm pass order.
- New leaf files: `point_ops_loop.cpp` (+ golden), `point_ops_switch.cpp`. SetVar-SubGraph extends the existing var-op leaves.

## 3. Flat-vs-resident mirror checklist (the S2b/S2c blood lesson — MANDATORY)

S2c found resident `cookCommand` was missing the Texture2D branch the flat path had → every textured layer drew black in production (production runs the **resident** leg). S3 has the **same trap, doubled**, because S3 adds *mutating* state into the same branch where S1's `savedReq` already lives. For every flow mechanism, the push/pop/re-cook MUST exist on BOTH legs:

| Mechanism | Flat leg (`point_graph.cpp`) | Resident leg (`point_graph_resident.cpp`) | Black-hole if mirror missed |
|---|---|---|---|
| `ctxVars` threaded into `CmdCookCtx` | set `cc.ctxVars = &vars` (~`:466-472`) | set `cc.ctxVars = &vars` (~`:511-517`) | Get*Var in a collected subtree reads stale/empty map → wrong value, silent |
| SetVar-SubGraph push/restore around subtree | mirror the `savedReq` block at `:453-462` for the var | mirror the resident `savedReq` block at `:494-507` | resident production never applies the scoped var → child renders with default, **prod-only black-hole** |
| Loop per-iteration var write + re-cook | new `for i<Count` wrapping the `cookCommand(sub)` collect call | **same `for` loop in the resident extraConns gather** (`:498-506`) | flat golden green, resident (production) cooks subtree ONCE with index=last → only the final iteration's layer draws |
| Per-frame var clear (Reset analog) | already at top of `cookStatefulValueNodes` (shared) | shared (same `s_ctxVars`) — verify the SAME instance reaches both `cook` legs | one leg sees a fresh map, the other a leaked one → cross-leg divergence |
| Switch index selection | cook only `commands[index]` in the collector loop | cook only the index-th of (primary wire + extraConns) | resident wires are stored as primary+`extraConns` (`:499-505`) — index math MUST account for that split or off-by-one selects wrong branch |

**Golden discipline (non-negotiable):** every S3 golden runs through `PointGraph::cook` on **both** the flat path and the resident path (the `--selftest-group`/`--selftest-layercompose` precedent ran both legs). A flow golden that only exercises flat is a S2c-style black-hole waiting to ship. The resident `-bug` leg must be a distinct assertion.

## 4. Staging (ordered by unlock-value ÷ risk)

### S3a — Command-rail context-var bridge + SetVar-SubGraph (the keystone, smallest, highest leverage)
- Add `CmdCookCtx::ctxVars` (`point_graph.h`); thread `&s_ctxVars` through `PointGraph::cook` → both `cookCommand` lambdas → `cc.ctxVars`.
- Implement **SetFloatVar / SetIntVar SubGraph branch** as Command ops: when the node has a wired Command (SubGraph) input, in the Command branch do `auto prev = vars->floatVars; vars->floatVars[name] = value;` → cook subtree → restore prev/remove (faithful to `SetFloatVar.cs:26-45`, including `clearAfterExecution`). **Reuse the exact RAII shape of the S1 `savedReq` block** (`point_graph.cpp:453-462`) — same save/mutate/cook/restore.
- **Proving op:** SetFloatVar-with-SubGraph feeding a Layer2d whose param is driven by a GetFloatVar.
- **Golden `--selftest-setvar-scope`:** SetFloatVar("k", 0.7, SubGraph=LayerA), where LayerA's opacity/color reads GetFloatVar("k"). Closed-form: the collected layer's draw item carries the value 0.7 written by the Command-rail SetVar (not the value-rail default). `-bug`: skip the Command-rail write → child reads fallback → RED. Run BOTH legs (resident `-bug` = forget to mirror the write → prod-only RED).

### S3b — Switch (pure selection, no re-cook, low risk — proves the collector can sub-select)
- New `point_ops_switch.cpp`: the collector cooks only `commands[index % count]` (wrap, negative-safe per `Switch.cs:60-64`); `index==-2` => all; `-1`/empty => none. Count output = N.
- **Golden `--selftest-switch`:** wire LayerRed[0], LayerGreen[1], LayerBlue[2] into Switch; Index=1 => center pixel green; Index=2 => blue; Index=4 => green (wrap, 4%3=1); Index=-1 => black. `-bug`: ignore index / cook-all → center wrong → RED. Both legs (resident index math over primary+extraConns is the off-by-one trap from §3).

### S3c — Loop (the hard keystone, re-cook mechanism — highest value, highest risk)
- New `point_ops_loop.cpp`: in the Command branch, read `Count`/`IndexVariable`/`ProgressVariable`; `for i in [0,Count)`: write `vars->floatVars[index]=i; vars->intVars[index]=i; vars->floatVars[progress]= Count==1?0:i/(Count-1)` (exact `Loop.cs:25-35`), then cook the subtree branch and **concatenate that iteration's items** into the output chain. Faithful **no-restore** after the loop (TiXL `Loop.cs:21` TODO leaks them).
- This is the one cook-core change beyond threading: the `cookCommand(sub)` collect call moves **inside** a per-iteration loop. `InvalidateGraph()`/`GlobalInvalidationTick` are TiXL's dirty-broadcast for re-eval; sw cooks fresh each call already (no memoized Command output across the loop body — **verify** no per-node cook memo short-circuits the 2nd iteration; if one exists it must be bypassed inside the loop, FLAG).
- **Golden `--selftest-loop`:** Loop(Count=3, IndexVariable="i", SubGraph=Layer translated by GetFloatVar("i")*0.25). Closed-form: 3 draw items in the chain, item k's groupObjectToWorld/translate-X = k*0.25 (i=0,1,2). Progress golden: Count=4 => progress=[0, 1/3, 2/3, 1]; Count=1 => progress=0 (the `end==1` branch). `-bug` (a): cook subtree once (drop the for-loop) => 1 item => RED. `-bug` (b): write index but don't re-cook (reuse first cook) => all 3 items identical translate => RED. Both legs.

**Landing order: S3a → S3b → S3c.** S3a is the bridge everything else needs; S3b proves sub-selection cheaply and de-risks the resident index math before Loop's harder re-cook; S3c is the apex. (Mirrors S2a→S2c→S2b: keystone first, cheap prover second, hard transform/iteration last.)

## 5. TiXL faithfulness anchors (exact files/line-ranges to match)

| Concern | TiXL source | What to match |
|---|---|---|
| Var dict semantics + per-frame clear | `Core/Operator/EvaluationContext.cs:156-168` (dicts), `:43-58` (Reset clears all once/eval) | sw clears once at `cookStatefulValueNodes` top — already faithful; keep S3 reads/writes against the same instance |
| SetVar scoped push/restore | `flow/context/SetFloatVar.cs:26-45` | TryGetValue(prev) → set → cook SubGraph → restore prev / Remove-if-absent (honor `clearAfterExecution`) |
| Get fallback | `flow/context/GetFloatVar.cs:19-27` | `TryGetValue ? value : FallbackDefault` (already shipped) |
| Loop iteration model | `flow/Loop.cs:23-40` | write index→Float+Int, progress=`end==1?0:i/(end-1)`, cook subtree per i, **no restore after** (`:21` TODO) |
| Switch selection | `flow/Switch.cs:34-68` | `index%=count` negative-safe; `-2`=all, `-1`/empty=none, else cook only index-th |
| ObjectToWorld scope (for SetVar over Group-wrapped subtree nesting) | already mirrored by S2b per-item stamp, `point_ops_group.cpp:76-86` | context-var scope nests the same innermost-first way; verify SetVar-over-Loop-over-SetVar composes |

## 6. Risk list (silent-corruption traps)

1. **Resident-leg divergence (the S2c blood lesson, restated for mutating state).** S1's `savedReq` and S2c's Texture2D branch were each missed on one leg once. S3 adds a *write* into the same branch — a resident-only miss means production silently renders children with default vars while flat golden is green. **Every S3 golden runs both legs; resident `-bug` is a separate tooth.**
2. **Scope leak across iterations / siblings.** SetVar must restore prev/Remove after its SubGraph (`SetFloatVar.cs:33-40`); skipping it leaks the var to later siblings in the same Execute. Loop deliberately does NOT restore (faithful), but that means index/progress leak after the loop — a sibling Get*Var after a Loop sees the last index. **Match TiXL exactly: SetVar restores, Loop leaks.** Don't "fix" Loop's leak (it would diverge from `Loop.cs:21`).
3. **Two-rail split — which map wins.** The value-rail `cookStatefulValueNodes` writes `s_ctxVars` in its own pass; the Command-rail Loop/SetVar also writes it. If a value-rail Get*Var reads *after* a Command-rail Loop leaked an index, results depend on pass order. **Pin pass order** (value-rail writers → Command cook) and document that a value-rail Get*Var cannot see a Command-rail Loop's per-iteration index.
4. **Loop re-cook memoization.** If any per-node cook memo caches a Command/Points subtree across calls within one frame, Loop's 2nd+ iterations reuse iteration-0's geometry → all items identical (silent). **Audit `feedbackCooked`/`outCount`/any per-id memo** in `cookCommand`/`cookNode`; inside the Loop body the subtree must re-cook fresh. FLAG for builder.
5. **Context-var shadowing / nesting.** SetVar("k") over a subtree containing another SetVar("k") must restore the inner's prev correctly (innermost-first, like S2b group nesting). The RAII save/restore DOES give correct nesting **if** each SetVar saves before and restores after its own subtree. Test a SetVar→SetVar(same name)→Get nesting golden.
6. **Loop `Count` from a dynamic input.** `Count.GetValue(ctx)` is an int input; if wired to a value-graph node it must be resolved *before* the iteration loop (cook the Count input once). Resolving it per-iteration could re-enter the loop. FLAG.
7. **String-name resolution on the Command rail.** Index/Progress/Variable names are String inputs (`strInputs`). The Command cook must resolve them via the same `strInputs["IndexVariable"]` channel the value rail uses (`resident_eval_graph.h:101`) — don't smuggle through a float port.

## 7. Which of the ~35 flow nodes each stage unlocks

The 15 context-var **value-rail** Get/Set ops (FloatVar/IntVar shipped; Bool/String/Vec3/Matrix/Object/GetForegroundColor are cheap value-rail follow-ons gated only on their respective type seams, NOT on S3 cook-core) are already unblocked by the shipped context-var work.

| Stage | Directly unlocks | Count |
|---|---|---|
| **S3a** (ctxVars→CmdCookCtx + SetVar-SubGraph) | SetFloatVar, SetIntVar, SetBoolVar, SetStringVar, SetVec3Var, SetMatrixVar, SetObjectVar (the **Command/SubGraph** half of all 7 Set*Var); SetRequestedResolutionCmd (scope-complete) | ~8 |
| **S3b** (Switch selection) | Switch; BlendScenes (MultiInput Command + blend — selection-with-alpha); ExecuteOnce, LogMessage (Execute-family gates over a SubGraph) | ~4 |
| **S3c** (Loop re-cook) | Loop; ExecRepeatedly (re-cook N times = Loop without var injection); ResetSubtreeTrigger (re-cook + invalidate) | ~3 |
| **Cascade after S3** (need adjacent seams) | TimeClip (+transport time-remap), GetPosition (+camera3d ObjectToWorld read), ExecuteRawBufferUpdate (+RWStructuredBuffer), the 5 skillQuest ops (separate system) | ~10 (deferred, per-op secondary seams) |

S3a+S3b+S3c directly unblock ~15 flow Command ops; combined with the already-shipped 15 value-rail context-var ops, this closes the bulk of the ~35-node flow island. The remaining ~10 cascade as transport/camera3d/RWStructuredBuffer land.

## 8. Critical files for implementation
- `app/src/runtime/point_graph.cpp` (flat `cookCommand` Command branch `:449-464` — thread `ctxVars`, add SetVar-scope + Loop re-cook)
- `app/src/runtime/point_graph_resident.cpp` (resident mirror `:490-509` — the blood-lesson leg, must mirror every write/re-cook)
- `app/src/runtime/point_graph.h` (`CmdCookCtx` `:128-160` — add `ContextVarMap* ctxVars`)
- `app/src/app/frame_cook.cpp` (`:341-346` — pass the live `s_ctxVars` into `PointGraph::cook`; pin value-rail→Command pass order)
- `app/src/runtime/stateful_value_ops.h` (`ContextVarMap` `:71-73` — the shared map type both rails now write)
- ref-only: `external/tixl/Operators/Lib/flow/Loop.cs:23-40` + `context/SetFloatVar.cs:26-45` + `Switch.cs:34-68` (iteration / scoped-write / selection ground truth)
