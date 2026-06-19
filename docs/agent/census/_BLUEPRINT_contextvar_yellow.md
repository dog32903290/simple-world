# Build Blueprint: context-var YELLOW (flat var map + writer-before-reader) — block #1 of 柏為 6-seam directive

Scout output (Cut 87 session, 2026-06-20). Build order: **string-channel sub-seam FIRST → flat-map seam + 2-pass → 3 proving ops**. Single sequential lane (shared infra). Defaults-and-fork rules: 預設照 TiXL, fork 具名. RED tier deferred (scoped push/pop, WasTrigger, nested scopes).

## 0. Verified load-bearing facts
- GPU `EvaluationContext` is 16 bytes, `static_assert(sizeof==16)` `eval_context.h:39` → CANNOT carry a map. The var map lives HOST-side.
- TiXL var map: `EvaluationContext.cs:156-158` Dictionary<string,…> Bool/Int/FloatVariables; `Reset()` `EvaluationContext.cs:43-58` clears them (lines 51,53) once per top-level eval. YELLOW must clear the host map once per frame.
- Value rail is float-only end to end: `PortSpec` (`graph.h:27-39`) no string field; `Node::params` `std::map<string,float>` (`graph.h:77`); `SymbolChild::overrides` `std::map<string,float>` (`compound_graph.h:69`); `ResidentInput::constant` is float (`resident_eval_graph.h:47`); `evaluate(const float* in)` (`graph.h:47`). **NO string param channel anywhere** → the var name is a real sub-seam.
- Cook rail already hosts this shape: `cookStatefulValueNodes` (`frame_cook.cpp:179-205`) filters `isStatefulValueOp`, resolves Float inputs, steps per-path state, writes `extOut[0..7]`; `evalResidentFloat` reads extOut back via the no-evaluate path (`resident_eval_graph.cpp:58-65`). StopWatch/AudioReaction precedent. Dispatch table `kStatefulValueOps[]` (`stateful_value_ops.cpp:1196-1228`) is data-driven.

## 1. Seam design
### 1a. var-map home (Option A, recommended)
New POD in `stateful_value_ops.h` (next to TransportSnapshot, runtime POD, never touches GPU ctx):
```
struct ContextVarMap { std::map<std::string,float> floatVars; std::map<std::string,long> intVars; };
```
- Owner: function-local `static ContextVarMap` in `frame_cook.cpp run()` (mirror `s_svState`/`s_arState` ~line 282/292). NOT a runtime-global (keeps runtime→app dir clean).
- Thread `ContextVarMap& vars` into `cookStatefulValueNodes` → `cookStatefulValueOp(..., ContextVarMap*)`. Step-fn typedef gains `ContextVarMap*` (nullable, DEFAULTED nullptr so ~30 existing step fns + ~40 selftest call sites compile unchanged — same trick as `TransportSnapshot& tr = {}` `stateful_value_ops.h:76`).
- Set*Var output: TiXL output is a Command passthrough (no value-rail analog) → `extOut[0]` echoes written value (for golden); real product = map mutation.

### 1b. reset point
Clear `vars.floatVars/intVars` once per frame at the TOP of `cookStatefulValueNodes`, BEFORE the writer pass (the EvaluationContext.Reset analog). Pure per-frame scratchpad, no cross-frame leak.

### 1c. Get*Var read path
Get*Var = stateful-value op (evaluate=nullptr) in `kStatefulValueOps[]`. Step fn reads `vars->floatVars[name]` with FallbackDefault when absent (`GetFloatVar.cs:20-27`), writes `out[0]`. NO GPU-ctx touch, NO evaluate-signature change.

### 1d. writer-before-reader ordering (LOAD-BEARING)
TiXL ordering is structural (SubGraph pull). simple_world iterates `g.nodes` in build order, not dataflow → impose ordering explicitly. Split `cookStatefulValueNodes`'s single loop into:
```
// pass 0: clear map (Reset analog)
// pass 1: WRITERS — Set*Var only (isContextVarWriter predicate)
// pass 2: everything else (readers + all other stateful ops)
```
`isContextVarWriter(opType)` = small predicate (opType in Set*Var family). Guarantees all Set*Var before any Get*Var deterministically every frame.
**Boundary (flag to builder):** Set→Get→Set→Get chain within ONE frame is NOT supported (two passes = one write-generation). That needs topological/scope order = RED. Two passes = exactly one write-gen visible to all readers. Verify no Set*Var's VALUE input resolves through a Get*Var's extOut (else 2 passes insufficient).

### 1e. string-name channel (THE SUB-SEAM — bulk of the work, land FIRST)
Var name is `InputSlot<string>` (`SetFloatVar.cs:52`/`GetFloatVar.cs:58`, default "f"). Minimal addition (smallest blast radius), build as its own slice with a save/load roundtrip golden BEFORE the var ops:
1. `Node::strParams` `std::map<string,string>` (`graph.h:73-82`, parallel to `params` line 77), serialized in toJson/fromJson.
2. `SymbolChild::strOverrides` `std::map<string,string>` (`compound_graph.h:68-90`, parallel to `overrides` line 69), carried through projection.
3. `ResidentNode` carries resolved string params (`strInputs` map, or `ResidentInput.strConstant` for `dataType=="String"` ports).
4. `PortSpec.dataType=="String"` ports SKIPPED by every Float-only loop (`resolveResidentFloatInputs` filters `dataType=="Float"` `resident_eval_graph.cpp:126` → String falls through, zero regression).
5. `cookStatefulValueNodes` resolves node's String param(s) (read resident `strInputs["VariableName"]`) and passes name into step fn alongside `ContextVarMap*`.
Inspector text widget for editing name = separate UI slice OUT OF SCOPE (goldens drive name directly).

### 1f. files
Seam infra: `stateful_value_ops.h` (ContextVarMap + sig), `stateful_value_ops.cpp` (string-name+map dispatch, step fns, kTable rows, isContextVarWriter), `frame_cook.cpp` (split cookStatefulValueNodes into clear+writer+reader, static s_ctxVars), `graph.h`+`compound_graph.h`+`resident_eval_graph.h` (strParams/strOverrides/String-port resolution), `graph.cpp` toJson/fromJson + projection `resident_eval_flatten.cpp` (carry strParams through save/load + projection).
Leaf ops: step fns + kTable rows + `--selftest-contextvar` harness in `stateful_value_ops.cpp` (mirror runStatefulValueSelfTest line 1247); NodeSpec rows (evaluate=nullptr, String + Float ports) in node registry (`node_registry_math.cpp` — stateful ops use central registration per `value_op_registry.h:34-36`).

## 2. Named ordering fork
TiXL = scoped push/pop (`SetFloatVar.cs:26-41`: TryGetValue→set→SubGraph.GetValue→restore previous/remove). YELLOW = flat global map + 2-pass. Essential divergences (forced by locked GPU ctx + flat pull engine, no scope stack):
1. cross-sibling leakage (var visible to ALL Get*Var that frame, not just SubGraph branch; TiXL restores on exit `SetFloatVar.cs:33-40`).
2. no nested scopes/shadowing (same-name → last-writer-wins by g.nodes order in writer pass).
3. one write-generation per frame (no Set→Get→Set chain).
Proof it's essential not bug: faithful behavior requires the scoped SubGraph machinery = RED tier. **Faithful-for-global-VJ**: dominant usage = global broadcast (one Set near root, many Get scattered, no nesting/collision) → observationally identical to TiXL. Fork only bites nested-scope shadowing, which global-VJ doesn't use.

## 3. Proving consumers (3) — SetFloatVar + GetFloatVar + GetIntVar (optional 4th SetIntVar)
- **SetFloatVar** (`SetFloatVar.cs:14-46`, no-SubGraph branch 42-45): `vars->floatVars[name]=FloatValue; out[0]=FloatValue`. Empty-name → no-op (`cs:20-24`). Ports: VariableName(String,"f"), FloatValue(Float,0), Output(extOut[0] echo). DROP ClearAfterExecution/SubGraph (fork: no Command sub-tree).
- **GetFloatVar** (`GetFloatVar.cs:14-28`): `out[0] = found ? value : FallbackDefault`. Ports: VariableName(String,"f"), FallbackDefault(Float,0), Result(Float→extOut[0]). DROP ICustomDropdownHolder (editor UI).
- **GetIntVar** (`GetIntVar.cs:16-50`): read intVars, truncate `(long)std::trunc` (CountInt convention `stateful_value_ops.cpp:709`), fallback when absent. Ports: VariableName(String), FallbackValue(Float-carrying-int,0), Result(Float→extOut[0]). DROP LogLevels enum.
- (SetIntVar `SetIntVar.cs:22-64` no-SubGraph: `intVars[name]=(int)Value`; ports VariableName/Value/Output.)

### goldens (`--selftest-contextvar`, run through REAL cookStatefulValueNodes 2-pass, not mocked)
Tiny resident graph: SetFloatVar(name"x",val 3.5), GetFloatVar(name"x"), GetFloatVar(name"missing",fallback 9.0), SetIntVar(name"n",val 7), GetIntVar(name"n").
- A roundtrip: GetFloatVar("x")==3.5 (anti-orphan: value crosses seam writer→map→reader).
- B unset default: GetFloatVar("missing")==9.0.
- C ordering tooth: place GetFloatVar node EARLIER in g.nodes than its SetFloatVar writer → with 2-pass reads 3.5; injectBug collapses to single in-order loop → reads fallback → FAIL.
- D per-frame reset: frame1 SetFloatVar present → "x"=3.5; frame2 SetFloatVar node REMOVED → GetFloatVar("x") reads fallback (clear wiped stale); injectBug skips clear → reads stale 3.5 → FAIL.
- E int map: SetIntVar("n",7)→GetIntVar("n")==7; GetIntVar("unset",fallback 4)==4 (proves Int map independent of Float).

## 4. Risk callouts (verify before trusting)
1. **String channel is the single biggest risk** — must land strParams/strOverrides + String-port resolution + save/load roundtrip BEFORE var ops; do NOT smuggle name through a float port or path hack (refuter will catch).
2. Verify no Set*Var value input resolves through a Get*Var extOut (else 2 passes insufficient = RED). Add a selftest leg / build assert.
3. Reset clear at TOP of cookStatefulValueNodes, once/frame, NOT in per-node loop, NOT skipped on rebuild frame. cookStatefulValueNodes called once per run() (`frame_cook.cpp:293`).
4. Get*/Set*Var write only extOut[0]; Result/Output port must be FIRST output in NodeSpec (`resident_eval_graph.cpp:62-64` index mapping).
5. Step-fn sig change touches ~30 ops + ~40 selftest call sites (lines 1259-1809) — use defaulted/nullable params, zero diff to existing ops.
6. int truncation `(long)std::trunc` toward zero (7.9→7), NOT rounding (CountInt/HasIntChanged convention `stateful_value_ops.cpp:709,797`).

## 5. Anti-orphan: SetFloatVar writes 3.5 → shared map → GetFloatVar reads 3.5 → extOut[0] → readable via evalResidentFloat no-evaluate path = the rail a downstream wire consumes. Leg A asserts bit-exact. Not an orphan.

## 6. DEFERRED to RED (do not over-build): scoped push/pop engine (SubGraph branch `SetFloatVar.cs:26-41`), ClearAfterExecution/SubGraph ports, faithful WasTrigger, nested-scope shadowing/same-name restore/Set→Get→Set chains, ICustomDropdownHolder var-name dropdown UI, the other 11 var ops (GetBool/String/Vec3/Matrix/Object + GetForegroundColor[needs Vec3/foreground-color seam]) — cheap follow-on AFTER string channel + 2-pass + Float/Int maps exist.
