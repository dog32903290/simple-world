# Build Blueprint: shader-graph block #3 = single-input field modifiers (InvertSDF/AbsoluteSDF/Translate)

Scout output (Cut 89, 2026-06-20). **BOTTOM LINE: shader-graph is NOT a new seam — it IS the field SDF graph machinery, ~85% built + GPU-proven (Cut 70-84).** file headers say so: field_graph.h:1 "shader-graph CODEGEN", field_render.h:3 "the shader-graph island", metal_compile.h:9-16 carves the runtime-MSL fork for "the shader-graph field island". Census aliases shader-graph == field-graph root (SEAM_GRAPH.md:16). All 60 TiXL field/ ops output Slot<ShaderGraphNode> = one subsystem. NO second graph type.

Codegen tension RESOLVED in-codebase: metal_compile.h:9-16 = explicit scoped named fork (static ops precompiled into shaders.metallib; field/shader-graph island = the ONE runtime-codegen path, mirrors TiXL: static ops ship compiled, shader-graph generates at runtime). Faithful to both. DO NOT build a parallel graph type — EXTEND/drive the existing field infra.

## The untested seam edge (what this batch proves)
field_graph.cpp:45-87 collectEmbeddedShaderCode (port of TiXL ShaderGraphNode.cs:183-251) has 3 branches: leaf (52-57, proven by 16 SDF gens), multi-input subContext fold (59-81, GPU-proven CombineSDF Cut 73), **single-input pre/post wrap (82-86, CODE PRESENT but NO shipped op drives it)**. Block #3 = 3 single-input modifier ops driving this edge.

## Existing infra (reuse wholesale, file:line) — frozen base (field_graph.h:160-164), do NOT edit
- recursion incl single-input branch: field_graph.cpp:45-87 (the :82-86 else-branch = the edge; node.preShaderCode before child recursion, node.postShaderCode after = TiXL ShaderGraphNode.cs:244-251 order)
- FieldNode interface: field_graph.h:93-122 (addGlobals/preShaderCode/postShaderCode/collectParams/collectTextures)
- pushContext/popContext p/f seeding: field_graph.cpp:26-39
- param packing (scalar/vec2/vec3 16B align packed_float3): field_graph.cpp:154-187 (appendVec3Param etc)
- assembleFieldMSL + srcHash + template hooks: field_graph.cpp:191-261, field_render_template.metal
- renderField2d GPU dispatch (R32Float, distance→RED): field_render.cpp:21-85
- srcHash PSO cache: tex_op_cache.h:42-56 cachedSourcePSO
- FieldOp self-registration glob: field_node_registry.h/.cpp

## 3 proving ops (backward-traced .cs — port emit string VERBATIM)
**(A) InvertSDF** — adjust/InvertSDF.cs:26-29. postShaderCode ONLY: `f{c}.w *= -1;` (use sw prefix convention). No globals, no params, no pre. Golden: Invert→GoldenSphere tree, probe point at known dist d → RED == -(sphere distance). injectBug: drop the *-1.
**(B) AbsoluteSDF** — adjust/AbsoluteSDF.cs:25-28. postShaderCode ONLY: `f{c}.w = abs(f{c}.w);`. No params. Golden: INTERIOR probe (sphere dist negative) → RED == +|d| (the tooth); EXTERIOR probe → unchanged. injectBug: drop abs.
**(C) Translate** — space/Translate.cs:33-36. preShaderCode: `p{c}.xyz -= {prefix}Translation;` + `[GraphParam] Vector3 Translation` (Translate.cs:41-43). Exercises preShaderCode (other half) + appendVec3Param packing under the modifier's prefix. Needs a `configureTranslate` downcast seam (mirror configureCombineSdf). Golden: Translate→GoldenSphere, probe q → RED == sphereDistance(q - T); pick q,T so sample lands on a deterministic plateau INSIDE [-1,1] field space (Cut 71-72 probe rule; avoid boundary/crossing teeth unless negative region on-screen). injectBug: wrong sign / drop.

## Golden discipline (Cut 62-63 + 71-72): deterministic value probes in [-1,1] field space, NO fwidth/derivative/temporal (these ops are pure distance arithmetic → safe). Sampler is filter::nearest. Copy golden template field_ops_combinesdf_golden.cpp:153-157 (local GoldenSphere children, makeFieldNode + configure* downcast, renderField2d, CPU-read R32Float RED).

## Forced forks (no choice)
- {prefix} param-name derivation: MUST match sw's shipped CombineSDF/SDF-leaf convention (prefix + "_", self-consistent struct-member↔P.<name>). **★BLOOD LESSON (raymarch3D + Cut88 sampler): backward-trace sw's actual prefix convention from field_ops_combinesdf.cpp:288, do NOT forward-assume TiXL's `{ShaderNode}Translation` literal.** Golden catches mismatch (reads 0, like Radius-as-0 fork).
- globals std::map KEY-order → forward-decl prototype if a helper calls another (Cut 73 lesson). These 3 ops self-contained, fine; later ops beware.
- HLSL inout → MSL thread X& in any by-ref helper.
- AddDefinitions vs addGlobals: sw collapsed to single addGlobals (field_graph.h:103); both → c.globals; map cleanly.

## Genuine choices (recommended): single Field-typed input port per modifier (faithful, Field dataType blocks wrong wires, field_ops_combinesdf.cpp:352); configure* downcast reuse for Translate, none for Invert/Absolute (no params).

## Scope ONE batch (small R1, ~Cut 73/74): 3 field_ops_<name>.cpp + 3 golden files + central wiring (3 CMake lines, 3 kTable rows, 3 forward-decls, 3 NodeSpecs [1 Field input each; Translate + Vec3 param Widget::Vec], configureTranslate seam, selftest registration). ZERO edits to field_graph.*/field_render.*/template/frame_cook.cpp.
DEFERRED: live field graph-cook wiring (G3 — Slot<Field> ports in frame_cook → FieldNode tree → viewable Command/texture = Render2dField/RaymarchField, needs camera3d/Layer2d = the real NEXT big seam); raymarch3D (PBR-blocked stash@{0}); ~40 other field modifiers (RepeatAxis/RotateAxis/Twist/Bend/Reflect/PushPull/BlendSDF/Stair/SdfToVector/Raster3dField → Phase C mass-mine once this edge proven); SDFToColor (gradient-widget); HeightMapSdf (Seam B).

## Risks (all low): single-input branch latent bug (the point of the batch; Invert post + Translate pre bracket both halves); param-prefix mismatch (backward-trace, golden catches); over-build temptation = wiring the live field cook (G3) — RESIST, leaf ops only.

## Critical files
- field_graph.h (FieldNode interface — subclass; frozen base, do NOT edit)
- field_graph.cpp:45-87 (the single-input wrap at :82-86 = what batch proves; drive, don't edit)
- field_ops_combinesdf.cpp (template: FieldNode subclass + NodeSpec + factory + configure downcast)
- field_ops_combinesdf_golden.cpp (golden template: Modifier→GoldenSphere tree, render, read R32Float)
- external/tixl/Operators/Lib/field/adjust/InvertSDF.cs + AbsoluteSDF.cs + space/Translate.cs (authoritative emit strings, port verbatim)
