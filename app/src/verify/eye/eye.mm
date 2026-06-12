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

#include "verify/hand/hand.h"  // hand_pending in map.json (verify-internal sibling, same leaf)

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

// Native NSMenu rows: registered ONCE by the menu builder at startup; persistent
// (beginWidgetFrame never clears them — they don't live inside an imgui frame).
struct NativeMenuItem {
  std::string label;     // "nsmenu:<menu>:<title>"
  std::string shortcut;  // "cmd+s" / "cmd+shift+s"
};
std::vector<NativeMenuItem> g_nativeItems;

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

void recordNativeMenuItem(const char* menu, const char* title, const char* key, bool shift) {
  g_nativeItems.push_back({std::string("nsmenu:") + menu + ":" + title,
                           std::string("cmd+") + (shift ? "shift+" : "") + key});
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

namespace {
// True if the recorded set currently holds a row whose label starts with `prefix`.
bool itemsHavePrefix(const char* prefix) {
  const std::string pre = prefix;
  for (const Item& it : g_items)
    if (it.label.rfind(pre, 0) == 0) return true;
  return false;
}
}  // namespace

int runMapSelfTest(bool injectBug) {
  @autoreleasepool {
    // Headless ImGui frame: no platform/renderer backend, just DisplaySize + a built
    // font atlas (NewFrame asserts both). This exercises the SAME recordItem() the app
    // feeds from editor_ui — GetItemRectMin reads g.LastItemData, which only exists
    // inside an active frame, so a real frame is the honest way to test the map path.
    IMGUI_CHECKVERSION();
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 800.0f);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* tex = nullptr;
    int tw = 0, th = 0;
    io.Fonts->GetTexDataAsRGBA32(&tex, &tw, &th);  // build the atlas so NewFrame passes
    io.Fonts->SetTexID((ImTextureID)1);

    // One toolbar pass, recorded exactly like drawToolbar(): unconditional recordItem()
    // after each widget, plus the Add-Node popup's rows when it is open.
    auto toolbarPass = [&](bool openPopup, bool closeInside, bool* popupWasOpen) {
      ImGui::Begin("Toolbar");
      ImGui::Button("New");      recordItem("New");
      ImGui::Button("Open");     recordItem("Open");
      ImGui::Button("Save");     recordItem("Save");
      ImGui::Button("Add Node"); recordItem("Add Node");
      if (openPopup) ImGui::OpenPopup("add_node_popup");
      if (ImGui::BeginPopup("add_node_popup")) {
        if (popupWasOpen) *popupWasOpen = true;
        ImGui::MenuItem("RadialPoints"); recordItem("menu:RadialPoints");
        ImGui::MenuItem("LinePoints");   recordItem("menu:LinePoints");
        if (closeInside) ImGui::CloseCurrentPopup();  // mirrors clicking a row -> popup dismissed
        ImGui::EndPopup();
      }
      ImGui::End();
    };

    // Frame 1 — fresh: open the popup. Toolbar buttons must be recorded.
    ImGui::NewFrame();
    beginWidgetFrame();
    toolbarPass(/*openPopup=*/true, /*closeInside=*/false, nullptr);
    const size_t c1 = g_items.size();
    ImGui::EndFrame();

    // Frame 2 — popup OPEN: its rows are recorded, then we dismiss it (the user's click).
    bool popupOpen2 = false;
    ImGui::NewFrame();
    beginWidgetFrame();
    toolbarPass(/*openPopup=*/false, /*closeInside=*/true, &popupOpen2);
    const size_t c2 = g_items.size();
    const bool menuRecorded2 = itemsHavePrefix("menu:");
    ImGui::EndFrame();

    // Frame 3 — popup CLOSED: THE reported failing frame. The map must still be
    // populated, and must carry NO stale "menu:" rows from frame 2 (clear+refill).
    // injectBug models the regression by suppressing this post-popup record pass.
    ImGui::NewFrame();
    beginWidgetFrame();
    if (!injectBug) toolbarPass(/*openPopup=*/false, /*closeInside=*/false, nullptr);
    const size_t c3 = g_items.size();
    const bool staleMenu3 = itemsHavePrefix("menu:");
    ImGui::EndFrame();

    // Frame 4 — BeginPopupContextItem path (批次9: the Inspector param right-click menu,
    // "popup rows must be findable in the map" live pain). The popup is keyed to the LAST
    // item's id; headless can't right-click, so OpenPopup on the SAME str_id (same window
    // scope -> same ImGuiID) stands in for it — the RECORD path is what's under test, not
    // the right-click mechanics. injectBug suppresses the row's record -> leg goes RED.
    ImGui::NewFrame();
    beginWidgetFrame();
    ImGui::Begin("Inspector");
    ImGui::Button("Center");
    recordItem("param:center");
    ImGui::OpenPopup("##anim_center");
    bool ctxPopupSeen = false;
    if (ImGui::BeginPopupContextItem("##anim_center")) {
      ctxPopupSeen = true;
      ImGui::MenuItem("Animate");
      if (!injectBug) recordItem("insp:Animate");
      ImGui::EndPopup();
    }
    ImGui::End();
    const bool inspRecorded = ctxPopupSeen && itemsHavePrefix("insp:");
    ImGui::EndFrame();

    // Native NSMenu rows (批次9): registered once at startup, must SURVIVE the per-frame
    // clear (they're not imgui widgets) and never leak into the rect items.
    g_nativeItems.clear();
    if (!injectBug) recordNativeMenuItem("File", "Save", "s", false);
    beginWidgetFrame();
    const bool nativeOk = g_nativeItems.size() == 1 &&
                          g_nativeItems[0].label == "nsmenu:File:Save" &&
                          g_nativeItems[0].shortcut == "cmd+s";
    g_nativeItems.clear();  // don't leak test rows into a live map

    ImGui::DestroyContext(ctx);

    // The承重 invariant: the editor was drawn on every frame, so the map is never
    // empty — regardless of the prior popup interaction — and never leaks stale rows.
    // (OpenPopup makes BeginPopup true the SAME frame, so c1 already carries the menu
    // rows; the clear+refill proof is c2 > c3 — popup-open frame has the menu rows,
    // the post-popup frame has dropped them back to the toolbar.)
    const bool toolbarFresh = c1 >= 4;                 // at least the 4 toolbar buttons
    const bool popupRecorded = popupOpen2 && menuRecorded2 && c2 > c3;  // menu rows when open
    const bool postPopupOk  = c3 >= 4 && !staleMenu3;  // <- breaks under the reported bug
    const bool pass = toolbarFresh && popupRecorded && postPopupOk && inspRecorded && nativeOk;
    printf("[selftest-map] fresh=%zu popup=%zu(menu=%d) post_popup=%zu(stale=%d) ctxitem=%d "
           "native=%d -> %s\n",
           c1, c2, (int)menuRecorded2, c3, (int)staleMenu3, (int)inspRecorded, (int)nativeOk,
           pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
  }
}

}  // namespace sw::eye
