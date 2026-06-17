// runtime/field_graph_selftest.cpp — --selftest-field-codegen. PURE STRING assertions, ZERO GPU.
//
// Locks the shader-graph codegen mechanism: builds a minimal SphereSDF field, runs assembleFieldMSL,
// and asserts the assembled MSL string is correct (distance formula present, single packed param
// buffer layout correct with the right prefix, all three template hooks filled — no residual
// /*{...}*/). injectBug breaks one expectation to prove the tooth BITES.
#include "runtime/field_graph.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace sw {

namespace {

// Read the field render template from the compile-time path. Returns "" on failure.
std::string loadTemplate() {
#ifdef SW_FIELD_TEMPLATE
  std::ifstream f(SW_FIELD_TEMPLATE);
  if (!f) return "";
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
#else
  return "";
#endif
}

bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

}  // namespace

int runFieldCodegenSelfTest(bool injectBug) {
  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::fprintf(stderr, "[selftest-field-codegen] FAIL: could not load field template "
                         "(SW_FIELD_TEMPLATE unset or unreadable)\n");
    return 1;
  }

  // Minimal SphereSDF leaf: Center=(0,0,0), Radius=0.5 (SphereSDF.t3 defaults). A fixed short id so
  // the prefix is deterministic and the layout assertions are stable.
  auto sphere = std::make_shared<SphereSDFNode>("abc123");
  sphere->centerX = 0.f;
  sphere->centerY = 0.f;
  sphere->centerZ = 0.f;
  sphere->radius = 0.5f;
  const std::string prefix = "SphereSDF_abc123_";

  AssembledField a = assembleFieldMSL(sphere, tmpl);

  int rc = 0;

  // (1) distance formula went in: `length(p` + the prefixed Center/Radius reads, with the P. struct
  //     qualifier (the HLSL->MSL fork). injectBug corrupts the Radius suffix so this assertion fails.
  const std::string radiusToken = injectBug ? ("P." + prefix + "RadiusXX") : ("P." + prefix + "Radius");
  if (!contains(a.msl, "length(p")) {
    std::fprintf(stderr, "[selftest-field-codegen] FAIL: distance formula `length(p` missing\n");
    rc = 1;
  }
  if (!contains(a.msl, "P." + prefix + "Center")) {
    std::fprintf(stderr, "[selftest-field-codegen] FAIL: Center param read missing/unprefixed\n");
    rc = 1;
  }
  if (!contains(a.msl, radiusToken)) {
    std::fprintf(stderr, "[selftest-field-codegen] FAIL: Radius param read `%s` missing%s\n",
                 radiusToken.c_str(), injectBug ? " (injectBug: corrupted suffix)" : "");
    rc = 1;
  }

  // (2) single packed float-param buffer layout: Center(vec3) + Radius(scalar) = exactly 4 floats,
  //     all 0 except none here (Center=0, Radius=0.5). No padding (offset 3 -> scalar needs none).
  if (a.floatParams.size() != 4) {
    std::fprintf(stderr, "[selftest-field-codegen] FAIL: floatParams size %zu, expected 4\n",
                 a.floatParams.size());
    rc = 1;
  } else if (a.floatParams[0] != 0.f || a.floatParams[1] != 0.f || a.floatParams[2] != 0.f ||
             a.floatParams[3] != 0.5f) {
    std::fprintf(stderr,
                 "[selftest-field-codegen] FAIL: floatParams [%g %g %g %g], expected [0 0 0 0.5]\n",
                 a.floatParams[0], a.floatParams[1], a.floatParams[2], a.floatParams[3]);
    rc = 1;
  }

  // The FLOAT_PARAMS struct must declare the prefixed fields with the right types. Center is
  // `packed_float3` (NOT `float3`): the HLSL->MSL alignment fork (see field_graph.cpp appendVec3Param)
  // — a plain float3 member is 16-aligned in MSL and would push the following scalar to byte 16,
  // diverging from TiXL's HLSL cbuffer packing (float3+float in one 16B register). The Build-2 GPU
  // golden caught this (Radius read as 0); packed_float3 restores byte-for-byte layout parity.
  if (!contains(a.msl, "packed_float3 " + prefix + "Center")) {
    std::fprintf(stderr,
                 "[selftest-field-codegen] FAIL: `packed_float3 %sCenter` field decl missing\n",
                 prefix.c_str());
    rc = 1;
  }
  if (!contains(a.msl, "float " + prefix + "Radius")) {
    std::fprintf(stderr, "[selftest-field-codegen] FAIL: `float %sRadius` field decl missing\n",
                 prefix.c_str());
    rc = 1;
  }

  // (3) all three template hooks filled — no residual /*{GLOBALS}*/ /*{FLOAT_PARAMS}*/ /*{FIELD_CALL}*/.
  for (const char* hook : {"/*{GLOBALS}*/", "/*{FLOAT_PARAMS}*/", "/*{FIELD_CALL}*/"}) {
    if (contains(a.msl, hook)) {
      std::fprintf(stderr, "[selftest-field-codegen] FAIL: hook %s left unfilled\n", hook);
      rc = 1;
    }
  }

  // srcHash must be non-zero and stable (FNV-1a of the assembled MSL).
  if (a.srcHash == 0) {
    std::fprintf(stderr, "[selftest-field-codegen] FAIL: srcHash is zero\n");
    rc = 1;
  }

  if (injectBug) {
    // Under injectBug the Radius-token assertion above MUST have fired (rc==1). If codegen somehow
    // still produced the corrupted token, the tooth has no bite — fail loudly.
    if (rc == 0) {
      std::fprintf(stderr,
                   "[selftest-field-codegen] FAIL: injectBug did not trip any assertion "
                   "(tooth has no bite)\n");
      return 1;
    }
    std::printf("[selftest-field-codegen] injectBug correctly RED\n");
    return 1;
  }

  if (rc == 0) std::printf("[selftest-field-codegen] PASS\n");
  return rc;
}

}  // namespace sw
