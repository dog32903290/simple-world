// platform/glyph_outline — implementation. See glyph_outline.h for the contract; runtime/glyph_points.h
// for the seam rationale (TiXL-absent「中文字 as points」, 柏為 2026-07-06 拍板破例).
//
// metal-cpp-discipline / CFRelease posture (mirrors image_decode.mm): this file mixes ObjC CoreText/
// CoreGraphics CFTypeRefs with C++. Compiled -fno-objc-arc (see CMakeLists) — every Create/Copy is
// released explicitly. No per-frame loop (one-shot per glyph), so no AutoreleasePool needed.
#include "platform/glyph_outline.h"

#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>

#include <cmath>
#include <string>
#include <vector>

namespace sw {
namespace platform {
namespace {

// --- Bezier flattening (fixed subdivision to a `flatness`-point tolerance) ---------------------------
// A glyph outline is small (a few dozen curves); a fixed subdivision count keeps the walk simple and
// deterministic (the golden pins fixture geometry, not curve density — curves are a named non-parity).
// steps grows as the control-polygon span / flatness, clamped [2, 64]. NOT a parity claim against any
// other tessellator (TiXL has none); a reasonable, monotone-in-flatness sampler.
int curveSteps(double spanPts, float flatness) {
  double f = flatness > 0.01f ? (double)flatness : 0.01;
  int n = (int)std::ceil(spanPts / f);
  if (n < 2) n = 2;
  if (n > 64) n = 64;
  return n;
}

struct Walk {
  std::vector<PlatformGlyphContour>* contours = nullptr;
  double curX = 0.0, curY = 0.0;      // current pen point
  double startX = 0.0, startY = 0.0;  // subpath start (for close)
  float flatness = 1.0f;
};

void pushPoint(Walk& w, double x, double y) {
  if (w.contours->empty()) return;
  w.contours->back().xy.push_back((float)x);
  w.contours->back().xy.push_back((float)y);
}

// CGPathApply callback: dispatch each path element, flattening beziers inline.
void applyElement(void* info, const CGPathElement* e) {
  Walk& w = *static_cast<Walk*>(info);
  const CGPoint* p = e->points;
  switch (e->type) {
    case kCGPathElementMoveToPoint: {
      w.contours->push_back(PlatformGlyphContour{});
      w.curX = w.startX = p[0].x;
      w.curY = w.startY = p[0].y;
      pushPoint(w, w.curX, w.curY);
      break;
    }
    case kCGPathElementAddLineToPoint: {
      w.curX = p[0].x;
      w.curY = p[0].y;
      pushPoint(w, w.curX, w.curY);
      break;
    }
    case kCGPathElementAddQuadCurveToPoint: {
      const double c0x = w.curX, c0y = w.curY;  // start
      const double cx = p[0].x, cy = p[0].y;    // control
      const double ex = p[1].x, ey = p[1].y;    // end
      const double span = std::hypot(cx - c0x, cy - c0y) + std::hypot(ex - cx, ey - cy);
      const int steps = curveSteps(span, w.flatness);
      for (int i = 1; i <= steps; ++i) {
        double t = (double)i / steps, u = 1.0 - t;
        double x = u * u * c0x + 2 * u * t * cx + t * t * ex;
        double y = u * u * c0y + 2 * u * t * cy + t * t * ey;
        pushPoint(w, x, y);
      }
      w.curX = ex;
      w.curY = ey;
      break;
    }
    case kCGPathElementAddCurveToPoint: {
      const double c0x = w.curX, c0y = w.curY;  // start
      const double c1x = p[0].x, c1y = p[0].y;  // control 1
      const double c2x = p[1].x, c2y = p[1].y;  // control 2
      const double ex = p[2].x, ey = p[2].y;    // end
      const double span = std::hypot(c1x - c0x, c1y - c0y) + std::hypot(c2x - c1x, c2y - c1y) +
                          std::hypot(ex - c2x, ey - c2y);
      const int steps = curveSteps(span, w.flatness);
      for (int i = 1; i <= steps; ++i) {
        double t = (double)i / steps, u = 1.0 - t;
        double x = u * u * u * c0x + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * ex;
        double y = u * u * u * c0y + 3 * u * u * t * c1y + 3 * u * t * t * c2y + t * t * t * ey;
        pushPoint(w, x, y);
      }
      w.curX = ex;
      w.curY = ey;
      break;
    }
    case kCGPathElementCloseSubpath: {
      // Repeat the subpath start to close the loop (first==last, LoadSvg's NeedsClosing convention).
      if (!w.contours->empty() && !w.contours->back().xy.empty()) {
        pushPoint(w, w.startX, w.startY);
        w.contours->back().closed = true;
      }
      w.curX = w.startX;
      w.curY = w.startY;
      break;
    }
  }
}

// Create a CTFont from a family/PostScript name or a font-FILE path. Empty name → Helvetica.
// Caller CFRelease()s the result. Returns NULL only if creation fails entirely.
CTFontRef makeFont(const char* fontName, float fontSize) {
  const CGFloat size = (CGFloat)(fontSize > 0.0f ? fontSize : 1.0f);
  if (!fontName || fontName[0] == '\0') {
    return CTFontCreateWithName(CFSTR("Helvetica"), size, nullptr);
  }
  std::string s(fontName);
  bool looksLikePath = (s.find('/') != std::string::npos) &&
                       (s.size() > 4 &&
                        (s.rfind(".ttf") == s.size() - 4 || s.rfind(".otf") == s.size() - 4 ||
                         s.rfind(".TTF") == s.size() - 4 || s.rfind(".OTF") == s.size() - 4 ||
                         s.rfind(".ttc") == s.size() - 4));
  if (looksLikePath) {
    CFStringRef cfPath =
        CFStringCreateWithCString(kCFAllocatorDefault, s.c_str(), kCFStringEncodingUTF8);
    if (cfPath) {
      CFURLRef url =
          CFURLCreateWithFileSystemPath(kCFAllocatorDefault, cfPath, kCFURLPOSIXPathStyle, false);
      CFRelease(cfPath);
      if (url) {
        CFArrayRef descs = CTFontManagerCreateFontDescriptorsFromURL(url);
        CFRelease(url);
        if (descs && CFArrayGetCount(descs) > 0) {
          CTFontDescriptorRef d = (CTFontDescriptorRef)CFArrayGetValueAtIndex(descs, 0);
          CTFontRef f = CTFontCreateWithFontDescriptor(d, size, nullptr);
          CFRelease(descs);
          if (f) return f;
        }
        if (descs) CFRelease(descs);
      }
    }
    // fall through to name-based creation if the path route failed
  }
  CFStringRef cfName = CFStringCreateWithCString(kCFAllocatorDefault, s.c_str(), kCFStringEncodingUTF8);
  if (!cfName) return CTFontCreateWithName(CFSTR("Helvetica"), size, nullptr);
  CTFontRef f = CTFontCreateWithName(cfName, size, nullptr);
  CFRelease(cfName);
  return f;
}

}  // namespace

bool extractGlyphOutline(uint32_t codepoint, const char* fontName, float fontSize, float flatness,
                         PlatformGlyphOutline& out) {
  out.contours.clear();
  out.advance = 0.0f;
  out.valid = false;

  CTFontRef font = makeFont(fontName, fontSize);
  if (!font) return false;

  // codepoint (UTF-32) → UTF-16 unichar pair (surrogate for astral planes; BMP → single unit).
  UniChar utf16[2];
  CFIndex nUnits = 0;
  if (codepoint <= 0xFFFF) {
    utf16[0] = (UniChar)codepoint;
    nUnits = 1;
  } else {
    uint32_t v = codepoint - 0x10000;
    utf16[0] = (UniChar)(0xD800 + (v >> 10));
    utf16[1] = (UniChar)(0xDC00 + (v & 0x3FF));
    nUnits = 2;
  }

  CGGlyph glyphs[2] = {0, 0};
  bool haveGlyph = CTFontGetGlyphsForCharacters(font, utf16, glyphs, nUnits);
  CGGlyph g = glyphs[0];

  // Advance (fontSize-scaled, since the font was created at fontSize).
  CGSize adv = CGSizeZero;
  CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, &g, &adv, 1);
  out.advance = (float)adv.width;
  out.valid = true;

  if (haveGlyph || g != 0) {
    CGPathRef path = CTFontCreatePathForGlyph(font, g, nullptr);
    if (path) {
      Walk w;
      w.contours = &out.contours;
      w.flatness = flatness > 0.0f ? flatness : 1.0f;
      CGPathApply(path, &w, applyElement);
      CFRelease(path);
      // Drop any degenerate <2-point (i.e. <4 floats) contour a stray move produced.
      std::vector<PlatformGlyphContour> kept;
      kept.reserve(out.contours.size());
      for (auto& c : out.contours)
        if (c.xy.size() >= 4) kept.push_back(std::move(c));
      out.contours = std::move(kept);
    }
  }

  CFRelease(font);
  return true;
}

}  // namespace platform
}  // namespace sw
