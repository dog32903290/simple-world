// FilesInFolder stringlist op (stringlist self-registration seam leaf — Folder(String) + Filter(String)
// + TriggerUpdate(bool) → List<string> of file PATHS + NumberOfFiles(int)). A CLEAN LEAF on the EXISTING
// StringList cook flow (the same path SplitString rides): a StringList PRODUCER driven by String inputs,
// reading the real filesystem via std::filesystem (the std lib — pure-CPU, no runtime→platform call,
// exactly like FilePathParts' File.Exists). TiXL authority: Operators/Lib/io/file/FilesInFolder.cs:
//
//   FilesInFolder.cs Update():
//     // (dirty-flag gating omitted — sw cooks on demand; the COMPUTED result is identical)
//     var filter = Filter.GetValue(context);
//     var filePaths = Directory.Exists(_resolvedFolder)
//                         ? Directory.GetFiles(_resolvedFolder).ToList()
//                         : new List<string>();                       // non-existent folder → empty list
//     Files.Value = string.IsNullOrEmpty(Filter.Value)
//                       ? filePaths
//                       : filePaths.FindAll(filepath => filepath.Contains(filter)).ToList();
//     NumberOfFiles.Value = Files.Value.Count;
//
//   Ports: Folder = InputSlot<string> (default "."); Filter = InputSlot<string> (default "*.png");
//          TriggerUpdate = InputSlot<bool>. Outputs: Files = Slot<List<string>>; NumberOfFiles = Slot<int>.
//
// EVAL-SIDE LAYOUT: a StringList PRODUCER (rides the StringListCookCtx String gather, same as SplitString).
// Folder is the FIRST String input → inputStrings[0]; Filter the SECOND → inputStrings[1]. TriggerUpdate
// is a bool transport-trigger that, in TiXL, only forces a re-read of an unchanged folder — sw cooks every
// frame on demand (no dirty-flag caching), so the trigger has no effect on the COMPUTED list and is a
// pinless/unconsumed port for parity only (fork-filesinfolder-trigger-unconsumed).
//
// SEMANTICS (ported 1:1 from the .cs):
//   • Directory.GetFiles returns the FULL PATHS of the REGULAR FILES directly in the folder (NOT
//     subdirectories, NOT recursive). We mirror with std::filesystem::directory_iterator + is_regular_file.
//   • Filter is applied as a PLAIN SUBSTRING test on the full path string (filepath.Contains(filter)), NOT
//     a glob — despite the "*.png" default LOOKING like a glob, C# string.Contains("*.png") would match the
//     literal sequence "*.png" (it is a faithful substring test). An EMPTY filter → keep every file.
//   • Non-existent folder → empty list + count 0.
//
// FORKS (named):
//   - fork-filesinfolder-substring-not-glob: Filter is a SUBSTRING match (faithful to the .cs
//     filepath.Contains(filter)), NOT a glob/regex. The "*.png" default is matched literally as a substring.
//   - fork-filesinfolder-sorted-deterministic: sw SORTS the result list lexicographically. TiXL relies on
//     Directory.GetFiles order, which is filesystem-/OS-dependent and NON-deterministic; a hermetic golden
//     needs a stable order, so we sort. (TiXL itself imposes no order, so sorting is a strict refinement —
//     same SET of files, deterministic sequence.)
//   - fork-filesinfolder-flat-only-no-resident: the StringList rail is flat-cook (does NOT enter the
//     resident eval graph for a list-PRODUCING leaf); the golden asserts the FLAT leg only. (Same flat
//     channel SplitString produces on.)
//   - fork-filesinfolder-count-deferred: TiXL's NumberOfFiles(int) is Files.Count. StringListCookCtx has a
//     single list output (no scalar-output sink, exactly as SplitString's Count is deferred). NumberOfFiles
//     is registered as a port for parity but NOT transported — a consumer recomputes it as the list .size().
//     (The golden still ASSERTS the count = output list size at the op boundary.)
//   - fork-filesinfolder-trigger-unconsumed: TriggerUpdate only re-reads an unchanged folder in TiXL's
//     dirty-flag model; sw cooks on demand so it does not affect the computed list — pinless parity port.
//   - fork-string-host-not-gpu: string list is host currency; no GPU EvaluationContext touched.
#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget
#include "runtime/stringlist_op_registry.h"  // StringListOp / StringListCookCtx / stringListInjectBug

namespace sw {

namespace {

// List the FULL PATHS of the regular files directly in `folder` (NOT recursive, NOT directories), filtered
// by SUBSTRING `filter` on the full path (empty filter → keep all), SORTED lexicographically. Mirrors
// Directory.GetFiles + FindAll(Contains) + the sort fork. Non-existent / non-directory folder → empty.
// Pure std::filesystem (no throw — the error_code overloads are used; pure-CPU, no platform call).
void listFilesInFolder(const std::string& folder, const std::string& filter,
                       std::vector<std::string>& out) {
  out.clear();
  std::error_code ec;
  const std::filesystem::path dir(folder);
  if (!std::filesystem::is_directory(dir, ec)) return;  // non-existent / not-a-dir → empty (Directory.Exists)

  for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code fec;
    if (!std::filesystem::is_regular_file(it->path(), fec)) continue;  // skip subdirs (Directory.GetFiles)
    const std::string path = it->path().string();                     // the FULL path (the .cs emits paths)
    // Filter: plain substring Contains on the full path. Empty filter → keep every file.
    if (filter.empty() || path.find(filter) != std::string::npos) out.push_back(path);
  }
  // fork-filesinfolder-sorted-deterministic: TiXL's Directory.GetFiles order is non-deterministic; sort.
  std::sort(out.begin(), out.end());
}

// FilesInFolder: Folder (input 0) + Filter (input 1) → host string list of full file paths.
void cookFilesInFolder(StringListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();

  // inputStrings[0] = Folder; inputStrings[1] = Filter. Unwired ports → their strDef consts ("." / "*.png").
  const std::string folder =
      (c.inputStrings && c.inputStrings->size() > 0) ? (*c.inputStrings)[0] : std::string{"."};
  const std::string filter =
      (c.inputStrings && c.inputStrings->size() > 1) ? (*c.inputStrings)[1] : std::string{"*.png"};

  listFilesInFolder(folder, filter, *c.output);  // Directory.GetFiles + FindAll(Contains) + sort fork
  // NumberOfFiles (= output->size()) is NOT transported — fork-filesinfolder-count-deferred.

  // Test-only: corrupt the REAL output (drop the last path) so the golden's RED case fires on the actual
  // cook path (count + membership both wrong), not by flipping the expected value. Off in production.
  if (stringListInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringListOp — independent leaf .cpp (no shared edit point).
//   Port ORDER (position in spec = gather order for inputStrings):
//     [0] "Files"         = StringList output (the host string-list currency — StringList PRODUCER)
//     [1] "NumberOfFiles" = Float output     (TiXL Int NumberOfFiles; NOT transported — count-deferred fork)
//     [2] "Folder"        = String input      (wire-OR-const; strDef "." = current dir, the TiXL default)
//     [3] "Filter"        = String input      (wire-OR-const; strDef "*.png" = the TiXL default substring)
//     [4] "TriggerUpdate" = Float input       (bool transport-trigger; unconsumed parity port)
//   The stringlist driver gathers String input ports into inputStrings in spec order: ports [2]/[3] are
//   the two String inputs → inputStrings[0]==Folder, inputStrings[1]==Filter.
static const StringListOp _reg_filesinfolder{
    {"FilesInFolder", "FilesInFolder",
     {{"Files", "Files", "StringList", false},
      {"NumberOfFiles", "NumberOfFiles", "Float", false},
      {"Folder", "Folder", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "."},
      {"Filter", "Filter", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false,
       "*.png"},
      {"TriggerUpdate", "TriggerUpdate", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       false, ""}},
     /*evaluate=*/nullptr},  // StringList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookFilesInFolder};

}  // namespace sw
