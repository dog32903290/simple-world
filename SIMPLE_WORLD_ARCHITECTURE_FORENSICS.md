# Simple World Architecture Forensics

**Status:** audit handoff, not an implementation specification

**Baseline inspected:** `a10ff633be8d453f4e08a06abb15c5102552183b` (`main`, 2026-07-16)
**Purpose:** make structural risks visible before more TiXL-parity work extends them.

## Executive finding

The production direction is coherent:

```text
SymbolLibrary → ResidentEvalGraph → PointGraph::cookResident()
```

The risk is not that the repository has no architecture. Its documented five-zone dependency rule and isolated selftests are real strengths. The risk is that compatibility, import, and test paths still contain alternative ways to identify, transform, and execute the same operator. A locally correct change can therefore be routed through a different implementation than production, or preserve a lossy transformation without making that loss visible.

This report separates **verified code facts** from **audit hypotheses**. A remediation PR must validate a hypothesis against the current branch before changing behavior. Do not treat a historical observation as proof that the current implementation is wrong.

## Scope and evidence standard

This is a structural audit of repository code and tools, not a claim of TiXL output parity. It inspected the code paths named below and the repository state at the baseline commit. It did not run the macOS/Metal build or compare rendered output with a TiXL executable.

Evidence labels:

| Label | Meaning |
| --- | --- |
| **Verified** | Directly visible in the baseline source or tooling. |
| **Risk** | A failure mode enabled by the verified design; it needs a reproducer or census to quantify. |
| **Hypothesis** | A prior audit concern that needs current-branch validation before it becomes a finding. |

## System map

```text
.t3 / .swproj / .swpkg
            │
            ▼
       SymbolLibrary  ◄── document/UI commands
            │
            ├── refreshCompoundSpecs → dynamic NodeSpec table
            ▼
     buildEvalGraph
            ▼
     ResidentEvalGraph ──► PointGraph::cookResident()  (production)

Legacy/test bridge:
Graph ──► PointGraph::cook()
  └────► libFromGraph() ──► ResidentEvalGraph ──► cookResident()
```

The desired direction is one editable document model and one production cook route. Audit work should harden the boundaries around that direction, rather than replace it with a large new framework.

## Findings

### F-01 — Node resolution is an ordered, multi-owner search

**Severity:** P0 — prevent new ambiguity

**Confidence:** verified design; collision impact requires census

`app/src/runtime/node_registry.cpp` implements `findSpec()` as an ordered scan through the fixed registry, numerous live self-registration sinks, then `dynamicSpecs()`. The first equal type name wins. `dynamicSpecs()` is rebuilt from compound symbols and is intentionally last, so a built-in takes precedence over a compound name collision.

This order is a real compatibility rule, but it makes ownership implicit. The current `--selftest-specdedup` is valuable: it detects duplicate names in `specTypes()`, including a synthetic injected duplicate. It does **not** by itself answer all of these questions:

- Which owner won when a type is available in different lookup sources?
- Is a dynamic compound deliberately shadowed by an active atom, a legacy alias, or a mistake?
- Does every resolved type have the intended cook route?
- Is a persisted/imported identity allowed to resolve to a different implementation after a registry change?

Relevant evidence:

- `app/src/runtime/node_registry.cpp`: `findSpec()`, `specTypes()`, `dynamicSpecs()`.
- `app/src/selftests_registry_dedup.cpp`: existing duplicate-name gate.
- `app/src/runtime/t3import_*_retire_golden.cpp`: retirement tests explicitly exercise the atom-before-dynamic shadow polarity.

**Risk:** a newly registered atom can silently take precedence over a dynamic compound with the same human-facing name. Existing tests cover selected retirements; they are not a global ownership ledger.

**Minimum safe intervention:** add a read-only registry census before changing lookup behavior. The census should enumerate every offered `NodeSpec` with:

```text
type | owner/source | implementation kind | lookup priority | cook route | alias/canonical identity
```

It should fail for unapproved duplicate canonical identities, undocumented shadowing, or a spec with no valid execution route. Keep explicit, tested legacy takeover cases as allowlisted records, not accidental ordering.

### F-02 — Flat and resident cook drivers duplicate orchestration

**Severity:** P0 — contain divergence

**Confidence:** verified two drivers; production-vs-flat gaps require census

`PointGraph::cook(const Graph&, ...)` in `app/src/runtime/point_graph.cpp` and `PointGraph::cookResident(const ResidentEvalGraph&, ...)` in `app/src/runtime/point_graph_resident.cpp` are both substantial drivers. Production frame orchestration in `app/src/app/frame_cook.cpp` projects `doc::g_lib()` into a `ResidentEvalGraph` and calls `cookResident()`.

The two drivers share some leaves and helpers, but both contain traversal, parameter resolution, memoization, bypass/feedback behavior, and multiple data-currency gathering paths. The repository already contains tests that send a flat `Graph` through `libFromGraph() → buildEvalGraph() → cookResident()`, showing the intended convergence path.

**Risk:** a feature can be complete on the flat test leg while incomplete on the resident production leg, or vice versa. A test that validates only one driver cannot establish the other driver's parity.

**Minimum safe intervention:** do not delete the flat driver in one refactor. First produce a route census for every selftest and supported operator:

```text
flat only | resident only | both, separate assertions | flat → resident adapter
```

Then enforce a rule for new features: resident execution is required; flat coverage is either a compatibility test or an adapter to the resident path, never a new independent orchestration feature.

### F-03 — The importer is a lowering pipeline without a first-class report

**Severity:** P0 — prevent silent partial imports

**Confidence:** verified

`app/src/runtime/t3_import.cpp` does more than parse `.t3`: it maps GUIDs to Simple World types, skips helper nodes, folds ComputeShader source and render state, reanchors connections, refines input types from destination `NodeSpec`s, recurses into compounds, and can collapse a root into one atom. Related passes are split among `t3_import_collapse.cpp`, `t3_import_texcompute.cpp`, `t3_import_renderstate.cpp`, and `t3_import_srvtexfold.cpp`.

This lowering is a reasonable porting strategy. The structural issue is observability: many unsupported or unresolved elements call `warn(...)` and continue, while a successful import currently returns a boolean. A caller cannot distinguish an exact import from one that removed or rewrote material graph structure.

Relevant evidence:

- `app/src/runtime/t3_import.cpp`: `tryCollapseRoot()`, ComputeShader elision, render-state fold, connection construction, `appendSrvFromTexFold()`, `return true` after warning-capable paths.
- `app/src/runtime/t3_import_texcompute.cpp`: root-collapse choices.
- `app/src/runtime/t3_import_renderstate.cpp` and `t3_import_srvtexfold.cpp`: semantic rewrites.
- `app/src/runtime/t3_import_maps*.cpp`: GUID/type mapping data.

**Risk:** a partially imported compound remains usable enough to render, while dropped nodes or connections are only console warnings. The next save can then normalize the reduced graph as though it were exact.

**Minimum safe intervention:** retain current behavior initially, but change the import result from a boolean-only success signal to a structured report. At minimum, record source/emitted/dropped children and connections, every rewrite rule, and each warning with source identity. Classify results as `Exact`, `Rewritten`, `Lossy`, or `Rejected`.

Every collapse/fold rule should record provenance, for example:

```text
source child GUID → removed/folded into destination path.slot → rule identifier
```

### F-04 — Operator identity is distributed across several maps

**Severity:** P0 — prevent incompatible routing

**Confidence:** verified distributed representations; exact conflict set needs audit

An operator can be known by a TiXL GUID, an importer mapping, a runtime `NodeSpec.type`, a dynamic compound ID, a persistence UUID, a display name, and a legacy alias. The source locations are intentionally separate because they serve different stages, but the repository has no demonstrated machine-readable ledger that proves their compatibility as a set.

**Risk:** an imported graph, an Add-menu node, and a reloaded legacy document can each reach different implementations for what a user regards as the same operator.

**Minimum safe intervention:** create an identity audit that emits a row per canonical operator and verifies that imported, persisted, and runtime identities resolve to the declared active implementation. Entries that are compound-only, retired, or import-unsupported must be explicit classifications, not missing rows.

### F-05 — SymbolLibrary mutation is coupled to manual invalidation

**Severity:** P0 — prevent stale projections and dirty state

**Confidence:** verified

`doc::g_lib()` exposes a mutable global `SymbolLibrary&`. `libRevision()` is a separate counter and `bumpLibRevision()` is a separate call. The revision drives resident-graph rebuilds. `document_io.cpp` also documents direct-write paths that must call `invalidateDirtyCache()` because they bypass a revision bump.

The existing comments make the contract visible, but it remains easy to violate: modifying the library, bumping projection revision, and invalidating the dirty cache are separate actions at many call sites.

Relevant evidence:

- `app/src/app/document.cpp`: mutable `g_lib()` and separate `bumpLibRevision()`.
- `app/src/app/document_io.cpp`: revision-keyed dirty cache and explicit bypass invalidation contract.
- UI/app call sites that directly mutate the library and call `bumpLibRevision()`.

**Risk:** a document edit can update only some of document persistence, dirty indication, and resident projection.

**Minimum safe intervention:** begin with a mutation-call-site census. Then introduce a narrow mutation gateway (or command-level equivalent) that owns revision bump, dirty invalidation, and a diagnostic reason. Do not change every caller at once; migrate a bounded family and add a test that proves each notification occurs.

### F-06 — Load repair and serialization sanitization can hide data loss

**Severity:** P1 — make loss actionable

**Confidence:** verified

The document loader accepts warnings/repairs and installs the resulting library; the UI status appends a repaired count. Separately, `compound_save.cpp` converts non-finite floats to `0.0` to keep JSON valid.

Both choices avoid a broken document workflow, but their current presentation does not distinguish a clean open/save from a repaired or sanitized one.

**Risk:** a user can overwrite the source document after a lossy repair or unknowingly persist a zero in place of a NaN/Inf symptom.

**Minimum safe intervention:** introduce load/save result classes and a repair report. A lossy load should require an explicit Save As rather than silently becoming the new source. Serialize non-finite values only together with a machine-readable diagnostic identifying symbol, slot, and replacement.

### F-07 — CI and status truth need an independent gate

**Severity:** P1 — make local proof repeatable

**Confidence:** verified at baseline

At the baseline, the repository contains local tools for `check_arch`, status stamping, golden linting, and sweeping selftests, but no `.github/workflows/` files. The plan also describes generated status data and distinguishes it from historical prose, which is necessary because prose can become stale.

**Risk:** a commit may carry convincing local evidence without an independent clean checkout reproducing it. Status documents can also retain stale descriptive numbers even when a stamped block changes.

**Minimum safe intervention:** add CI only after identifying the supported macOS runner and dependencies. The first workflow should be conservative:

1. clean configure/build;
2. `tools/check_arch.sh`;
3. `tools/run_all_selftests.sh --bite` where runner capability permits;
4. status/document consistency checks that fail on multiple contradictory census or HEAD claims.

Do not claim CI will establish TiXL parity; it establishes repeatability of Simple World checks.

### F-08 — Internal oracles need an explicit external-parity boundary

**Severity:** P1 — avoid overclaiming parity

**Confidence:** audit hypothesis, supported by architecture

The repository has extensive golden and injected-bug testing. These tests are useful and should remain the primary local regression net. They cannot automatically prove that a shared importer/cook/oracle interpretation matches TiXL unless the expected result is grounded in an independent source.

**Required validation:** classify existing parity tests by oracle:

```text
independent TiXL fixture/output | source-derived CPU oracle | Simple World internal invariant | smoke test
```

Prioritize external fixture/output comparisons for semantic rewrites and root collapses, where Simple World intentionally replaces a TiXL subgraph with an atom.

## Prioritized remediation plan

The order below creates visibility before behavior change. Each phase should be a small PR with one owner, one test gate, and no incidental refactor.

| Phase | Objective | Deliverable | Exit criterion |
| --- | --- | --- | --- |
| 0 | Freeze the evidence | This report plus baseline command record | No behavior change; report links resolve and `git diff --check` is clean. |
| 1 | Expose registry/identity ambiguity | Registry + identity census in selftest/CI-readable form | Every active type has an owner and route; approved shadows are explicit. |
| 2 | Expose import loss | `ImportReport` and result classification | Fixtures prove exact, rewritten, lossy, and rejected outcomes distinctly. |
| 3 | Contain state mutation | Mutation call-site census, then a narrow mutation gate | Migrated edits update library, revision, and dirty state together. |
| 4 | Stop cook-driver expansion | Flat/resident route census and admission rule | New production feature has resident coverage; no new flat-only orchestration. |
| 5 | Make proof repeatable | CI plus status consistency check | Clean runner reproduces selected build and audit gates. |

## First implementation PR: audit-only contract

The first code PR should not rewrite registry lookup, importer lowering, or cook traversal. Its target is a single audit command, for example `--selftest-architecture-census`, that returns non-zero on machine-detectable ambiguity.

Minimum output schema:

```text
TYPE type=TYPE_ID owner=SOURCE priority=ORDER kind=atom|compound|alias cook=ROUTE
IDENTITY canonical=NAME runtime=TYPE_ID tixl_guid=GUID persistence=UUID status=active|retired|compound-only
IMPORT_MAP guid=GUID type=TYPE_ID registry=ok|missing route=ok|missing
```

It must be deterministic, checked into no generated artifact, and have a negative test that proves a synthetic collision fails. This is intentionally narrower than a registry rewrite.

## Codex validation checklist

Use this checklist before accepting any remediation claim.

1. **Re-establish the baseline.** Confirm branch, `HEAD`, clean status, remote default branch, and any changed architecture instructions.
2. **Verify each cited path.** Re-read the specific source function before acting; this report is a map, not the source of truth.
3. **Classify the finding.** Mark it verified, disproved, or changed since baseline. Do not carry a historical assertion forward untested.
4. **Choose one seam.** State the requested behavior, owner, inputs, outputs, failure mode, and exact verification command.
5. **Keep the blast radius bounded.** Do not combine registry ordering, importer rewrites, persistence migration, and cook convergence in one change.
6. **Test both polarities.** A gate needs a green case and an injected/fixture red case that demonstrably fails for the intended reason.
7. **Run the closest available checks.** At minimum run `git diff --check`, the focused selftest, `tools/check_arch.sh`, and the relevant build/sweep when the macOS/Metal environment is available.
8. **Record evidence in the PR.** Include command, exit status, and whether it ran locally, in CI, or could not run due to environment.
9. **Update the plan truthfully.** Do not stamp a status ledger as complete until the actual code and proof are merged or present on the branch.

Suggested baseline commands:

```bash
git status -sb
git rev-parse HEAD
tools/check_arch.sh
cd app && cmake -S . -B build && cmake --build build -j
../tools/run_all_selftests.sh --bite
```

The final two commands require the repository's macOS/Metal dependencies. If unavailable, record that as an environment limitation, not a passing result.

## Non-goals and guardrails

- Do not remove `Graph` or `PointGraph::cook()` before the route census shows what still relies on them.
- Do not alter first-match lookup semantics before every intentional takeover is represented and tested.
- Do not make importer warnings fatal globally before fixtures distinguish acceptable rewrites from true loss.
- Do not serialize a new canonical identity table without an explicit migration/readback plan.
- Do not use file length alone as evidence that `frame_cook` or another coordinator has a single responsibility.
- Do not equate green internal goldens with verified TiXL equivalence.

## Definition of architectural progress

This audit is successful only when a future contributor can answer, mechanically:

1. Which implementation owns this operator and why?
2. Which execution driver runs it in production and in each test?
3. What did importing this `.t3` preserve, rewrite, or drop?
4. Which mutation invalidates the resident projection and marks the document dirty?
5. Which checks reproduced this conclusion from a clean checkout?

Until those answers are generated or enforced by the repository, this document is a useful warning map—not a substitute for the gates it recommends.
