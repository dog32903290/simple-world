// field_paramapply_cases — the DATA half of --selftest-field-paramapply (see field_paramapply_cases.h).
// One row per applied slot of every migrated field op; the golden TU (field_paramapply_golden.cpp) loops
// these through the REAL graph path and asserts the packed floatParams index. Peeled out of the golden to
// keep that TU ≤400 lines (ARCHITECTURE.md rule 4, NO grandfather bump). Adding a slot's coverage = add a
// row here — data-driven, no harness edit.
#include "field_paramapply_cases.h"

namespace sw {

std::vector<RoundTripCase> fieldParamApplyRoundTripCases() {
  return {
      // --- the 5 already-migrated ops (RE-VERIFY) ---
      {"SphereSDF", {{"Radius", 0.8f}}, "Radius", 3, 0.8f, ""},
      // BoxSDF DERIVED: Size=[2,2,2] + UniformScale=2 -> CombinedScale[4..6] = 2*2/2 = 2 (each component).
      {"BoxSDF", {{"Size.x", 2.f}, {"Size.y", 2.f}, {"Size.z", 2.f}, {"UniformScale", 2.f}},
       "CombinedScale.x (derived Size*UniformScale/2)", 4, 2.0f, "derived"},
      {"BoxSDF", {{"EdgeRadius", 0.3f}}, "EdgeRadius", 3, 0.3f, ""},
      {"TorusSDF", {{"Thickness", 0.7f}}, "Thickness", 4, 0.7f, ""},
      {"CombineSDF", {{"K", 0.6f}}, "K", 0, 0.6f, ""},
      {"ToroidalVortexField", {{"SwirlGain", 3.5f}}, "SwirlGain", 5, 3.5f, ""},
      // --- the 6 wave-1 proving ops ---
      {"Translate", {{"Translation.x", 1.5f}}, "Translation.x", 0, 1.5f, ""},
      {"TranslateUV", {{"Translation.z", -2.0f}}, "Translation.z", 2, -2.0f, ""},
      {"RepeatField3", {{"Size.y", 4.0f}}, "Size.y", 1, 4.0f, ""},
      // RotatedPlaneSDF: Center[0..2], pad[3], Normal[4..6] (two consecutive vec3s, padForVec3 pad@3).
      {"RotatedPlaneSDF", {{"Normal.x", 0.5f}}, "Normal.x", 4, 0.5f, ""},
      {"OctahedronSDF", {{"Size", 0.9f}}, "Size", 3, 0.9f, ""},
      {"ReflectField", {{"Offset", 1.25f}}, "Offset", 3, 1.25f, ""},

      // ===================== WAVE 2 — EVERY applied slot of every wave-2 op =====================
      // Closes DEBT_LEDGER fieldslot-member-binding-coverage for the wave-2 set: each APPLIED slot is set
      // non-default via the REAL graph path and asserted in its exact packed floatParams index, so a
      // wrong-member typo on ANY slot bites (not just one slot per op). Layouts traced from each op's
      // collectParams + padForVec3 (currentStart%4: ==2 pad 2, ==3 pad 1) / padForVec2 (==odd pad 1).

      // --- BoxFrameSDF: Center[0..2], Thickness[3], CombinedScale(derived)[4..6]. 7 floats. ---
      {"BoxFrameSDF", {{"Center.x", 1.1f}}, "Center.x", 0, 1.1f, ""},
      {"BoxFrameSDF", {{"Center.y", 1.2f}}, "Center.y", 1, 1.2f, ""},
      {"BoxFrameSDF", {{"Center.z", 1.3f}}, "Center.z", 2, 1.3f, ""},
      {"BoxFrameSDF", {{"Thickness", 0.4f}}, "Thickness", 3, 0.4f, ""},
      // DERIVED CombinedScale = Size*UniformScale/2, per component. Set Size+UniformScale RAW, assert the
      // recompute lands. Size.x=3,UniformScale=2 -> CombinedScale.x=3*2/2=3 at slot[4].
      {"BoxFrameSDF", {{"Size.x", 3.f}, {"UniformScale", 2.f}},
       "CombinedScale.x (derived Size.x*UniformScale/2)", 4, 3.0f, "derived"},
      {"BoxFrameSDF", {{"Size.y", 4.f}, {"UniformScale", 2.f}},
       "CombinedScale.y (derived Size.y*UniformScale/2)", 5, 4.0f, "derived"},
      {"BoxFrameSDF", {{"Size.z", 5.f}, {"UniformScale", 2.f}},
       "CombinedScale.z (derived Size.z*UniformScale/2)", 6, 5.0f, "derived"},

      // --- CapsuleLineSDF: Center[0..2], pad[3], StartingPoint[4..6], pad[7], EndPoint[8..10],
      //     Thickness[11]. 12 floats. ---
      {"CapsuleLineSDF", {{"Center.x", 0.11f}}, "Center.x", 0, 0.11f, ""},
      {"CapsuleLineSDF", {{"Center.y", 0.12f}}, "Center.y", 1, 0.12f, ""},
      {"CapsuleLineSDF", {{"Center.z", 0.13f}}, "Center.z", 2, 0.13f, ""},
      {"CapsuleLineSDF", {{"StartingPoint.x", 0.21f}}, "StartingPoint.x", 4, 0.21f, ""},
      {"CapsuleLineSDF", {{"StartingPoint.y", 0.22f}}, "StartingPoint.y", 5, 0.22f, ""},
      {"CapsuleLineSDF", {{"StartingPoint.z", 0.23f}}, "StartingPoint.z", 6, 0.23f, ""},
      {"CapsuleLineSDF", {{"EndPoint.x", 0.31f}}, "EndPoint.x", 8, 0.31f, ""},
      {"CapsuleLineSDF", {{"EndPoint.y", 0.32f}}, "EndPoint.y", 9, 0.32f, ""},
      {"CapsuleLineSDF", {{"EndPoint.z", 0.33f}}, "EndPoint.z", 10, 0.33f, ""},
      {"CapsuleLineSDF", {{"Thickness", 0.44f}}, "Thickness", 11, 0.44f, ""},

      // --- ChainLinkSDF: Center[0..2], Length[3], Size[4], Thickness[5]. 6 floats. ---
      {"ChainLinkSDF", {{"Center.x", 0.51f}}, "Center.x", 0, 0.51f, ""},
      {"ChainLinkSDF", {{"Center.y", 0.52f}}, "Center.y", 1, 0.52f, ""},
      {"ChainLinkSDF", {{"Center.z", 0.53f}}, "Center.z", 2, 0.53f, ""},
      {"ChainLinkSDF", {{"Length", 0.61f}}, "Length", 3, 0.61f, ""},
      {"ChainLinkSDF", {{"Size", 0.62f}}, "Size", 4, 0.62f, ""},
      {"ChainLinkSDF", {{"Thickness", 0.63f}}, "Thickness", 5, 0.63f, ""},

      // --- FractalSDF: Scale[0], Minrad[1], pad[2..3], Clamping[4..6], pad[7], Increment[8..10],
      //     pad[11], Fold[12..13]. 14 floats. (Iterations = compile-time code selector, NOT packed.) ---
      {"FractalSDF", {{"Scale", 3.5f}}, "Scale", 0, 3.5f, ""},
      {"FractalSDF", {{"Minrad", 0.42f}}, "Minrad", 1, 0.42f, ""},
      {"FractalSDF", {{"Clamping.x", 0.71f}}, "Clamping.x", 4, 0.71f, ""},
      {"FractalSDF", {{"Clamping.y", 0.72f}}, "Clamping.y", 5, 0.72f, ""},
      {"FractalSDF", {{"Clamping.z", 0.73f}}, "Clamping.z", 6, 0.73f, ""},
      {"FractalSDF", {{"Increment.x", 0.81f}}, "Increment.x", 8, 0.81f, ""},
      {"FractalSDF", {{"Increment.y", 0.82f}}, "Increment.y", 9, 0.82f, ""},
      {"FractalSDF", {{"Increment.z", 0.83f}}, "Increment.z", 10, 0.83f, ""},
      {"FractalSDF", {{"Fold.x", 0.91f}}, "Fold.x", 12, 0.91f, ""},
      {"FractalSDF", {{"Fold.y", 0.92f}}, "Fold.y", 13, 0.92f, ""},

      // --- NoiseDisplaceSDF: Amount[0], Scale[1], pad[2..3], Offset[4..6], StepFactor[7]. 8 floats.
      //     (UseLocalSpace = BoolSel compile-time selector, NOT packed — proven via assembled-MSL text
      //      in the golden's (5b) check, not a buffer slot.) ---
      {"NoiseDisplaceSDF", {{"Amount", 1.5f}}, "Amount", 0, 1.5f, ""},
      {"NoiseDisplaceSDF", {{"Scale", 2.5f}}, "Scale", 1, 2.5f, ""},
      {"NoiseDisplaceSDF", {{"Offset.x", 0.31f}}, "Offset.x", 4, 0.31f, ""},
      {"NoiseDisplaceSDF", {{"Offset.y", 0.32f}}, "Offset.y", 5, 0.32f, ""},
      {"NoiseDisplaceSDF", {{"Offset.z", 0.33f}}, "Offset.z", 6, 0.33f, ""},
      {"NoiseDisplaceSDF", {{"StepFactor", 0.7f}}, "StepFactor", 7, 0.7f, ""},

      // --- SpatialDisplaceSDF: Amount[0], Scale[1], pad[2..3], vScale[4..6], pad[7], Offset[8..10],
      //     pad[11], SamplePos[12..14]. 15 floats. ---
      {"SpatialDisplaceSDF", {{"Amount", 1.6f}}, "Amount", 0, 1.6f, ""},
      {"SpatialDisplaceSDF", {{"Scale", 2.6f}}, "Scale", 1, 2.6f, ""},
      {"SpatialDisplaceSDF", {{"vScale.x", 0.41f}}, "vScale.x", 4, 0.41f, ""},
      {"SpatialDisplaceSDF", {{"vScale.y", 0.42f}}, "vScale.y", 5, 0.42f, ""},
      {"SpatialDisplaceSDF", {{"vScale.z", 0.43f}}, "vScale.z", 6, 0.43f, ""},
      {"SpatialDisplaceSDF", {{"Offset.x", 0.51f}}, "Offset.x", 8, 0.51f, ""},
      {"SpatialDisplaceSDF", {{"Offset.y", 0.52f}}, "Offset.y", 9, 0.52f, ""},
      {"SpatialDisplaceSDF", {{"Offset.z", 0.53f}}, "Offset.z", 10, 0.53f, ""},
      {"SpatialDisplaceSDF", {{"SamplePos.x", 0.61f}}, "SamplePos.x", 12, 0.61f, ""},
      {"SpatialDisplaceSDF", {{"SamplePos.y", 0.62f}}, "SamplePos.y", 13, 0.62f, ""},
      {"SpatialDisplaceSDF", {{"SamplePos.z", 0.63f}}, "SamplePos.z", 14, 0.63f, ""},
  };
}

}  // namespace sw
