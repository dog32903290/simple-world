// snapshot.cpp — see snapshot.h. Pure C++ (path policy + timestamp); the native PNG encode
// lives in platform/image_save, the document path in app/document.
#include "app/snapshot.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "app/document.h"          // doc::documentPath() — the project folder home for Screenshots
#include "platform/image_decode.h" // platform::assetLibraryRoot() — the unsaved-doc fallback root
#include "platform/image_save.h"   // platform::saveTextureToPng — the product PNG writer

namespace sw {

namespace {

// Parent directory of an absolute file path ("" if it has no separator).
std::string parentOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

}  // namespace

std::string snapshotDir() {
  // TEST seam: a scenario / selftest points $SW_SNAPSHOT_DIR at a temp dir so it can assert
  // the PNG landed WITHOUT an interactive dialog. Highest priority so verification is
  // deterministic. Production never sets it.
  if (const char* env = std::getenv("SW_SNAPSHOT_DIR"))
    if (env[0] != '\0') return std::string(env);

  // TiXL home: <project folder>/Screenshots (RenderProcess.cs:54-55). The project folder is
  // the directory holding the saved .swproj.
  const std::string& docPath = doc::documentPath();
  if (!docPath.empty()) {
    const std::string folder = parentOf(docPath);
    if (!folder.empty()) return folder + "/Screenshots";
  }

  // Unsaved document: no project folder yet. Fall back to <assets>/Screenshots — a stable
  // user-visible location (the same asset root LoadImage resolves against).
  const std::string assets = platform::assetLibraryRoot();
  if (!assets.empty()) return assets + "/Screenshots";

  // Last resort (no SW_ASSETS_DIR compiled in — only happens in stripped test builds).
  return "/tmp/sw_snapshots";
}

std::string snapshotStamp() {
  // TiXL: $"{DateTime.Now:yyyy_MM_dd-HH_mm_ss_fff}" (RenderProcess.cs:60). Local time, with a
  // millisecond field so rapid successive snapshots don't collide.
  timespec ts{};
  clock_gettime(CLOCK_REALTIME, &ts);
  std::tm tm{};
  localtime_r(&ts.tv_sec, &tm);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d_%02d_%02d-%02d_%02d_%02d_%03d", tm.tm_year + 1900,
                tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                (int)(ts.tv_nsec / 1000000));
  return std::string(buf);
}

std::string saveSnapshot(MTL::Texture* tex, std::string* outPath) {
  const std::string path = snapshotDir() + "/" + snapshotStamp() + ".png";
  if (outPath) *outPath = path;
  if (!platform::saveTextureToPng(tex, path)) return "";
  return path;
}

}  // namespace sw
