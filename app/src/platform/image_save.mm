// image_save.mm — see image_save.h. ObjC++ / MRC (no ARC), like eye.mm / image_decode.mm:
// the ImageIO/CoreGraphics CFTypeRefs are released by hand.
#include "platform/image_save.h"

#include <cstdint>
#include <vector>

#include <Metal/Metal.hpp>  // metal-cpp: read the texture via the C++ API only

#import <Foundation/Foundation.h>  // NSFileManager: create the Screenshots dir
#import <ImageIO/ImageIO.h>        // PNG encode with NO color management (byte-exact)
#import <CoreGraphics/CoreGraphics.h>

namespace sw {
namespace platform {

namespace {

// Create every missing parent directory of `absPath` (e.g. <project>/Screenshots).
bool ensureParentDir(const std::string& absPath) {
  @autoreleasepool {
    NSString* p = [NSString stringWithUTF8String:absPath.c_str()];
    NSString* dir = [p stringByDeletingLastPathComponent];
    if (dir.length == 0) return true;
    return [[NSFileManager defaultManager] createDirectoryAtPath:dir
                                    withIntermediateDirectories:YES
                                                     attributes:nil
                                                          error:nil] == YES;
  }
}

// RGBA8 bytes (top row first) → PNG. Identical encode discipline to verify/eye/eye.mm
// writePNG: an explicit sRGB space + AlphaLast (non-premultiplied) so the stored bytes
// are written verbatim, no NSBitmapImageRep gamma rewrite — the snapshot PNG carries the
// EXACT pixels the renderer produced.
bool writePngBytes(const std::string& path, const uint8_t* rgba, int w, int h) {
  CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CFDataRef cfd = CFDataCreate(NULL, rgba, (CFIndex)w * h * 4);
  CGDataProviderRef prov = CGDataProviderCreateWithCFData(cfd);
  CGImageRef img = CGImageCreate(w, h, 8, 32, (size_t)w * 4, cs,
                                 kCGImageAlphaLast | kCGBitmapByteOrderDefault, prov, NULL, false,
                                 kCGRenderingIntentDefault);
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8*)path.c_str(),
                                                         (CFIndex)path.size(), false);
  CGImageDestinationRef dst = url ? CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL)
                                  : NULL;
  bool ok = false;
  if (dst && img) {
    CGImageDestinationAddImage(dst, img, NULL);
    ok = CGImageDestinationFinalize(dst);
  }
  if (dst) CFRelease(dst);
  if (url) CFRelease(url);
  if (img) CGImageRelease(img);
  CGDataProviderRelease(prov);
  CFRelease(cfd);
  CGColorSpaceRelease(cs);
  return ok;
}

}  // namespace

bool saveTextureToPng(MTL::Texture* tex, const std::string& absPath) {
  // A reusable platform primitive owns its own autorelease boundary so the
  // NSString/NSFileManager temporaries (via ensureParentDir) drain per call and
  // never rely on a caller's pool.
  @autoreleasepool {
    if (!tex || absPath.empty()) return false;
    // Only the linear RGBA8 layout the Output preview target uses (point_graph target /
    // displayTex). A drawable BGRA path is the agent eye's concern (verify), not a product
    // snapshot of the render layer.
    if (tex->pixelFormat() != MTL::PixelFormatRGBA8Unorm) return false;
    const int w = (int)tex->width();
    const int h = (int)tex->height();
    if (w <= 0 || h <= 0) return false;
    if (!ensureParentDir(absPath)) return false;

    std::vector<uint8_t> px((size_t)w * h * 4, 0);
    tex->getBytes(px.data(), w * 4, MTL::Region::Make2D(0, 0, w, h), 0);
    return writePngBytes(absPath, px.data(), w, h);
  }
}

}  // namespace platform
}  // namespace sw
