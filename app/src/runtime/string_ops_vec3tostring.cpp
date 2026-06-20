// Vec3ToString string op (string self-registration seam leaf — Vector3 + Format(string) → string).
// TiXL authority: Operators/Lib/string/convert/Vec3ToString.cs (verbatim below):
//
//   Vec3ToString.cs Update():
//     var v = Vector.GetValue(context);            // Vector = InputSlot<Vector3>
//     var s = Format.GetValue(context);            // Format = InputSlot<string>
//     try {
//       if (string.IsNullOrEmpty(s)) {
//         // Fixed width: 7 chars total (e.g., "  -1.50" or "   1.50")
//         Output.Value = string.Format(InvariantCulture,
//             "X: {0,7:F2}\nY: {1,7:F2}\nZ: {2,7:F2}", v.X, v.Y, v.Z);
//       } else {
//         var formatWithNewlines = s.Replace("\\n", "\n");           // literal \n → real newline
//         Output.Value = string.Format(InvariantCulture, formatWithNewlines, v.X, v.Y, v.Z);
//       }
//     } catch (FormatException) { Output.Value = "Invalid Format"; }
//
//   Vec3ToString.t3: Vector DefaultValue {0,0,0}, Format DefaultValue "" (empty → the fixed default form).
//
// REPLACES TransformVec3 (the work-order's #3, which is BLOCKED): TransformVec3.cs is `Vector4.Transform(
// (A,1), Matrix)` — it needs a Matrix4x4 INPUT (16 floats). The runtime has NO Matrix port type (grep:
// no dataType=="Matrix" anywhere) and NO Matrix-producing value op ported, so the matrix input would be
// 16 raw Float ports that nothing can drive → the op degenerates to the identity transform. Not a clean
// portable leaf (it needs a matrix-currency seam). Per the work order ("已港就回報換 … 另一顆未港純值
// R1"), swapped for Vec3ToString — a clean String-producer sibling of FloatToString/IntToString that
// formats a Vector3 to text. UNPORTED in both seam tables (grep verified).
//
// EVAL-SIDE LAYOUT (a String PRODUCER, rides cookStringNode — mirror of FloatToString/IntToString): its
// Vector is read from the RESOLVED Float params (Vector.x/.y/.z via the value spine); its Format is its
// ONE String input → inputStrings[0] (wired upstream string, or strDef const "" when unwired → the fixed
// default form). Writes *c.output.
//
// FORKS (named):
//   - fork-vec3tostring-vec3-as-3-floats: TiXL's Vector is InputSlot<Vector3>; sw has only Float ports, so
//     the vec3 is three Float ports (Vector.x/.y/.z) under one Widget::Vec head (vecArity=3) — the
//     standard vec-decompose convention (DotVec3 etc.). The X/Y/Z values are byte-identical to TiXL.
//   - fork-vec3tostring-format-narrow-vocabulary: like FloatToString/IntToString, C# composite formatting
//     is implemented for the COMMON cases and falls back to "Invalid Format" (TiXL's FormatException) for
//     anything outside the vocabulary. SUPPORTED:
//       • EMPTY format → the FIXED default form "X: {0,7:F2}\nY: {1,7:F2}\nZ: {2,7:F2}" rendered verbatim:
//         each component as F2 (2 fractional digits), right-aligned to width 7, with the X:/Y:/Z: labels
//         and real newlines. (v=(0,0,0) → "X:    0.00\nY:    0.00\nZ:    0.00".)
//       • NON-EMPTY format → first replace literal "\\n" with a real newline (Vec3ToString.cs), then a
//         3-arg composite supporting placeholders {0}/{1}/{2} (X/Y/Z) with an optional alignment ",W"
//         (right-align to width W, or left-align for negative W) and an optional ":F<n>"/":f<n>" fixed-
//         point spec (default = shortest float ToString). Literal text between placeholders is kept
//         verbatim. Placeholders may repeat / reorder. Anything else (other specs, missing braces,
//         indices ≥3, '#' placeholders, escaped braces) → "Invalid Format".
//   - fork-vec3tostring-float-tostring-default: a bare {0}/{1}/{2} (no spec) renders the component via the
//     SAME C# float shortest-round-trip ToString engine FloatToString uses (defaultToString) — shared
//     contract across the convert family. (Re-implemented locally here to keep the leaf independent; the
//     two engines are kept in sync by the shared string_rail_golden legs.)
//   - fork-string-host-not-gpu: string is host currency; no GPU EvaluationContext touched.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug / stringFloatParam

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for the registrar)

namespace {

// C# float.ToString(InvariantCulture) shortest round-trip (a trimmed copy of FloatToString's
// defaultToString — handles the bare {N} placeholder case; the convert family shares this contract).
std::string floatDefaultToString(float v) {
  if (std::isnan(v)) return "NaN";
  if (std::isinf(v)) return v < 0 ? "-Infinity" : "Infinity";
  if (v == 0.0f) return std::signbit(v) ? "-0" : "0";
  char buf[64];
  std::string shortest;
  for (int prec = 1; prec <= 9; ++prec) {
    std::snprintf(buf, sizeof(buf), "%.*g", prec, (double)v);
    if (std::strtof(buf, nullptr) == v) { shortest = buf; break; }
  }
  if (shortest.empty()) { std::snprintf(buf, sizeof(buf), "%.9g", (double)v); shortest = buf; }
  return shortest;  // (the rare scientific-band C# uppercasing is not exercised by Vec3ToString goldens)
}

// F<n> fixed-point (InvariantCulture '.').
std::string fixedPoint(float v, int n) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.*f", n, (double)v);
  return std::string(buf);
}

// Right-align (width>0) / left-align (width<0) to |width|, like C#'s {N,W} alignment.
std::string align(const std::string& s, int width) {
  int w = width < 0 ? -width : width;
  if ((int)s.size() >= w) return s;
  std::string pad(w - s.size(), ' ');
  return width < 0 ? (s + pad) : (pad + s);
}

// Render one placeholder body (the text between '{' and '}', WITHOUT braces): "N", "N,W", "N:Fk", "N,W:Fk".
// On success writes `out` and returns true; false = unrecognised → caller emits "Invalid Format".
// comps[0..2] = X/Y/Z. Only indices 0..2 are valid.
bool renderPlaceholder(const std::string& body, const float comps[3], std::string& out) {
  if (body.empty() || body[0] < '0' || body[0] > '9') return false;
  // index (single digit 0..2 — Vec3ToString feeds exactly 3 args)
  std::string::size_type i = 0;
  int idx = 0;
  while (i < body.size() && body[i] >= '0' && body[i] <= '9') { idx = idx * 10 + (body[i] - '0'); ++i; }
  if (idx > 2) return false;  // index out of range → FormatException

  int alignW = 0;
  bool hasAlign = false;
  if (i < body.size() && body[i] == ',') {
    ++i;
    bool neg = false;
    if (i < body.size() && (body[i] == '-' || body[i] == '+')) { neg = (body[i] == '-'); ++i; }
    if (i >= body.size() || body[i] < '0' || body[i] > '9') return false;  // ',' with no width
    int w = 0;
    while (i < body.size() && body[i] >= '0' && body[i] <= '9') { w = w * 10 + (body[i] - '0'); ++i; }
    alignW = neg ? -w : w;
    hasAlign = true;
  }

  std::string rendered;
  if (i < body.size() && body[i] == ':') {
    ++i;
    // only F<n>/f<n> supported
    if (i >= body.size() || (body[i] != 'F' && body[i] != 'f')) return false;
    ++i;
    int n = 2;        // C# default F precision = 2
    bool hasN = false;
    int nn = 0;
    while (i < body.size() && body[i] >= '0' && body[i] <= '9') { nn = nn * 10 + (body[i] - '0'); hasN = true; ++i; }
    if (i != body.size()) return false;  // trailing junk in the spec → invalid
    if (hasN) n = nn;
    if (n > 99) return false;
    rendered = fixedPoint(comps[idx], n);
  } else if (i == body.size()) {
    rendered = floatDefaultToString(comps[idx]);  // bare {N} or {N,W}
  } else {
    return false;  // junk after index/alignment, not a ':spec' → invalid
  }

  out = hasAlign ? align(rendered, alignW) : rendered;
  return true;
}

// Apply a composite format with N placeholders ({0}/{1}/{2}) + literal text. Walks the string, copies
// literal text verbatim, substitutes each "{body}". Returns true + writes `out`; false → "Invalid Format".
// (Escaped braces "{{"/"}}" and indices ≥3 are NOT supported → invalid, matching the narrow vocabulary.)
bool tryCompositeVec3(const std::string& fmt, const float comps[3], std::string& out) {
  std::string result;
  std::string::size_type i = 0;
  bool sawPlaceholder = false;
  while (i < fmt.size()) {
    char ch = fmt[i];
    if (ch == '}') return false;  // stray '}' (no '{{'/'}}' escape support) → FormatException
    if (ch == '{') {
      std::string::size_type close = fmt.find('}', i + 1);
      if (close == std::string::npos) return false;  // unbalanced → FormatException
      // reject a nested '{' before the close (no escape support)
      if (fmt.find('{', i + 1) < close) return false;
      std::string body = fmt.substr(i + 1, close - i - 1);
      std::string piece;
      if (!renderPlaceholder(body, comps, piece)) return false;
      result += piece;
      sawPlaceholder = true;
      i = close + 1;
    } else {
      result.push_back(ch);
      ++i;
    }
  }
  if (!sawPlaceholder) return false;  // no placeholder → C# string.Format leaves args unused (we treat
                                      // a no-placeholder format as out-of-vocabulary → "Invalid Format")
  out = result;
  return true;
}

// Vec3ToString: Vector (resolved Float params Vector.x/.y/.z) + Format (String input) → host string.
void cookVec3ToString(StringCookCtx& c) {
  if (!c.output) return;
  const float comps[3] = {
      stringFloatParam(c.params, "Vector.x", 0.0f),
      stringFloatParam(c.params, "Vector.y", 0.0f),
      stringFloatParam(c.params, "Vector.z", 0.0f),
  };
  std::string fmt;
  if (c.inputStrings && !c.inputStrings->empty()) fmt = (*c.inputStrings)[0];  // Format (inputStrings[0])

  std::string result;
  if (fmt.empty()) {
    // FIXED default form: "X: {0,7:F2}\nY: {1,7:F2}\nZ: {2,7:F2}" — each F2 right-aligned to 7.
    result = "X: " + align(fixedPoint(comps[0], 2), 7) + "\n"
           + "Y: " + align(fixedPoint(comps[1], 2), 7) + "\n"
           + "Z: " + align(fixedPoint(comps[2], 2), 7);
  } else {
    // NON-EMPTY: replace literal "\n" (two chars backslash-n) with a real newline, then composite-format.
    std::string withNewlines;
    for (std::string::size_type i = 0; i < fmt.size(); ++i) {
      if (i + 1 < fmt.size() && fmt[i] == '\\' && fmt[i + 1] == 'n') { withNewlines.push_back('\n'); ++i; }
      else withNewlines.push_back(fmt[i]);
    }
    if (!tryCompositeVec3(withNewlines, comps, result)) result = "Invalid Format";
  }
  *c.output = result;

  // Test-only: corrupt the REAL output (drop the last char) so a golden's transport RED bites on the
  // actual cook path, not by flipping the expected value. Off in production.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — leaf .cpp (registered via the StringOp seam; its CMake
// entry must be added to app/CMakeLists.txt — the string_ops_*.cpp list is NOT globbed).
//   Ports: "Vector.x/.y/.z" = Vec3 input under one Widget::Vec head (read via resolved params);
//          "Format"         = String input (wire-OR-const; strDef "" = Vec3ToString.t3 → fixed default);
//          "Output"         = the String output (host string currency).
static const StringOp _reg_vec3tostring{
    {"Vec3ToString", "Vec3ToString",
     {{"Output", "Output", "String", false},
      {"Vector.x", "Vector",   "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 3},
      {"Vector.y", "Vector.y", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Vector.z", "Vector.z", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Format", "Format", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookVec3ToString};

}  // namespace sw
