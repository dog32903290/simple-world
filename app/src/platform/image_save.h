#pragma once
// platform/image_save — PRODUCT-facing texture → PNG file writer (zone: platform).
//
// The agent's eye already encodes textures to PNG (verify/eye/eye.mm dumpTextureRGBA),
// but that is the TEST readback path — it lives in verify/ and always writes to the
// .eye/ scratch dir. The product "Snapshot" affordance (TiXL OutputWindow.cs:332
// Icon.Snapshot → RenderProcess.TryRenderScreenShot) needs a PNG write the USER keeps,
// on a real path. ARCHITECTURE 鐵律 3: verify is a leaf, business code must not call its
// internals — so the product PNG encode lives HERE in platform/ (native ImageIO), not
// in verify/. Mirrors the eye's byte-exact encode (sRGB space, AlphaLast, no color
// management) so a product snapshot has the SAME pixels as a test capture.
//
// Compiled WITHOUT ARC (CMake -fno-objc-arc), like eye.mm / image_decode.mm: the
// CoreGraphics/ImageIO CFTypeRefs are released manually.
#include <cstdint>
#include <string>

namespace MTL { class Texture; }

namespace sw {
namespace platform {

// Read back `tex` (RGBA8Unorm linear, e.g. the Output preview / point graph target) and
// write it to `absPath` as a PNG, colors verbatim. Creates parent dirs as needed.
// Returns false (and writes nothing) on a null texture, an unsupported pixel layout, or
// any encode/IO failure — the caller surfaces the failure to the user.
bool saveTextureToPng(MTL::Texture* tex, const std::string& absPath);

// Encode a raw RGBA8 buffer (`rgba` = w*h*4 bytes, top row first, non-premultiplied AlphaLast)
// to a PNG at `absPath`, colors verbatim — the byte-level sibling of saveTextureToPng that takes
// pixels ALREADY read back (the export PNG-sequence sink hands it frames pulled from the cook
// target). Same encoder, so a snapshot and an export frame are byte-identical. Creates parent
// dirs. Returns false on a bad arg or any encode/IO failure.
bool saveRgbaToPng(const uint8_t* rgba, int w, int h, const std::string& absPath);

}  // namespace platform
}  // namespace sw
