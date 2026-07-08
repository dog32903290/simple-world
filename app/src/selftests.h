// CLI self-test router. Keeps main.cpp a thin app shell (ARCHITECTURE.md: 入口只放外殼):
// every --selftest-* / --audio-* command-line entry lives here as a DATA-DRIVEN table
// (鐵律 7: add a self-test = add a row, not another hand-written if in the shell). This
// is also what makes the point-operator fan-out collision-safe — a new node's self-test
// is one row here, not an edit to main's dispatch that every parallel agent fights over.
#pragma once

namespace sw {

// Editor background color as plain components, so both the live MTKView clear (shell)
// and the color self-test assert against the SAME source of truth — without dragging
// Metal into this header.
constexpr double kBgR = 0.12, kBgG = 0.12, kBgB = 0.12;  // V5: TiXL UiColors.cs:29 CanvasBackground=(0.12,0.12,0.12,0.98) pure grey (was G=0.14,B=0.18)

// If argv carries a recognized --selftest-* / --audio-* flag, run it and return its
// process exit code. Returns -1 when no flag matched (caller proceeds to launch the GUI).
int runSelftestFromArgs(int argc, char** argv);

// --probe-import <path.t3>: headless ready-set scanner for the retirement sweep (退場戰役 §2). Feeds
// ONE .t3 through the production importT3Symbol + a sibling-folder resolver and prints a machine-grep
// verdict line ("PROBE <name>: READY|NOT-READY (…)|EXCLUDED-COLLAPSE"). Exit: 0=READY / 2=NOT-READY /
// 3=EXCLUDED (collapse-8 root). Read-only — never mutates a live lib or the registry. (Impl:
// selftests_retire.cpp; dispatched from runSelftestFromArgs.)
int runProbeImport(const char* t3Path);

// --lint-catalog-names: hard-fail (nonzero) when two assets/catalog_t3/*.t3 compounds share the
// readable NAME the §1 name-fallback keys on (退場戰役 §5 風險#1). injectBug splices a synthetic
// duplicate → the RED path is a real --bite tooth. Also registered as the "lint-catalog-names"
// selftest (impl selftests_retire.cpp) so run_all_selftests.sh auto-covers it.
int runLintCatalogNames(bool injectBug);

}  // namespace sw
