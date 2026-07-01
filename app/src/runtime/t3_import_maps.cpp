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
      // image-fx collapse (Blend 子類): Vector4Components decomposes a ColorA/ColorB vec4 boundary input
      // into X/Y/Z/W scalars that feed the fx-setup FloatParams rail. sw has value_op_vector4components.cpp.
      {"b15e4950-5c72-4655-84bc-c00647319030", "Vector4Components"},           // numbers/vec4/Vector4Components.cs:3 (sw value op)
      // 187 量產第一波: _ExecuteCombineBuffers (point/combine/_ExecuteCombineBuffers.cs:8) — the CODE-OP that
      // hides CombineBuffers.t3's whole compute-stage assembly (result alloc + per-input dispatch loop) in
      // C#. sw has a DEDICATED replay atom (buffer_ops_executecombinebuffers.cpp). Its ComputeShader child's
      // Source (CombineBuffersAsInt.hlsl) folds onto the atom's KernelName (fold pass generalized in t3_import).
      {"56f7cf15-678d-4527-a328-8666a80882d0", "_ExecuteCombineBuffers"},      // point/combine/_ExecuteCombineBuffers.cs:8
      // ComputeShader (Gfx/ComputeShader.cs) has NO standalone sw atom — its Source string folds onto the
      // ComputeShaderStage's KernelName (fork computeshader-source-folded-onto-stage, applied in a post-pass
      // in t3_import.cpp). Deliberately NOT in this table (it must NOT become a child); the fold pass handles it.
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
  };
  auto t = kTable.find(swType);
  if (t == kTable.end()) return std::string();
  auto s = t->second.find(t3Lc(slotGuid));
  return s != t->second.end() ? s->second : std::string();
}

// ── IMAGE-FX COLLAPSE SEAM tables (image-fx-wrapper-collapses-to-tex-atom) ────────────────────────────
// The fx-setup framework wrapper SymbolIds (the child collapsed AWAY). Guids from the .cs [Guid]:
//   _multiImageFxSetupStatic  cc34a183  (image/fx/_/_multiImageFxSetupStatic.cs:3) — HSE's single child
//   _multiImageFxSetup        a2567844  (image/fx/_/_multiImageFxSetup.cs:3)
//   _trippleImageFxSetup      5b999887  (image/fx/_/_trippleImageFxSetup.cs:3)
bool isImageFxSetupGuid(const std::string& guid) {
  static const std::map<std::string, bool> kSet = {
      {"cc34a183-3978-4b6b-8ef1-dd8102410816", true},  // _multiImageFxSetupStatic
      {"a2567844-3314-48de-bda7-7904b5546535", true},  // _multiImageFxSetup
      {"5b999887-19df-4e91-9f58-1df2d8f1440b", true},  // _trippleImageFxSetup
  };
  return kSet.count(t3Lc(guid)) != 0;
}

// TABLE ④b: ROOT symbol guid → sw tex op type. Only roots whose .t3 is a KNOWN, SINGLE-fx-setup-child
// wrapper we can collapse 1:1 to a flat sw tex atom (ports match the op's .cs). Add an image-fx op =
// add a row here + its ④c fixed-slot rows + its ④d FloatParams order (data-driven, ARCHITECTURE rule 7).
std::string swTexOpForCollapseRootGuid(const std::string& rootGuid) {
  static const std::map<std::string, std::string> kTable = {
      // HSE (image/color/HSE.t3): the ONLY pure single-child cc34a183 wrapper. Root guid = HSE.cs [Guid].
      {"3c8003e8-70ca-4d71-9294-3df845bbb4a5", "HSE"},
      // Blend (image/use/Blend.t3): the MULTI-child generalization proof. Root = Blend.cs [Guid]. Its
      // fx-setup child's FloatParams rail is fed by helper value ops (Vector4Components×2 → ColorA/ColorB,
      // IntToFloat×3 → BlendMode/AlphaMode/ScaleMode, BoolToFloat → NormalForUpperHalf), all KEPT as real
      // sw children by the collapse (they have atoms + map rows). Proves the collapse table is not HSE-only.
      {"9f43f769-d32a-4f49-92ac-e0be3ba250cf", "Blend"},
  };
  auto it = kTable.find(t3Lc(rootGuid));
  return it != kTable.end() ? it->second : std::string();
}

// TABLE ④c: (collapsed swType, fx-setup-child FIXED slot guid) → sw tex atom port NAME. The fx-setup
// framework's ImageA/ImageB/Output slot guids are SHARED across all wrappers (same _*FxSetup*.cs), so
// these rows are effectively per-framework, keyed by swType for clarity. FloatParams (2929c4c9) is NOT
// here — it is the positional scalar rail resolved by ④d (swFloatParamOrderForCollapse).
std::string swCollapseSlotNameForGuid(const std::string& swType, const std::string& slotGuid) {
  static const std::map<std::string, std::map<std::string, std::string>> kTable = {
      {"HSE",
       {
           {"55126bff-8c94-415d-96dd-3c16e216e663", "Image"},      // _*FxSetupStatic.ImageA  → HSE.Image
           {"0bb90f8d-88c9-4a99-b44f-f284b505c65b", "FxTexture"},  // _*FxSetupStatic.ImageB  → HSE.FxTexture
           {"76b6c677-12db-4404-aff7-ee3391d2d831", "out"},        // _*FxSetupStatic.Output  → HSE.out
       }},
      {"Blend",
       {
           {"55126bff-8c94-415d-96dd-3c16e216e663", "ImageA"},  // _*FxSetupStatic.ImageA → Blend.ImageA
           {"0bb90f8d-88c9-4a99-b44f-f284b505c65b", "ImageB"},  // _*FxSetupStatic.ImageB → Blend.ImageB
           {"76b6c677-12db-4404-aff7-ee3391d2d831", "out"},     // _*FxSetupStatic.Output → Blend.out
       }},
  };
  auto t = kTable.find(swType);
  if (t == kTable.end()) return std::string();
  auto s = t->second.find(t3Lc(slotGuid));
  return s != t->second.end() ? s->second : std::string();
}

// TABLE ④d: for a collapsed swType, the ORDERED sw scalar port names that the fx-setup FloatParams
// MultiInput (2929c4c9) wires land on positionally. HSE.t3 wires Hue,Saturation,Exposure IN THAT ORDER
// into FloatParams (verified against HSE.t3 Connections + HueShift.hlsl cbuffer {Hue,Saturation,Exposure}).
const std::vector<std::string>& swFloatParamOrderForCollapse(const std::string& swType) {
  static const std::map<std::string, std::vector<std::string>> kTable = {
      {"HSE", {"Hue", "Saturation", "Exposure"}},
      // Blend FloatParams rail (verified against Blend.t3 wire order + Blend.cs input order + guids):
      // ColorA(Vec4C).xyzw, ColorB(Vec4C).xyzw, then BlendMode, AlphaMode, NormalForUpperHalf, ScaleMode
      // (the int/bool scalars, fed through IntToFloat/BoolToFloat helpers). Matches Blend atom port ids.
      {"Blend",
       {"ColorA.x", "ColorA.y", "ColorA.z", "ColorA.w",
        "ColorB.x", "ColorB.y", "ColorB.z", "ColorB.w",
        "BlendMode", "AlphaMode", "NormalForUpperHalf", "ScaleMode"}},
  };
  static const std::vector<std::string> kEmpty;
  auto it = kTable.find(swType);
  return it != kTable.end() ? it->second : kEmpty;
}

}  // namespace sw
