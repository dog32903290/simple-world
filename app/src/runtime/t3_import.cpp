// runtime/t3_import — .t3 importer impl (see t3_import.h for the forward-port rationale + honest
// scope). Pure CPU: strip .t3 inline comments, crude_json parse, three Guid→sw maps, fill one Symbol.
#include "runtime/t3_import.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

#include "crude_json.h"          // same JSON lib compound_save uses (NODE_EDITOR_DIR on -I path)
#include "runtime/graph.h"       // NodeSpec / findSpec (production atom registry)
#include "runtime/graph_bridge.h"  // atomicSymbolFromSpec

namespace sw {

// Test-only injection seam (routing RED case). When set, the FIRST MultiInput collision (two wires
// into the same (childId, slotName)) has its connection order REVERSED — corrupting only the order.
bool& t3ImportInjectBug() {
  static bool flag = false;
  return flag;
}

namespace {

// ---- guid normalization: .cs writes UPPERCASE input guids, .t3 writes lowercase. Lowercase so the
// per-atom tables (keyed lowercase) match either source. Empty guid stays empty.
std::string lc(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

constexpr const char* kGuidEmpty = "00000000-0000-0000-0000-000000000000";
bool isBoundaryGuid(const std::string& g) { return g.empty() || g == kGuidEmpty; }

// ---- TABLE ③: t3 symbol guid → sw op type. The 6 buffer atoms sw HAS (forward-port widening).
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
      // ComputeShader (Gfx/ComputeShader.cs) has NO standalone sw atom — its Source string folds onto the
      // ComputeShaderStage's KernelName (fork computeshader-source-folded-onto-stage, applied in a post-pass
      // below). Deliberately NOT in this table (it must NOT become a child); the fold pass handles it.
  };
  auto it = kTable.find(lc(guid));
  return it != kTable.end() ? it->second : std::string();
}

// The ComputeShader symbol guid (Gfx/ComputeShader.cs [Guid]) + its Source input guid + its CS output
// guid. The fold post-pass reads Source off a ComputeShader child, finds the ComputeShaderStage it wires
// its CS output into, and sets that stage's KernelName strOverride (fork computeshader-source-folded-
// onto-stage). ComputeShader itself never becomes an sw child.
constexpr const char* kComputeShaderGuid = "a256d70f-adb3-481d-a926-caf35bd3e64c";
constexpr const char* kComputeShaderSourceSlot = "afb69c81-5063-4cb9-9d42-841b994b5ec0";  // Source (String)
constexpr const char* kComputeShaderCsOutSlot = "6c118567-8827-4422-86cc-4d4d00762d87";   // CS output
constexpr const char* kComputeStageCsInSlot = "5c0e9c96-9aba-4757-ae1f-cc50fb6173f1";     // ComputeShaderStage.ComputeShader in

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
           // Length / Stride / IsValid scalar outputs still DEFERRED (ride the SwBuffer metadata).
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
  };
  auto t = kTable.find(swType);
  if (t == kTable.end()) return std::string();
  auto s = t->second.find(lc(slotGuid));
  return s != t->second.end() ? s->second : std::string();
}

std::string asStr(const crude_json::value& v, const char* key) {
  return v[key].is_string() ? v[key].get<crude_json::string>() : std::string();
}

// Strip TiXL inline `/* ... */` comments so crude_json can parse. Quote state tracked so a literal
// "/*" inside a JSON string value survives verbatim.
std::string stripT3Comments(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  bool inStr = false;
  for (size_t i = 0; i < in.size(); ++i) {
    char c = in[i];
    if (inStr) {
      out.push_back(c);
      if (c == '\\' && i + 1 < in.size()) {
        out.push_back(in[++i]);
      } else if (c == '"') {
        inStr = false;
      }
      continue;
    }
    if (c == '"') { inStr = true; out.push_back(c); continue; }
    if (c == '/' && i + 1 < in.size() && in[i + 1] == '*') {
      size_t end = in.find("*/", i + 2);
      if (end == std::string::npos) break;
      i = end + 1;
      continue;
    }
    out.push_back(c);
  }
  return out;
}

}  // namespace

bool importT3Symbol(const std::string& t3Json, SymbolLibrary& lib, std::string* outSymbolId,
                    std::vector<std::string>* warnings) {
  auto warn = [&](const std::string& m) { if (warnings) warnings->push_back(m); };

  crude_json::value root = crude_json::value::parse(stripT3Comments(t3Json));
  if (!root.is_object()) { warn("t3: not a JSON object"); return false; }

  const std::string symGuid = lc(asStr(root, "Id"));
  if (symGuid.empty()) { warn("t3: missing top-level Id"); return false; }

  Symbol sym;
  sym.id = symGuid;
  sym.name = symGuid;
  sym.atomic = false;

  // Top-level Inputs[] → the symbol's external input SlotDefs (boundary ports). Named by guid.
  if (root["Inputs"].is_array()) {
    for (const crude_json::value& iv : root["Inputs"].get<crude_json::array>()) {
      if (!iv.is_object()) continue;
      const std::string sid = lc(asStr(iv, "Id"));
      if (sid.empty()) { warn("t3: input slot missing Id, skipped"); continue; }
      SlotDef d;
      d.id = sid;
      d.name = sid;
      d.dataType = "Float";
      if (iv["DefaultValue"].is_number()) d.def = (float)iv["DefaultValue"].get<crude_json::number>();
      sym.inputDefs.push_back(d);
    }
  }

  // ---- TABLE ①: t3 child guid → int childId. Built while walking Children[].
  std::map<std::string, int> childGuidToId;
  std::map<int, std::string> childIdToSwType;
  // Fold pass state (computeshader-source-folded-onto-stage): ComputeShader child guid → its Source string.
  // ComputeShader is NOT emitted as an sw child; its Source rides onto the ComputeShaderStage it feeds.
  std::map<std::string, std::string> computeShaderSource;
  int nextChildId = 1;

  if (root["Children"].is_array()) {
    for (const crude_json::value& cv : root["Children"].get<crude_json::array>()) {
      if (!cv.is_object()) continue;
      const std::string childGuid = lc(asStr(cv, "Id"));
      const std::string symbolId = asStr(cv, "SymbolId");
      if (childGuid.empty()) { warn("t3: child missing Id, skipped"); continue; }
      // Fold: a ComputeShader child contributes its Source (not a child); capture it for the post-pass.
      if (lc(symbolId) == kComputeShaderGuid) {
        if (cv["InputValues"].is_array())
          for (const crude_json::value& ivv : cv["InputValues"].get<crude_json::array>()) {
            if (ivv.is_object() && lc(asStr(ivv, "Id")) == kComputeShaderSourceSlot &&
                ivv["Value"].is_string())
              computeShaderSource[childGuid] = ivv["Value"].get<crude_json::string>();
          }
        continue;  // ComputeShader itself is never an sw child
      }
      const std::string swType = swTypeForSymbolGuid(symbolId);
      if (swType.empty()) {
        warn("t3: child " + childGuid + " unmapped SymbolId " + lc(symbolId) +
             " (no sw atom — e.g. ComputeShaderStage/StructuredBufferWithViews/TransformMatrix), skipped");
        continue;
      }
      const NodeSpec* fs = findSpec(swType);
      if (!fs) {
        warn("t3: no NodeSpec in findSpec for type " + swType + ", child skipped");
        continue;
      }
      if (!lib.symbols.count(swType)) lib.symbols[swType] = atomicSymbolFromSpec(*fs);

      const int childId = nextChildId++;
      childGuidToId[childGuid] = childId;
      childIdToSwType[childId] = swType;

      SymbolChild child;
      child.id = childId;
      child.symbolId = swType;

      // InputValues[] → constant overrides on THIS instance (non-default only).
      if (cv["InputValues"].is_array()) {
        for (const crude_json::value& ivv : cv["InputValues"].get<crude_json::array>()) {
          if (!ivv.is_object()) continue;
          const std::string slotGuid = lc(asStr(ivv, "Id"));
          const std::string slotName = swSlotNameForGuid(swType, slotGuid);
          if (slotName.empty()) {
            warn("t3: child " + childGuid + " InputValue unknown slot " + slotGuid + ", skipped");
            continue;
          }
          const std::string vtype = asStr(ivv, "Type");
          if (vtype == "System.Single" || vtype == "System.Int32") {
            if (ivv["Value"].is_number())
              child.overrides[slotName] = (float)ivv["Value"].get<crude_json::number>();
          } else if (vtype == "System.String") {
            if (ivv["Value"].is_string())
              child.strOverrides[slotName] = ivv["Value"].get<crude_json::string>();
          } else if (vtype == "System.Boolean") {
            if (ivv["Value"].is_boolean())
              child.overrides[slotName] = ivv["Value"].get<crude_json::boolean>() ? 1.0f : 0.0f;
          } else {
            // Int3 (Dispatch) etc. — no scalar sw slot; drop with a warning (honest gap).
            warn("t3: child " + childGuid + " InputValue type " + vtype + " unsupported, skipped");
          }
        }
      }
      sym.children.push_back(child);
    }
  }
  sym.nextChildId = nextChildId;

  // ---- FOLD PASS (computeshader-source-folded-onto-stage): for each raw connection ComputeShader.CS →
  // ComputeShaderStage.ComputeShader, set the STAGE child's KernelName strOverride to the ComputeShader's
  // Source. ComputeShader has no sw child, so this is the ONLY way its Source reaches the dispatch.
  if (!computeShaderSource.empty() && root["Connections"].is_array()) {
    for (const crude_json::value& wv : root["Connections"].get<crude_json::array>()) {
      if (!wv.is_object()) continue;
      const std::string srcGuid = lc(asStr(wv, "SourceParentOrChildId"));
      const std::string srcSlot = lc(asStr(wv, "SourceSlotId"));
      const std::string dstGuid = lc(asStr(wv, "TargetParentOrChildId"));
      const std::string dstSlot = lc(asStr(wv, "TargetSlotId"));
      auto cs = computeShaderSource.find(srcGuid);
      if (cs == computeShaderSource.end() || srcSlot != kComputeShaderCsOutSlot) continue;
      if (dstSlot != kComputeStageCsInSlot) continue;
      auto dit = childGuidToId.find(dstGuid);
      if (dit == childGuidToId.end()) continue;  // stage not mapped (shouldn't happen)
      for (SymbolChild& ch : sym.children)
        if (ch.id == dit->second) { ch.strOverrides["KernelName"] = cs->second; break; }
    }
  }

  // Connections[] → SymbolConnection 4-tuples, ARRAY ORDER PRESERVED (MultiInput order).
  auto slotNameForEndpoint = [&](int childId, const std::string& slotGuid) -> std::string {
    if (childId == kSymbolBoundary) return lc(slotGuid);  // matches SlotDef.id from Inputs[]
    auto t = childIdToSwType.find(childId);
    if (t == childIdToSwType.end()) return std::string();
    return swSlotNameForGuid(t->second, slotGuid);
  };

  std::vector<SymbolConnection> conns;
  if (root["Connections"].is_array()) {
    for (const crude_json::value& wv : root["Connections"].get<crude_json::array>()) {
      if (!wv.is_object()) continue;
      const std::string srcChildGuid = lc(asStr(wv, "SourceParentOrChildId"));
      const std::string dstChildGuid = lc(asStr(wv, "TargetParentOrChildId"));
      const std::string srcSlotGuid = asStr(wv, "SourceSlotId");
      const std::string dstSlotGuid = asStr(wv, "TargetSlotId");

      int srcChild = kSymbolBoundary, dstChild = kSymbolBoundary;
      if (!isBoundaryGuid(srcChildGuid)) {
        auto it = childGuidToId.find(srcChildGuid);
        if (it == childGuidToId.end()) {
          warn("t3: wire src child " + srcChildGuid + " unmapped (skipped op), dropped");
          continue;
        }
        srcChild = it->second;
      }
      if (!isBoundaryGuid(dstChildGuid)) {
        auto it = childGuidToId.find(dstChildGuid);
        if (it == childGuidToId.end()) {
          warn("t3: wire dst child " + dstChildGuid + " unmapped (skipped op), dropped");
          continue;
        }
        dstChild = it->second;
      }
      const std::string srcSlot = slotNameForEndpoint(srcChild, srcSlotGuid);
      const std::string dstSlot = slotNameForEndpoint(dstChild, dstSlotGuid);
      if (srcSlot.empty() || dstSlot.empty()) {
        warn("t3: wire slot unresolved (src=" + srcSlotGuid + " dst=" + dstSlotGuid + "), dropped");
        continue;
      }
      SymbolConnection c;
      c.srcChild = srcChild;
      c.srcSlot = srcSlot;
      c.dstChild = dstChild;
      c.dstSlot = dstSlot;
      conns.push_back(c);
    }
  }

  // Routing RED tooth: reverse the FIRST MultiInput collision's order.
  if (t3ImportInjectBug()) {
    for (size_t i = 0; i + 1 < conns.size(); ++i) {
      for (size_t j = i + 1; j < conns.size(); ++j) {
        if (conns[i].dstChild == conns[j].dstChild && conns[i].dstSlot == conns[j].dstSlot) {
          std::swap(conns[i], conns[j]);
          goto doneSwap;
        }
      }
    }
  doneSwap:;
  }

  sym.connections = std::move(conns);

  lib.symbols[sym.id] = sym;
  if (lib.rootId.empty()) lib.rootId = sym.id;
  if (outSymbolId) *outSymbolId = sym.id;
  return true;
}

}  // namespace sw
