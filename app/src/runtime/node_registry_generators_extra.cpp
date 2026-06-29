// runtime/node_registry_generators_extra — SECOND half of the point GENERATOR NodeSpec table.
// Split out of node_registry_generators.cpp (param-completion fan-out, 2026-06-29): that file hit
// the hard 400-line ARCHITECTURE rule-4 cap with zero headroom, blocking the ~16 generators still
// owed their full TiXL [Input] param set. The four CORE generators (RadialPoints / LinePoints /
// GridPoints / SpherePoints — the gate calibration pair lives here) stay in the primary file; the
// remaining families (Hex / Doyle / Repetition / CommonPointSets / BoundingBox / MeshVertices /
// PointsOnMesh / PointTrail[Fast]) move here. generatorSpecs() concatenates both halves, so the
// flat registry order is UNCHANGED (this file's entries follow the four core ones verbatim).
// Adding a new generator: append its NodeSpec here (this file has the headroom now).
#include "runtime/node_registry_generators.h"
#include "runtime/graph.h"

namespace sw {

const std::vector<NodeSpec>& generatorSpecsExtra() {
  static const std::vector<NodeSpec> specs = {
      // ---- batch 19: point generate — HexGridPoints --------------------------------
      // TiXL parity: external/tixl .../point/generate/HexGridPoints.cs + .hlsl (Pattern=2 Hexa)
      // A GENERATOR op: no input bag. Writes a hexagonal tiling grid of SwPoints.
      // Math: same cell->clampedCount->zeroAdjustedSize->SizeMode base as GridPoints,
      // then applies hex X-offset = HexOffsetsAndAngles[hexAttrIndex].x * sizeX * 0.3333
      // and rescales pos.x *= 0.578 * 3 (HexScale), with per-cell rotation rotDelta.
      // Defaults: CountX=4, CountY=4, CountZ=1, Size=(1,1,1), Center=(0,0,0),
      //   W=1.0, OrientationAxis=(0,1,0), OrientationAngle=0, Pivot=(0,0,0), SizeMode=Cell.
      // NOTE: Count = buffer CAPACITY = CountX * CountY * CountZ (host responsibility).
      // FORK: Pattern baked to 2 (Hexa); Triangular (1) and default (3) branches deferred.
      //       Color baked white; Scale baked 1.
      {"HexGridPoints",
       "HexGridPoints",
       {{"points", "points", "Points", false},
        // Count = output buffer capacity (host sets = CountX*CountY*CountZ)
        {"Count", "Count", "Float", true, 16.0f, 1.0f, 65536.0f},
        {"CountX", "CountX", "Float", true, 4.0f, 1.0f, 256.0f, Widget::Slider},
        {"CountY", "CountY", "Float", true, 4.0f, 1.0f, 256.0f, Widget::Slider},
        {"CountZ", "CountZ", "Float", true, 1.0f, 1.0f, 256.0f, Widget::Slider},
        {"SizeMode", "SizeMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"Cell", "Bounds"}},
        // Size (TiXL Vector3) — per-axis cell extent
        {"Size.x", "Size", "Float", true, 1.0f, 0.01f, 10.0f, Widget::Vec, {}, true, 3},
        {"Size.y", "Size.y", "Float", true, 1.0f, 0.01f, 10.0f, Widget::Vec, {}, true, 1},
        {"Size.z", "Size.z", "Float", true, 1.0f, 0.01f, 10.0f, Widget::Vec, {}, true, 1},
        // Center (TiXL Vector3) — global translation
        {"Center.x", "Center", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Center.y", "Center.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Center.z", "Center.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // Pivot (TiXL Vector3) — grid anchor offset
        {"Pivot.x", "Pivot", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 3},
        {"Pivot.y", "Pivot.y", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
        {"Pivot.z", "Pivot.z", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
        {"W", "W", "Float", true, 1.0f, 0.0f, 1.0f},
        // OrientationAxis (TiXL Vector3, default 0,1,0) + OrientationAngle (degrees)
        {"OrientationAxis.x", "OrientationAxis", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"OrientationAxis.y", "OrientationAxis.y", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"OrientationAxis.z", "OrientationAxis.z", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"OrientationAngle", "OrientationAngle", "Float", true, 0.0f, -360.0f, 360.0f}},
       nullptr,
       "point.generate"},
      // ---- point generate — DoyleSpiralPoints2 -------------------------------------
      // TiXL parity: external/tixl .../point/generate/DoyleSpiralPoints2.cs (.t3 compound) +
      //   _DoyleSpiralRoot.cs (CPU Newton-Raphson) + .../shaders/.../DoyleSpiralPoints.hlsl.
      // A GENERATOR op: no input bag. Produces a Doyle circle-packing spiral (logarithmic arms
      // of exponentially growing circles). The cook clamps PointsPerStep->P, takes SpiralSteepness
      // ->Q, runs the Newton-Raphson root finder to derive A/B/R, then dispatches the kernel.
      // Defaults verbatim from the .t3 InputValues: Steps=1000, Offset=100, PointsPerStep=8,
      //   SpiralSteepness=31, Scale=1, ScaleBias=1, CenterPositionScale=0, W=1, WBias=1,
      //   CenterSizeScale=0, Center=(0,0,0), OrientationAxis=(0,0,1), OrientationAngle=0.
      // NOTE: Count = output buffer CAPACITY = the spiral's point count (TiXL: clamp(Steps,..)).
      //   The user-facing "Steps" semantic maps to this single Count port (host responsibility),
      //   mirroring the HexGridPoints Count-as-capacity convention.
      // FORK: kernel guards steps>=1 (div/mod) for the degenerate Count<=PointsPerStep case.
      {"DoyleSpiralPoints",
       "DoyleSpiralPoints",
       {{"points", "points", "Points", false},
        // Count = output buffer capacity = total spiral points (TiXL Steps, clamp 1..10000000).
        {"Count", "Count", "Float", true, 1000.0f, 1.0f, 100000.0f},
        // PointsPerStep (TiXL Int, default 8) — clamped 1..100 -> P (arms count).
        {"PointsPerStep", "PointsPerStep", "Float", true, 8.0f, 1.0f, 100.0f, Widget::Slider},
        // SpiralSteepness (TiXL Int, default 31) -> Q (winding / size-growth intensity).
        {"SpiralSteepness", "SpiralSteepness", "Float", true, 31.0f, 1.0f, 100.0f, Widget::Slider},
        // Offset (TiXL Single, default 100) — shifts which points sit on the spiral.
        {"Offset", "Offset", "Float", true, 100.0f, -500.0f, 500.0f},
        // Scale (TiXL Single, default 1) — uniform size of the spiral.
        {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 10.0f},
        // ScaleBias (TiXL Single, default 1) -> kernel Bias2 (position-on-spiral compression).
        {"ScaleBias", "ScaleBias", "Float", true, 1.0f, 0.0f, 5.0f},
        // CenterPositionScale (TiXL Single, default 0) -> kernel CutOff (center cut-off).
        {"CenterPositionScale", "CenterPositionScale", "Float", true, 0.0f, -10.0f, 10.0f},
        // W (TiXL Single, default 1) — scales the per-point W (size) channel.
        {"W", "W", "Float", true, 1.0f, 0.0f, 10.0f},
        // WBias (TiXL Single, default 1) -> kernel Bias (outer-point growth exponent).
        {"WBias", "WBias", "Float", true, 1.0f, 0.0f, 5.0f},
        // CenterSizeScale (TiXL Single, default 0) -> kernel CutOff2 (inner-size cut-off).
        {"CenterSizeScale", "CenterSizeScale", "Float", true, 0.0f, -10.0f, 10.0f},
        // Center (TiXL Vector3, default 0,0,0) — translation/pivot.
        {"Center.x", "Center", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Center.y", "Center.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Center.z", "Center.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // OrientationAxis (TiXL Vector3, default 0,0,1) + OrientationAngle (degrees).
        {"OrientationAxis.x", "OrientationAxis", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"OrientationAxis.y", "OrientationAxis.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"OrientationAxis.z", "OrientationAxis.z", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"OrientationAngle", "OrientationAngle", "Float", true, 0.0f, -360.0f, 360.0f}},
       nullptr,
       "point.generate"},
      // ---- batch 36: point generate — RepetitionPoints -----------------------------
      // TiXL parity: external/tixl .../point/generate/RepetitionPoints.cs (CPU StructuredList
      // generator; NO .hlsl). GPU NAMED FORK: thread i computes the i-th transformed point.
      // Per-point recipe verbatim in repetitionpoints.metal (CreateTransformationMatrix ported
      // from Core/Utils/Geometry/GraphicsMath.cs:56-97, row-vector System.Numerics convention).
      //
      // Port order = .cs [Input] declaration order (APPEND not insert; pin ids are index-based):
      //   Count, StartPosition, StartW, Translate, Scale, Rotate, Pivot, Phase, AddSeparator.
      // Defaults from RepetitionPoints.t3 (NOT guessed): Count=0 (UI hint 16; TiXL clamps 1..10000),
      //   StartPosition=0, StartW=0, Translate=0, Scale=1 (float), Rotate=0, Pivot=0, Phase=0,
      //   AddSeparator=true. Scale is a single float broadcast to Vector3 (note the commented-out
      //   Vector3 Scale in the .cs — TiXL kept the float port).
      // NOTE: Count = real point count; the bag grows to Count+1 when AddSeparator (the
      //   repCountTransform in point_ops_repetitionpoints.cpp, reading the cook-set global).
      {"RepetitionPoints",
       "RepetitionPoints",
       {{"points", "points", "Points", false},
        // Count (TiXL Int, .t3 default 0; clamped 1..10000 by the cook). UI default 16 = a usable
        // hint, not load-bearing — the .cs clamps it regardless.
        {"Count", "Count", "Float", true, 16.0f, 1.0f, 10000.0f},
        // StartPosition (TiXL Vector3, default 0) — added to translateStep*u.
        {"StartPosition.x", "StartPosition", "Float", true, 0.0f, -50.0f, 50.0f, Widget::Vec, {}, true, 3},
        {"StartPosition.y", "StartPosition.y", "Float", true, 0.0f, -50.0f, 50.0f, Widget::Vec, {}, true, 1},
        {"StartPosition.z", "StartPosition.z", "Float", true, 0.0f, -50.0f, 50.0f, Widget::Vec, {}, true, 1},
        // StartW (TiXL Single, default 0) — added to scale.Length()/sqrt(3) for F1 (point W/size).
        {"StartW", "StartW", "Float", true, 0.0f, -10.0f, 10.0f},
        // Translate (TiXL Vector3, default 0) — per-step translation increment.
        {"Translate.x", "Translate", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Translate.y", "Translate.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Translate.z", "Translate.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // Scale (TiXL Single, default 1) — broadcast to Vector3 in scale = (1-Scale)*u + 1.
        {"Scale", "Scale", "Float", true, 1.0f, -5.0f, 5.0f},
        // Rotate (TiXL Vector3, default 0, degrees/step): X=yaw Y=pitch Z=roll (YawPitchRoll).
        {"Rotate.x", "Rotate", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"Rotate.y", "Rotate.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"Rotate.z", "Rotate.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        // Pivot (TiXL Vector3, default 0) — rotationCenter in CreateTransformationMatrix.
        {"Pivot.x", "Pivot", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Pivot.y", "Pivot.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Pivot.z", "Pivot.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // Phase (TiXL Single, default 0) — added to (i+1) -> u, sliding the whole series.
        {"Phase", "Phase", "Float", true, 0.0f, -100.0f, 100.0f},
        // AddSeparator (TiXL Bool, .t3 default true) — append one NaN-Scale Point.Separator().
        {"AddSeparator", "AddSeparator", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr,
       "point.generate"},
      // CommonPointSets (batch 37) — CPU-fill fork of external/tixl .../point/generate/
      // CommonPointSets.cs. A pure CPU StructuredList generator: the Set enum picks one of seven
      // hard-coded vertex tables. The .cs has exactly ONE [Input]: Set (int, MappedType=Shapes).
      // .t3 default = 0 (Cross). The output point count varies by Set (the table length); the
      // cook's cpsCountTransform sizes the bag. Enum labels = the private `enum Shapes` order
      // (CommonPointSets.cs:230-239): Cross, CrossXY, Cube, Quad, ArrowX, ArrowY, ArrowZ.
      {"CommonPointSets",
       "CommonPointSets",
       {{"points", "points", "Points", false},
        {"Set", "Set", "Float", true, 0.0f, 0.0f, 6.0f, Widget::Enum,
         {"Cross", "CrossXY", "Cube", "Quad", "ArrowX", "ArrowY", "ArrowZ"}}},
       nullptr,
       "point.generate"},
      // BoundingBoxPoints (batch 38) — CPU-readback fork of external/tixl .../point/generate/
      // BoundingBoxPoints. Reads a Points input bag, computes the axis-aligned bounding box of all
      // VALID (non-NaN-position) source points, emits exactly ONE output point: Position=center=
      // (min+max)/2, Scale=box size=max-min, FX2(Selected)=1, FX1(W)=1, Color=1, Rotation=identity
      // (BoundingBoxPoints.hlsl:118-127). The .cs has EXACTLY ONE [Input]: Points (BufferWithViews);
      // its SampleModes/RotationModes enums are dead copy-paste, NOT [Input]s -> no knob. So this
      // node has ONE input port (Points, port 0) + ONE output port (out, port 1). No params.
      // Output count is always 1 (bbCountTransform in the cook).
      {"BoundingBoxPoints",
       "BoundingBoxPoints",
       {{"points", "points", "Points", true},   // input bag (port 0) — source points to bound
        {"out", "out", "Points", false}},        // single AABB point (port 1)
       nullptr,
       "point.generate"},
      // MeshVerticesToPoints — the FIRST Points op with a MESH INPUT (the mesh-into-points seam's
      // proving op). Ports 1:1 with MeshVerticesToPoints.cs: Mesh (MeshBuffers, gathered by the cook
      // drivers' Mesh loop into PointCookCtx::meshVtx → one Point per vertex), OffsetByTBN (Vector3,
      // .t3 default (0,0,0)), W (the .t3 W input = the shader's OffsetScale, default 1.0). Output count
      // = the mesh vertex count (countFromMeshVtx in the cook registration).
      {"MeshVerticesToPoints",
       "MeshVerticesToPoints",
       {{"Mesh", "Mesh", "Mesh", true},                 // input mesh (port 0) — the seam input
        {"out", "out", "Points", false},                // one Point per vertex (port 1)
        // OffsetByTBN (Vector3, TiXL default (0,0,0)) — per-axis Tangent/Bitangent/Normal offset weights.
        {"OffsetByTBN.x", "OffsetByTBN", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"OffsetByTBN.y", "OffsetByTBN.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"OffsetByTBN.z", "OffsetByTBN.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // W (TiXL float input, default 1.0) → the shader's OffsetScale (global TBN-offset multiplier).
        {"W", "W", "Float", true, 1.0f, -100.0f, 100.0f}},
       nullptr,
       "point.generate"},
      // PointsOnMesh — area-weighted barycentric SURFACE SCATTER (the FIRST op to consume meshIdx;
      // rides BOTH the mesh-into-points seam AND the texture-into-points seam). Ports 1:1 with
      // PointsOnMesh.cs [Input] order: Mesh (MeshBuffers → meshVtx+meshIdx), Count (int, the output
      // bag size, .t3 default 10000), Seed (float, .t3 default 10), Texture (Texture2D ColorMap →
      // inputTextures[0], optional → 1×1 white fallback), UseVertexSelection (bool, .t3 default true →
      // 1.0). Output count = the Count Float port (DEFAULT count policy, NOT countFromMeshVtx). FORKS:
      // the second `Colors` output is DEFERRED (color lives in p.Color); the dead IsEnabled input is
      // omitted (a graph wrapper, not a kernel param).
      {"PointsOnMesh",
       "PointsOnMesh",
       {{"Mesh", "Mesh", "Mesh", true},                // input mesh (port 0) — meshVtx + meshIdx seam
        {"out", "out", "Points", false},               // scattered surface points (port 1)
        // Count (TiXL int, .t3 default 10000) — drives the output bag size through the value spine.
        {"Count", "Count", "Float", true, 10000.0f, 1.0f, 1000000.0f},
        // Seed (TiXL float, .t3 default 10) — the wang_hash RNG seed.
        {"Seed", "Seed", "Float", true, 10.0f, 0.0f, 1000.0f},
        // Texture (TiXL Texture2D ColorMap) — gathered into inputTextures[0]; unwired → 1×1 white.
        {"Texture", "Texture", "Texture2D", true},
        // UseVertexSelection (TiXL bool, .t3 default true) — per-face weight = sum of vert Selection.
        {"UseVertexSelection", "UseVertexSelection", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Enum,
         {"Off", "On"}}},
       nullptr,
       "point.generate"},

      // ---- cross-frame trail seam: PointTrailFast (TiXL STATEFUL generate op, single kernel) --------
      // TiXL parity: external/tixl .../point/generate/PointTrailFast.{cs,t3} +
      //              .../Assets/shaders/points/sim/PointTrailFast.hlsl
      // A FIXED-size trail ring: each source point gets a (TrailLength+1)-slot ring in the persistent
      // output buffer; the write head (CycleIndex==FrameCount) walks one slot per enabled frame so old
      // samples persist across frames. Output count = srcN*(TrailLength+1) via the static-stash
      // countTransform (point_ops_pointtrailfast.cpp). Rides the built ensureState mechanism additively.
      // Ports 1:1 with PointTrailFast.cs ([Input] order), .t3 defaults: TrailLength=100, IsEnabled=true,
      // Reset=false, AddSeperatorThreshold=0.0.
      {"PointTrailFast",
       "PointTrailFast",
       {{"GPoints", "GPoints", "Points", true},                 // port 0: source bag (one ring per point)
        {"out", "out", "Points", false},                         // port 1: the trail ring output
        {"TrailLength", "TrailLength", "Float", true, 100.0f, 1.0f, 10000.0f},      // port 2 (.t3 100)
        {"IsEnabled", "IsEnabled", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},   // port 3 (.t3 true)
        {"Reset", "Reset", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},           // port 4 (.t3 false)
        {"AddSeperatorThreshold", "AddSeperatorThreshold", "Float", true, 0.0f, 0.0f, 100.0f}}, // port 5
       nullptr,
       "point.generate"},

      // ---- cross-frame trail seam: PointTrail (TiXL STATEFUL generate op, 3-pass Clear/Collect/Copy) --
      // TiXL parity: external/tixl .../point/generate/PointTrail.{cs,t3} +
      //              .../Assets/shaders/points/sim/PointTrail-{Clear,Collect,Copy}.hlsl
      // Same fixed ring as PointTrailFast, but a 3-PASS pipeline over a persistent CyclePoints ring +
      // a per-frame TrailPoints OUTPUT: Clear (NaN the output), Collect (write source into the ring at
      // the cross-frame head), Copy (emit NEWEST-FIRST into the output, fade FX1/FX2/Scale, NaN line
      // separators). Output count = srcN*(TrailLength+1) (static-stash countTransform). Ports 1:1 with
      // PointTrail.cs, .t3 defaults: TrailLength=100, WriteTrailOrderTo=F1(1), IsEnabled=true,
      // Reset=false, WriteLineSeparators=true.
      {"PointTrail",
       "PointTrail",
       {{"GPoints", "GPoints", "Points", true},                 // port 0: source bag
        {"out", "out", "Points", false},                         // port 1: newest-first trail output
        {"TrailLength", "TrailLength", "Float", true, 100.0f, 1.0f, 10000.0f},      // port 2 (.t3 100)
        {"WriteTrailOrderTo", "WriteTrailOrderTo", "Float", true, 1.0f, 0.0f, 3.0f, Widget::Enum,
         {"None", "F1", "F2", "Scale"}},                                            // port 3 (.t3 F1=1)
        {"IsEnabled", "IsEnabled", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},   // port 4 (.t3 true)
        {"Reset", "Reset", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},           // port 5 (.t3 false)
        {"WriteLineSeparators", "WriteLineSeparators", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool}}, // port 6 (.t3 true)
       nullptr,
       "point.generate"},
  };
  return specs;
}

}  // namespace sw
