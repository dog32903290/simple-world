// runtime/node_registry_point_combine — NodeSpec table for point COMBINE ops.
// Ops that merge or pair multiple Points bags into one output: CombineBuffers, SnapToPoints.
// Split from node_registry.cpp (批次16-R, ARCHITECTURE rule 4).
#include "runtime/node_registry_point_combine.h"
#include "runtime/graph.h"

namespace sw {

const std::vector<NodeSpec>& pointCombineSpecs() {
  static const std::vector<NodeSpec> specs = {
      // ── CombineBuffers flat atom RETIRED (廢棄節點退場 pilot #2, 2026-07-08) ──────────────────────────
      // The flat CombineBuffers NodeSpec (the head 壓平原子) is retired: its behaviour is now provided by
      // the nested .t3 compound (assets/catalog_t3/CombineBuffers.t3, guid 4dd8a618…). A human-name
      // reference to "CombineBuffers" (makeNode / .swproj) no longer hits this sink — it falls through
      // findSpec's tail to the compound's name alias (graph_bridge.cpp refreshCompoundSpecs). The .t3
      // replay parity ORACLE stays LIVE in t3import_combinebuffers_golden.cpp (its own concat oracle +
      // the _ExecuteCombineBuffers code-op), independent of this leaf. See RETIREMENT_BATTLE_SPEC §7.2.

      // ---- batch 21: point combine — SnapToPoints — RETIRED 2026-07-10 (廢棄節點退場) --------------------
      // The flat SnapToPoints NodeSpec (the head 壓平原子) is retired: its behaviour is now provided by the
      // nested .t3 compound (assets/catalog_t3/SnapToPoints.t3, guid 5822b0d8…). A human-name reference to
      // "SnapToPoints" no longer hits this sink — it falls through findSpec's tail to the compound's name
      // alias (graph_bridge.cpp refreshCompoundSpecs). The generic ComputeShaderStage (DUAL-SRV Points1 t0 /
      // Points2 t1 + UAV) cooks the ABI-repacked computeshaderstage_snaptopoints.metal; the compound's
      // ComputeShaderStage inherently sizes the dispatch from Points1 (t0), preserving the count-from-first
      // policy. Kernel math stays mathv-verified (selftests_mathv_snaptopoints.cpp dispatches
      // app/shaders/snaptopoints.metal); takeover in t3import_snaptopoints_retire_golden.cpp. See
      // RETIREMENT_BATTLE_SPEC §5. (Leaf cook point_ops_snaptopoints.cpp deleted; snaptopoints_params.h +
      // .metal stay. NB: the catalog .t3 bakes StructuredBufferWithViews Stride 32→64 for sw's 64B SwPoint —
      // fork legacypoint-stride-32-to-64-swpoint, see the ABI kernel header.)

      // ---- point lane: MultiUpdatePoints (TiXL _internal fan-in helper) -------------
      // TiXL parity: external/tixl .../point/_internal/MultiUpdatePoints.{cs,t3}
      // A pass-through helper: forces multiple in-place point modifiers (each returning the SAME
      // BufferWithViews) to evaluate, then returns the LAST connected buffer unchanged. .t3ui:
      // "A helper to combine multiple point modifiers like ApplyNoise or ApplyForce to the same
      // Point buffer." No shader, no params (the cook is a single GPU blit of the last wired bag).
      //
      // PORTS: TiXL's ONE MultiInputSlot<BufferWithViews> is modeled as FIXED input0..input3 Points
      //   ports — same convention CombineBuffers uses (the cook driver's buffer-input gather reads one
      //   wire per PORT, it does not expand a MultiInput buffer port). Unwired inputs contribute null.
      // COUNT POLICY: output count = first wired input's count (== last in faithful same-buffer usage;
      //   countFromFirstPointsInput=true). NOT the sum — this op passes a buffer through, never concats.
      {"MultiUpdatePoints",
       "MultiUpdatePoints",
       {{"input0", "input0", "Points", true},
        {"input1", "input1", "Points", true},
        {"input2", "input2", "Points", true},
        {"input3", "input3", "Points", true},
        {"out", "out", "Points", false}},  // port 4: pass-through output bag
       nullptr,
       "point._internal"},

      // ---- count-product seam: RepeatAtPoints (TiXL GPU generate op) ----------------
      // TiXL parity: external/tixl .../point/generate/RepeatAtPoints.{cs,t3} (slots + count graph) +
      //              .../Assets/shaders/points/generate/RepeatAtGPoints.hlsl (the per-point math)
      // Places each SourcePoint into EACH TargetPoint's local frame -> the FULL CARTESIAN PRODUCT.
      // Output count = source.N * target.N — the canonical count-PRODUCT (NOT a sum). The driver's
      // countTransform hook is fed the product via a file-static set by the cook fn (the (B) static-
      // stash, proven by PairPointsForLines; zero driver signature change). See point_ops_repeatatpoints.cpp.
      //
      // Ports (append after MultiUpdatePoints, do NOT insert):
      //   port 0: SourcePoints (input, GPoints) — bag repeated at each target
      //   port 1: TargetPoints (input, GTargets) — destination frames
      //   port 2: out          (output) — the cartesian product bag
      //   port 3..: Float params, names verbatim from RepeatAtPoints.cs (.t3 defaults):
      //     Scale 1.0 / ApplyOrientation 1 (bool) / ApplyPointScale 1 (bool) /
      //     ScaleFactor 0 (UseFSources enum) / SetF1To 5 / SetF2To 6 (UseFSources) /
      //     CombineMode 0 (Linear|Interwoven) / AddSeparators 1 (bool; count fork — see leaf)
      {"RepeatAtPoints",
       "RepeatAtPoints",
       {{"SourcePoints", "SourcePoints", "Points", true},   // port 0: GPoints (repeated bag)
        {"TargetPoints", "TargetPoints", "Points", true},   // port 1: GTargets (destination frames)
        {"out", "out", "Points", false},                     // port 2: product output bag
        {"Scale",            "Scale",            "Float", true, 1.0f, 0.0f, 10.0f},                       // port 3
        {"ApplyOrientation", "ApplyOrientation", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},          // port 4
        {"ApplyPointScale",  "ApplyPointScale",  "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},          // port 5
        {"ScaleFactor",      "ScaleFactor",      "Float", true, 0.0f, 0.0f, 6.0f, Widget::Enum,
         {"None", "Target_F1", "Target_F2", "Source_F1", "Source_F2", "Multiplied_F1", "Multiplied_F2"}},// port 6
        {"SetF1To",          "SetF1To",          "Float", true, 5.0f, 0.0f, 6.0f, Widget::Enum,
         {"None", "Target_F1", "Target_F2", "Source_F1", "Source_F2", "Multiplied_F1", "Multiplied_F2"}},// port 7
        {"SetF2To",          "SetF2To",          "Float", true, 6.0f, 0.0f, 6.0f, Widget::Enum,
         {"None", "Target_F1", "Target_F2", "Source_F1", "Source_F2", "Multiplied_F1", "Multiplied_F2"}},// port 8
        {"CombineMode",      "CombineMode",      "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"Linear", "Interwoven"}},                                                                       // port 9
        {"AddSeparators",    "AddSeparators",    "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool}},         // port 10
       nullptr,
       "point.generate"},

      // ---- count-product + GrowthMap seam: GrowStrains (TiXL experimental sim op) -------------------
      // TiXL parity: external/tixl .../point/sim/experimental/GrowStrains.{cs,t3} +
      //              .../Assets/shaders/points/sim/GrowStrains.hlsl
      // A STATELESS 2-input transform: PointsA × PointsB cartesian product (+1 NaN separator/source loop),
      // gated by a GrowthMap Texture2D (sampled at float2(B.W, 1-A.W)). Output count = (A.N+1)*B.N via the
      // static-stash countTransform. NoiseAmount drives a simplex displacement; the GrowthMap gates strand
      // length (d=saturate(r-0.05)). Ports 1:1 with GrowStrains.cs [Input] order, .t3 defaults.
      // fork[white-growthmap-fallback]: unwired GrowthMap → 1×1 white (live transform; see leaf).
      {"GrowStrains",
       "GrowStrains",
       {{"GPoints", "GPoints", "Points", true},                  // port 0: PointsA (GPoints, t0)
        {"GTargets", "GTargets", "Points", true},                 // port 1: PointsB (GTargets, t1)
        {"out", "out", "Points", false},                          // port 2: ResultPoints
        {"GrowthMap", "GrowthMap", "Texture2D", true},            // port 3: GrowthMap (t2; unwired→white)
        {"Variation", "Variation", "Float", true, 0.0f, 0.0f, 1.0f},                  // port 4 (.t3 0)
        {"NoiseAmount", "NoiseAmount", "Float", true, 0.0f, 0.0f, 10.0f},             // port 5 (.t3 0)
        {"Frequency", "Frequency", "Float", true, 0.0f, 0.0f, 10.0f},                 // port 6 (.t3 0)
        {"NoisePhase", "NoisePhase", "Float", true, 0.0f, -100.0f, 100.0f},           // port 7 (.t3 0)
        {"NoiseDistribution.x", "NoiseDistribution", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3}, // port 8
        {"NoiseDistribution.y", "NoiseDistribution.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1}, // port 9
        {"NoiseDistribution.z", "NoiseDistribution.z", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1}, // port 10
        {"NoiseRotationLookUp", "NoiseRotationLookUp", "Float", true, 0.5f, 0.0f, 10.0f},  // port 11 (.t3 0.5)
        {"Length", "Length", "Float", true, 0.13f, 0.0f, 10.0f},                      // port 12 (.t3 0.13)
        {"Width", "Width", "Float", true, 0.25f, 0.0f, 10.0f},                        // port 13 (.t3 0.25)
        {"NoiseDensity", "NoiseDensity", "Float", true, 1.0f, 0.0f, 10.0f}},          // port 14 (.t3 1.0)
       nullptr,
       "point.sim"},
  };
  return specs;
}

}  // namespace sw
