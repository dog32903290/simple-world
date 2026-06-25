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
#include <string>

namespace MTL { class Texture; }

namespace sw {
namespace platform {

// Read back `tex` (RGBA8Unorm linear, e.g. the Output preview / point graph target) and
// write it to `absPath` as a PNG, colors verbatim. Creates parent dirs as needed.
// Returns false (and writes nothing) on a null texture, an unsupported pixel layout, or
// any encode/IO failure — the caller surfaces the failure to the user.
bool saveTextureToPng(MTL::Texture* tex, const std::string& absPath);

}  // namespace platform
}  // namespace sw
