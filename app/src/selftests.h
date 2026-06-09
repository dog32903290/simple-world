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
constexpr double kBgR = 0.12, kBgG = 0.14, kBgB = 0.18;

// If argv carries a recognized --selftest-* / --audio-* flag, run it and return its
// process exit code. Returns -1 when no flag matched (caller proceeds to launch the GUI).
int runSelftestFromArgs(int argc, char** argv);

}  // namespace sw
