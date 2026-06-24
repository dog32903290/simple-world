// variation_pool_json_selftest — --selftest-variation-pool-json. Headless RED->GREEN proof of the
// on-disk JSON round-trip for the in-memory VariationPool (closes fork-pool-in-memory). Clones the
// round-trip golden shape of asset_ref_roundtrip_selftest.cpp: build → serialize → save to a REAL temp
// file → reload → assert byte-stable + value-equal, CJK survives, an ExcludedFromPresets input is
// skipped, a malformed (missing) input is tolerated, and a tampered key bites (injectBug).
//
// TiXL ground-truth: Variation.ToJson / TryLoadVariationFromJson (Editor/.../Variation.cs:62-211).
// The pool format + per-value encoding live in runtime/variation_pool_json.h (the SSOT); this is its
// harness.
//
// ASSERTIONS:
//   1. round-trip value-equal: a pool with a CJK-titled snapshot carrying a vec3 + a float param →
//      toJson → fromJson → every stored value matches (title, activationIndex, isPreset, params).
//   2. byte-stable on disk: the bytes WRITTEN to the temp file reload to the same canonical JSON
//      (toJson(reloaded) == the bytes on disk) — no drift in the serialization.
//   3. CJK title: a "中文快照" title survives byte-stable (crude_json sw-patch utf8 — same property the
//      .swproj symbol-name round-trip pins, here pinned for variation titles).
//   4. ExcludedFromPresets skip: an input flagged excluded by the load predicate does NOT land in the
//      reloaded pool (faithful to TiXL .cs:147-153 inputUi.ExcludedFromPresets → continue).
//   5. missing-input tolerance: a malformed value entry in the JSON is SKIPPED on load (no crash, no
//      bogus value) — faithful to TiXL's `continue` on an input the symbol doesn't define.
//
// injectBug TAMPERS a reloaded param value before the value-equal check → assertion 1 FAILS (teeth bite
// the REAL fidelity property: a save that does not preserve the exact stored value would silently load
// a WRONG snapshot — the VJ would recall a morph that isn't the one they saved).
#include <unistd.h>  // getpid

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "runtime/selftest_registry.h"      // REGISTER_SELFTESTS
#include "runtime/variation_pool.h"         // VariationPool / Variation / VariationValue
#include "runtime/variation_pool_json.h"    // variationPoolToJson / variationPoolFromJson

namespace fs = std::filesystem;

namespace sw {
namespace {

constexpr InputId kVec3Input = 100;
constexpr InputId kFloatInput = 200;
constexpr InputId kExcludedInput = 999;  // an input flagged ExcludedFromPresets on load

// Build a pool: one snapshot @ index 1, CJK title, child kCompositionNode carrying a vec3 + a float +
// one input that the load predicate will treat as excluded-from-presets.
VariationPool buildPool() {
  VariationPool pool;
  std::vector<SnapshotChildState> children(1);
  children[0].childId = kCompositionNode;
  children[0].enabledForSnapshots = true;
  children[0].values[kVec3Input] = VariationValue::makeVec3(1.0f, 2.0f, 3.0f);
  children[0].values[kFloatInput] = VariationValue::makeFloat(42.5f);
  children[0].values[kExcludedInput] = VariationValue::makeFloat(7.0f);
  pool.createOrUpdateSnapshot(/*activationIndex=*/1, children, /*title=*/"中文快照");
  return pool;
}

std::string readFile(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

const Variation* firstVar(const VariationPool& pool) {
  return pool.size() ? &pool.allVariations()[0] : nullptr;
}

}  // namespace

int runVariationPoolJsonSelfTest(bool injectBug) {
  std::error_code ec;
  const fs::path dir = fs::temp_directory_path() / ("sw_varpool_" + std::to_string(::getpid()));
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);

  // --- serialize + write to a real temp file ---------------------------------------------------
  VariationPool pool0 = buildPool();
  const std::string json0 = variationPoolToJson(pool0);
  const fs::path file = dir / "pool.json";
  { std::ofstream f(file, std::ios::binary); f << json0; }
  const std::string onDiskBytes = readFile(file);

  // --- reload, excluding kExcludedInput (the ExcludedFromPresets predicate) --------------------
  const ExcludedPredicate excluded = [](NodeId, InputId in) { return in == kExcludedInput; };
  VariationPool back;
  bool loadOk = variationPoolFromJson(onDiskBytes, back, excluded);

  // teeth: tamper a reloaded param value → the value-equal assertion (1) FAILS.
  if (injectBug && back.size()) {
    Variation v = back.allVariations()[0];
    v.parameterSets[kCompositionNode][kFloatInput] = VariationValue::makeFloat(-1.0f);
    VariationPool tampered;
    tampered.adopt(std::move(v));
    back = std::move(tampered);
  }

  const Variation* rv = firstVar(back);
  const VariationValue* gotVec3 = rv ? rv->find(kCompositionNode, kVec3Input) : nullptr;
  const VariationValue* gotFloat = rv ? rv->find(kCompositionNode, kFloatInput) : nullptr;
  const VariationValue* gotExcluded = rv ? rv->find(kCompositionNode, kExcludedInput) : nullptr;

  // 1: value-equal (title / index / preset / vec3 / float).
  bool valueOk = loadOk && rv && rv->title == "中文快照" && rv->activationIndex == 1 &&
                 rv->isPreset == false &&
                 gotVec3 && gotVec3->equals(VariationValue::makeVec3(1.0f, 2.0f, 3.0f)) &&
                 gotFloat && gotFloat->equals(VariationValue::makeFloat(42.5f));
  // 3: CJK title survived (folded into valueOk's title check; reported separately for the log).
  bool cjkOk = rv && rv->title == "中文快照";
  // 4: ExcludedFromPresets input was skipped on load.
  bool excludedSkipped = (gotExcluded == nullptr);
  // 2: byte-stable — a FAITHFUL reload (no exclusion) re-serializes to the exact on-disk bytes (no
  // drift in the serialization). The excluded reload above intentionally drops an input, so byte
  // stability is measured on the lossless path. (injectBug tampers only the excluded `back`; the
  // faithful path is independent, so assertion 1 still carries the teeth.)
  VariationPool faithful;
  bool faithfulLoadOk = variationPoolFromJson(onDiskBytes, faithful);  // no excluded predicate
  bool byteStable = faithfulLoadOk && variationPoolToJson(faithful) == onDiskBytes;

  // 5: missing-input tolerance — a malformed value entry is skipped, not crashed/loaded. Inject a
  // bogus input value (type/v stripped) into a fresh JSON and assert the loader tolerates it (loads
  // the well-formed sibling, drops the malformed one, no crash).
  bool tolerantOk = false;
  {
    // A pool JSON with two inputs on the composition; one is malformed ({} with no type/v).
    const std::string malformed =
        "{\"Variations\":[{\"Id\":5,\"IsPreset\":false,\"ActivationIndex\":2,"
        "\"ParameterSetsForChildIds\":{\"0\":{\"100\":{\"type\":0,\"v\":[9]},"
        "\"200\":{}}}}]}";
    VariationPool mback;
    bool mok = variationPoolFromJson(malformed, mback);  // must NOT crash
    const Variation* mv = mback.size() ? &mback.allVariations()[0] : nullptr;
    const VariationValue* good = mv ? mv->find(kCompositionNode, 100) : nullptr;
    const VariationValue* bad = mv ? mv->find(kCompositionNode, 200) : nullptr;
    tolerantOk = mok && good && good->equals(VariationValue::makeFloat(9.0f)) && (bad == nullptr);
  }

  fs::remove_all(dir, ec);

  bool pass = valueOk && byteStable && cjkOk && excludedSkipped && tolerantOk;
  std::printf("[selftest-variation-pool-json] valueEqual=%d byteStable=%d cjk=%d excludedSkipped=%d "
              "missingTolerant=%d -> %s\n",
              valueOk ? 1 : 0, byteStable ? 1 : 0, cjkOk ? 1 : 0, excludedSkipped ? 1 : 0,
              tolerantOk ? 1 : 0, pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/306, {"variation-pool-json", runVariationPoolJsonSelfTest});

}  // namespace sw
