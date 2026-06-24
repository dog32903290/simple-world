// eye.mm — see eye.h. ObjC++ / MRC (no ARC), like the imgui backends.
#include "verify/eye/eye.h"
#include "verify/eye/eye_internal.h"  // detail:: buffers/primitives shared with eye_selftest.mm

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <Metal/Metal.hpp>  // metal-cpp: read the texture via the C++ API only

#import <AppKit/AppKit.h>    // window geometry, done by AppKit itself
#import <ImageIO/ImageIO.h>  // PNG encode/decode with NO color management (byte-exact)

#include "imgui.h"           // GetItemRectMin/Max for the widget map
#include "imgui_internal.h"  // OpenPopupStack / ImGuiWindow — walk open combo/popup items

#include "verify/hand/hand.h"  // hand_pending in map.json (verify-internal sibling, same leaf)

#ifndef SW_EYE_DIR
#define SW_EYE_DIR "/tmp/sw_eye"  // overridden by CMake to <source>/.eye
#endif

namespace sw::eye {

namespace {
NSString* eyeDir() { return @SW_EYE_DIR; }
}  // namespace

// Shared verify-internal seam (eye_internal.h): the live buffers + primitives the
// headless self-tests (eye_selftest.mm) drive directly. Non-anonymous so that TU links.
namespace detail {

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

// The live per-frame buffers. Item/NativeMenuItem are declared in eye_internal.h so the
// self-test TU sees the same types. accessors items()/nativeItems() hand the self-test the
// real buffers (no copy) — it asserts on exactly what the live map serializes.
std::vector<Item> g_items;
std::vector<NativeMenuItem> g_nativeItems;
std::vector<Item>& items() { return g_items; }
std::vector<NativeMenuItem>& nativeItems() { return g_nativeItems; }

// True if the recorded set currently holds a row whose label starts with `prefix`.
bool itemsHavePrefix(const char* prefix) {
  const std::string pre = prefix;
  for (const Item& it : g_items)
    if (it.label.rfind(pre, 0) == 0) return true;
  return false;
}

// Gap 1: ImGui combos / context popups / dropdowns are SEPARATE internal windows
// (e.g. "##Combo_00") that the app never draws item-by-item, so their rows never go
// through a recordItem() hook and stay invisible to the hand. This walks the live
// OpenPopupStack — the engine's own retained record of which popups are open — and
// synthesizes one addressable rect per row of each open popup window, indexed:
//   popup_item:<window_name>:<row>   imgui_rect (same schema as recordItem rows)
// Row geometry is reconstructed from the popup window's own WorkRect (content area,
// already shrunk by WindowPadding) and the uniform list stride ImGui itself uses for
// menu/combo height (g.FontSize + ItemSpacing.y — see CalcMaxPopupHeightFromItemCount).
// Rows are clipped to the content extent (ContentSize.y) so a 3-item combo emits 3
// rows, not a screenful. This is ADDITIVE: existing recordItem rows are untouched, and
// app-drawn popups that DO record their rows (menu:/ctx:/insp:) keep their named rects
// alongside these generic indexed ones. The driver flow is unchanged: open the popup ->
// req_map -> read popup_item rects -> click the row's center_pt.
void recordOpenPopupItems() {
  ImGuiContext* g = ImGui::GetCurrentContext();
  if (!g) return;
  const float stride = g->FontSize + g->Style.ItemSpacing.y;  // ImGui's own list row stride
  if (stride <= 0.0f) return;
  for (int p = 0; p < g->OpenPopupStack.Size; ++p) {
    ImGuiWindow* win = g->OpenPopupStack[p].Window;
    if (!win || !win->Active || win->Hidden) continue;  // only popups actually on screen
    const ImRect work = win->WorkRect;       // content rows live here (padding already removed)
    const float x0 = work.Min.x, x1 = work.Max.x;
    // How many rows fit: content height / stride, clamped to the work-area height so a
    // partially-scrolled / clipped popup doesn't manufacture phantom rows past the window.
    const float contentH = win->ContentSize.y > 0.0f ? win->ContentSize.y
                                                      : (work.Max.y - work.Min.y);
    int rows = (int)((contentH + g->Style.ItemSpacing.y + 0.5f) / stride);
    if (rows < 1) rows = 1;
    if (rows > 256) rows = 256;  // sanity ceiling; popups this tall don't happen here
    const char* wname = win->Name ? win->Name : "popup";
    for (int r = 0; r < rows; ++r) {
      float ry0 = work.Min.y + (float)r * stride;
      float ry1 = ry0 + g->FontSize;
      if (ry0 >= work.Max.y) break;                       // ran past the visible content
      if (ry1 > work.Max.y) ry1 = work.Max.y;
      char lbl[128];
      snprintf(lbl, sizeof(lbl), "popup_item:%s:%d", wname, r);
      g_items.push_back({lbl, x0, ry0, x1, ry1});
    }
  }
}

}  // namespace detail

// The live sink + map serialization below uses the detail buffers/primitives directly.
using namespace detail;

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

void recordNativeMenuItem(const char* menu, const char* title, const char* key, bool shift) {
  g_nativeItems.push_back({std::string("nsmenu:") + menu + ":" + title,
                           std::string("cmd+") + (shift ? "shift+" : "") + key});
}

void writeWidgetMap(void* mtkView, const char* outName) {
  ensureDir();
  // Gap 1: fold any currently-open combo/popup window rows into the item set so the
  // hand can address them. Additive; runs after the app's own recordItem() pass.
  recordOpenPopupItems();
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
  // Queued hand steps not yet applied: a driver polls map round-trips until 0 to
  // know a multi-frame gesture (click=3 steps, drag=15) actually finished.
  [j appendFormat:@"  \"hand_pending\": %s,\n", sw::hand::hasPending() ? "true" : "false"];
  // Native NSMenu rows (metadata only — see eye.h: no rect, drive via shortcut).
  // %@ via stringWithUTF8String, NOT %s: NSString's %s decodes in the SYSTEM encoding,
  // which silently empties labels with multibyte UTF-8 (e.g. the "Open…" ellipsis).
  [j appendString:@"  \"native_menu_items\": [\n"];
  for (size_t i = 0; i < g_nativeItems.size(); ++i)
    [j appendFormat:@"    {\"label\": \"%@\", \"shortcut\": \"%@\"}%s\n",
                    [NSString stringWithUTF8String:g_nativeItems[i].label.c_str()],
                    [NSString stringWithUTF8String:g_nativeItems[i].shortcut.c_str()],
                    (i + 1 < g_nativeItems.size()) ? "," : ""];
  [j appendString:@"  ],\n"];
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

}  // namespace sw::eye
