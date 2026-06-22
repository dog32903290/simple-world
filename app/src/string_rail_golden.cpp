// string_rail_golden — --selftest-stringrail. TRANSPORT + GATHER-ORDER + INPUT-DRIVABLE golden for
// the 6th cook flow (host std::string): the String value rail. Mirror of floatlist_golden.cpp.
//
// Build flat graphs that exercise the three new String leaves end-to-end through PointGraph::cook
// (the cookStringNode + cookStringLength branches), read the host result back (debugCookedString for
// String producers, debugCookedFloatList for StringLength's host scalar), and assert against
// closed-form values hand-derived from the TiXL .cs sources (NOT self-consistent — every expected
// value is computed by hand here).
//
// What this proves (the architecture-defining parts of the seam):
//   (1) TRANSPORT: a String producer's output flows node→node via the driver-owned stringBuf
//       (FloatToString → readback "3.14"; FloatToString → CombineStrings → readback joined).
//   (2) GATHER ORDER: CombineStrings' MultiInput honours WIRE-DECLARATION order (the load-bearing
//       spot, mirroring floatlist) — wires [a,b,c] → "a-b-c", not sorted, not first-wire-only.
//   (3) INPUT-DRIVABLE (the new fork): a String input port is WIRE-OR-CONST — StringLength reads the
//       wired upstream string when wired, and the strDef const when the wire is cut.
//   (4) FORMAT (now NON-DEGENERATE — earlier LEGs 1-10 only hit the plain-decimal band / n≤3 digits /
//       1e6 / NaN, so they閃過 the three real divergence points the refuter flagged):
//       • default ToString of moderate values ("{0:0.000}" 3 digits, "{0:F1}", 1000000 → "1000000");
//       • LEG 11 (BLOCK-2) large-magnitude default → SCIENTIFIC at exp≥7: 1e11 → "1E+11", 1e14 →
//         "1E+14" (uppercase E, sign, min-2-digit exponent — NOT noise integers, NOT 1e15-wide band);
//       • LEG 12 (BLOCK-3) small-magnitude default → SCIENTIFIC at exp<-4: 1e-5 → "1E-05", 1.234e-5 →
//         "1.234E-05" (uppercase E, NOT %g's lowercase "1e-05");
//       • LEG 13 (BLOCK-1) high-precision F/N/E specifiers EXPOSE the float→double binary value
//         (TiXL net10.0 / .NET Core 3.0+): "{0:F8}" 3.14 → "3.14000010", "{0:E10}" 0.1 →
//         "1.0000000149E-001" — NOISE, not zero-padded (the work-order's zero-pad values were .NET
//         Framework 4.x behaviour; corrected to net10.0 ground-truth here);
//       • NaN → "NaN" (C# literal, not printf "nan").
//
// injectBug routes through stringInjectBug(): the RED case corrupts the REAL cook output (drops the
// last char / clears StringLength's host scalar), NOT the expected value — teeth on the actual op
// path (mirror of floatlist_golden.cpp / mesh_golden.cpp).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"      // SymbolLibrary (R-2 resident leg)
#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/graph.h"                // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"        // libFromGraph (flat Graph -> SymbolLibrary, paths == ids)
#include "runtime/point_graph.h"          // PointGraph::cook + debugCookedString/debugCookedFloatList
#include "runtime/resident_eval_graph.h" // buildEvalGraph / cookStringNodes / ResidentEvalGraph (R-2)
#include "runtime/string_op_registry.h"  // stringInjectBug

namespace sw {
namespace {

// Build a graph with a single FloatToString(value, format) and cook it as the terminal. Returns the
// cooked host string (empty on failure). format is set on strParams["Format"] (the stored String const).
std::string cookFloatToString(PointGraph& pg, float value, const std::string& format) {
  Graph g;
  Node ftl; ftl.id = 1; ftl.type = "FloatToString";
  ftl.params["Value"] = value;          // Value (resolved Float param, the value spine)
  ftl.strParams["Format"] = format;     // Format (String const — wire-OR-const, here the const path)
  g.nodes.push_back(ftl);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);

  const std::string* out = pg.debugCookedString(1);
  return out ? *out : std::string{};
}

// Build { producers (StringConst-style via FloatToString of integer values) -> CombineStrings.Input
// (multiInput), in vals order, sep } — the SHARED graph driven by BOTH the flat and resident legs. Each
// producer is a FloatToString with empty format → its integer value's decimal text. Wire order = vals
// order → wire-declaration order; the readback must equal the joined sequence IF the gather honours it.
// CombineStrings is node id 1 (→ resident path "1"); producers are ids 2.. (FloatToString.Output →
// CombineStrings.Input is a GENUINE String wire — the whole point of the resident string-wire rail).
Graph makeCombineGraph(const std::vector<float>& producerVals, const std::string& sep) {
  Graph g;
  Node cmb; cmb.id = 1; cmb.type = "CombineStrings";
  cmb.strParams["Separator"] = sep;  // Separator (String const)
  g.nodes.push_back(cmb);
  // CombineStrings ports: [0]=Result(out), [1]=Input(multiInput), [2]=Separator. Input pin = port 1.
  const int inputPin = pinId(1, /*portIndex=*/1);

  int connId = 100;
  int nextNode = 2;
  for (size_t i = 0; i < producerVals.size(); ++i) {
    Node p; p.id = nextNode++; p.type = "FloatToString";
    p.params["Value"] = producerVals[i];
    p.strParams["Format"] = "";  // empty → default ToString (integer-valued → "1","2",...)
    g.nodes.push_back(p);
    // FloatToString ports: [0]=Output(out), [1]=Value, [2]=Format. Output pin = port 0.
    g.connections.push_back({connId++, pinId(p.id, /*out port*/ 0), inputPin});
  }
  g.nextId = nextNode;  // monotonic floor for libFromGraph (resident leg)
  return g;
}

// FLAT leg: cook CombineStrings as the terminal, read back via debugCookedString.
std::string cookCombine(PointGraph& pg, const std::vector<float>& producerVals,
                        const std::string& sep) {
  Graph g = makeCombineGraph(producerVals, sep);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
  const std::string* out = pg.debugCookedString(1);
  return out ? *out : std::string{};
}

// ★R-2 RESIDENT leg (production): mirror the SAME flat Graph into a SymbolLibrary (libFromGraph →
// resident paths == flat node ids as strings), build the resident graph, run cookStringNodes (the
// per-frame production pass frame_cook.cpp drives — String producers gathered THROUGH the resident
// String-wire drivers the flatten now projects), then read the host string off CombineStrings'
// extStrOut[Result port idx 0] — the EXACT production channel a downstream resident consumer reads.
// This is the leg that was a DROPPED wire before the rail: without it, CombineStrings.Input read its
// unwired-const fallback (empty) on the resident path → joined "" — the flat-only self-deception.
std::string cookCombineResident(const std::vector<float>& producerVals, const std::string& sep) {
  Graph g = makeCombineGraph(producerVals, sep);
  SymbolLibrary lib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
  ResidentEvalCtx rc;
  rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
  cookStringNodes(rg, rc);  // PRODUCTION pass: walks the resident graph, writes extStrOut
  const ResidentNode* n = rg.node("1");  // CombineStrings resident path == flat node id "1"
  if (!n) return {};
  auto it = n->extStrOut.find(/*Result out port idx*/ 0);
  return it != n->extStrOut.end() ? it->second : std::string{};
}

// Cook StringLength with its InputString either WIRED to a FloatToString producer (whose text we
// know exactly: FloatToString(3.14,"") → "3.14", 4 chars) or UNWIRED so it falls back to the strDef
// const (constText, set via strParams). Returns the host scalar (the character count) via
// debugCookedFloatList[0], or -1 on failure. The only String SOURCE leaf in this batch is
// FloatToString, so the wired leg's literal comes from it (CombineStrings could chain but adds no
// coverage here — FloatToString already proves the wired-recurse path).
float cookStringLength(PointGraph& pg, bool wired, const std::string& constText) {
  Graph g;
  Node sl; sl.id = 1; sl.type = "StringLength";
  if (!wired) sl.strParams["InputString"] = constText;  // unwired → strDef const path
  g.nodes.push_back(sl);
  // StringLength ports: [0]=Length(out), [1]=InputString. InputString pin = port 1.
  const int inPin = pinId(1, /*portIndex=*/1);

  if (wired) {
    Node fts; fts.id = 2; fts.type = "FloatToString";
    fts.params["Value"] = 3.14f; fts.strParams["Format"] = "";  // → "3.14" (4 chars)
    g.nodes.push_back(fts);
    // FloatToString ports: [0]=Output(out), [1]=Value, [2]=Format. Output pin = port 0.
    g.connections.push_back({300, pinId(2, /*out*/ 0), inPin});  // FloatToString.Output → InputString
  }

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);

  const std::vector<float>* out = pg.debugCookedFloatList(1);
  return (out && !out->empty()) ? (*out)[0] : -1.0f;
}

// Build a single IntToString(value, format) and cook it as terminal → cooked host string. Value rides
// params["Value"] (the value spine, truncated to int by the op); format rides strParams["Format"].
std::string cookIntToString(PointGraph& pg, int value, const std::string& format) {
  Graph g;
  Node n; n.id = 1; n.type = "IntToString";
  n.params["Value"] = (float)value;
  n.strParams["Format"] = format;
  g.nodes.push_back(n);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
  const std::string* out = pg.debugCookedString(1);
  return out ? *out : std::string{};
}

// Build a single Vec3ToString(vector, format) and cook it as terminal → cooked host string. The vec3
// rides params["Vector.x"/.y/.z"] (the value spine); format rides strParams["Format"].
std::string cookVec3ToString(PointGraph& pg, float x, float y, float z, const std::string& format) {
  Graph g;
  Node n; n.id = 1; n.type = "Vec3ToString";
  n.params["Vector.x"] = x; n.params["Vector.y"] = y; n.params["Vector.z"] = z;
  n.strParams["Format"] = format;
  g.nodes.push_back(n);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
  const std::string* out = pg.debugCookedString(1);
  return out ? *out : std::string{};
}

}  // namespace

int runStringRailSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  bool ok = true;

  // LEG 1 — FloatToString TRANSPORT + default-format: FloatToString(3.14, "") → "3.14". Proves a
  // String producer's output is readable via the driver-owned stringBuf (the transport mechanism).
  // injectBug drops the last char in the REAL cook → "3.1" ≠ "3.14" → RED.
  {
    stringInjectBug() = injectBug;
    std::string got = cookFloatToString(pg, 3.14f, "");
    stringInjectBug() = false;
    bool pass = (got == "3.14");
    ok = ok && pass;
    std::printf("[selftest-stringrail] FloatToString(3.14,\"\")=\"%s\" want=\"3.14\" -> %s\n",
                got.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 2 — FloatToString FORMAT "{0:F1}": FloatToString(3.14, "{0:F1}") → "3.1" (fixed 1 digit).
  // injectBug drops the last char → "3." ≠ "3.1" → RED.
  {
    stringInjectBug() = injectBug;
    std::string got = cookFloatToString(pg, 3.14f, "{0:F1}");
    stringInjectBug() = false;
    bool pass = (got == "3.1");
    ok = ok && pass;
    std::printf("[selftest-stringrail] FloatToString(3.14,\"{0:F1}\")=\"%s\" want=\"3.1\" -> %s\n",
                got.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 3 — CombineStrings GATHER ORDER (the subtle spot): producers of [1,2,3] wired in order, sep="-"
  // → "1-2-3" (wire-declaration order, NOT sorted, NOT first-wire-only). Each producer is a
  // FloatToString of an integer value (empty format → "1"/"2"/"3"). injectBug (drop last char) →
  // "1-2-" ≠ "1-2-3" → RED.
  {
    stringInjectBug() = injectBug;
    std::string got = cookCombine(pg, {1.0f, 2.0f, 3.0f}, "-");
    stringInjectBug() = false;
    bool pass = (got == "1-2-3");
    ok = ok && pass;
    std::printf("[selftest-stringrail] CombineStrings([1,2,3],\"-\")=\"%s\" want=\"1-2-3\" -> %s\n",
                got.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 4 — CombineStrings wire-order proof: producers [3,1,2] must read back "3-1-2" (wire-declaration
  // order), NOT sorted "1-2-3". This is the load-bearing gather assertion (mirror of floatlist LEG 2).
  // injectBug (drop last char) → "3-1-" ≠ "3-1-2" → RED.
  {
    stringInjectBug() = injectBug;
    std::string got = cookCombine(pg, {3.0f, 1.0f, 2.0f}, "-");
    stringInjectBug() = false;
    bool pass = (got == "3-1-2");
    ok = ok && pass;
    std::printf("[selftest-stringrail] CombineStrings([3,1,2],\"-\")=\"%s\" want=\"3-1-2\" (wire-decl) -> %s\n",
                got.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 5 — StringLength INPUT-DRIVABLE, WIRED: StringLength ← FloatToString(3.14,"") = "3.14" (4
  // chars) → host scalar 4.0. Proves a String input port reads the WIRED upstream string. injectBug
  // clears the REAL host scalar → empty (read -1) ≠ 4.0 → RED.
  {
    stringInjectBug() = injectBug;
    float got = cookStringLength(pg, /*wired=*/true, "");
    stringInjectBug() = false;
    bool pass = std::fabs(got - 4.0f) < 1e-5f;
    ok = ok && pass;
    std::printf("[selftest-stringrail] StringLength(wired \"3.14\")=%.1f want=4.0 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }

  // LEG 6 — StringLength INPUT-DRIVABLE, UNWIRED const fallback: cut the wire → StringLength reads its
  // strDef const "Line\nLine" (9 chars: L,i,n,e,\n,L,i,n,e) → host scalar 9.0. Proves the wire-OR-const
  // dual identity (the new fork) — both the wired path (LEG 5) and the const path are live. injectBug
  // clears the host scalar → -1 ≠ 9.0 → RED.
  {
    stringInjectBug() = injectBug;
    float got = cookStringLength(pg, /*wired=*/false, "Line\nLine");  // real newline → 9 chars
    stringInjectBug() = false;
    bool pass = std::fabs(got - 9.0f) < 1e-5f;
    ok = ok && pass;
    std::printf("[selftest-stringrail] StringLength(const \"Line\\nLine\")=%.1f want=9.0 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }

  // LEG 7 — FloatToString DEFAULT FORMAT "{0:0.000}" (the TiXL .t3:6 default — the load-bearing case
  // LEGs 1-2 閃過 because they used "" / "{0:F1}"). C# custom numeric "0.000" = 3 fixed fractional
  // digits → v=0 → "0.000", v=3.14 → "3.140". Proves the default-format path matches TiXL's
  // string.Format(InvariantCulture, "{0:0.000}", v). injectBug drops last char → "0.00" ≠ "0.000" RED.
  {
    stringInjectBug() = injectBug;
    std::string got0 = cookFloatToString(pg, 0.0f, "{0:0.000}");
    std::string got314 = cookFloatToString(pg, 3.14f, "{0:0.000}");
    stringInjectBug() = false;
    bool pass = (got0 == "0.000") && (got314 == "3.140");
    ok = ok && pass;
    std::printf("[selftest-stringrail] FloatToString(0,\"{0:0.000}\")=\"%s\" want=\"0.000\"; "
                "(3.14)=\"%s\" want=\"3.140\" -> %s\n",
                got0.c_str(), got314.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 8 — FloatToString COMPOSITE LITERAL: surrounding text kept verbatim, placeholder substituted.
  // string.Format(InvariantCulture, "x={0:F1}", 3.14) → "x=3.1"; "{0:F1} units" → "3.1 units".
  // injectBug drops last char → "x=3." / "3.1 unit" ≠ expected → RED.
  {
    stringInjectBug() = injectBug;
    std::string gotPre = cookFloatToString(pg, 3.14f, "x={0:F1}");
    std::string gotPost = cookFloatToString(pg, 3.14f, "{0:F1} units");
    stringInjectBug() = false;
    bool pass = (gotPre == "x=3.1") && (gotPost == "3.1 units");
    ok = ok && pass;
    std::printf("[selftest-stringrail] FloatToString(3.14,\"x={0:F1}\")=\"%s\" want=\"x=3.1\"; "
                "(\"{0:F1} units\")=\"%s\" want=\"3.1 units\" -> %s\n",
                gotPre.c_str(), gotPost.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 9 — FloatToString empty-format LARGE INTEGRAL + NaN (the printf-vs-C# divergence the refuter
  // flagged): C# v.ToString(InvariantCulture) keeps 1000000 as "1000000" (NOT %g's "1e+06"), and
  // spells NaN "NaN" (NOT printf's "nan"). injectBug drops last char → "100000" / "Na" → RED.
  {
    stringInjectBug() = injectBug;
    std::string gotBig = cookFloatToString(pg, 1000000.0f, "");
    std::string gotNan = cookFloatToString(pg, std::nanf(""), "");
    stringInjectBug() = false;
    bool pass = (gotBig == "1000000") && (gotNan == "NaN");
    ok = ok && pass;
    std::printf("[selftest-stringrail] FloatToString(1000000,\"\")=\"%s\" want=\"1000000\"; "
                "(NaN,\"\")=\"%s\" want=\"NaN\" -> %s\n",
                gotBig.c_str(), gotNan.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 10 — FloatToString custom-format on NaN + grouped "{0:N0}": custom "{0:0.000}" on NaN →
  // "NaN" (non-finite falls through to ToString); "{0:N0}" on 1234567 → "1,234,567" (',' thousands
  // grouping, C# InvariantCulture). injectBug drops last char → "Na" / "1,234,56" → RED.
  {
    stringInjectBug() = injectBug;
    std::string gotNan = cookFloatToString(pg, std::nanf(""), "{0:0.000}");
    std::string gotGrp = cookFloatToString(pg, 1234567.0f, "{0:N0}");
    stringInjectBug() = false;
    bool pass = (gotNan == "NaN") && (gotGrp == "1,234,567");
    ok = ok && pass;
    std::printf("[selftest-stringrail] FloatToString(NaN,\"{0:0.000}\")=\"%s\" want=\"NaN\"; "
                "(1234567,\"{0:N0}\")=\"%s\" want=\"1,234,567\" -> %s\n",
                gotNan.c_str(), gotGrp.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 11 — DEFAULT (empty) format, LARGE-magnitude scientific switchover (the refuter's BLOCK-2: the
  // NON-degenerate case LEGs 1-10 全閃過 — they only tested 1e6, which sits INSIDE C#'s plain-decimal
  // band so a too-wide band still passed). C# default float ToString flips to scientific at decimal
  // exponent ≥ 7: 1e11f → "1E+11", 1e14f → "1E+14" (uppercase 'E', sign, MIN-2-digit exponent). The
  // earlier engine emitted binary-noise integers here ("99999997952") because its plain band was 1e15
  // wide and its source was (double)v, not the float shortest round-trip. injectBug drops last char →
  // "1E+1" ≠ "1E+11" → RED. (Probed vs .NET 10 / learn.microsoft.com G-specifier rules.)
  {
    stringInjectBug() = injectBug;
    std::string got11 = cookFloatToString(pg, 1e11f, "");
    std::string got14 = cookFloatToString(pg, 1e14f, "");
    stringInjectBug() = false;
    bool pass = (got11 == "1E+11") && (got14 == "1E+14");
    ok = ok && pass;
    std::printf("[selftest-stringrail] FloatToString(1e11,\"\")=\"%s\" want=\"1E+11\"; "
                "(1e14,\"\")=\"%s\" want=\"1E+14\" -> %s\n",
                got11.c_str(), got14.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 12 — DEFAULT (empty) format, SMALL-magnitude scientific (the refuter's BLOCK-3): C# default
  // float ToString flips to scientific at decimal exponent < -4. 1e-5f → "1E-05"; 1.234e-5f →
  // "1.234E-05" (uppercase 'E', sign, MIN-2-digit exponent; shortest mantissa kept). The earlier
  // engine leaked %g's lowercase 2-digit form "1e-05" / "1.234e-05" — wrong CASE, faithful-looking but
  // not C#. This leg pins the case + the mantissa preservation. injectBug drops last char → "1E-0" /
  // "1.234E-0" → RED. (1.234e-5f's shortest round-trip IS "1.234E-05" — verified by the cook itself.)
  {
    stringInjectBug() = injectBug;
    std::string got5 = cookFloatToString(pg, 1e-5f, "");
    std::string gotM = cookFloatToString(pg, 1.234e-5f, "");
    stringInjectBug() = false;
    bool pass = (got5 == "1E-05") && (gotM == "1.234E-05");
    ok = ok && pass;
    std::printf("[selftest-stringrail] FloatToString(1e-5,\"\")=\"%s\" want=\"1E-05\"; "
                "(1.234e-5,\"\")=\"%s\" want=\"1.234E-05\" -> %s\n",
                got5.c_str(), gotM.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 13 — HIGH-PRECISION standard specifiers EXPOSE the float→double binary value (the refuter's
  // BLOCK-1). ★GROUND-TRUTH CORRECTION vs. this lane's work-order: the order asked for ZERO-PADDED
  // "{0:F8}" 3.14 = "3.14000000" and E10 0.1 = "1.0000000000E-001" — that is .NET FRAMEWORK 4.x
  // behaviour (old G7-then-pad). TiXL targets **net10.0** (Operators/*.csproj), and since .NET Core
  // 3.0 the F/N/E specifiers expose the EXACT IEEE-754 value: 3.14f = 3.1400001049…, 0.1f =
  // 0.10000000149…. So the faithful-to-TiXL values are the NOISE renderings, NOT the zero-padded ones:
  //   {0:F8} 3.14  → "3.14000010"          (NOT "3.14000000")
  //   {0:E10} 0.1  → "1.0000000149E-001"   (NOT "1.0000000000E-001"; standard 'E' = MIN-3-digit exp,
  //                                          distinct from default-G's 2-digit — see LEG 11/12)
  // (Confirmed against learn.microsoft.com "standard-numeric-format-strings" + the .NET Core 3.0
  //  floating-point-formatting blog.) These are NON-degenerate: they fail any engine that feeds the
  // shortest-decimal back through %f (which would zero-pad). injectBug drops last char → RED.
  {
    stringInjectBug() = injectBug;
    std::string gotF8 = cookFloatToString(pg, 3.14f, "{0:F8}");
    std::string gotE10 = cookFloatToString(pg, 0.1f, "{0:E10}");
    stringInjectBug() = false;
    bool pass = (gotF8 == "3.14000010") && (gotE10 == "1.0000000149E-001");
    ok = ok && pass;
    std::printf("[selftest-stringrail] FloatToString(3.14,\"{0:F8}\")=\"%s\" want=\"3.14000010\"; "
                "(0.1,\"{0:E10}\")=\"%s\" want=\"1.0000000149E-001\" -> %s\n",
                gotF8.c_str(), gotE10.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 14 — IntToString (TiXL string/convert/IntToString.cs) DEFAULT + custom zero-pad + decimal. The
  // t3 default Format "{0:0}" = C# integer custom-numeric (min 1 digit): 42 → "42", -7 → "-7". Empty
  // format → int.ToString(InvariantCulture) = "42". "{0:000}" → zero-pad to 3 digits → "042". Hand-
  // derived against IntToString.cs. injectBug drops the last char in the REAL cook → "4" / "0" → RED.
  {
    stringInjectBug() = injectBug;
    std::string d0   = cookIntToString(pg, 42,  "{0:0}");
    std::string dNeg = cookIntToString(pg, -7,  "{0:0}");
    std::string dEmp = cookIntToString(pg, 42,  "");
    std::string dPad = cookIntToString(pg, 42,  "{0:000}");
    stringInjectBug() = false;
    bool pass = (d0 == "42") && (dNeg == "-7") && (dEmp == "42") && (dPad == "042");
    ok = ok && pass;
    std::printf("[selftest-stringrail] IntToString 42/\"{0:0}\"=\"%s\"(42) -7=\"%s\"(-7) 42/\"\"=\"%s\"(42) "
                "42/\"{0:000}\"=\"%s\"(042) -> %s\n",
                d0.c_str(), dNeg.c_str(), dEmp.c_str(), dPad.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 15 — IntToString STANDARD specifiers D/N/X (the integer-oriented vocabulary, vs FloatToString's
  // F/N/E). "{0:N0}" 1234567 → "1,234,567" (',' grouping); "{0:X}" 255 → "FF"; "{0:X}" -1 → "FFFFFFFF"
  // (two's-complement 32-bit hex of a negative int, ported verbatim). Hand-derived against C#
  // InvariantCulture. injectBug drops the last char → RED.
  {
    stringInjectBug() = injectBug;
    std::string grp  = cookIntToString(pg, 1234567, "{0:N0}");
    std::string hex  = cookIntToString(pg, 255,     "{0:X}");
    std::string hexN = cookIntToString(pg, -1,      "{0:X}");
    stringInjectBug() = false;
    bool pass = (grp == "1,234,567") && (hex == "FF") && (hexN == "FFFFFFFF");
    ok = ok && pass;
    std::printf("[selftest-stringrail] IntToString 1234567/\"{0:N0}\"=\"%s\"(1,234,567) 255/\"{0:X}\"=\"%s\"(FF) "
                "-1/\"{0:X}\"=\"%s\"(FFFFFFFF) -> %s\n",
                grp.c_str(), hex.c_str(), hexN.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 16 — IntToString COMPOSITE LITERAL (surrounding text kept verbatim, placeholder substituted) +
  // INVALID FORMAT fallback (TiXL's FormatException catch). "id={0:000}!" on 42 → "id=042!";
  // "{0:Q}" (unrecognised spec) → "Invalid Format" (NOT a silent passthrough). injectBug drops last char.
  {
    stringInjectBug() = injectBug;
    std::string lit = cookIntToString(pg, 42, "id={0:000}!");
    std::string inv = cookIntToString(pg, 42, "{0:Q}");
    stringInjectBug() = false;
    // Note: injectBug also drops the last char of "Invalid Format" → "Invalid Forma" ≠ → RED (good: the
    // fallback string is a REAL cook output, so the tooth bites it too).
    bool pass = (lit == "id=042!") && (inv == "Invalid Format");
    ok = ok && pass;
    std::printf("[selftest-stringrail] IntToString 42/\"id={0:000}!\"=\"%s\"(id=042!) 42/\"{0:Q}\"=\"%s\""
                "(Invalid Format) -> %s\n",
                lit.c_str(), inv.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 17 — Vec3ToString (TiXL string/convert/Vec3ToString.cs) DEFAULT (empty) format = the fixed form
  // "X: {0,7:F2}\nY: {1,7:F2}\nZ: {2,7:F2}" — each component F2, right-aligned to 7, real newlines.
  // (1.5,-2,3.25) → "X:    1.50\nY:   -2.00\nZ:    3.25". Hand-derived against the .cs default branch.
  // injectBug drops the last char ("3.25"→"3.2…") → RED.
  {
    stringInjectBug() = injectBug;
    std::string got = cookVec3ToString(pg, 1.5f, -2.0f, 3.25f, "");
    stringInjectBug() = false;
    bool pass = (got == "X:    1.50\nY:   -2.00\nZ:    3.25");
    ok = ok && pass;
    std::printf("[selftest-stringrail] Vec3ToString(1.5,-2,3.25,\"\")=[%s] want=[X:    1.50/Y:   -2.00/Z:    "
                "3.25] -> %s\n",
                got.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 18 — Vec3ToString CUSTOM format: placeholder reorder/substitution + the literal "\n"→newline
  // replacement (Vec3ToString.cs s.Replace("\\n","\n")). "({0},{1},{2})" on (1,2,3) → "(1,2,3)" (bare
  // {N} → C# float ToString, integer-valued → "1"/"2"/"3"). "{0:F1}|{1:F1}|{2:F1}" on (1.5,2.5,3.5) →
  // "1.5|2.5|3.5". injectBug drops last char → RED.
  {
    stringInjectBug() = injectBug;
    std::string bare = cookVec3ToString(pg, 1.0f, 2.0f, 3.0f, "({0},{1},{2})");
    std::string f1   = cookVec3ToString(pg, 1.5f, 2.5f, 3.5f, "{0:F1}|{1:F1}|{2:F1}");
    stringInjectBug() = false;
    bool pass = (bare == "(1,2,3)") && (f1 == "1.5|2.5|3.5");
    ok = ok && pass;
    std::printf("[selftest-stringrail] Vec3ToString custom (1,2,3)=\"%s\"((1,2,3)) F1=\"%s\"(1.5|2.5|3.5) "
                "-> %s\n",
                bare.c_str(), f1.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 19 — Vec3ToString INVALID FORMAT fallback (TiXL's FormatException catch): an out-of-vocabulary
  // format (index ≥3 / unsupported spec) → "Invalid Format". "{3}" (no 4th arg) → invalid; "{0:Z}"
  // (unknown spec) → invalid. injectBug drops last char → RED.
  {
    stringInjectBug() = injectBug;
    std::string i3 = cookVec3ToString(pg, 1.0f, 2.0f, 3.0f, "{3}");
    std::string iz = cookVec3ToString(pg, 1.0f, 2.0f, 3.0f, "{0:Z}");
    stringInjectBug() = false;
    bool pass = (i3 == "Invalid Format") && (iz == "Invalid Format");
    ok = ok && pass;
    std::printf("[selftest-stringrail] Vec3ToString \"{3}\"=\"%s\" \"{0:Z}\"=\"%s\" want=Invalid Format -> %s\n",
                i3.c_str(), iz.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 20 — ★R-2 PRODUCTION RESIDENT path (the whole point of task_32b5b6e5): the SAME CombineStrings
  // graph (FloatToString producers → CombineStrings.Input, a GENUINE resident String wire) driven
  // through libFromGraph → buildEvalGraph → cookStringNodes (the per-frame pass the running app drives)
  // → extStrOut. Mirrors the FLAT legs 3/4: [1,2,3] joins "1-2-3" AND wire-order [3,1,2] joins "3-1-2"
  // (NOT sorted, NOT first-wire-only). Proves the String currency is LIVE on the production resident
  // path — before the resident string-wire rail, the flatten DROPPED the FloatToString.Output →
  // CombineStrings.Input wire, so the resident gather read the unwired-const fallback (empty) and
  // joined "" — the flat-only self-deception this gate kills. injectBug corrupts the REAL cook on the
  // resident path too: FloatToString (the producer) drops its last char in cook AND CombineStrings
  // drops its last char → the joined result is wrong → RED on the SAME StringCookFn that runs flat.
  {
    stringInjectBug() = injectBug;
    std::string gotSeq = cookCombineResident({1.0f, 2.0f, 3.0f}, "-");
    std::string gotWire = cookCombineResident({3.0f, 1.0f, 2.0f}, "-");
    stringInjectBug() = false;
    bool pass = (gotSeq == "1-2-3") && (gotWire == "3-1-2");
    ok = ok && pass;
    std::printf("[selftest-stringrail] RESIDENT(production) CombineStrings([1,2,3],\"-\")=\"%s\" want=\"1-2-3\"; "
                "([3,1,2],\"-\")=\"%s\" want=\"3-1-2\" (wire-decl) -> %s\n",
                gotSeq.c_str(), gotWire.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 21 — ★RESIDENT StringLength (refuter followup — closes the gate): StringLength is the named
  // production consumer of the resident string-wire seam (task_32b5b6e5). Before the rail, the flatten
  // DROPPED its String wire so cookHostScalarNodes skipped it (extOut[0] stayed 0). Now the flatten
  // projects a Connection driver onto the String slot and cookHostScalarNodes reads the WIRED upstream
  // via cookResidentString → .size() → extOut[0]. Two sub-cases (mirror of flat LEGs 5/6):
  //   WIRED: FloatToString(3.14,"") → "3.14" (4 chars) → StringLength.InputString → extOut[0] = 4.0
  //   CONST: StringLength.InputString strDef = "Line\nLine" (9 chars) → extOut[0] = 9.0
  // Call order (the production sequence): cookStringNodes first (drives FloatToString → extStrOut),
  // THEN cookHostScalarNodes (StringLength gathers via cookResidentString → .size() → extOut[0]).
  // injectBug corrupts the upstream cook (FloatToString drops its last char → shorter string) AND
  // then the StringLength branch clears extOut[0] to 0.0 — both sub-cases go → 0.0 under bug → RED.
  {
    // WIRED sub-case: FloatToString(3.14,"") wired into StringLength.InputString.
    // Graph: StringLength id=1 (terminal), FloatToString id=2 (producer).
    // StringLength ports: [0]=Length(out), [1]=InputString. FloatToString ports: [0]=Output(out), others.
    auto makeStringLengthGraph = [](bool wired, const std::string& constText) {
      Graph g;
      Node sl; sl.id = 1; sl.type = "StringLength";
      if (!wired) sl.strParams["InputString"] = constText;  // UNWIRED → strDef const (via libFromGraph)
      g.nodes.push_back(sl);
      if (wired) {
        Node fts; fts.id = 2; fts.type = "FloatToString";
        fts.params["Value"] = 3.14f; fts.strParams["Format"] = "";  // → "3.14" (4 chars)
        g.nodes.push_back(fts);
        // FloatToString.Output (port 0) → StringLength.InputString (port 1).
        g.connections.push_back({400, pinId(2, /*out*/ 0), pinId(1, /*InputString*/ 1)});
        g.nextId = 3;
      } else {
        g.nextId = 2;
      }
      return g;
    };

    stringInjectBug() = injectBug;
    // Wired sub-case.
    float gotWired = [&]() -> float {
      Graph g = makeStringLengthGraph(/*wired=*/true, "");
      SymbolLibrary lib = libFromGraph(g);
      ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
      ResidentEvalCtx rc;
      rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
      cookStringNodes(rg, rc);      // 1st: cooks FloatToString → extStrOut (the String wire source)
      cookHostScalarNodes(rg, rc);  // 2nd: StringLength gathers via cookResidentString → extOut[0]
      const ResidentNode* n = rg.node("1");  // StringLength resident path == flat node id "1"
      return n ? n->extOut[0] : -1.0f;
    }();
    // Const sub-case (unwired → strDef fallback "Line\nLine", 9 chars, matching flat LEG 6).
    float gotConst = [&]() -> float {
      Graph g = makeStringLengthGraph(/*wired=*/false, "Line\nLine");
      SymbolLibrary lib = libFromGraph(g);
      ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
      ResidentEvalCtx rc;
      rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
      cookStringNodes(rg, rc);      // no-op for const-only graph (no String producers to cook)
      cookHostScalarNodes(rg, rc);  // StringLength reads strInputs[InputString] → "Line\nLine" → 9
      const ResidentNode* n = rg.node("1");
      return n ? n->extOut[0] : -1.0f;
    }();
    stringInjectBug() = false;

    bool passWired = std::fabs(gotWired - 4.0f) < 1e-5f;  // wired "3.14" → 4 chars; bug → 0.0
    bool passConst = std::fabs(gotConst - 9.0f) < 1e-5f;  // const "Line\nLine" → 9 chars; bug → 0.0
    bool pass = passWired && passConst;
    ok = ok && pass;
    std::printf("[selftest-stringrail] RESIDENT(production) StringLength(wired \"3.14\")=%.1f want=4.0; "
                "(const \"Line\\nLine\")=%.1f want=9.0 -> %s\n",
                gotWired, gotConst, pass ? "PASS" : "FAIL");
  }

  q->release();
  dev->release();
  pool->release();

  // Harness convention (run_all_selftests.sh --bite): the -bug variant must exit NON-zero. injectBug
  // corrupts the REAL cook outputs -> ok is false -> return 1 (the tooth bites). No inversion.
  std::printf("[selftest-stringrail] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
