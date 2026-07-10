// Host<->shader params for TiXL _AppendPoints (points/_internal, mathv transpiler-batch kernel
// INVENTORY, 2026-07-10, MATH_VERIFY_WORKFLOW.md §10). NOT wired to node_registry / kernelNameFor /
// t3_import — kernel-inventory entry only; connecting to a stage is a separate future lane (owner-lock).
//
// Mirrors external/tixl .../Assets/shaders/points/combine/AppendPoints.hlsl cbuffer Params : b0.
// AppendPoints concatenates Points1 (first CountA elements of the result) + Points2 (the rest) into
// ResultPoints, writing a NaN-W separator at each bag boundary — TiXL's own AppendPoints.hlsl comment
// convention for "W==NaN marks a point-bag break" (already referenced in app/shaders/draw_lines.metal:12
// even though the kernel itself was un-ported before this batch).
//
// CountA/CountB are INTERNALLY DERIVED (not user-authored floats): _AppendPoints.t3 wires them from
// GetBufferComponents -> IntToFloat on the actual Points1/Points2 SRV element counts (no t3ui Min/Max
// exists for this internal glue op — see fuzz TU's ParamDomain provenance note for the citation).
#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct AppendPointsParams {
  float CountA;  // AppendPoints.hlsl:5 -- Points1's element count (as a float, internally derived)
  float CountB;  // AppendPoints.hlsl:6 -- Points2's element count (as a float, internally derived)
};

enum AppendPointsBinding {
  APPENDPOINTS_Params       = 0,  // constant AppendPointsParams& (b0)
  APPENDPOINTS_ResultPoints = 1,  // device LegacyPoint* (u0) -- output, in-place (see header note on
                                   // the OOB branch's partial write)
  APPENDPOINTS_Points1      = 2,  // const device LegacyPoint* (t0)
  APPENDPOINTS_Points2      = 3,  // const device LegacyPoint* (t1)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(AppendPointsParams) == 8, "AppendPointsParams must be 8 bytes (2 floats)");
#endif
