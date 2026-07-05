// platform/write_to_file — impl. See write_to_file.h for spec + TiXL line citations.
#include "platform/write_to_file.h"

#include <fstream>

namespace sw {

bool writeToFileUpdate(WriteToFileState& state, const std::string& content,
                       const std::string& filepath, bool& wroteOk) {
  wroteOk = true;
  // Change-gate (WriteToFile.cs:21): write only when content differs from the last write. A never-written
  // instance always writes the first time (even if content == "" == default lastContent) — TiXL's
  // _lastContent starts null, and null != "" for the first non-null content; we model that with hasWritten.
  const bool changed = !state.hasWritten || content != state.lastContent;
  if (!changed) return false;

  // File.WriteAllText(filepath, content) (cs:26). std::ofstream truncates + writes the whole string.
  std::ofstream f(filepath, std::ios::binary | std::ios::trunc);
  if (!f) {
    wroteOk = false;  // catch (cs:28-31): logged, not thrown; gate NOT advanced so a retry re-attempts.
    return false;
  }
  f.write(content.data(), (std::streamsize)content.size());
  wroteOk = f.good();

  state.hasWritten = true;
  state.lastContent = content;  // (cs:33)
  return true;
}

}  // namespace sw
