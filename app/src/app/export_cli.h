#pragma once
// app/export_cli — the headless `--export` entry (zone: app). Parses the export flags, bootstraps a
// Metal device the SAME way the goldens do (CreateSystemDefaultDevice + the metallib + the leaf-seam
// registrations main.mm wires for the GUI), loads the target project, runs the deterministic export
// engine, and returns a process exit code. Split out of main/selftests so both routers stay thin and
// so the whole headless render path is a single testable seam.
//
// Usage (all flags optional except --export):
//   simple_world --export <out.mov|out_dir> [--open <proj.swproj>] [--fps N] [--from F] [--to F]
//                [--width W] [--height H] [--codec prores4444|h264|png]
//
// Returns 0 on a finalized file, nonzero on any failure (bad args / device / load / encode). Prints
// a one-line verdict to stdout so the harness can key on the exit code + message.
namespace sw {
namespace app {

// Handle argv if it contains `--export`; returns the process exit code (0/nonzero). Returns -1 when
// there is NO --export flag (so the caller falls through to the GUI, same protocol as the selftest
// router's -1). Never launches a window.
int runExportCli(int argc, char** argv);

}  // namespace app
}  // namespace sw
