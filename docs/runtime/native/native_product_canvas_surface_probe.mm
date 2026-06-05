#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
std::string nsStringToStd(NSString* value)
{
    if (value == nil)
        return "";
    const char* text = [value UTF8String];
    return text != nullptr ? std::string(text) : "";
}

std::string escapeJson(const std::string& text)
{
    std::ostringstream out;
    for (const char c : text)
    {
        switch (c)
        {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                    out << ' ';
                else
                    out << c;
                break;
        }
    }
    return out.str();
}

int positiveInt(const char* text, int fallback)
{
    if (text == nullptr)
        return fallback;
    const int value = std::atoi(text);
    return value > 0 ? value : fallback;
}

void printResult(
    const std::string& status,
    bool ok,
    bool appKitRan,
    bool metalDeviceCreated,
    bool metalKitViewCreated,
    bool windowCreated,
    bool layerBacked,
    int width,
    int height,
    const std::string& title,
    const std::string& message)
{
    std::cout
        << "{"
        << "\"status\":\"" << escapeJson(status) << "\","
        << "\"ok\":" << (ok ? "true" : "false") << ","
        << "\"actualAppKitRan\":" << (appKitRan ? "true" : "false") << ","
        << "\"actualMetalDeviceCreated\":" << (metalDeviceCreated ? "true" : "false") << ","
        << "\"actualMetalKitViewCreated\":" << (metalKitViewCreated ? "true" : "false") << ","
        << "\"actualWindowCreated\":" << (windowCreated ? "true" : "false") << ","
        << "\"layerBacked\":" << (layerBacked ? "true" : "false") << ","
        << "\"viewClass\":\"MTKView\","
        << "\"backingLayer\":\"CAMetalLayer\","
        << "\"width\":" << width << ","
        << "\"height\":" << height << ","
        << "\"title\":\"" << escapeJson(title) << "\","
        << "\"message\":\"" << escapeJson(message) << "\""
        << "}\n";
    std::cout.flush();
}

int fail(const std::string& status, const std::string& message, int width, int height, const std::string& title)
{
    printResult(status, false, false, false, false, false, false, width, height, title, message);
    return 1;
}
}

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        if (argc != 4)
            return fail("usage_error", "usage: native_product_canvas_surface_probe <width> <height> <title>", 0, 0, "");

        const int width = positiveInt(argv[1], 640);
        const int height = positiveInt(argv[2], 360);
        const std::string title = argv[3] != nullptr ? std::string(argv[3]) : "simple_world";

        @try
        {
            NSApplication* app = [NSApplication sharedApplication];
            if (app == nil)
                return fail("blocked_native_window_unavailable", "NSApplication unavailable", width, height, title);
            [app setActivationPolicy:NSApplicationActivationPolicyProhibited];

            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            if (device == nil)
                return fail("blocked_native_window_unavailable", "Metal device unavailable", width, height, title);

            NSRect frame = NSMakeRect(0, 0, width, height);
            NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                           styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable)
                                                             backing:NSBackingStoreBuffered
                                                               defer:NO];
            if (window == nil)
                return fail("blocked_native_window_unavailable", "NSWindow creation failed", width, height, title);
            [window setTitle:[NSString stringWithUTF8String:title.c_str()]];

            MTKView* view = [[MTKView alloc] initWithFrame:frame device:device];
            if (view == nil)
                return fail("blocked_native_window_unavailable", "MTKView creation failed", width, height, title);
            [view setColorPixelFormat:MTLPixelFormatRGBA8Unorm];
            [view setPaused:YES];
            [view setEnableSetNeedsDisplay:YES];
            [window setContentView:view];

            const bool layerBacked = [view layer] != nil;
            printResult(
                "native_surface_ready",
                true,
                true,
                true,
                true,
                true,
                layerBacked,
                width,
                height,
                nsStringToStd([window title]),
                "NSWindow with MTKView created");
            std::_Exit(0);
        }
        @catch (NSException* exception)
        {
            return fail("blocked_native_window_unavailable", nsStringToStd([exception reason]), width, height, title);
        }
    }
}
