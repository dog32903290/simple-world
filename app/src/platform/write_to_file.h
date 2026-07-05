// platform/write_to_file — the WriteToFile op's file-write core (io/file family). TiXL WriteToFile writes
// its Content string to Filepath, but ONLY when Content changed since the last write (a debounce so a
// constant string doesn't re-touch the file every frame); the outputs forward Content + Filepath.
//
// The change-gate is the load-bearing behavior (write-on-change, not write-every-frame). This helper owns
// the per-instance `_lastContent` so the gate is testable in isolation: feed the same content twice → the
// second call is a no-op (returns false); feed new content → it writes (returns true). The real byte I/O
// is std::ofstream (a golden writes a temp file + reads it back byte-for-byte).
//
// platform leaf: std::ofstream + a tiny state struct, no runtime/UI, no upward dep.
//
// Ground truth mirrored (external/tixl, read-only):
//   Operators/Lib/io/file/WriteToFile.cs:17-42  Update — if(content != _lastContent) File.WriteAllText(
//     filepath, content); _lastContent = content;  Result = content; OutFilepath = filepath.
#pragma once
#include <string>

namespace sw {

// Per-instance write-gate state (TiXL private string _lastContent, :44). A fresh instance has never
// written, so the first call with ANY content writes.
struct WriteToFileState {
  bool        hasWritten = false;
  std::string lastContent;
};

// Run one WriteToFile update (WriteToFile.cs Update :17-42). Writes `content` to `filepath` via
// File.WriteAllText IFF content differs from the last write (or nothing has been written yet). Updates
// the gate state. Returns true iff a write happened this call. `wroteOk` (out) is false if the write was
// attempted but the ofstream failed (bad path) — mirrors the catch in the .cs (logged, not thrown).
bool writeToFileUpdate(WriteToFileState& state, const std::string& content,
                       const std::string& filepath, bool& wroteOk);

}  // namespace sw
