// selftests_retire_kernelprobe.cpp — R4 (compute-stage kernel PORTED) judgment + its regression golden.
// See selftests_retire_kernelprobe.h for the R4 rationale. All functions are READ-ONLY over the imported
// SymbolLibrary and the on-disk app/shaders set; none touch kernelNameFor / registry / a live lib.
#include "selftests_retire_kernelprobe.h"

#include <cstdio>
#include <filesystem>
#include <set>
#include <string>

#include "selftests.h"                  // runProbeImport (end-to-end leg of the golden)
#include "runtime/compound_graph.h"     // SymbolLibrary / Symbol / SymbolChild

namespace sw {

namespace fs = std::filesystem;

// READ-ONLY MIRROR of buffer_ops_computeshaderstage.cpp kernelNameFor() (KEEP IN SYNC — one row per
// ported compute kernel). Unmapped path → returned unchanged (still '/' or ".hlsl") = the unported tell.
std::string probeResolveComputeKernel(const std::string& src) {
  if (src.empty()) return src;
  // Already an MSL kernel name (no path separator / .hlsl) → pass through (a direct-golden KernelName).
  if (src.find('/') == std::string::npos && src.find(".hlsl") == std::string::npos) return src;
  if (src.find("points/modify/TransformPoints.hlsl") != std::string::npos)
    return "computeshaderstage_transformpoints";
  if (src.find("mesh/mesh-TransformVertices.hlsl") != std::string::npos)
    return "computeshaderstage_transformmesh";
  if (src.find("mesh/mesh-LegacyNoiseDisplace.hlsl") != std::string::npos)
    return "computeshaderstage_displacemeshnoise";
  if (src.find("points/modify/WrapPointPosition.hlsl") != std::string::npos)
    return "computeshaderstage_wrappointposition";
  if (src.find("points/modify/AddNoise.hlsl") != std::string::npos)
    return "computeshaderstage_addnoise";
  if (src.find("points/_internal/SnapPointsToGrid.hlsl") != std::string::npos)
    return "computeshaderstage_snaptogrid";
  if (src.find("points/modify/ClearSomePoints.hlsl") != std::string::npos)
    return "computeshaderstage_clearsomepoints";
  if (src.find("points/modify/SnapToPoints.hlsl") != std::string::npos)
    return "computeshaderstage_snaptopoints";
  if (src.find("points/combine/BlendPoints.hlsl") != std::string::npos)
    return "computeshaderstage_blendpoints";
  return src;  // unmapped path → unported
}

bool probeComputeKernelMetalExists(const std::string& kernel) {
  if (kernel.empty()) return false;
#ifdef SW_SHADERS_DIR
  std::error_code ec;
  return fs::exists(fs::path(SW_SHADERS_DIR) / (kernel + ".metal"), ec);
#else
  return false;
#endif
}

namespace {

// A ComputeShaderStage kernel is PORTED ⇔ its Source resolves to a mapped MSL name (no residual path
// markers) AND that kernel has a committed .metal. Either failure = unported (necessary-not-sufficient).
bool kernelPorted(const std::string& source) {
  const std::string r = probeResolveComputeKernel(source);
  if (r.find('/') != std::string::npos || r.find(".hlsl") != std::string::npos) return false;
  return probeComputeKernelMetalExists(r);
}

std::string firstUnported(const SymbolLibrary& lib, const std::string& symId,
                          std::set<std::string>& seen) {
  if (symId.empty() || !seen.insert(symId).second) return std::string();
  const Symbol* s = lib.find(symId);
  if (!s) return std::string();
  for (const SymbolChild& ch : s->children) {
    if (ch.symbolId == "ComputeShaderStage") {
      // KernelName rides child.strOverrides (folded ComputeShader.Source, fork
      // computeshader-source-folded-onto-stage). Absent/empty = a stage with no kernel = uncookable.
      const auto it = ch.strOverrides.find("KernelName");
      const std::string source = (it != ch.strOverrides.end()) ? it->second : std::string();
      if (!kernelPorted(source)) return source.empty() ? std::string("<none>") : source;
    } else {
      const Symbol* def = lib.find(ch.symbolId);  // recurse only into NESTED COMPOUND children
      if (def && !def->atomic) {
        const std::string u = firstUnported(lib, ch.symbolId, seen);
        if (!u.empty()) return u;
      }
    }
  }
  return std::string();
}

}  // namespace

std::string probeFirstUnportedComputeKernel(const SymbolLibrary& lib, const std::string& symId) {
  std::set<std::string> seen;
  return firstUnported(lib, symId, seen);
}

// ── §2 R4 regression golden ─────────────────────────────────────────────────────────────────────────
// Two layers weld the "structural READY ≠ cookable" contract:
//   (a) the classifier primitives on Source strings byte-copied from real .t3 ComputeShader.Source, and
//   (b) END-TO-END through production runProbeImport over committed .t3: the negative fixture (a flat
//       point compound whose ComputeShaderStage folds an UNPORTED kernel) must gate rc=2, while the
//       ported-kernel TransformPoints and the non-ComputeShaderStage code-op CombineBuffers stay rc=0.
// All three fixtures pass R1/R2/R3, so the ONLY rc=2 source for them is R4 (no R2 conflation).
// injectBug points the negative leg at the ALL-PORTED TransformPoints catalog root → rc=0 → the rc==2
// assert fails → RED (proves the gate reads real per-child kernel structure, not a vacuous constant).
int runProbeR4Golden(bool injectBug) {
  int fails = 0;
  auto check = [&](bool ok, const char* what) {
    if (!ok) { std::printf("[probe-r4] FAIL: %s\n", what); ++fails; }
  };

  // (a) classifier primitives — real TiXL asset paths.
  check(probeComputeKernelMetalExists(
            probeResolveComputeKernel("Lib:shaders/points/modify/TransformPoints.hlsl")),
        "ported TransformPoints.hlsl misclassified (unmapped or .metal missing)");
  // A still-unported point compute kernel (SnapPointsToGrid was the prior example; it is now ported for
  // the retirement sweep, so this uses MapPointAttributes — matching the UnportedCompute.t3 fixture).
  const std::string unported =
      probeResolveComputeKernel("Lib:shaders/points/modify/MapPointAttributes.hlsl");
  check(unported.find(".hlsl") != std::string::npos && !kernelPorted(unported),
        "unported MapPointAttributes.hlsl misclassified as ported");

#ifdef SW_CATALOG_T3_DIR
  const std::string cat = SW_CATALOG_T3_DIR;
#else
  const std::string cat = "assets/catalog_t3";
#endif
#ifdef SW_TESTDATA_DIR
  const std::string td = SW_TESTDATA_DIR;
#else
  const std::string td = "testdata";
#endif

  // (b) end-to-end via production runProbeImport rc.
  check(runProbeImport((cat + "/TransformPoints.t3").c_str()) == 0,
        "ported-kernel compound TransformPoints did not stay READY");
  check(runProbeImport((cat + "/CombineBuffers.t3").c_str()) == 0,
        "code-op compound CombineBuffers (no ComputeShaderStage) did not stay READY");
  const std::string neg =
      injectBug ? (cat + "/TransformPoints.t3") : (td + "/probe_r4/UnportedCompute.t3");
  check(runProbeImport(neg.c_str()) == 2,
        "unported-kernel ComputeShaderStage compound was NOT gated by R4");

  if (injectBug) {
    const bool bit = fails > 0;  // negative leg fed an all-ported root → rc==2 assert must have failed
    std::printf("[probe-r4] -bug (negative leg → all-ported TransformPoints) -> %s\n",
                bit ? "RED (ported compound read READY, R4 rc==2 assert bit)"
                    : "NO-BITE (gate mis-fired on a ported compound — the tooth did not trip)");
    std::fflush(stdout);
    return bit ? 1 : 0;  // did-not-trip → 0 so --bite's NO-BITE list catches a dead tooth
  }
  std::printf("[probe-r4] %d check(s) failed -> %s\n", fails, fails == 0 ? "PASS" : "FAIL");
  std::fflush(stdout);
  return fails == 0 ? 0 : 1;
}

}  // namespace sw
