# Field Param-Apply Fan-Out Scope (PF-0c mechanism → remaining ops)

> Scout output, HEAD 5d3d94e. The PF-0c mechanism (fieldConfigurers() dispatch + slot-table setter-lambdas) is live for 5 ops; this fans it out to the rest. 38 field ops total.

## Classification
**Already migrated (PF-0c, fc53993):** SphereSDF, BoxSDF, TorusSDF, CombineSDF, ToroidalVortexField.

**FLOAT-CLEAN (23, migratable now)** — all params float / int-selector(enum) / bool-selector:
BendField, BlendSdfWithSdf*, BoxFrameSDF, CappedTorusSDF, CapsuleLineSDF, ChainLinkSDF, CombineFieldColor(enum), CylinderSDF(enum Axis), FractalSDF, NoiseDisplaceSDF(BoolSel UseLocalSpace), OctahedronSDF, PlaneSDF(enum), PrismSDF(enum), PyramidSDF(enum), ReflectField, RepeatAxis(enum), RepeatField3, RepeatFieldLimit(enum), RepeatPolar(enum), RotateAxis(enum), RotatedPlaneSDF, RotateField*, SpatialDisplaceSDF, StairCombineSDF(enum), Translate, TranslateUV, TwistField(enum).

**NO-PARAMS (null configurer):** AbsoluteSDF, InvertSDF, PushPullSDF.

**PF-0d deferred (matrix/string/texture):** TransformField (float4x4), CustomSDF (2× string), Image2dSDF (Texture2D).

## Subtleties (per-op care)
- **BoxSDF (ALREADY MIGRATED — re-verify!):** derived param `CombinedScale = Size×UniformScale/2`. Graph supplies Size+UniformScale separately; collectParams packs the derived value. The configurer MUST applyFloatSlot to size/uniformScale members AND ensure CombinedScale recomputes (either collectParams recomputes from members, or the configurer recomputes). **If PF-0c migrated BoxSDF by setting members without the recompute, non-default BoxSDF params are wrong.** The parameterized golden (set Size=[2,2,2],UniformScale=2 → assert CombinedScale=[2,2,2]) catches this. RE-VERIFY in wave 1.
- **RotateField:** currently NO collectParams override → no floats packed. Must add collectParams (appendVec3Param Rotation) before/with the configurer.
- **BlendSdfWithSdf:** uses tryBuildCustomCode (custom multi-input walk); params Range/Offset injected via preShaderCode string. Migration straightforward (applyFloatSlot Range/Offset) but verify the custom-code reads match PortSpec ids.
- **15 enum-selector ops:** enum value changes EMITTED MSL TEXT (swizzle/helper), not just buffer. Need a TEXT-assertion (assemble → assert MSL contains the expected helper/swizzle for a non-default enum), not just buffer round-trip. Low cost (regex on assembled MSL).

## Verification strategy (no 30 GPU goldens)
1. **Parameterized buffer-round-trip golden:** for each migrated op, set one non-default param via the REAL graph path (Graph→resolver→buildFieldTree→configureFieldNodeFromParams→assembleFieldMSL), assert the value lands in the correct floatParams slot. Proves graph→member→packing fires. (Slot ids are already-parity-checked PortSpec ids; member→packing→TiXL-output was golden-pinned at original port → buffer round-trip is sufficient for float params.)
2. **Slot-id==port-id guard (HARDENED, Option B):** new `fieldSlotSpecs()` sink in field_node_registry — each op registers its {opType, slotId} at static init; guard loops the sink × fieldSpecSink() asserting every slot id is a real PortSpec.id. Replaces the golden's hand-copied list (closes DEBT_LEDGER pf0c-slotid-guard-indirection). Reads the REAL tables, can't drift.
3. **Per-enum MSL-text assertion:** for each enum-selector op, assemble with a non-default enum → assert the MSL text switched to the expected helper/swizzle.
4. **Regression:** the 5 already-migrated ops + ToroidalVortex/probe + 4 force goldens stay byte-identical; default-param assembly byte-identical.

## Batch shape
Conflict-free per-`.cpp` (each op edits only its field_ops_X.cpp + self-registers). The Option-B `fieldSlotSpecs()` sink is a shared serial prerequisite (field_node_registry.{h,cpp}) → build it FIRST, then ops register in parallel.
- **Wave 1 (this build):** sink + guard hardening + parameterized golden harness + migrate the simplest pure-scalar/vec3 ops (Translate, TranslateUV, RepeatField3, RotatedPlaneSDF, OctahedronSDF, ReflectField) as the proving set + RE-VERIFY the 5 already-migrated (esp. BoxSDF derived). Closes the NIT, proves the harness.
- **Wave 2+:** the remaining ~17 ops in mechanical batches (enum ops get text-assertions; RotateField + BlendSdf get their per-op care). Parallel write-leaf, central merge+golden+refuter.

## Critical files
- field_node_registry.{h,cpp} (sink + guard), field_graph.h (applyXxxSlot helpers, exist), field_ops_spheresdf.cpp / toroidalvortexfield.cpp (migrated examples), field_ops_boxsdf.cpp (derived-param re-verify), app/src/field_paramapply_golden.cpp (extend to parameterized).
