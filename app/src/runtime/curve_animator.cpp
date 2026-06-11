// runtime/curve_animator — implementation. = TiXL Animator.cs, with int childId / string inputId.
#include "runtime/curve_animator.h"

#include <cstdio>
#include <cstdlib>

namespace sw {

bool Animator::isAnimated(int childId, const std::string& inputId) const {
  auto cit = curves_.find(childId);
  return cit != curves_.end() && cit->second.count(inputId) > 0;
}

bool Animator::isInstanceAnimated(int childId) const { return curves_.count(childId) > 0; }

const Animator::CurveArray* Animator::curvesFor(int childId, const std::string& inputId) const {
  auto cit = curves_.find(childId);
  if (cit == curves_.end()) return nullptr;
  auto iit = cit->second.find(inputId);
  return iit != cit->second.end() ? &iit->second : nullptr;
}

Animator::CurveArray* Animator::curvesFor(int childId, const std::string& inputId) {
  auto cit = curves_.find(childId);
  if (cit == curves_.end()) return nullptr;
  auto iit = cit->second.find(inputId);
  return iit != cit->second.end() ? &iit->second : nullptr;
}

void Animator::setCurves(int childId, const std::string& inputId, CurveArray curves) {
  curves_[childId][inputId] = std::move(curves);
}

std::string Animator::animateFloat(int childId, const std::string& inputId, double time,
                                   float value) {
  CurveArray arr;
  Curve c;
  // = Animator.AddCurvesForFloatVector (cs:114-121): one key, Linear in/out, brokenTangents=true.
  VDefinition key;
  key.value = value;
  key.inInterpolation = KeyInterpolation::Linear;
  key.outInterpolation = KeyInterpolation::Linear;
  key.brokenTangents = true;
  c.addOrUpdate(time, key);
  arr.push_back(std::move(c));
  setCurves(childId, inputId, std::move(arr));
  return makeRef(childId, inputId, 0);
}

void Animator::remove(int childId, const std::string& inputId) {
  auto cit = curves_.find(childId);
  if (cit == curves_.end()) return;
  cit->second.erase(inputId);
  if (cit->second.empty()) curves_.erase(cit);  // drop empty bucket (= cs:288-289)
}

void Animator::removeChild(int childId) { curves_.erase(childId); }

void Animator::copyAnimationsTo(Animator& target, const std::vector<int>& childrenToCopyFrom,
                                const std::map<int, int>& oldToNew) const {
  // = Animator.CopyAnimationsTo (cs:28-55): deep-clone curves, remap child id.
  for (const auto& [oldChildId, inputDict] : curves_) {
    bool wanted = false;
    for (int c : childrenToCopyFrom)
      if (c == oldChildId) { wanted = true; break; }
    if (!wanted) continue;
    auto nit = oldToNew.find(oldChildId);
    if (nit == oldToNew.end()) continue;
    int newChildId = nit->second;
    for (const auto& [inputId, arr] : inputDict) {
      if (arr.empty()) continue;
      CurveArray cloned = arr;  // Curve is value-type (std::map member) -> deep copy
      target.setCurves(newChildId, inputId, std::move(cloned));
    }
  }
}

const Curve* Animator::resolveRef(const std::string& curveRef) const {
  int childId = 0, index = 0;
  std::string inputId;
  if (!parseRef(curveRef, childId, inputId, index)) return nullptr;
  const CurveArray* arr = curvesFor(childId, inputId);
  if (!arr || index < 0 || index >= (int)arr->size()) return nullptr;
  return &(*arr)[index];
}

std::string Animator::makeRef(int childId, const std::string& inputId, int index) {
  std::string r = std::to_string(childId) + ":" + inputId;
  if (index != 0) r += "#" + std::to_string(index);
  return r;
}

bool Animator::parseRef(const std::string& ref, int& childId, std::string& inputId, int& index) {
  // "<childId>:<inputId>[#<index>]". childId is the leading int up to ':'; inputId is everything
  // after ':' up to an optional '#'; index after '#' (default 0). inputId must be non-empty.
  auto colon = ref.find(':');
  if (colon == std::string::npos || colon == 0) return false;
  for (size_t i = 0; i < colon; ++i)
    if (!std::isdigit((unsigned char)ref[i]) && !(i == 0 && ref[i] == '-')) return false;
  childId = std::atoi(ref.substr(0, colon).c_str());
  std::string rest = ref.substr(colon + 1);
  auto hash = rest.find('#');
  if (hash == std::string::npos) {
    inputId = rest;
    index = 0;
  } else {
    inputId = rest.substr(0, hash);
    index = std::atoi(rest.substr(hash + 1).c_str());
  }
  return !inputId.empty();
}

}  // namespace sw
