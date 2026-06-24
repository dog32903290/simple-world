// platform/image_decode — native macOS PNG (and any ImageIO-readable image) decode.
//
// ZONE: platform (native macOS interface). Uses ImageIO/CoreGraphics — the correct region for a
// native image decoder (ARCHITECTURE.md 五區: platform = 原生 macOS 接口). It produces an
// MTL::Texture* directly because MTL::* is a *system framework type*, not a higher zone — platform
// is allowed to touch Metal (it includes no runtime/app/ui/verify source). So the whole native
// decode -> upload path lives in one platform file and never crosses a zone boundary.
//
// PARITY (TiXL Core/DataTypes/Texture.cs:96-118 TryCreate): TiXL decodes via WIC
// FormatConverter -> PixelFormat.Format32bppRGBA (RGBA byte order) and uploads as
// Format.R8G8B8A8_UNorm (LINEAR UNorm, NOT _SRgb — the PNG's sRGB ICC profile is ignored; shaders
// sample the stored bytes raw). We match exactly: ImageIO -> DeviceRGB, byte-order 32-big +
// PremultipliedLast (RGBA), then MTLPixelFormatRGBA8Unorm. Verified byte-identical to TiXL/WIC on
// the perlin-noise-rgb.png asset (probe: ImageIO == PIL == stored PNG bytes).
//
// FORK (named): CoreGraphics 8-bit RGBA bitmap contexts only support PREMULTIPLIED alpha
// (kCGImageAlphaLast / straight alpha is rejected by CGBitmapContextCreate — verified). TiXL/WIC
// produce STRAIGHT alpha. For fully-opaque assets (alpha=255 everywhere, like the noise asset) the
// premultiply is a no-op, so this is exact for Phase 1. A future asset with real translucency would
// diverge by the premultiply; decode it via a different path or un-premultiply then.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MTL { class Device; class Texture; }

namespace sw {
namespace platform {

// Pure-bytes decode result: raw RGBA8, tightly packed (stride = width*4), straight stored values
// (no colorspace conversion). ok=false on any failure (file missing, undecodable, etc.).
struct DecodedImage {
  bool ok = false;
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba;  // size = width*height*4 when ok
};

// Decode an image file at an ABSOLUTE path to RGBA8 bytes via ImageIO (no Metal, no zone crossing).
DecodedImage decodeImageFile(const std::string& absPath);

// Decode in-memory PNG/image bytes to RGBA8 (same pipeline as decodeImageFile).
DecodedImage decodeImageBytes(const uint8_t* data, size_t len);

// Generic CPU-buffer -> texture core (factored out of textureFromRgba8's non-mipped path; Slice B).
// Allocates a NON-MIPPED, ShaderRead, StorageMode=Shared texture of (w*h) texels in `fmt` and uploads
// `bytes` (tightly packed, rowPitch = w*bytesPerTexel). StorageMode=Shared is load-bearing: it lets a
// CPU getBytes readback golden (ValuesToTexture's R32Float chain golden) read the result. Returns an
// OWNED texture (caller release()s / NS::TransferPtr) or nullptr on bad args.
//   `fmt` is the MTL::PixelFormat raw enum value passed as uint64_t (the .mm casts it back) so this
//   header stays free of the heavy Metal.hpp include — same convention as tex_op_cache.h::TexPixelFormat.
//   ValuesToTexture passes (R32Float, 4); textureFromRgba8 passes (RGBA8Unorm, 4).
MTL::Texture* textureFromCpuBuffer(MTL::Device* dev, const void* bytes, uint32_t w, uint32_t h,
                                   uint64_t fmt, uint32_t bytesPerTexel);

// Upload decoded RGBA8 bytes to an MTLPixelFormatRGBA8Unorm texture (StorageModeShared,
// ShaderRead). Returns an OWNED texture (caller release()s / NS::TransferPtr) or nullptr.
// mipped=false for the noise asset (TiXL generates mips on LoadImage, but the raw decode itself
// is level-0; Phase 1 verifies the decode, not mip generation). The non-mipped path delegates to
// textureFromCpuBuffer; the mipped path keeps its own mip-allocating descriptor (PNG-decode callers
// asking for mips are NOT regressed).
MTL::Texture* textureFromRgba8(MTL::Device* dev, const DecodedImage& img, bool mipped = false);

// Convenience: decode a file straight to a texture. nullptr on any failure.
MTL::Texture* decodeImageToTexture(MTL::Device* dev, const std::string& absPath,
                                   bool mipped = false);

// --selftest-imagedecode entry. Decodes the copied perlin-noise-rgb.png asset, asserts dimensions
// and pinned pixels (RGBA8Unorm texture readback). injectBug swaps R<->B channels on readback to
// prove the tooth BITES (the asymmetric green pixel at (100,200)=(106,191,102) flips to (102,191,106)
// -> RED). fn(bool injectBug) -> process exit code (0 PASS / 1 FAIL).
int runImageDecodeSelfTest(bool injectBug);

// Asset resolution. Maps a TiXL-style asset key to our repo's copied asset.
//   TiXL "Lib:images/basic/perlin-noise-rgb.png"  ->  <repo>/assets/images/basic/perlin-noise-rgb.png
// Returns the absolute path under SW_ASSETS_DIR (compile-time define) for the given relative key,
// or "" if SW_ASSETS_DIR is unset. The key is the path AFTER the "Lib:" prefix (which we strip).
std::string resolveAssetPath(const std::string& assetKey);

// The absolute ROOT of the shared-install asset library (SW_ASSETS_DIR), or "" if unset. This is the
// directory the asset browser ENUMERATES to list available `Lib:` assets — the inverse of
// resolveAssetPath (a key resolves to root + "/" + relative). Kept here (platform) so the one place
// that knows SW_ASSETS_DIR owns both directions; the app-zone enumerator (asset_library) walks it.
std::string assetLibraryRoot();

}  // namespace platform
}  // namespace sw
