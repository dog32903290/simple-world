// runtime/t3_import_maps — the three-table Guid→sw mapping DATA (see t3_import_maps.h). Split from
// t3_import.cpp so the importer's parse/fill logic stays under the arch line ratchet and the mapping
// tables are one data-driven job: add an atom = add a row here. Pure CPU (two static std::maps).
#include "runtime/t3_import_maps.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace sw {

std::string t3Lc(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

// ComputeShader fold-pass guids (computeshader-source-folded-onto-stage).
const char* const kComputeShaderGuid = "a256d70f-adb3-481d-a926-caf35bd3e64c";
const char* const kComputeShaderSourceSlot = "afb69c81-5063-4cb9-9d42-841b994b5ec0";  // Source (String)
const char* const kComputeShaderCsOutSlot = "6c118567-8827-4422-86cc-4d4d00762d87";    // CS output
const char* const kComputeStageCsInSlot = "5c0e9c96-9aba-4757-ae1f-cc50fb6173f1";      // ComputeShaderStage.ComputeShader in
const char* const kCombineBuffersCsInSlot = "d91e52f2-52c6-4533-ac14-f5b2ce8b4c0f";    // _ExecuteCombineBuffers.ComputeShader in (187)

// image-fx-wrapper-collapses-to-tex-atom: the FloatParams MultiInput slot every fx-setup framework
// symbol exposes (positional scalar rail — Hue/Sat/Exposure… land here in wire order).
const char* const kFxSetupFloatParamsSlot = "2929c4c9-6d6a-47b7-b80e-d7a1f90b6945";

// TEXTURE-COMPUTE COLLAPSE guids (t3_import_texcompute.cpp). Verified against the TiXL .cs [Guid]/[Input]/
// [Output] and the _ComputeBRDFLookup.t3 child/slot ids.
const char* const kComputeShaderStageGuid = "8bef116d-7d1c-4c1b-b902-25c1d5e925a9";  // ComputeShaderStage.cs:5
const char* const kComputeStageUavsSlot = "599384c2-bf6c-4953-be74-d363292ab1c7";    // Uavs (.cs:257-258)
const char* const kComputeStageOutputSlot = "c382284f-7e37-4eb0-b284-bc735247f26b";  // Output (.cs:8-9)
const char* const kUavFromTexture2dGuid = "84e02044-3011-4a5e-b76a-c904d9b4557f";    // UavFromTexture2d.cs:3
const char* const kUavFromTexture2dOutSlot = "83d2dcfd-3850-45d8-bb1b-93fe9c9f4334"; // .cs:6-7 (UAV output)
const char* const kSrvFromTexture2dGuid = "c2078514-cf1d-439c-a732-0d7b31b5084a";    // SrvFromTexture2d.cs:3 (stage 2)
const char* const kTexture2dGuid = "f52db9a4-fde9-49ca-9ef7-131825c34e65";           // Texture2d (UAV alloc)
const char* const kTexture2dFormatSlot = "67cd82c3-504b-4c80-8c49-5b303733ed52";     // Format InputValue
const char* const kTexture2dSizeSlot = "b77088a9-2676-4caa-809a-5e0f120d25d7";       // Size input
const char* const kCalcInt2DispatchCountGuid = "cc11774e-82dd-409f-97fb-5be3f2746f9d"; // CalcInt2DispatchCount.cs:3
const char* const kExecuteTextureUpdateGuid = "6c2f8241-9f4b-451e-8a1d-871631d21163";  // ExecuteTextureUpdate.cs:3
const char* const kExecuteTextureUpdateOutSlot = "c955f2a2-9823-4844-ac11-98ea07dc50aa"; // Output (.cs:6-7)

// STAGE 2 SRV-tex + b0 CB collapse guids (_ComputeDepthToLinear shape). Verified against the TiXL .cs
// [Guid]/[Input]/[Output] and the _ComputeDepthToLinear.t3 child/slot ids.
const char* const kSrvFromTexture2dTexSlot = "d5afa102-2f88-431e-9cd4-af91e41f88f6";       // SrvFromTexture2d.Texture in (.cs:75-76)
const char* const kSrvFromTexture2dOutSlot = "dc71f39f-3fba-4fc6-b8ef-ce57c82bf78e";       // SrvFromTexture2d.ShaderResourceView out (.cs:10-11)
const char* const kComputeStageShaderResourcesSlot = "88938b09-d5a7-437c-b6e1-48a5b375d756"; // ComputeShaderStage.ShaderResources (.cs:251-252)
const char* const kComputeStageConstantBuffersSlot = "34cf06fe-8f63-4f14-9c59-35a2c021b817"; // ComputeShaderStage.ConstantBuffers (.cs:248-249)
const char* const kGetTextureSizeGuid = "daec568f-f7b4-4d81-a401-34d62462daab";           // GetTextureSize.cs:3
const char* const kSamplerStateGuid = "9515d59d-0bd5-406b-96da-6a5f60215700";             // SamplerState.cs (_ComputeDepthToLinear.t3 child)
const char* const kFloatsToBufferGuid = "724da755-2d0c-42ab-8335-8c88ec5fb078";           // FloatsToBuffer.cs (b0 CB assembler)
const char* const kFloatsToBufferParamsSlot = "49556d12-4cd1-4341-b9d8-c356668d296c";     // FloatsToBuffer.Params MultiInput (.cs:81-82)

// Redundant-subgraph elision guids (rationale in t3_import_maps.h; used by t3_import_collapse.cpp).
const char* const kGradientsToTextureGuid = "2c53eee7-eb38-449b-ad2a-d7a674952e5b";
const char* const kGradientsToTextureGradientsSlot = "588be11f-d0db-4e51-8dbb-92a25408511c";
const char* const kTransformImageGuid = "32e18957-3812-4f64-8663-18454518d005";
const char* const kTransformImageImageSlot = "3aab9b12-1e02-4d7a-83b6-da1500a6bcbf";
const char* const kGenerateMipsGuid = "32a6a351-6d22-4915-aa0e-e0483b7f4e76";
const char* const kPickFloatGuid = "63e6e642-827b-4518-ac64-9ab0a8d4391e";
const char* const kPickFloatValuesSlot = "d7ef7f1a-a6bd-4f94-a29a-bb19e2854001";
const char* const kPickFloatIndexSlot = "465b4fc3-899c-4b97-9892-f237fa6613e8";
const char* const kMultiplyOffsetGuid = "17b60044-9125-4961-8a79-ca94697b3726";

// ---- TABLE ③: t3 symbol guid → sw op type. The buffer atoms sw HAS (forward-port widening).
// The atom's NodeSpec is resolved via findSpec (production registry) — no per-atom provider on main.
// Guids verified against the TiXL .cs [Guid] on the symbol class:
//   FloatsToBuffer        724da755  (numbers/data/FloatsToBuffer.cs)  — .t3 child d45b52bf
//   IntsToBuffer          2eb20a76  (numbers/int/process/IntsToBuffer.cs) — .t3 child 2bf12c9a
//   GetBufferComponents   80dff680  (render/_dx11/buffer/GetBufferComponents.cs) — .t3 3aa1/4877
//   GetSRVProperties      bc489196  (render/_dx11/api/GetSRVProperties.cs) — .t3 child b23f0e8b
//   ExecuteBufferUpdate   58351c8f  (render/_dx11/fxsetup/ExecuteBufferUpdate.cs) — .t3 d7f0248f
//   TransformsConstBuffer (present in sw; NOT in TransformPoints.t3 but part of the 6-atom map spec)
std::string swTypeForSymbolGuid(const std::string& guid) {
  static const std::map<std::string, std::string> kTable = {
      {"724da755-2d0c-42ab-8335-8c88ec5fb078", "FloatsToBuffer"},
      {"2eb20a76-f8f7-49e9-93a5-1e5981122b50", "IntsToBuffer"},
      {"80dff680-5abf-484a-b9e0-81d72f3b7aa4", "GetBufferComponents"},
      {"bc489196-9a30-4580-af6f-dc059f226da1", "GetSRVProperties"},
      {"58351c8f-4a73-448e-b7bb-69412e71bd76", "ExecuteBufferUpdate"},
      // TransformsConstBuffer symbol guid (render/_dx11/api/TransformsConstBuffer.cs:6 [Guid]).
      {"a60adc26-d7c6-4615-af78-8d2d6da46b79", "TransformsConstBuffer"},
      // compute-stage keystone: the 5 formerly-unmapped children now have sw atoms.
      {"8bef116d-7d1c-4c1b-b902-25c1d5e925a9", "ComputeShaderStage"},          // Gfx/ComputeShaderStage.cs:5
      {"b6c5be1d-b133-45e9-a269-8047ea0d6ad7", "StructuredBufferWithViews"},   // Gfx/StructuredBufferWithViews.cs:3
      {"eb68addb-ec59-416f-8608-ff9d2319f3a3", "CalcDispatchCount"},           // render/_dx11/api/CalcDispatchCount.cs:3
      {"17324ce1-8920-4653-ac67-c211ad507a81", "TransformMatrix"},             // render/_/TransformMatrix.cs:7 (sw math atom)
      // 骨4 mesh↔buffer bridge: the two nested passthrough compounds now have sw buffer atoms (each a
      // single-buffer passthrough on the collapsed MeshBuffers currency). Mapping them makes the four
      // mesh-bridge wires import NATIVELY (no golden scaffold rewire) — the mesh-family seam closure.
      {"5b9f1d97-4e10-4f31-ba83-4cbf7be9719b", "_MeshBufferComponents"},       // mesh/_/_MeshBufferComponents.cs:3
      {"e0849edd-ea1b-4657-b22d-5aa646318aa8", "_AssembleMeshBuffers"},        // mesh/_/_AssembleMeshBuffers.cs:3
      // 骨4 residual value atoms the mesh scaffold also worked around (unmapped → their wires dropped):
      {"9db2fcbf-54b9-4222-878b-80d1a0dc6edf", "BoolToFloat"},                 // numbers/bool/convert/BoolToFloat.cs:3 (sw value op)
      {"cc07b314-4582-4c2c-84b8-bb32f59fc09b", "Const"},                       // Types/Values/IntValue.cs:3 → sw Const (int const producer)
      // 骨9 mixed-slot MultiInput proof (DisplaceMeshNoise): two more value atoms feed the interleaved
      // FloatsToBuffer.Params. IntToFloat (Space/Direction int→float) and Vector3Components (AmountDistribution
      // vec3→3 floats) both already exist as sw value ops (value_op_inttofloat.cpp / value_op_vector3components.cpp).
      {"17db8a36-079d-4c83-8a2a-7ea4c1aa49e6", "IntToFloat"},                  // numbers/int/basic/IntToFloat.cs:3 (sw value op)
      {"a8083b41-951e-41f2-bb8a-9b511da26102", "Vector3Components"},           // numbers/vec3/Vector3Components.cs:3 (sw value op)
      // DataSet-timeline seam: TimeClip = the timeline-window Command scope (flow/TimeClip.cs:5 [Guid]).
      // Its per-instance TimeRange/SourceRange rides the child's Outputs[].OutputData (importer reads it
      // into SymbolChild.clips), NOT an InputValue; the cook applies the window gate + time remap.
      {"3036067a-a4c2-434b-b0e3-ac95c5c943f4", "TimeClip"},                    // flow/TimeClip.cs:5
      // image-fx collapse (Blend 子類): Vector4Components decomposes a ColorA/ColorB vec4 boundary input
      // into X/Y/Z/W scalars that feed the fx-setup FloatParams rail. sw has value_op_vector4components.cpp.
      {"b15e4950-5c72-4655-84bc-c00647319030", "Vector4Components"},           // numbers/vec4/Vector4Components.cs:3 (sw value op)
      // image-fx collapse (BubbleZoom 子類, gradient-fed): Vector2Components decomposes a Center/GainAndBias
      // vec2 boundary input into X/Y scalars that feed the fx-setup FloatParams rail. sw value_op_vector2components.cpp.
      {"0946c48b-85d8-4072-8f21-11d17cc6f6cf", "Vector2Components"},           // numbers/vec2/Vector2Components.cs:3 (sw value op)
      // NOTE: LinearGradient.t3's Multiply (17b60044) + PickFloat (63e6e642) Offset-routing subgraph is NOT
      // mapped here — it is ELIDED by the collapse (fork offset-routing-subgraph-elided-atom-reimplements,
      // t3_import_collapse.cpp), because sw's LinearGradient atom recomputes the OffsetMode selection itself.
      // 187 量產第一波: _ExecuteCombineBuffers (point/combine/_ExecuteCombineBuffers.cs:8) — the CODE-OP that
      // hides CombineBuffers.t3's whole compute-stage assembly (result alloc + per-input dispatch loop) in
      // C#. sw has a DEDICATED replay atom (buffer_ops_executecombinebuffers.cpp). Its ComputeShader child's
      // Source (CombineBuffersAsInt.hlsl) folds onto the atom's KernelName (fold pass generalized in t3_import).
      {"56f7cf15-678d-4527-a328-8666a80882d0", "_ExecuteCombineBuffers"},      // point/combine/_ExecuteCombineBuffers.cs:8
      // ComputeShader (Gfx/ComputeShader.cs) has NO standalone sw atom — its Source string folds onto the
      // ComputeShaderStage's KernelName (fork computeshader-source-folded-onto-stage, applied in a post-pass
      // in t3_import.cpp). Deliberately NOT in this table (it must NOT become a child); the fold pass handles it.

      // ---- ENGINE_GAP_GLUE.md 發現⑤ (2026-07-10, docs/agent/census/ENGINE_GAP_GLUE.md): a nested .t3 child
      // whose SymbolId is the guid of ANOTHER TiXL op sw ALREADY has a hand-coded atom for (a "flattened"
      // op — e.g. SimForceOffset.t3 embeds AddNoise, FadingSlideShow.t3 embeds Blend) used to fall through to
      // "unmapped SymbolId … skipped" because TABLE ③'s prior 85 rows only covered TiXL's own framework
      // glue (FloatsToBuffer/GetBufferComponents/…), never sw's OWN ported atoms. SAME mechanism as every row
      // above (guid → sw findSpec() type NAME) — the data source just widens from the framework whitelist to
      // node_health.sh's flattened-op census. Regenerate this exact row set: intersect
      // `node_health.sh --class flattened` (op → TiXL relpath) against every guid that appears as a
      // `"SymbolId": "<guid>"` value in ANY OTHER .t3 under external/tixl/Operators/Lib (i.e. genuinely
      // embedded somewhere), keeping only names present verbatim in `--dump-nodespec-types` (a real,
      // routable findSpec() hit — not a leaf-filename/comment false-positive).
      //   ★NOT here: guids of flattened ops that have since been RETIRED to a nested .t3 compound (AddNoise/
      //     BlendPoints/ClearSomePoints/CombineBuffers/MeshVerticesToPoints/ReorientLinePoints/SnapPointsToGrid/
      //     SnapToPoints/TransformFromClipSpace/TransformPoints/WrapPointPosition — 11 as of this commit).
      //     Mapping THEIR guid here would route through the wrong branch: t3_import.cpp:223-231 treats any
      //     non-empty swType as an ATOM (`atomicSymbolFromSpec`, expects `evaluate!=nullptr`+flat ports), but
      //     a retired op's findSpec(name) now resolves (when it resolves at all) to a COMPOUND spec
      //     (evaluate==nullptr) via graph_bridge.cpp's name-fallback — atomicSymbolFromSpec would build a
      //     hollow, childless stand-in instead of the real subgraph. Those guids are correctly left OUT of
      //     this table; they resolve instead via t3ResolveNestedCompound's GENERIC nested-compound recursion
      //     (t3_import_recurse.cpp), which already works with ZERO code change the moment the retired op's
      //     `assets/catalog_t3/<Name>.t3` sits alongside the embedding .t3 in the SAME resolver-indexed
      //     folder (verified: --probe-import against a copy of SimForceOffset.t3 placed in assets/catalog_t3/
      //     resolves its embedded AddNoise child cleanly through the already-retired compound).
      //   ★EXCLUDED (genuine gaps, not fixable by a guid row): `DrawMesh` (no sw atom under any name — TiXL's
      //     PBR-shaded full mesh draw; sw's DrawMeshUnlit is a materially different, simpler op) and
      //     `PairPointsForLines`/`PairPointsForSplines` (sw HAS a working cook via `registerPointOp`, but
      //     NEITHER has a NodeSpec entry in node_registry_point_combine.cpp — findSpec() misses; that is a
      //     separate NODE-REGISTRATION gap, not a guid-mapping one — flagged for its own lane, not patched here).
      {"946da16c-f536-4887-b764-af9468f22c0f", "Blur"},                    // image/fx/blur/Blur.t3
      {"7da55d23-0bd1-457b-a036-9b6b51d2e34b", "BlendWithMask"},           // image/use/BlendWithMask.t3
      {"27b0e1af-cb2e-4603-83f9-5c9b042d87e6", "Blob"},                    // image/generate/basic/Blob.t3
      {"1a411be2-1757-4019-8ce2-e29f808ed839", "CheckerBoard"},            // image/generate/basic/CheckerBoard.t3
      {"42d86738-d644-47c8-ab92-cc426d958e51", "ColorGrade"},              // image/color/ColorGrade.t3
      {"c276f4eb-f19c-405b-b247-3db159677571", "CombineMeshes"},           // mesh/modify/CombineMeshes.t3
      {"d5516087-f7dd-44d4-a7e1-3c18767de921", "ConvertColors"},           // image/color/ConvertColors.t3
      {"2d62dd4b-9597-4569-a09e-495abf880e34", "DepthBufferAsGrayScale"},  // image/use/DepthBufferAsGrayScale.t3
      {"1b149f1f-529c-4418-ac9d-3871f24a9e38", "Displace"},                // image/fx/distort/Displace.t3
      {"9123651a-5df8-4f85-9e14-2068f33e2ff1", "DrawBoxGizmo"},            // render/gizmo/DrawBoxGizmo.t3
      {"1998f949-5c0a-4f39-82cf-b0bda31f7f21", "DrawSphereGizmo"},         // render/gizmo/DrawSphereGizmo.t3
      {"18251874-5d5a-4384-8dcd-fcf297e54886", "FilterPoints"},            // point/modify/FilterPoints.t3
      {"5a0b0485-7a55-4bf4-ae23-04f51d890334", "FractalNoise"},            // image/generate/noise/FractalNoise.t3
      {"b5102fba-f05b-43fc-aa1d-613fe1d68ad2", "Grain"},                   // image/generate/noise/Grain.t3
      {"592a2b6f-d4e3-43e0-9e73-034cca3b3900", "ImageLevels"},             // image/analyze/ImageLevels.t3
      {"f7366bdc-86b2-4951-8788-3826126ed8c2", "KochKaleidoskope"},        // image/fx/distort/KochKaleidoskope.t3 (leaf filename forks English spelling; registered NodeSpec.type matches TiXL exactly)
      {"4ae9e2f5-7cb3-40b0-a662-0662e8cb7c68", "LinePoints"},              // point/generate/LinePoints.t3
      {"bb4803d2-0c23-470a-94a8-c477e4f7dd8c", "LinearSamplePointAttributes"}, // point/modify/LinearSamplePointAttributes.t3
      {"191e5057-4da4-447e-b7cf-e9e0ed8c5dd8", "MapPointAttributes"},      // point/modify/MapPointAttributes.t3
      {"22a3cd4e-02b3-44d7-ad2b-aab5810c5e88", "NGon"},                    // image/generate/basic/NGon.t3
      {"acc71a14-daad-4b36-b0bc-cf0a796cc5d9", "OrientPoints"},            // point/transform/OrientPoints.t3
      {"25db2a97-38b2-4503-8842-fab3922d7a6c", "PointTrailFast"},          // point/generate/PointTrailFast.t3
      {"3352d3a1-ab04-4d0a-bb43-da69095b73fd", "RadialPoints"},            // point/generate/RadialPoints.t3
      {"ec0675d7-6b72-4b15-b141-80bdd2367cd8", "RandomizePoints"},         // point/modify/RandomizePoints.t3
      {"4f89b41b-1643-442b-bec8-9f9ef2173baa", "Raster"},                  // image/generate/pattern/Raster.t3
      {"68e0d0cb-1e57-4e9c-9f22-bd7927ddb4c5", "RecomputeNormals"},        // mesh/modify/RecomputeNormals.t3
      {"780edb20-f83f-494c-ab17-7015e2311250", "RepeatAtPoints"},          // point/generate/RepeatAtPoints.t3
      {"bf7fcd04-0cf6-4518-86cc-48f74483d98d", "RoundedRect"},             // image/generate/basic/RoundedRect.t3
      {"cb28a67e-80cb-460a-8130-00e3cd85b7c2", "RyojiPattern2"},           // image/generate/pattern/RyojiPattern2.t3
      {"695d20dc-d1fe-4648-80fb-e1159b8aead4", "SelectPointsWithSDF"},     // point/modify/SelectPointsWithSDF.t3
      {"86b61bcf-4eaa-4f77-a535-8a1dc876aada", "SetPointAttributes"},      // point/modify/SetPointAttributes.t3
      {"7a08d73e-1aea-479f-8d36-ecb119d75c3a", "SimDirectionalOffset"},    // point/sim/SimDirectionalOffset.t3
      {"9c378944-9a57-4ae4-a88e-36c07244bcf7", "SimForceOffset"},          // point/sim/SimForceOffset.t3
      {"5f846187-e109-45d1-97e0-ae95e3e7d9ba", "SimNoiseOffset"},          // point/sim/SimNoiseOffset.t3
      {"697bbc2d-0b2e-4013-bbc3-d58a28a79f31", "SoftTransformPoints"},     // point/transform/SoftTransformPoints.t3
      {"1a241222-200b-417d-a8c7-131e3b48cc36", "SpherePoints"},            // point/generate/SpherePoints.t3
      {"3f0f0c40-482d-4d79-a201-b4651a0cd3a8", "SplitMeshVertices"},       // mesh/modify/SplitMeshVertices.t3
      {"d9a71078-8296-4a07-b7de-250d4e2b95ac", "Tint"},                    // image/color/Tint.t3
      // NOTE: 32e18957 (TransformImage) is ALSO kTransformImageGuid (used by t3_import_collapse.cpp's
      // image-fx collapse pass to ELIDE an identity-copy TransformImage helper — RemapColor.t3 shape). No
      // conflict: the collapse pass checks that guid literally BEFORE reaching swTypeForSymbolGuid (its own
      // dedicated pre-check, t3_import_collapse.cpp:193), so it keeps eliding inside a collapse root exactly
      // as before; THIS row only fires from the generic per-child loop (t3_import.cpp), routing a REAL
      // (non-collapse-root) TransformImage embedding to sw's actual image-transform atom.
      {"32e18957-3812-4f64-8663-18454518d005", "TransformImage"},         // image/transform/TransformImage.t3
      {"026e6917-6e6f-4ee3-b2d4-58f4f1de74c9", "TransformMesh"},           // mesh/modify/TransformMesh.t3
      {"208a86b5-55cc-460a-86e6-2b17da818494", "TransformMeshUVs"},        // mesh/modify/TransformMeshUVs.t3
      {"3f955def-cf86-4bd4-be98-ff1adea8c495", "ValueRaster"},             // image/generate/pattern/ValueRaster.t3
  };
  auto it = kTable.find(t3Lc(guid));
  return it != kTable.end() ? it->second : std::string();
}

// ---- TABLE ②: (sw op type, t3 slot guid) → sw slot NAME (= PortSpec.id). Per-atom, hand-verified
// from the TiXL .cs [Input]/[Output] Guid attributes AND cross-checked against the sw atom's
// NodeSpec port ids (buffer_ops_*.cpp). "" if unknown for that atom.
//   NOTE: sw's GetBufferComponents/GetSRVProperties atoms are STRUCTURAL PASSTHROUGHS — they expose
//   only a single "Buffer" output + a single Buffer input, NOT the SRV/UAV/ElementCount outputs the
//   .t3 wires reference. Those output guids therefore have NO sw slot name (they map to nothing) —
//   recorded here as the honest gap (a wire off them drops). Only the slots sw actually HAS are rows.
std::string swSlotNameForGuid(const std::string& swType, const std::string& slotGuid) {
  static const std::map<std::string, std::map<std::string, std::string>> kTable = {
      {"FloatsToBuffer",
       {
           {"49556d12-4cd1-4341-b9d8-c356668d296c", "Params"},      // MultiInput float (.cs:81-82)
           {"914ea6e8-abc6-4294-b895-8bfbe5afea0e", "Vec4Params"},  // MultiInput Vector4[] (.cs:78-79)
           {"f5531ffb-dbde-45d3-af2a-bd90bcbf3710", "Buffer"},      // Buffer output (.cs:11-12)
       }},
      {"IntsToBuffer",
       {
           {"49556d12-4cd1-4341-b9d8-c356668d296c", "Params"},      // MultiInput int→float rail (.cs:52-53)
           {"f5531ffb-dbde-45d3-af2a-bd90bcbf3710", "Buffer"},      // Result buffer output (.cs:11-12)
       }},
      {"GetBufferComponents",
       {
           {"7a13b834-21e5-4cef-ad5b-23c3770ea763", "BufferWithViews"},  // input (.cs:97-98)
           {"a7d11905-eb9e-42a4-a077-11d2c1cb41b2", "Buffer"},           // Buffer output (.cs:12-13)
           // VIEW OUTPUTS now MAPPED (compute-stage keystone): SRV/UAV alias the forwarded buffer on Metal
           // (fork getbuffercomponents-views-alias-buffer). These fed the compute stage's SRV/UAV wires.
           {"1368ab8e-d75e-429f-8ecd-0944f3ede9ab", "ShaderResourceView"},   // SRV output (.cs:47)
           {"f03246a7-e39f-4a41-a0c3-22bc976a6000", "UnorderedAccessView"},  // UAV output (.cs:48)
           // 骨9: Length output (.cs:60 = SRV.Description.Buffer.ElementCount) rides the Buffer view rail —
           // sw's GetBufferComponents "Buffer" output IS the passthrough buffer carrying elementCount, and
           // StructuredBufferWithViews.Count reads a wired Buffer input's elementCount. So mapping Length →
           // "Buffer" makes DisplaceMeshNoise's SBV.Count ← GBC(mesh).Length resolve to the mesh vertex
           // count. Same fork as GetSRVProperties.ElementCount → Buffer view rail (elementcount-is-buffer-view).
           {"d7918fd8-906e-424d-8c5c-9631941cfc9d", "Buffer"},               // Length output → Buffer view rail (骨9)
           // Stride / IsValid scalar outputs still DEFERRED (ride the SwBuffer metadata).
       }},
      {"GetSRVProperties",
       {
           {"e79473f4-3fd2-467e-acda-b27ef7dae6a9", "SRV"},     // input (.cs:36-37)
           {"59c4fe70-9129-4bce-ba39-6d252a59fb97", "Buffer"},  // Buffer output (.cs:9-10)
           // ElementCount now MAPPED (compute-stage keystone): rides the Buffer view rail (fork
           // getsrvproperties-elementcount-is-buffer-view) → StructuredBufferWithViews.Count reads it.
           {"431b39fd-4b62-478b-bbfa-4346102c3f61", "ElementCount"},  // element count output (.cs:6-7,28)
       }},
      {"ComputeShaderStage",
       {
           {"34cf06fe-8f63-4f14-9c59-35a2c021b817", "ConstantBuffers"},  // b0.. (.cs:248-249)
           {"88938b09-d5a7-437c-b6e1-48a5b375d756", "ShaderResources"},  // t0.. (.cs:251-252)
           {"599384c2-bf6c-4953-be74-d363292ab1c7", "Uavs"},             // u0.. (.cs:257-258)
           {"c382284f-7e37-4eb0-b284-bc735247f26b", "Output"},           // Command→Buffer output (.cs:8-9)
           // Dispatch(180cae35) / DispatchCallCount / ComputeShader(5c0e9c96 = folded) / SamplerStates /
           // VariousResources / UavBufferCounter: NO sw slot (folded/ignored) → those wires drop honestly.
       }},
      {"StructuredBufferWithViews",
       {
           {"16f98211-fe97-4235-b33a-ddbbd2b5997f", "Count"},            // Count int rail (view collapse, .cs:74-75)
           {"c997268d-6709-49de-980e-64d7a47504f7", "BufferWithViews"},  // allocated output (.cs:7-8)
           {"0016dd87-8756-4a97-a0da-096e1a879c05", "Stride"},           // Stride param InputValue (=64, .cs:77-78)
       }},
      {"CalcDispatchCount",
       {
           {"f79ccc37-05fd-4f81-97d6-6c1cafca180c", "Count"},          // element count via view rail (.cs:24-25)
           {"35c0e513-812f-49e2-96fa-17541751c19b", "DispatchCount"},  // count passthrough (.cs:6-7)
           // ThreadGroupSize(3979e440) → folded (stage derives dispatch from SRV); that wire drops.
       }},
      {"TransformMatrix",
       {
           // sw TransformMatrix decomposes Vector3 inputs into .x/.y/.z; a single .t3 Vector3 wire lands on
           // the HEAD (.x) port (the established vec-decomposition fork). Y/Z of a WIRED vector are not
           // separately routable from one .t3 wire — flagged as fork-t3-vec3-wire-lands-on-head.
           {"3b817e6c-f532-4a8c-a2ff-a00dc926eeb2", "Translation.x"},          // Translation (Vec3)
           {"5339862d-5a18-4d0c-b908-9277f5997563", "Rotation_PitchYawRoll.x"},// Rotation (Vec3)
           {"58b9dfb6-0596-4f0d-baf6-7fb3ae426c94", "Scale.x"},                // Stretch/Scale (Vec3)
           {"566f1619-1de0-4b41-b167-7fc261730d62", "UniformScale"},           // Scale (uniform float)
           {"f53f3311-e1fc-418b-8861-74adc175d5fa", "Shear.x"},                // Shearing (Vec3)
           {"279730b7-c427-4924-9fde-77eb65a3076c", "Pivot.x"},                // Pivot (Vec3)
           {"751e97de-c418-48c7-823e-d4660073a559", "Result"},                 // the 4-row matrix output
       }},
      {"ExecuteBufferUpdate",
       {
           {"51110d89-083e-42b8-b566-87b144dfbed9", "UpdateCommand"},    // Command input (.cs:30-31)
           {"72cfe742-88fb-41cd-b6cf-d96730b24b23", "BufferWithViews"},  // Buffer input (.cs:33-34)
           {"6887f319-cf3f-4e87-9a8c-a7c912dbf5ad", "IsEnabled"},        // bool input (.cs:36-37)
           {"9a66687e-a834-452c-a652-ba1fc70c2c7b", "Output2"},          // Buffer output (.cs:6-7)
       }},
      {"TransformsConstBuffer",
       {
           {"7a76d147-4b8e-48cf-aa3e-aac3aa90e888", "Buffer"},  // main Buffer output (.cs:9-10 [Output])
       }},
      // 骨4: the two mesh-bridge passthrough atoms. Slot guids from _MeshBufferComponents.cs /
      // _AssembleMeshBuffers.cs [Input]/[Output]; the sw atom port ids match (single-buffer passthroughs).
      {"_MeshBufferComponents",
       {
           {"1b0b7587-de86-4fc4-be78-a21392e8aa9b", "MeshBuffers"},  // input (.cs:61-62)
           {"0c5e2ec1-ab60-43ce-b823-3df096ff9a28", "Vertices"},    // output (.cs:6-7)
           {"78c53086-bb28-4c58-8b51-42cfdf6620c4", "Indices"},     // output (.cs:9-10)
           {"8fef2e09-4f1e-4ba8-8d62-858c3fb0ac23", "ChunkDefs"},   // output (.cs:13-14)
       }},
      {"_AssembleMeshBuffers",
       {
           {"d71893dd-6ca2-4ab7-9e04-0bd7285eccfb", "NewMeshBuffers"},  // output (.cs:6-7)
           {"ba53b274-62ca-40a2-b8d2-87d08f0bc259", "Vertices"},       // input (.cs:45-46) — the load-bearing feed
           {"892838c5-fa5a-418e-81d6-a3a523819324", "Indices"},        // input (.cs:48-49)
           {"87116a9a-beb0-4e2a-a2ef-f3e0b357f81d", "ChunkDefs"},      // input (.cs:51-52)
           {"5e82e351-e8a8-4594-83e3-e86c888d0588", "PrepareCommand"}, // input (.cs:54-55)
       }},
      // 骨4: BoolToFloat (sw value op) — supplies the useVertexSelection scalar into FloatsToBuffer.Params.
      {"BoolToFloat",
       {
           {"253b9ae4-fac5-4641-bf0c-d8614606a840", "BoolValue"},  // bool input (.cs:23-24)
           {"24ffa0a7-9195-4b38-9c88-37cf4c3afc36", "ForFalse"},   // float input (.cs:26-27)
           {"0a53a4ff-4dfb-455a-b70b-0d7eed5e5f22", "ForTrue"},    // float input (.cs:29-30)
           {"f0321a54-e844-482f-a161-7f137abc54b0", "Result"},     // float output (.cs:6-7)
       }},
      // 骨4: IntValue (Types/Values/IntValue.cs) → sw Const. PBRVertex.Stride feeds SBV.Stride. NAMED FORK
      // `pbrvertex-stride-64-to-80-swvertex`: the .t3 value is 64 (TiXL PbrVertex), but sw's SwVertex is
      // 80B — the golden overrides this Const child's `value` to 80 (the imported wire IntValue→SBV.Stride
      // is faithful; only the constant is forked, authored ON the imported child).
      {"Const",
       {
           {"4515c98e-05bc-4186-8773-4d2b31a8c323", "value"},  // Int input (.cs:19-20)
           {"8a65b34b-40be-4dbf-812c-d4c663464c7f", "out"},    // Result output (.cs:6-7)
       }},
      // 骨9: IntToFloat (numbers/int/basic/IntToFloat.cs) — Space/Direction int→float into FloatsToBuffer.Params.
      // sw port ids from value_op_inttofloat.cpp: IntValue (input) / out (output).
      {"IntToFloat",
       {
           {"01809b63-4b4a-47be-9588-98d5998ddb0c", "IntValue"},  // int input (.cs:19-20)
           {"db1073a1-b9d8-4d52-bc5c-7ae8c0ee1ac3", "out"},       // Result output (.cs:6-7)
       }},
      // 187 量產第一波: _ExecuteCombineBuffers (point/combine/_ExecuteCombineBuffers.cs). The ComputeShader
      // input (d91e52f2) is NOT a row: like the ComputeShaderStage.ComputeShader slot, it is the FOLD target
      // (the ComputeShader child's Source rides onto KernelName via the generalized fold pass), never a wire.
      {"_ExecuteCombineBuffers",
       {
           {"c8a5769e-2536-4caa-8380-22fbeed1ef12", "InputBuffers"},  // MultiInput<BufferWithViews> (.cs:169-170)
           {"d6770718-842e-441d-a5f6-db9b2a20839b", "Output"},        // combined Buffer output (.cs:11-12)
       }},
      // 骨9: Vector3Components (numbers/vec3/Vector3Components.cs) — AmountDistribution vec3 → 3 scalar floats.
      // sw port ids from value_op_vector3components.cpp: Value.x head (the vec3-on-head fork), X/Y/Z outputs.
      {"Vector3Components",
       {
           {"bc217d95-25d4-44e8-b5ba-05b7facd9a20", "Value.x"},  // Vector3 input → lands on .x head (fork)
           {"2f05b628-8fc0-46dc-b312-9b107b8ca4a2", "X"},        // X output (.cs:6-7)
           {"f07622c1-aca1-4b8b-8e4a-42d94be87539", "Y"},        // Y output (.cs:8-9)
           {"5173cf99-c9ae-4da4-8b7a-a6b6f27daa84", "Z"},        // Z output (.cs:10-11)
       }},
      // image-fx collapse (Blend 子類): Vector4Components. Value(980ef785) vec4 input lands on the .x head
      // (same vec-on-head fork as Vector3Components); X/Y/Z/W outputs feed the collapsed atom's scalar rail.
      // sw port ids from value_op_vector4components.cpp.
      {"Vector4Components",
       {
           {"980ef785-6ae2-44d1-803e-febfc75791c5", "Value.x"},  // Vector4 input → .x head (fork)
           {"cfb58526-0053-4bca-aa85-d83823efba96", "X"},        // X output (.cs)
           {"2f8e90dd-ba03-43dc-82a2-8d817df45cc7", "Y"},        // Y output (.cs)
           {"162bb4fe-3c59-45c2-97cc-ecba85c1b275", "Z"},        // Z output (.cs)
           {"e1dede5f-6963-4bcc-aa12-abeb819bb5da", "W"},        // W output (.cs)
       }},
      // image-fx collapse (BubbleZoom 子類): Vector2Components. Value(36f14238) vec2 input lands on the .x
      // head (same vec-on-head fork); X/Y outputs feed the collapsed atom's FloatParams scalar rail.
      // sw port ids from value_op_vector2components.cpp; slot guids from Vector2Components.cs [Input]/[Output].
      {"Vector2Components",
       {
           {"36f14238-5bb8-4521-9533-f4d1e8fb802b", "Value.x"},  // Vector2 input → .x head (fork)
           {"1cee5adb-8c3c-4575-bdd6-5669c04d55ce", "X"},        // X output (.cs:6-7)
           {"305d321d-3334-476a-9fa3-4847912a4c58", "Y"},        // Y output (.cs:8-9)
       }},
      // DataSet-timeline seam: TimeClip slots (flow/TimeClip.cs). The Output slot is where the child's
      // TimeClip OutputData is anchored (the importer keys child.clips by THIS name = "out"). The Command
      // MultiInput is the SubTree the window gate wraps (= sw "SubTree", the SetTime precedent name).
      {"TimeClip",
       {
           {"de6ff8b5-40fe-47fa-b9f2-d926b17f9a7f", "out"},       // Output TimeClipSlot<Command> (.cs:7-8)
           {"35f501f4-5c79-4628-9441-8b3782544bf6", "SubTree"},   // Command MultiInput (.cs:44-45)
       }},
  };
  auto t = kTable.find(swType);
  if (t == kTable.end()) return std::string();
  auto s = t->second.find(t3Lc(slotGuid));
  return s != t->second.end() ? s->second : std::string();
}


}  // namespace sw
