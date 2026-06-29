// runtime/point_ops_radial — RadialPoints generator cook (split out of point_ops.cpp when the
// param-completion gate pushed that file past the 400-line ratchet, ARCHITECTURE rule 4). Holds
// ONLY cookRadialPoints + its parity-gate -bug latch. Registered via point_ops_register_generators.
#include "runtime/point_ops.h"

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"         // calcDispatchCount
#include "runtime/particle_params.h"  // RadialParams, RadialBinding
#include "runtime/point_graph.h"      // PointCookCtx, cookParam/cookVecN
#include "runtime/tex_op_cache.h"     // cachedComputePSO

namespace sw {

// Parity-gate -bug latch (off in production). When true, cookRadialPoints reverts the newly exposed
// knobs to the PRE-GATE baked constants — Axis +Z, GainAndBias identity, no closeCircle, Scale=(1,0),
// F1=F2=0, Color white — reproducing the "knob not wired" deviation this gate closed. Used by
// --selftest-radial-parity's injectBug leg so the value teeth flip RED. Declared in point_ops.h.
// (Orientation's baked-identity is proven RED by the git-stash red-first run, not via a
// production-shader test branch — that would violate the no-test-seam-in-shader rule.)
bool& radialBakedBugForceForTest() { static bool b = false; return b; }

// RadialPoints generator: dispatch the radial_points kernel into the node's output bag.
// PARAM-COMPLETION GATE: every TiXL input is now read from the NodeSpec (no more baked defaults).
// Scalars via cookParam, Vector3/4 via cookVecN, the bool/enum via cookParam (float on the value
// rail). Defaults below all cite RadialPoints.t3.
// NOTE: builds the PSO per cook — fine for the headless golden (one cook). The live loop (A1.5)
// must cache PSOs; flagged there, not here.
void cookRadialPoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "radial_points");
  if (!pso) return;
  const bool baked = radialBakedBugForceForTest();

  RadialParams P{};
  P.Count = c.count;
  P.Radius = cookParam(c, "Radius", 1.0f);          // .t3:65 default 1.0
  P.RadiusOffset = cookParam(c, "RadiusOffset", 0.0f);  // .t3:14 default 0.0
  P.StartAngle = cookParam(c, "StartAngle", 0.0f);  // .t3:25 default 0.0
  P.Cycles = cookParam(c, "Cycles", 1.0f);          // Rotations .t3:57 default 1.0

  float center[3] = {0.0f, 0.0f, 0.0f};             // Center .t3:80 default (0,0,0)
  cookVecN(c, "Center", center, 3, center);
  P.CenterX = center[0]; P.CenterY = center[1]; P.CenterZ = center[2];

  float axis[3] = {0.0f, 0.0f, 1.0f};               // Axis .t3:45 default (0,0,1)
  cookVecN(c, "Axis", axis, 3, axis);
  P.AxisX = axis[0]; P.AxisY = axis[1]; P.AxisZ = axis[2];

  float offc[3] = {0.0f, 0.0f, 0.0f};               // OffsetCenter .t3:99 default (0,0,0)
  cookVecN(c, "OffsetCenter", offc, 3, offc);
  P.OffsetCenterX = offc[0]; P.OffsetCenterY = offc[1]; P.OffsetCenterZ = offc[2];

  float gainBias[2] = {0.5f, 0.5f};                 // GainAndBias .t3:92 default (0.5,0.5)
  cookVecN(c, "GainAndBias", gainBias, 2, gainBias);
  P.GainX = gainBias[0]; P.GainY = gainBias[1];

  P.CloseCircle = cookParam(c, "CloseCircleLine", 0.0f);  // .t3:53 default false

  float scale[2] = {1.0f, 0.0f};                    // Scale .t3:73 default (1,0)
  cookVecN(c, "Scale", scale, 2, scale);
  P.ScaleBase = scale[0]; P.ScaleByF = scale[1];

  float f1[2] = {1.0f, 0.0f};                       // F1 .t3:18 default (1,0)  ★not (0,0)
  cookVecN(c, "F1", f1, 2, f1);
  P.F1Base = f1[0]; P.F1ByF = f1[1];

  float f2[2] = {1.0f, 0.0f};                       // F2 .t3:29 default (1,0)  ★not (0,0)
  cookVecN(c, "F2", f2, 2, f2);
  P.F2Base = f2[0]; P.F2ByF = f2[1];

  float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};        // Color .t3:36 default white
  cookVecN(c, "Color", color, 4, color);
  P.ColorR = color[0]; P.ColorG = color[1]; P.ColorB = color[2]; P.ColorA = color[3];

  float oAxis[3] = {0.0f, 0.0f, 1.0f};              // OrientationAxis .t3:7 default (0,0,1)
  cookVecN(c, "OrientationAxis", oAxis, 3, oAxis);
  P.OrientAxisX = oAxis[0]; P.OrientAxisY = oAxis[1]; P.OrientAxisZ = oAxis[2];

  P.OrientAngle = cookParam(c, "OrientationAngle", 0.0f);  // .t3:88 default 0.0
  P.OrientMode = cookParam(c, "OrientationMode", 0.0f);    // .t3:61 default 0 (Classic)

  if (baked) {
    // Re-bake the pre-gate constants: knobs ignored exactly as the old kernel did.
    P.AxisX = 0.0f; P.AxisY = 0.0f; P.AxisZ = 1.0f;
    P.OffsetCenterX = P.OffsetCenterY = P.OffsetCenterZ = 0.0f;
    P.GainX = 0.5f; P.GainY = 0.5f;       // identity
    P.CloseCircle = 0.0f;
    P.ScaleBase = 1.0f; P.ScaleByF = 0.0f;
    P.F1Base = 0.0f; P.F1ByF = 0.0f;      // old kernel wrote FX1 = 0
    P.F2Base = 0.0f; P.F2ByF = 0.0f;      // old kernel wrote FX2 = 0
    P.ColorR = P.ColorG = P.ColorB = P.ColorA = 1.0f;  // white
  }

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(c.output, 0, RADIAL_Points);
  enc->setBytes(&P, sizeof(P), RADIAL_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

}  // namespace sw
