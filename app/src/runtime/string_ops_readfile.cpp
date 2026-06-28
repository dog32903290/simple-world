// ReadFile string op — reads a TEXT file at a path string and emits its WHOLE contents as a host string.
// A CLEAN LEAF on the EXISTING String cook flow (no new seam): one String input (FilePath) → one String
// output (Result). TiXL authority: Operators/Lib/io/file/ReadFile.cs.
//
//   ReadFile.cs (the load-bearing path, distilled):
//     _fileContents = new Resource<string>(FilePath, TryLoad);    // Resource<> wraps a file watcher
//     Result.UpdateAction += Update; TriggerUpdate.UpdateAction += OnTriggerUpdate;
//     OnTriggerUpdate: _fileContents.MarkFileAsChanged();          // force a re-load on the next pull
//     TryLoad(file, ...): TryOpenFileStream(Read) → new StreamReader(stream).ReadToEnd();  // whole file
//                         on open/read failure → newValue = null (→ our "")
//     Update: Result.Value = _fileContents.GetValue(context);      // the cached/loaded whole-file string
//     Ports: FilePath = InputSlot<string>; TriggerUpdate = InputSlot<bool>. Output: Result = Slot<string>.
//
// EVAL-SIDE LAYOUT: a SINGLE-output String PRODUCER. FilePath is the ONE String input → inputStrings[0]
// (wired upstream string, OR the strDef const when unwired). Result → *output (port 0 — the MAIN String
// channel a downstream String consumer reads). TriggerUpdate is a re-read trigger only (see fork below) —
// it carries no value into the output, so we expose it but never branch on it (sw re-reads every cook).
//
// ★HERMETIC GOLDEN: file I/O touches the real filesystem. The COOK does the real read (production wants the
// real file contents); the GOLDEN (selftests_readfile.cpp) drives this cook directly against a committed
// fixture (assets/text/readfile_fixture.txt) and asserts the exact known bytes — see that file.
//
// FORKS (named):
//   - fork-readfile-no-hot-reload: TiXL wraps the read in Resource<string> with a FILE WATCHER and hot-
//     reload (MarkFileAsChanged on TriggerUpdate, GetValue returns the cached value until the watcher fires).
//     sw has no watcher/cache: every cook RE-READS the file from disk. TriggerUpdate is therefore a no-op
//     re-read trigger in sw (we re-read every cook anyway), so the observable Result is faithful — it is
//     always the current on-disk contents (TiXL converges to the same value once its watcher fires).
//   - fork-readfile-flat-only-no-resident: the String rail is FLAT-cook (string_op_registry.h:24) — it does
//     NOT enter resident_eval_graph. Same current scope as every sibling String op (and as LoadImage's
//     fork[resident-uses-default-path]). The golden covers the FLAT leg (the cook fn run directly).
//   - fork-readfile-raw-path-no-resolve: ReadFile.cs resolves the path via FileResource (TiXL's resource
//     manager). sw's String-rail siblings (FilePathParts) use the RAW path through std::filesystem with no
//     asset-root rewrite; we match that — std::ifstream opens the path verbatim (the golden hands an
//     absolute path so it is cwd-independent).
//   - fork-readfile-null-to-empty: TiXL returns null on missing/empty path or open/read failure; sw has no
//     null string currency, so we emit "" (the same null→"" dissolve every ported String op uses).
//   - fork-readfile-text-binary-verbatim: StreamReader.ReadToEnd() decodes bytes as text (default UTF-8,
//     may normalize a BOM); sw reads the file in BINARY and emits the bytes verbatim. For ASCII (the golden
//     fixture) the two are byte-identical. Named NOT-PORTED: TiXL's encoding-sniff / BOM handling.
#include <fstream>
#include <ios>
#include <sstream>
#include <string>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug

namespace sw {

namespace {

// Read the WHOLE file at `path` as raw bytes → string. Empty/missing path or open failure → "" (the
// null→"" fork). Binary mode so the bytes are emitted verbatim (fork-readfile-text-binary-verbatim).
std::string readWholeFile(const std::string& path) {
  if (path.empty()) return "";                      // ReadFile.cs: empty path → no file → null → ""
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return "";                     // open failure → TryLoad returns false → null → ""
  std::ostringstream ss;
  ss << in.rdbuf();                                 // whole stream → buffer (ReadToEnd equivalent)
  if (in.bad()) return "";                          // read failure (catch in TryLoad) → null → ""
  return ss.str();
}

void cookReadFile(StringCookCtx& c) {
  if (!c.output) return;

  // FilePath: the ONE String input port → inputStrings[0] (wired upstream string, or strDef const when
  // unwired). TriggerUpdate carries no value here (fork-readfile-no-hot-reload) — we never read it.
  const std::string path =
      (c.inputStrings && !c.inputStrings->empty()) ? (*c.inputStrings)[0] : std::string{};

  *c.output = readWholeFile(path);  // Result (port 0 → MAIN String output) = whole file contents (or "")

  // Test-only: corrupt the REAL cook so the golden's RED bites on the actual read path. Drop the last
  // byte of the emitted contents — value collapses → mismatch → RED. (No expected-value inversion; the
  // production read path is perturbed.) Empty output (missing/empty path leg) is untouched → that leg
  // is not the biting one, which is correct.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared registry edit point).
//   Output port:
//     [0] "Result"  = String  (MAIN → ctx.output → extStrOut[0] / stringBuf[id])
//   Input ports:
//     [1] "FilePath"      = String input (wire-OR-const; strDef "" → empty → "" output)
//     [2] "TriggerUpdate" = Float input  (TiXL bool re-read trigger dissolved to Float w/ Widget::Bool —
//                           consumed by NOTHING in sw, fork-readfile-no-hot-reload; present for parity.
//                           Same positional form as SetBpm's TriggerUpdate port, node_registry_math_anim).
//   PortSpec positional: {id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless,
//                         vecArity, multiInput, strDef}.
static const StringOp _reg_readfile{
    {"ReadFile", "ReadFile",
     {{"Result", "Result", "String", false},
      {"FilePath", "FilePath", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       false, ""},
      {"TriggerUpdate", "TriggerUpdate", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookReadFile};

}  // namespace sw
