// FilePathParts string op (MULTI-OUTPUT seam consumer — Sub-seam B). Emits FOUR outputs in one cook:
//   Directory + FilenameWithoutExtension + Extension (3 Strings) + FileExists (bool dissolved to Float).
// TiXL authority: Operators/Lib/string/logic/FilePathParts.cs (ported below).
//
//   FilePathParts.cs Update():
//     var path = FilePath.GetValue(context);
//     if (!string.IsNullOrEmpty(path)) {
//       try {
//         FileExists.Value             = File.Exists(path);
//         Directory.Value              = Path.GetDirectoryName(path);
//         Extension.Value              = Path.GetExtension(path);
//         FilenameWithoutExtension.Value = Path.GetFileNameWithoutExtension(path);
//         return;
//       } catch { Reset(); return; }
//     }
//     Reset();   // FileExists=false; Directory/Filename/Extension = null
//
//   Ports: FilePath = InputSlot<string>. Outputs: Directory, FilenameWithoutExtension, Extension
//          (Slot<string>); FileExists (Slot<bool>).
//
// EVAL-SIDE LAYOUT: a multi-output String PRODUCER + bool scalar. FilePath is the ONE String input →
// inputStrings[0]. Outputs fan: Directory → *output (port 0); FilenameWithoutExtension → extraStrOutputs
// [1]; Extension → extraStrOutputs[2]; FileExists → scalarOutputs[3] (bool dissolved to Float 0/1).
//
// ★HERMETIC GOLDEN: File.Exists touches the real filesystem (non-deterministic). The COOK implements it
// faithfully (production wants the real answer), but the GOLDEN asserts ONLY the path-parsing outputs.
// Named: fork-fileexists-environment-dependent — the FileExists output is environment-dependent; the
// golden does not assert it. (We DO compute it in cook so production is faithful.)
//
// PATH-PARSING FORKS (named — C# System.IO.Path on the .cs vs the hand-ported C++ here):
//   - fork-path-separator-both: C# Path on Windows treats BOTH '/' and '\\' as directory separators; on
//     Unix only '/'. TiXL ships Windows-first (DirectoryInfo etc.), so we port the WINDOWS rule: split on
//     the LAST '/' or '\\'. This is faithful to where TiXL runs (DirectX/Windows). Deterministic, golden-
//     asserted with '/'-paths to keep the expected values unambiguous across host OSes.
//   - fork-getdirectoryname-no-root-special: C# Path.GetDirectoryName(path):
//       • returns the substring BEFORE the last separator (no trailing separator);
//       • returns "" (empty) when there is NO separator ("file.txt" → "");
//       • returns null for null/empty/whitespace-only — unreachable here (wire-OR-const, never null).
//     We do NOT reproduce C#'s root-collapsing edge cases (e.g. drive-root "C:\\" → null) — named NOT-
//     PORTED; the golden avoids those (uses ordinary "/dir/sub/name.ext"-shape paths).
//   - fork-getextension-from-last-dot: C# Path.GetExtension returns from the LAST '.' in the FILENAME
//     component (after the last separator) to the end, INCLUDING the dot; "" when the filename has no '.'
//     or ends with '.'; a DOTFILE like ".bashrc" (dot at index 0 of the filename) → C# returns ".bashrc"
//     (the whole thing IS the extension). We port: find the last '.' in the filename; if none → ""; if it
//     is the trailing char → "" (C# returns "" for "name." ); else substring(dot..end).
//   - fork-getfilenamewithoutextension: the filename component (after the last separator) with its
//     extension (the last-dot tail) stripped. ".bashrc" → "" (whole thing is the extension); "name.ext"
//     → "name"; "name" (no dot) → "name".
//   - fork-int-bool-dissolve-to-float / fork-string-host-not-gpu: as every ported string op.
#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug / stringFloatParam

namespace sw {

namespace {

// Faithful File.Exists for PRODUCTION (C# File.Exists = "an existing FILE at this path, not a dir").
// std::filesystem::is_regular_file is the matching predicate (a directory → false, like File.Exists).
// Pure C++ standard library → the runtime leaf stays pure-CPU with NO runtime→platform dependency (this
// is std, not a native OS API region call). The GOLDEN never asserts this (hermetic fork), so a stray
// real-FS read here is harmless under selftest. The noexcept overload (error_code) never throws.
bool fileExists(const std::string& path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(std::filesystem::path(path), ec);  // false on any error
}

// Index of the last directory separator ('/' or '\\'), or npos when there is none (Windows-rule fork).
std::size_t lastSep(const std::string& path) {
  std::size_t fwd = path.find_last_of('/');
  std::size_t bwd = path.find_last_of('\\');
  if (fwd == std::string::npos) return bwd;
  if (bwd == std::string::npos) return fwd;
  return fwd > bwd ? fwd : bwd;
}

// Path.GetDirectoryName: substring before the last separator (no trailing sep), or "" when none.
std::string getDirectoryName(const std::string& path) {
  std::size_t sep = lastSep(path);
  if (sep == std::string::npos) return "";  // no separator → C# returns ""
  return path.substr(0, sep);               // everything before the last separator (sep itself dropped)
}

// The filename component = everything after the last separator (or the whole path when none).
std::string fileName(const std::string& path) {
  std::size_t sep = lastSep(path);
  return sep == std::string::npos ? path : path.substr(sep + 1);
}

// Path.GetExtension: from the LAST '.' in the FILENAME to the end, including the dot; "" when no '.' or
// the '.' is the trailing char (C# "name." → ""). A leading-dot dotfile (".bashrc") → the whole filename.
std::string getExtension(const std::string& path) {
  std::string name = fileName(path);
  std::size_t dot = name.find_last_of('.');
  if (dot == std::string::npos) return "";          // no '.' → no extension
  if (dot + 1 >= name.size()) return "";            // trailing '.' ("name.") → C# "" (no chars after dot)
  return name.substr(dot);                          // ".ext" (includes the dot); ".bashrc" → ".bashrc"
}

// Path.GetFileNameWithoutExtension: the filename with its last-dot extension stripped. ".bashrc" → ""
// (the whole thing is the extension); "name.ext" → "name"; "name" → "name".
std::string getFileNameWithoutExtension(const std::string& path) {
  std::string name = fileName(path);
  std::size_t dot = name.find_last_of('.');
  if (dot == std::string::npos) return name;        // no '.' → the whole filename
  return name.substr(0, dot);                       // before the last '.' (".bashrc" → "")
}

void cookFilePathParts(StringCookCtx& c) {
  if (!c.output) return;

  // FilePath: the ONE String input port → inputStrings[0] (wired upstream string or strDef const "").
  const std::string path =
      (c.inputStrings && !c.inputStrings->empty()) ? (*c.inputStrings)[0] : std::string{};

  // FilePathParts.cs: empty path → Reset() (all outputs empty / FileExists false).
  if (path.empty()) {
    *c.output = "";                                       // Directory (port 0)
    if (c.extraStrOutputs) {
      (*c.extraStrOutputs)[1] = "";                       // FilenameWithoutExtension (port 1)
      (*c.extraStrOutputs)[2] = "";                       // Extension (port 2)
    }
    if (c.scalarOutputs) (*c.scalarOutputs)[3] = 0.0f;    // FileExists false (port 3)
    return;  // (injectBug no-op on the empty path — every output already empty, the tooth bites non-empty)
  }

  const std::string dir  = getDirectoryName(path);
  const std::string fnwe = getFileNameWithoutExtension(path);
  const std::string ext  = getExtension(path);

  *c.output = dir;                                        // Directory (port 0 → MAIN String output)
  if (c.extraStrOutputs) {
    (*c.extraStrOutputs)[1] = fnwe;                       // FilenameWithoutExtension (port 1)
    (*c.extraStrOutputs)[2] = ext;                        // Extension (port 2)
  }
  // FileExists: faithful real-filesystem check for PRODUCTION (the golden never asserts it — hermetic
  // fork). bool dissolved to Float (1.0/0.0). (port 3 → scalarOutputs → resident extOut[3].)
  if (c.scalarOutputs) (*c.scalarOutputs)[3] = fileExists(path) ? 1.0f : 0.0f;

  // Test-only: corrupt the REAL cook so the golden's RED bites on the actual path. Drop the last char of
  // EVERY String output (Directory + Filename + Extension) — distinctness/value all collapse → RED.
  // (FileExists is hermetic / not asserted, so no need to perturb the scalar.)
  if (stringInjectBug()) {
    if (!c.output->empty()) c.output->pop_back();
    if (c.extraStrOutputs) {
      auto& f = (*c.extraStrOutputs)[1]; if (!f.empty()) f.pop_back();
      auto& e = (*c.extraStrOutputs)[2]; if (!e.empty()) e.pop_back();
    }
  }
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Output ports (the MULTI-OUTPUT layout — port index is the extraStrOutputs/scalarOutputs key):
//     [0] "Directory"                = String  (MAIN → ctx.output → extStrOut[0] / stringBuf[id])
//     [1] "FilenameWithoutExtension" = String  (extraStrOutputs[1] → extStrOut[1] / stringBuf[id:1])
//     [2] "Extension"                = String  (extraStrOutputs[2] → extStrOut[2] / stringBuf[id:2])
//     [3] "FileExists"               = Float   (bool dissolved → scalarOutputs[3] → extOut[3])
//   Input port:
//     [4] "FilePath" = String input (wire-OR-const; strDef "" → empty → Reset path)
//   PortSpec positional: {id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless,
//                         vecArity, multiInput, strDef}.
static const StringOp _reg_filepathparts{
    {"FilePathParts", "FilePathParts",
     {{"Directory",                "Directory",                "String", false},
      {"FilenameWithoutExtension", "FilenameWithoutExtension", "String", false},
      {"Extension",                "Extension",                "String", false},
      {"FileExists",               "FileExists",               "Float",  false},
      {"FilePath", "FilePath", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       false, ""}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookFilePathParts};

}  // namespace sw
