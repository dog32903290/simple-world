// string_ops_blendstrings_golden — BlendStrings golden (--selftest-blendstrings). Standalone; does
// NOT ride string_rail_golden.cpp so that file stays under its ratchet cap (1605).
//
// ALGORITHM SUMMARY (BlendStrings.cs):
//   Given MultiInput strings [strA, strB] and Blend:
//   floatIndex = Blend.Clamp(0, count-1.0001f), index=(int)floatIndex, blendIndex=floatIndex-index.
//   For each charIndex in [0, maxLength):
//     x = (maxLength<=1) ? 0 : charIndex/(float)(maxLength-1)
//     bp = ProgressTransition(x, blendIndex, spread) = ((x-blendIndex)/spread - blendIndex+1).Clamp(0,1)
//     charAInt = chars.IndexOf(charA).Clamp(0,charCount-1)
//     charBInt = chars.IndexOf(charB).Clamp(0,charCount-1)
//     scrambleOffset = (hashA<scramble)?(hash01(seed2)-0.5)*charCount:0
//     blendedValue = (int)(charBInt + (charAInt-charBInt)*bp + scrambleOffset).Clamp(0,charCount-1)
//     append chars[blendedValue]
//
// Palette "ABCD" (4 ASCII chars) chosen for all legs: std::string::find gives the same index as
// C# IndexOf for ASCII chars → no fork-blendstrings-characters-ascii-only-indexof impact.
// Scramble=0 for all legs → scrambleOffset=0 always; scramble path not exercised but parity is
// structural (hash01 uint32_t port is bit-identical by construction).
//
// Gather contract: inputStrings = [strA_wire, strB_wire, Characters_value] (Characters LAST).
//   The cook reads gathered.back() as Characters; gathered[0..1] as InputStrings wires.
//
// Golden values (hand-traced, strA="ABCD", strB="DCBA", palette="ABCD"):
//
//   LEG 1: Blend=0.0 → "ABCD"
//     floatIndex=0.0, blendIndex=0. bp=(x+1)≥1 → clamp 1. blendedValue=charAInt → strA.
//
//   LEG 2: Blend=1.0 (clamps to 0.9999) → "DCBA"
//     blendIndex=0.9999. bp=(x-0.9998).Clamp(0,1)=0 for all x∈[0,1]. blendedValue=charBInt → strB.
//
//   LEG 3: Blend=0.5, spread=1.0 → "DBBD"
//     blendIndex=0.5. bp=((x-0.5)/1 - 0.5+1)=(x-0.5+0.5)=x.Clamp(0,1)=x.
//     ci=0: x=0, bp=0   → (int)(3+(0-3)*0)=3 → 'D'
//     ci=1: x=1/3, bp=1/3 → (int)(2+(1-2)*(1/3))=(int)(1.666)=1 → 'B'
//     ci=2: x=2/3, bp=2/3 → (int)(1+(2-1)*(2/3))=(int)(1.666)=1 → 'B'
//     ci=3: x=1.0, bp=1.0 → (int)(0+(3-0)*1.0)=3 → 'D'
//     → "DBBD"
//
//   RED tooth (LEG3): injectBug drops last char → "DBB" ≠ "DBBD" → FAIL.
//
//   LEG 4: MaxLength=2, Blend=0.0 → "AB" (first 2 chars of strA)
//     maxLength=min(max(4,4),2)=2. x=ci/(2-1)=ci. ci=0→x=0,bp=1→'A'; ci=1→x=1,bp=1→'B'. → "AB"
//
//   LEG 5: empty inputs both → ""
//     strA="", strB="" → both empty guard → output "".
//
//   LEG 6: single input ("ABCD"), Blend=0.5
//     stringCount=1. floatIndex=0.5.Clamp(0,-0.0001)=-0.0001. index=(int)(-0.0001)=0.
//     strA=strB=input[0]="ABCD". blendIndex=-0.0001.
//     bp=((x-(-0.0001))/1 - (-0.0001)+1)=(x+0.0001+1.0001)=(x+1.0002)≥1 → clamp 1.
//     blendedValue=charAInt → output = "ABCD".
//
//   LEG 7: Blend=0.25, spread=0.5, strA="ABCD", strB="DCBA" → (hand-trace)
//     blendIndex=0.25, spread=0.5.
//     bp=((x-0.25)/0.5 - 0.25+1).Clamp(0,1) = (2x-0.5-0.25+1).Clamp(0,1) = (2x+0.25).Clamp(0,1)
//     ci=0: x=0,    bp=(0+0.25)=0.25       → (int)(3+(0-3)*0.25)=(int)(3-0.75)=(int)(2.25)=2 → 'C'
//     ci=1: x=1/3,  bp=(2/3+0.25)=0.916... → (int)(2+(1-2)*0.916)=(int)(2-0.916)=(int)(1.083)=1 → 'B'
//     ci=2: x=2/3,  bp=(4/3+0.25)=1.583→1 → (int)(1+(2-1)*1.0)=(int)(2)=2 → 'C'
//     ci=3: x=1.0,  bp=(2.0+0.25)=2.25→1  → (int)(0+(3-0)*1.0)=3 → 'D'
//     → "CBCD"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/string_op_registry.h"  // stringInjectBug, StringCookCtx, findStringOp

namespace sw {

namespace {

// Helper: invoke the BlendStrings cook fn directly through a hand-built StringCookCtx.
// gathered = [strA_wire, strB_wire, chars_val] (Characters last = back()).
static std::string directBlend(const std::string& strA, const std::string& strB,
                                const std::string& chars,
                                const std::map<std::string, float>& params) {
  const StringCookFn* fn = findStringOp("BlendStrings");
  if (!fn) return {"ERR:NO_FN"};

  std::vector<std::string> gathered{strA, strB, chars};
  std::string out;
  StringCookCtx ctx{};
  ctx.inputStrings = &gathered;
  ctx.output       = &out;
  ctx.params       = &params;
  (*fn)(ctx);
  return out;
}

// Helper: single-input variant. gathered = [strA, chars]. stringCount=1 → count-1.0001=-0.0001.
static std::string directBlendSingle(const std::string& strA, const std::string& chars,
                                      const std::map<std::string, float>& params) {
  const StringCookFn* fn = findStringOp("BlendStrings");
  if (!fn) return {"ERR:NO_FN"};

  std::vector<std::string> gathered{strA, chars};  // 1 InputStrings wire + Characters
  std::string out;
  StringCookCtx ctx{};
  ctx.inputStrings = &gathered;
  ctx.output       = &out;
  ctx.params       = &params;
  (*fn)(ctx);
  return out;
}

}  // namespace

static int runBlendStringsSelftestImpl(bool injectBug) {
  bool ok = true;

  const std::string strA{"ABCD"};
  const std::string strB{"DCBA"};
  const std::string palette{"ABCD"};  // ASCII-only: std::string::find == C# IndexOf

  // Shared base params (Scramble=0 → scrambleOffset=0 for all legs).
  std::map<std::string, float> p;
  p["BlendSpread"] = 1.0f;
  p["Scramble"]    = 0.0f;
  p["ScrambleSeed"]= 0.0f;
  p["MaxLength"]   = 10000.0f;

  // ── LEG 1: Blend=0.0 → strA → "ABCD" ──────────────────────────────────────────────────────
  {
    p["Blend"] = 0.0f; p["BlendSpread"] = 1.0f; p["MaxLength"] = 10000.0f;
    stringInjectBug() = false;
    std::string got  = directBlend(strA, strB, palette, p);
    const std::string want{"ABCD"};
    bool pass = (got == want);
    ok = ok && pass;
    std::printf("[selftest-blendstrings] LEG1 Blend=0.0 got=\"%s\" want=\"%s\" -> %s\n",
                got.c_str(), want.c_str(), pass ? "PASS" : "FAIL");
  }

  // ── LEG 2: Blend=1.0 → strB → "DCBA" ──────────────────────────────────────────────────────
  {
    p["Blend"] = 1.0f; p["BlendSpread"] = 1.0f; p["MaxLength"] = 10000.0f;
    stringInjectBug() = false;
    std::string got  = directBlend(strA, strB, palette, p);
    const std::string want{"DCBA"};
    bool pass = (got == want);
    ok = ok && pass;
    std::printf("[selftest-blendstrings] LEG2 Blend=1.0 got=\"%s\" want=\"%s\" -> %s\n",
                got.c_str(), want.c_str(), pass ? "PASS" : "FAIL");
  }

  // ── LEG 3: Blend=0.5 → mid-transition → "DBBD" (GREEN + RED tooth) ────────────────────────
  {
    p["Blend"] = 0.5f; p["BlendSpread"] = 1.0f; p["MaxLength"] = 10000.0f;

    // GREEN
    stringInjectBug() = false;
    std::string got  = directBlend(strA, strB, palette, p);
    const std::string want{"DBBD"};
    bool passGreen = (got == want);
    ok = ok && passGreen;
    std::printf("[selftest-blendstrings] LEG3 Blend=0.5 GREEN got=\"%s\" want=\"%s\" -> %s\n",
                got.c_str(), want.c_str(), passGreen ? "PASS" : "FAIL");

    // RED tooth: injectBug drops last char → "DBB" ≠ "DBBD"
    if (injectBug) {
      stringInjectBug() = true;
      std::string gotRed = directBlend(strA, strB, palette, p);
      stringInjectBug() = false;
      bool passRed = (gotRed != want);  // must differ
      ok = ok && passRed;
      std::printf("[selftest-blendstrings] LEG3 Blend=0.5 RED  got=\"%s\" want!=\"%s\" -> %s\n",
                  gotRed.c_str(), want.c_str(), passRed ? "PASS" : "FAIL");
    }
  }

  // ── LEG 4: MaxLength=2, Blend=0.0 → "AB" ──────────────────────────────────────────────────
  // maxLength = min(max(4,4), 2) = 2. At Blend=0.0, bp=1 everywhere → charAInt → first 2 of strA.
  {
    p["Blend"] = 0.0f; p["BlendSpread"] = 1.0f; p["MaxLength"] = 2.0f;
    stringInjectBug() = false;
    std::string got  = directBlend(strA, strB, palette, p);
    const std::string want{"AB"};
    bool pass = (got == want);
    ok = ok && pass;
    std::printf("[selftest-blendstrings] LEG4 MaxLength=2 Blend=0.0 got=\"%s\" want=\"AB\" -> %s\n",
                got.c_str(), pass ? "PASS" : "FAIL");
  }

  // ── LEG 5: both inputs empty → "" ──────────────────────────────────────────────────────────
  {
    p["Blend"] = 0.5f; p["BlendSpread"] = 1.0f; p["MaxLength"] = 10000.0f;
    stringInjectBug() = false;
    std::string got  = directBlend("", "", palette, p);
    bool pass = got.empty();
    ok = ok && pass;
    std::printf("[selftest-blendstrings] LEG5 both-empty got=\"%s\" want=\"\" -> %s\n",
                got.c_str(), pass ? "PASS" : "FAIL");
  }

  // ── LEG 6: single input, Blend=0.5 → passthrough → "ABCD" ────────────────────────────────
  // count=1 → floatIndex.Clamp(0,-0.0001)=-0.0001. index=0, strA=strB=input[0].
  // blendIndex=-0.0001 → bp=(x+1.0002)≥1 always → blendedValue=charAInt → "ABCD".
  {
    p["Blend"] = 0.5f; p["BlendSpread"] = 1.0f; p["MaxLength"] = 10000.0f;
    stringInjectBug() = false;
    std::string got  = directBlendSingle(strA, palette, p);
    const std::string want{"ABCD"};
    bool pass = (got == want);
    ok = ok && pass;
    std::printf("[selftest-blendstrings] LEG6 single-input Blend=0.5 got=\"%s\" want=\"%s\" -> %s\n",
                got.c_str(), want.c_str(), pass ? "PASS" : "FAIL");
  }

  // ── LEG 7: Blend=0.25, spread=0.5 → "CBCD" ────────────────────────────────────────────────
  // bp=(2x+0.25).Clamp(0,1). With strA="ABCD"(0,1,2,3), strB="DCBA"(3,2,1,0):
  // ci=0: x=0,   bp=0.25   → (int)(3+(0-3)*0.25)=(int)(2.25)=2 → 'C'
  // ci=1: x=1/3, bp=0.916  → (int)(2+(1-2)*0.916)=(int)(1.083)=1 → 'B'
  // ci=2: x=2/3, bp=1.583→1→ (int)(1+(2-1)*1.0)=2 → 'C'
  // ci=3: x=1.0, bp=2.25→1 → (int)(0+(3-0)*1.0)=3 → 'D'
  // → "CBCD"
  {
    p["Blend"] = 0.25f; p["BlendSpread"] = 0.5f; p["MaxLength"] = 10000.0f;
    stringInjectBug() = false;
    std::string got  = directBlend(strA, strB, palette, p);
    const std::string want{"CBCD"};
    bool pass = (got == want);
    ok = ok && pass;
    std::printf("[selftest-blendstrings] LEG7 Blend=0.25 spread=0.5 got=\"%s\" want=\"CBCD\" -> %s\n",
                got.c_str(), pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw

// Register as --selftest-blendstrings (order 241, after --selftest-stringbuilder at 240).
REGISTER_SELFTESTS(/*orderBase=*/241,
    {"blendstrings", sw::runBlendStringsSelftestImpl});
