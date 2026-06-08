// eye.mm — see eye.h. ObjC++ / MRC (no ARC), like the imgui backends.
#include "verify/eye/eye.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <Metal/Metal.hpp>  // metal-cpp: read the texture via the C++ API only

#import <AppKit/AppKit.h>    // window geometry, done by AppKit itself
#import <ImageIO/ImageIO.h>  // PNG encode/decode with NO color management (byte-exact)

#include "imgui.h"           // GetItemRectMin/Max for the widget map

#ifndef SW_EYE_DIR
#define SW_EYE_DIR "/tmp/sw_eye"  // overridden by CMake to <source>/.eye
#endif

namespace sw::eye {

namespace {

NSString* eyeDir() { return @SW_EYE_DIR; }

std::string outPath(const char* name) {
  NSString* p = [eyeDir() stringByAppendingPathComponent:[NSString stringWithUTF8String:name]];
  return std::string([p fileSystemRepresentation]);
}

void ensureDir() {
  [[NSFileManager defaultManager] createDirectoryAtPath:eyeDir()
                            withIntermediateDirectories:YES
                                             attributes:nil
                                                  error:nil];
}

// RGBA8 bytes (top row first) -> PNG. ImageIO with an explicit sRGB space and
// AlphaLast (non-premultiplied): bytes are stored verbatim, no color management
// rewrites them. NSBitmapImageRep+NSColor silently gamma-shifts values, which
// would make a dumped frame lie about its colors — so we don't use it.
bool writePNG(const std::string& path, const uint8_t* rgba, int w, int h) {
  CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CFDataRef cfd = CFDataCreate(NULL, rgba, (CFIndex)w * h * 4);
  CGDataProviderRef prov = CGDataProviderCreateWithCFData(cfd);
  CGImageRef img = CGImageCreate(w, h, 8, 32, (size_t)w * 4, cs,
                                 kCGImageAlphaLast | kCGBitmapByteOrderDefault, prov, NULL, false,
                                 kCGRenderingIntentDefault);
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8*)path.c_str(),
                                                         (CFIndex)path.size(), false);
  CGImageDestinationRef dst = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL);
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

// Collected widget rects (ImGui screen coords) for the pending map request.
struct Item {
  std::string label;
  float x0, y0, x1, y1;
};
std::vector<Item> g_items;

}  // namespace

Request poll() {
  Request r;
  NSFileManager* fm = [NSFileManager defaultManager];
  NSString* dir = eyeDir();
  auto consume = [&](NSString* name) -> bool {
    NSString* p = [dir stringByAppendingPathComponent:name];
    if ([fm fileExistsAtPath:p]) {
      [fm removeItemAtPath:p error:nil];
      return true;
    }
    return false;
  };
  r.clean = consume(@"req_clean");
  r.full = consume(@"req_full");
  r.map = consume(@"req_map");
  r.state = consume(@"req_state");
  return r;
}

void writeText(const char* outName, const char* content) {
  ensureDir();
  NSString* p = [eyeDir() stringByAppendingPathComponent:[NSString stringWithUTF8String:outName]];
  [[NSString stringWithUTF8String:content] writeToFile:p
                                            atomically:YES
                                              encoding:NSUTF8StringEncoding
                                                 error:nil];
}

void dumpTextureRGBA(MTL::Texture* tex, const char* outName) {
  if (!tex) return;
  ensureDir();
  const int w = (int)tex->width();
  const int h = (int)tex->height();
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  tex->getBytes(px.data(), w * 4, MTL::Region::Make2D(0, 0, w, h), 0);
  writePNG(outPath(outName), px.data(), w, h);
}

void dumpDrawableBGRA(MTL::Texture* tex, const char* outName) {
  if (!tex) return;
  ensureDir();
  const int w = (int)tex->width();
  const int h = (int)tex->height();
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  tex->getBytes(px.data(), w * 4, MTL::Region::Make2D(0, 0, w, h), 0);
  for (size_t i = 0; i + 3 < px.size(); i += 4) std::swap(px[i + 0], px[i + 2]);  // BGRA->RGBA
  writePNG(outPath(outName), px.data(), w, h);
}

void beginWidgetFrame() { g_items.clear(); }

void recordItem(const char* label) {
  ImVec2 mn = ImGui::GetItemRectMin();
  ImVec2 mx = ImGui::GetItemRectMax();
  g_items.push_back({label, mn.x, mn.y, mx.x, mx.y});
}

void recordRect(const char* label, float x0, float y0, float x1, float y1) {
  g_items.push_back({label, x0, y0, x1, y1});
}

void writeWidgetMap(void* mtkView, const char* outName) {
  ensureDir();
  NSView* v = (NSView*)mtkView;
  NSWindow* win = [v window];
  NSRect content = [win contentRectForFrameRect:[win frame]];  // screen pts, bottom-left
  NSScreen* scr = [win screen] ?: [NSScreen mainScreen];
  CGFloat Hs = [scr frame].size.height;
  CGFloat scale = [win backingScaleFactor];
  // content view's top-left corner in global TOP-LEFT screen points (= the unit
  // computer-use / CGEvent click in). AppKit does the flip; we don't hand-roll.
  CGFloat cx = content.origin.x;
  CGFloat cy = Hs - (content.origin.y + content.size.height);

  // ImGui main viewport pos: usually (0,0) == content top-left, but subtract it
  // so the math is correct even if the backend offsets the viewport.
  ImVec2 vp = ImGui::GetMainViewport()->Pos;

  NSMutableString* j = [NSMutableString string];
  [j appendString:@"{\n"];
  [j appendFormat:@"  \"note\": \"center_pt is the click target in global top-left screen points (computer-use units).\",\n"];
  [j appendFormat:@"  \"screen\": {\"height_pt\": %.1f, \"backing_scale\": %.2f},\n", Hs, scale];
  [j appendFormat:@"  \"window\": {\"number\": %ld, \"content_topleft_pt\": {\"x\": %.1f, \"y\": %.1f}, \"content_size_pt\": {\"w\": %.1f, \"h\": %.1f}},\n",
                  (long)[win windowNumber], cx, cy, content.size.width, content.size.height];
  [j appendFormat:@"  \"imgui_viewport_pos\": {\"x\": %.1f, \"y\": %.1f},\n", vp.x, vp.y];
  [j appendString:@"  \"items\": [\n"];
  for (size_t i = 0; i < g_items.size(); ++i) {
    const Item& it = g_items[i];
    float sx0 = cx + (it.x0 - vp.x), sy0 = cy + (it.y0 - vp.y);
    float sw = it.x1 - it.x0, sh = it.y1 - it.y0;
    [j appendFormat:@"    {\"label\": \"%s\", \"imgui_rect\": {\"x0\": %.1f, \"y0\": %.1f, \"x1\": %.1f, \"y1\": %.1f}, \"screen_topleft_pt\": {\"x\": %.1f, \"y\": %.1f, \"w\": %.1f, \"h\": %.1f}, \"center_pt\": {\"x\": %.1f, \"y\": %.1f}}%s\n",
                    it.label.c_str(), it.x0, it.y0, it.x1, it.y1, sx0, sy0, sw, sh,
                    sx0 + sw * 0.5f, sy0 + sh * 0.5f, (i + 1 < g_items.size()) ? "," : ""];
  }
  [j appendString:@"  ]\n}\n"];

  NSString* path = [NSString stringWithUTF8String:outPath(outName).c_str()];
  [j writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nil];
}

int runSelfTest(bool injectBug) {
  @autoreleasepool {
    const int W = 8, H = 8;
    const uint8_t R = 40, G = 80, B = 160;  // known truth
    std::vector<uint8_t> px((size_t)W * H * 4, 0);
    for (int i = 0; i < W * H; ++i) {
      px[i * 4 + 0] = injectBug ? 200 : R;  // wrong color when injecting a bug
      px[i * 4 + 1] = injectBug ? 10 : G;
      px[i * 4 + 2] = injectBug ? 10 : B;
      px[i * 4 + 3] = 255;
    }
    ensureDir();
    std::string path = outPath("selftest_eye.png");
    if (!writePNG(path, px.data(), W, H)) {
      printf("[selftest-eye] FAIL: writePNG\n");
      return 1;
    }
    // Reload through a fresh decode and read RAW decoded bytes (CGImage's data
    // provider), NOT via NSColor — so no color management sits between the file
    // and the assertion. Proves the PNG round-trips byte-exact.
    NSData* d = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path.c_str()]];
    CGImageSourceRef src = CGImageSourceCreateWithData((CFDataRef)d, NULL);
    CGImageRef img = src ? CGImageSourceCreateImageAtIndex(src, 0, NULL) : NULL;
    if (!img) {
      printf("[selftest-eye] FAIL: decode\n");
      if (src) CFRelease(src);
      return 1;
    }
    CFDataRef pix = CGDataProviderCopyData(CGImageGetDataProvider(img));
    const UInt8* bytes = CFDataGetBytePtr(pix);
    size_t bpr = CGImageGetBytesPerRow(img);
    const UInt8* p = bytes + (H / 2) * bpr + (W / 2) * 4;
    int gr = p[0], gg = p[1], gb = p[2];
    CFRelease(pix);
    CGImageRelease(img);
    CFRelease(src);
    bool pass = std::abs(gr - R) <= 2 && std::abs(gg - G) <= 2 && std::abs(gb - B) <= 2;
    printf("[selftest-eye] center=(%d,%d,%d) expect=(%d,%d,%d) -> %s\n", gr, gg, gb, R, G, B,
           pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
  }
}

}  // namespace sw::eye
