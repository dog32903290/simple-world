// platform/image_decode — implementation. See image_decode.h for the contract + parity notes.
//
// metal-cpp-discipline: this file mixes ObjC (ImageIO/CoreGraphics CFTypeRefs) with metal-cpp
// pointers. CF objects are released explicitly (CFRelease). The MTL::Texture we return is OWNED by
// the caller (dev->newTexture -> Rule 3: caller release()s). No per-frame loop here, so no
// AutoreleasePool needed for these one-shot calls; callers that loop must wrap their own.
#include "platform/image_decode.h"

#include <ImageIO/ImageIO.h>
#include <CoreGraphics/CoreGraphics.h>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

namespace sw {
namespace platform {
namespace {

// Decode a CGImageSource's frame 0 into tightly-packed RGBA8 via a DeviceRGB bitmap context.
// kCGBitmapByteOrder32Big | kCGImageAlphaPremultipliedLast => bytes laid out R,G,B,A. DeviceRGB +
// no ColorSync transform => stored values pass through unchanged (matches TiXL/WIC raw decode).
DecodedImage decodeSource(CGImageSourceRef src) {
  DecodedImage out;
  if (!src) return out;
  CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
  if (!img) return out;

  const size_t w = CGImageGetWidth(img);
  const size_t h = CGImageGetHeight(img);
  if (w == 0 || h == 0) { CGImageRelease(img); return out; }

  std::vector<uint8_t> buf((size_t)w * h * 4, 0);
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGContextRef ctx = CGBitmapContextCreate(
      buf.data(), w, h, 8, w * 4, cs,
      kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
  CGColorSpaceRelease(cs);
  if (!ctx) { CGImageRelease(img); return out; }

  // kCGBlendModeCopy: write source pixels verbatim (no compositing against the zero-cleared
  // backing), so opaque source bytes land unchanged.
  CGContextSetBlendMode(ctx, kCGBlendModeCopy);
  CGContextDrawImage(ctx, CGRectMake(0, 0, (CGFloat)w, (CGFloat)h), img);
  CGContextRelease(ctx);
  CGImageRelease(img);

  out.ok = true;
  out.width = (uint32_t)w;
  out.height = (uint32_t)h;
  out.rgba = std::move(buf);
  return out;
}

}  // namespace

DecodedImage decodeImageFile(const std::string& absPath) {
  CFStringRef pathStr =
      CFStringCreateWithCString(nullptr, absPath.c_str(), kCFStringEncodingUTF8);
  if (!pathStr) return {};
  CFURLRef url =
      CFURLCreateWithFileSystemPath(nullptr, pathStr, kCFURLPOSIXPathStyle, false);
  CFRelease(pathStr);
  if (!url) return {};
  CGImageSourceRef src = CGImageSourceCreateWithURL(url, nullptr);
  CFRelease(url);
  DecodedImage out = decodeSource(src);
  if (src) CFRelease(src);
  return out;
}

DecodedImage decodeImageBytes(const uint8_t* data, size_t len) {
  if (!data || len == 0) return {};
  CFDataRef cfData = CFDataCreate(nullptr, data, (CFIndex)len);
  if (!cfData) return {};
  CGImageSourceRef src = CGImageSourceCreateWithData(cfData, nullptr);
  CFRelease(cfData);
  DecodedImage out = decodeSource(src);
  if (src) CFRelease(src);
  return out;
}

MTL::Texture* textureFromRgba8(MTL::Device* dev, const DecodedImage& img, bool mipped) {
  if (!dev || !img.ok || img.width == 0 || img.height == 0) return nullptr;
  if (img.rgba.size() != (size_t)img.width * img.height * 4) return nullptr;

  // RGBA8Unorm (LINEAR), matching TiXL's Format.R8G8B8A8_UNorm (NOT _SRgb): the PNG's sRGB profile
  // is ignored; shaders sample the stored bytes raw.
  MTL::TextureDescriptor* td = MTL::TextureDescriptor::texture2DDescriptor(
      MTL::PixelFormatRGBA8Unorm, img.width, img.height, mipped);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* tex = dev->newTexture(td);  // OWNED -> caller releases
  if (!tex) return nullptr;

  tex->replaceRegion(MTL::Region::Make2D(0, 0, img.width, img.height), 0,
                     img.rgba.data(), img.width * 4);
  return tex;
}

MTL::Texture* decodeImageToTexture(MTL::Device* dev, const std::string& absPath, bool mipped) {
  return textureFromRgba8(dev, decodeImageFile(absPath), mipped);
}

std::string resolveAssetPath(const std::string& assetKey) {
#ifdef SW_ASSETS_DIR
  // Strip a leading "Lib:" prefix (TiXL's resource-pack prefix) if present.
  std::string rel = assetKey;
  const std::string kLib = "Lib:";
  if (rel.rfind(kLib, 0) == 0) rel = rel.substr(kLib.size());
  return std::string(SW_ASSETS_DIR) + "/" + rel;
#else
  (void)assetKey;
  return "";
#endif
}

}  // namespace platform
}  // namespace sw
