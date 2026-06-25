// app/src/selftests_snapshot.cpp — area manifest leaf for the --selftest router:
// out-snapshot-png product PNG write (saveSnapshot → platform::saveTextureToPng).
//
// Shell-tier (app/src/ root, like the other selftests_<area>.cpp): self-registers its row into
// selftestRegistry() during pre-main dynamic init; selftests.cpp reads that sink. LEAF-LOCAL — it
// includes its own headers directly (the snapshot fn has no other selftest caller), so
// selftests_decls.h is NOT touched. Reached via main's --selftest-snapshot (+ the -bug refuter).
//
// Ground truth: TiXL RenderProcess.TryRenderScreenShot (RenderProcess.cs:47-63) writes the Output
// texture to <project>/Screenshots/<stamp>.png. This proves the PRODUCT write end-to-end: build a
// known RGBA8 texture, point $SW_SNAPSHOT_DIR at a temp dir (the deterministic test seam — no NFD
// dialog), call saveSnapshot, and assert a real, non-empty, valid PNG landed at the returned path
// AND that decoding it back recovers the known pixel. injectBug skips the actual write so the
// "file exists" assertion FAILS (teeth — proves the assertion couples to the write, not to luck).
#include "runtime/selftest_registry.h"

#include "app/snapshot.h"
#include "platform/image_decode.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <Metal/Metal.hpp>

#include <sys/stat.h>
#include <unistd.h>

namespace sw {

namespace {

bool fileExists(const std::string& p) {
  struct stat st{};
  return ::stat(p.c_str(), &st) == 0 && st.st_size > 0;
}

long fileSize(const std::string& p) {
  struct stat st{};
  return ::stat(p.c_str(), &st) == 0 ? (long)st.st_size : -1;
}

// First 8 bytes of every PNG: 0x89 P N G \r \n 0x1A \n.
bool hasPngMagic(const std::string& p) {
  FILE* f = std::fopen(p.c_str(), "rb");
  if (!f) return false;
  unsigned char sig[8] = {0};
  size_t n = std::fread(sig, 1, 8, f);
  std::fclose(f);
  static const unsigned char kPng[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  if (n != 8) return false;
  for (int i = 0; i < 8; ++i)
    if (sig[i] != kPng[i]) return false;
  return true;
}

}  // namespace

int runSnapshotSaveSelfTest(bool injectBug) {
  // A unique temp dir under /tmp, threaded through the production path via the $SW_SNAPSHOT_DIR
  // test seam (exactly what tests/scenarios/output_snapshot_png.scn uses, so this gate exercises
  // the SAME dir-policy branch the live scenario hits).
  char tmpl[] = "/tmp/sw_snapshot_test_XXXXXX";
  const char* dir = ::mkdtemp(tmpl);
  if (!dir) {
    std::printf("[selftest-snapshot] FAIL: mkdtemp\n");
    return 1;
  }
  ::setenv("SW_SNAPSHOT_DIR", dir, /*overwrite=*/1);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-snapshot] FAIL: no Metal device\n");
    return 1;
  }

  // A known 4x3 RGBA8 texture with an asymmetric marker pixel at (2,1) = (10,200,30,255), so a
  // round-trip decode can prove the BYTES survived (not just that some PNG exists).
  const uint32_t W = 4, H = 3;
  std::vector<uint8_t> rgba((size_t)W * H * 4, 0);
  for (size_t i = 0; i + 3 < rgba.size(); i += 4) rgba[i + 3] = 255;  // opaque
  const size_t markIdx = (size_t)(1 * W + 2) * 4;  // pixel (x=2,y=1)
  rgba[markIdx + 0] = 10;
  rgba[markIdx + 1] = 200;
  rgba[markIdx + 2] = 30;
  rgba[markIdx + 3] = 255;

  MTL::Texture* tex = platform::textureFromCpuBuffer(dev, rgba.data(), W, H,
                                                     (uint64_t)MTL::PixelFormatRGBA8Unorm, 4);
  if (!tex) {
    std::printf("[selftest-snapshot] FAIL: could not build source texture\n");
    return 1;
  }

  std::string outPath;
  std::string written;
  if (!injectBug) {
    written = sw::saveSnapshot(tex, &outPath);  // the real product write
  } else {
    // BUG: skip the write entirely but still compute the path. The "file exists" assertion must
    // BITE — proving the green run's pass is caused by the write, not by a leftover/temp file.
    outPath = sw::snapshotDir() + "/" + sw::snapshotStamp() + ".png";
    written = outPath;  // pretend success without writing — the file is NOT there
  }

  tex->release();
  dev->release();

  int rc = 0;
  auto check = [&](bool cond, const char* msg) {
    if (!cond) {
      std::printf("[selftest-snapshot] FAIL: %s\n", msg);
      rc = 1;
    }
  };

  // The path is always reported and lands under our temp dir (the dir-policy seam works).
  check(!outPath.empty(), "saveSnapshot returned no path");
  check(outPath.rfind(std::string(dir) + "/", 0) == 0, "path not under SW_SNAPSHOT_DIR");
  check(written == outPath, "returned path != intended path");

  // The load-bearing assertion: a real, non-empty, valid PNG actually landed on disk.
  check(fileExists(outPath), "snapshot PNG missing or empty on disk");
  if (fileExists(outPath)) {
    check(fileSize(outPath) > 8, "snapshot PNG too small");
    check(hasPngMagic(outPath), "snapshot file lacks PNG magic bytes");

    // Decode it back and confirm the marker pixel survived byte-exact (the bytes are the render).
    platform::DecodedImage back = platform::decodeImageFile(outPath);
    check(back.ok, "snapshot PNG did not decode");
    check(back.width == W && back.height == H, "snapshot PNG wrong dimensions");
    if (back.ok && back.rgba.size() == (size_t)W * H * 4) {
      const uint8_t* m = back.rgba.data() + markIdx;
      check(m[0] == 10 && m[1] == 200 && m[2] == 30, "snapshot marker pixel corrupted");
    }
  }

  if (rc == 0) std::printf("[selftest-snapshot] PASS (%s)\n", outPath.c_str());
  return rc;
}

// High orderBase so this APPENDS at the end of --selftest-list (the registry sorts by `order`);
// it never reorders any existing row. 600 is clear of every block in use (max today is 500).
REGISTER_SELFTESTS(/*orderBase=*/600, {"snapshot", runSnapshotSaveSelfTest});

}  // namespace sw
