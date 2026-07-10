// runtime/t3_import_texcompute_cb — CB-scalar trace impl (split from t3_import_texcompute.cpp for the
// ≤400-line ratchet). Pure CPU JSON walk (a runtime leaf); see the header for the contract.
#include "runtime/t3_import_texcompute_cb.h"

#include "crude_json.h"
#include "runtime/t3_import_internal.h"  // t3i::{lc,asStr,isBoundaryGuid}

namespace sw {

using t3i::asStr;
using t3i::isBoundaryGuid;
using t3i::lc;

// ── STAGE 2 CB-trace guids (the value ops that feed FloatsToBuffer.Params in the SRV-tex shape). ──
const char* const kCbIntToFloatSym = "17db8a36-079d-4c83-8a2a-7ea4c1aa49e6";
const char* const kCbBoolToFloatSym = "9db2fcbf-54b9-4222-878b-80d1a0dc6edf";
const char* const kCbVec2ComponentsSym = "0946c48b-85d8-4072-8f21-11d17cc6f6cf";

namespace {
constexpr const char* kIntToFloatInSlot = "01809b63-4b4a-47be-9588-98d5998ddb0c";       // IntValue
constexpr const char* kBoolToFloatInSlot = "253b9ae4-fac5-4641-bf0c-d8614606a840";       // BoolValue
constexpr const char* kBoolToFloatFalseSlot = "24ffa0a7-9195-4b38-9c88-37cf4c3afc36";    // ForFalse (def 0)
constexpr const char* kBoolToFloatTrueSlot = "0a53a4ff-4dfb-455a-b70b-0d7eed5e5f22";     // ForTrue (def 1)
constexpr const char* kVec2ComponentsInSlot = "36f14238-5bb8-4521-9533-f4d1e8fb802b";    // Value (vec2)
constexpr const char* kVec2ComponentsYOut = "305d321d-3334-476a-9fa3-4847912a4c58";      // Y output (X = else)

// Read a boundary Input[].DefaultValue as a float. comp: 0 = scalar/vec.X, 1 = vec.Y. bool → 0/1.
float boundaryDefaultFloat(const crude_json::value& root, const std::string& slotId, int comp) {
  if (!root["Inputs"].is_array()) return 0.0f;
  for (const crude_json::value& iv : root["Inputs"].get<crude_json::array>()) {
    if (!iv.is_object() || lc(asStr(iv, "Id")) != lc(slotId)) continue;
    const crude_json::value& dv = iv["DefaultValue"];
    if (dv.is_number()) return (float)dv.get<crude_json::number>();
    if (dv.is_boolean()) return dv.get<crude_json::boolean>() ? 1.0f : 0.0f;
    if (dv.is_object()) {
      const char* k = comp == 1 ? "Y" : "X";
      if (dv[k].is_number()) return (float)dv[k].get<crude_json::number>();
    }
    return 0.0f;
  }
  return 0.0f;
}

// The FIRST wire feeding (dstGuid, dstSlot): out (srcGuid, srcSlot). false if none.
bool wireInto(const crude_json::value& root, const std::string& dstGuid, const std::string& dstSlot,
              std::string& srcGuid, std::string& srcSlot) {
  if (!root["Connections"].is_array()) return false;
  for (const crude_json::value& wv : root["Connections"].get<crude_json::array>()) {
    if (!wv.is_object()) continue;
    if (lc(asStr(wv, "TargetParentOrChildId")) == dstGuid && lc(asStr(wv, "TargetSlotId")) == dstSlot) {
      srcGuid = lc(asStr(wv, "SourceParentOrChildId"));
      srcSlot = lc(asStr(wv, "SourceSlotId"));
      return true;
    }
  }
  return false;
}

// A child's InputValue as a float (numbers/bools), else `def` (BoolToFloat ForTrue/ForFalse fallback).
float childInputValueFloat(const crude_json::value& child, const char* slotGuid, float def) {
  if (!child["InputValues"].is_array()) return def;
  for (const crude_json::value& iv : child["InputValues"].get<crude_json::array>()) {
    if (!iv.is_object() || lc(asStr(iv, "Id")) != lc(slotGuid)) continue;
    if (iv["Value"].is_number()) return (float)iv["Value"].get<crude_json::number>();
    if (iv["Value"].is_boolean()) return iv["Value"].get<crude_json::boolean>() ? 1.0f : 0.0f;
  }
  return def;
}

// The child object with lowercased Id == guid (for reading a value op's InputValues).
const crude_json::value* childByGuid(const crude_json::value& root, const std::string& guid) {
  if (!root["Children"].is_array()) return nullptr;
  for (const crude_json::value& cv : root["Children"].get<crude_json::array>())
    if (cv.is_object() && lc(asStr(cv, "Id")) == guid) return &cv;
  return nullptr;
}
}  // namespace

float resolveCbScalar(const crude_json::value& root,
                      const std::map<std::string, std::string>& childSym, const std::string& srcGuid,
                      const std::string& srcSlot) {
  if (isBoundaryGuid(srcGuid)) return boundaryDefaultFloat(root, srcSlot, 0);
  auto sit = childSym.find(srcGuid);
  if (sit == childSym.end()) return 0.0f;
  const std::string& s = sit->second;
  std::string bg, bs;
  if (s == kCbIntToFloatSym) {
    if (wireInto(root, srcGuid, kIntToFloatInSlot, bg, bs) && isBoundaryGuid(bg))
      return boundaryDefaultFloat(root, bs, 0);
  } else if (s == kCbBoolToFloatSym) {
    const crude_json::value* cv = childByGuid(root, srcGuid);
    const float fTrue = cv ? childInputValueFloat(*cv, kBoolToFloatTrueSlot, 1.0f) : 1.0f;
    const float fFalse = cv ? childInputValueFloat(*cv, kBoolToFloatFalseSlot, 0.0f) : 0.0f;
    if (wireInto(root, srcGuid, kBoolToFloatInSlot, bg, bs) && isBoundaryGuid(bg))
      return boundaryDefaultFloat(root, bs, 0) != 0.0f ? fTrue : fFalse;
    return fFalse;
  } else if (s == kCbVec2ComponentsSym) {
    const int comp = (lc(srcSlot) == std::string(kVec2ComponentsYOut)) ? 1 : 0;
    if (wireInto(root, srcGuid, kVec2ComponentsInSlot, bg, bs) && isBoundaryGuid(bg))
      return boundaryDefaultFloat(root, bs, comp);
  }
  return 0.0f;
}

}  // namespace sw
