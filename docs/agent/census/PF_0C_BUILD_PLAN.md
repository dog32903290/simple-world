# PF-0c Build Plan — generalize the field param-apply (data-driven, setter-lambda)

> Verified against HEAD 2ed360a on `sw-parity-lane` (2026-06-24). Plan-agent scoping. Supersedes the one-off `configureToroidalVortexFieldFromParams`.

## Verdict & scope
**PF-0c (this build) = data-driven float/enum/bool param dispatch** for field ops whose params are `{float | int-selector | bool-selector}`. **PF-0d (deferred, register null configurers now)** = matrix/string/texture params (`TransformField.transform[16]`, `CustomSDF.distanceFunction` string, `Image2dSDF.texture`) — these can't flow through `map<string,float>`; do NOT smuggle them in.

**This build's op coverage:** infrastructure + migrate the cascade-relevant ops only: **SphereSDF, BoxSDF, TorusSDF, CombineSDF, ToroidalVortexField**. All other float ops: leave as-is for a later mechanical fan-out (they keep working at ctor defaults — same as today, no regression). Matrix/string/texture ops: explicit null configurer.

## Current mechanism (cited)
- `field_graph.cpp:98-104` `collectAllParams` walks tree depth-first calling each node's `collectParams`; `field_graph.cpp:234` `assembleFieldMSL` calls it — **the ONLY producer of `AssembledField.floatParams`**. Each leaf packs from its own C++ members (ctor-seeded to .t3 defaults), e.g. `field_ops_spheresdf.cpp:61-67` packs `centerX/radius` (defaults `spheresdf.cpp:36-37`).
- Graph→param runs EARLIER at tree-build: `field_graph_builder.cpp:35-36`(flat)/`:62-63`(resident) call `configureFieldNodeFromParams(*node,type,*p)` BEFORE assembly.
- `field_node_registry.cpp:29-34` `configureFieldNodeFromParams` is a **one-line if-ladder**: only `ToroidalVortexField` handled; all 37 others = silent no-op → keep ctor defaults. `field_ops_toroidalvortexfield.cpp:260-272` is the only `*FromParams` impl (`dynamic_cast` + `pick(m,"Center.x",n->centerX)` per field, fallback=current value; `Axis` enum read as `(int)(pick+0.5f)`).
- **The gap: for all non-Toroidal ops, non-default graph params never reach `collectParams`.** Infra (resolver→builder→configure→members→collectParams) is fully wired; only per-op dispatch+apply missing.

## Design — table + builder (ARCHITECTURE rule 7), setter-lambda (NOT offsetof)
Keep leaf types TU-private (frozen-base invariant). Each op registers a configure-fn alongside its factory.

New registry surface (`field_node_registry.h`/`.cpp`):
```
using FieldConfigureFn = void(*)(FieldNode& node, const std::map<std::string,float>& params);
std::vector<std::pair<std::string, FieldConfigureFn>>& fieldConfigurers();   // sink, mirrors fieldNodeFactories()
```
- `FieldOp` ctor gains an overload taking the configurer → pushes `{spec.type, configurer}`. Ops passing nothing get a **null configurer** (explicit no-op = preserves PF-0d ops' default behavior).
- `configureFieldNodeFromParams` becomes a **table lookup** (no per-type branch): find type in `fieldConfigurers()`, call fn (or no-op if null/absent — same safety contract as today's unknown-type no-op).

**Per-op slot table + setter-lambda** (UB-safe — FieldNode subclasses are non-standard-layout, so NO `offsetof`):
```
// in each op TU (cast target is TU-private, so the table + dynamic_cast stay here):
void configureSphereSdf(FieldNode& node, const std::map<std::string,float>& m) {
  if (auto* n = dynamic_cast<SphereSDFNode*>(&node)) {
    applyFloatSlot(m, "Center.x", [&](float v){ n->centerX = v; });
    applyFloatSlot(m, "Center.y", [&](float v){ n->centerY = v; });
    applyFloatSlot(m, "Center.z", [&](float v){ n->centerZ = v; });
    applyFloatSlot(m, "Radius",   [&](float v){ n->radius  = v; });
  }
}
const FieldOp g_sphereSdfOp(sphereSdfSpec(), makeSphereSdf, configureSphereSdf);
```
- `applyFloatSlot(m, id, setter)` (shared helper in field_graph.cpp or registry): if `m` contains `id`, call `setter(m.at(id))`; else do nothing (**missing key → member keeps ctor default — identical to today's `pick` fallback**, the byte-identical guarantee).
- Int-selector: `applyIntSelSlot(m,id,setter)` rounds `(int)(v+0.5f)` (match `toroidalvortexfield.cpp:270` exactly; current enums ≥0). Bool-selector: `v>0.5f`.
- **Slot ids MUST equal the NodeSpec PortSpec.id** (the resolver keys by port id). This is the contract — a drift = silent default.

**ToroidalVortexField migration:** replace its bespoke `configureToroidalVortexFieldFromParams` with slot setters `{Center.x/.y/.z, Radius, Range, SwirlGain, RadialGain, FallOffRate}` (F) + `{Axis}` (IntSel). Member values byte-identical → assembled MSL + packed buffer byte-identical → ToroidalVortex + probe goldens stay green.

## Files touched + owner-lock (cook-core/frozen-base; NOT S4, no save-format change)
- `field_node_registry.{h,cpp}` — FieldConfigureFn + fieldConfigurers() sink + FieldOp ctor overload + table-lookup configureFieldNodeFromParams. Static-init registrar (same init-order safety).
- `field_graph.{h,cpp}` — shared `applyFloatSlot`/`applyIntSelSlot`/`applyBoolSelSlot` helpers. Additive only; `assembleFieldMSL`/`collectParams`/packing UNTOUCHED (the point: members change upstream, packing identical).
- `field_ops_{spheresdf,boxsdf,torussdf,combinesdf,toroidalvortexfield}.cpp` — add slot table + configureXxx + extend registrar line. (combinesdf: CombineMethod = IntSel.)
- `field_ops_{transformfield,customsdf,image2dsdf}.cpp` — register **null configurer** explicitly (PF-0d deferred marker).
- NEW golden `app/src/field_paramapply_golden.cpp` + register. No CMake change beyond the golden source.

## Golden design — closed-form non-default proof
`--selftest-field-paramapply`:
1. **Graph path, not by-hand.** Build a resolver/graph where SphereSDF's resolved param map = `{Radius:0.8}` (non-default), run through `buildFieldTree`→`configureFieldNodeFromParams` (REAL production path). Proves dispatch reaches SphereSDF (today it doesn't).
2. **Closed-form GPU assertion.** SphereSDF `f.w = length(p-Center)-Radius` (`spheresdf.cpp:56`). At p=(0.3,0,0), Center=0: default R=0.5→f.w=-0.2; **non-default R=0.8→f.w=-0.5.** Render field template, sample, assert readback == R=0.8 closed form AND != R=0.5 value.
3. **Buffer-level check.** `assembleFieldMSL(...).floatParams` contains 0.8 in the Radius slot when graph supplied 0.8, 0.5 when not.
4. **Enum proof (CombineSDF):** feed non-default CombineMethod → assert assembled MSL text switches to the corresponding mode helper (`combinesdf.cpp:302-338`) — selector changes TEXT not buffer.
5. **-bug:** assert the R=0.5 (default) value while graph supplied 0.8 → RED (proves the apply actually happened).

**Regression guard (byte-identical):** ToroidalVortex golden + particlefield-probe + field_render/fieldtree-builder goldens stay green unchanged (migration preserves member values exactly). Assert no-graph-param assembly is byte-identical before/after. `--selftest-field-codegen` still passes with injectBug.

## Refuter focus
1. **setter-lambda (no offsetof)** — confirm NO `offsetof` on the polymorphic FieldNode subclasses (UB); setters used throughout.
2. **slot-id == port-id** — add a self-test asserting every slot id in an op's table exists as a PortSpec.id in that op's spec (loop over fieldSpecSink()). A mismatch = silent default regression.
3. **ToroidalVortex byte-identical** — member values + assembled MSL + packed buffer unchanged vs HEAD (the migration must not drift the golden-locked op).
4. **Enum rounding** — `(int)(v+0.5f)` matches toroidalvortexfield.cpp:270; selectors ≥0.
5. **Null-configurer ops** — TransformField/CustomSDF/Image2dSDF genuinely no-op (PF-0d), not silently half-applied.
6. **Unknown-type still no-op** — the table lookup preserves today's safety (a type with no configurer keeps ctor defaults, no crash).

## Forks
- `fork-slot-home`: TU-local slot table (chosen) vs registry-central.
- `fork-offsetof-vs-setter`: **setter-lambda chosen** (standard-layout-safe).
- PF-0d deferral: matrix/string/texture params out of the float spine — null configurers, follow-up.

## Risk: medium-low. Byte-identical regression path exists (strong safety); only genuine UB risk (offsetof) neutralized by setter-lambda. Metallib rebuild expected (field op TU + template touch). run_all target PASS=416.

## Critical files
- `field_node_registry.{h,cpp}`, `field_graph.{h,cpp}`, `field_ops_toroidalvortexfield.cpp` (migrate), `field_ops_spheresdf.cpp` (migrate + golden anchor), combinesdf/boxsdf/torussdf (migrate), transformfield/customsdf/image2dsdf (null configurer).
