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

void printResult(const std::string& status, bool ok, bool appKitRan, bool metalDeviceCreated, bool mtkViewCreated, bool windowCreated, bool toolbarCreated, bool inspectorCreated, bool diagnosticsCreated, int width, int height, const std::string& title, const std::string& message)
{
    std::cout
        << "{"
        << "\"status\":\"" << escapeJson(status) << "\","
        << "\"ok\":" << (ok ? "true" : "false") << ","
        << "\"actualAppKitRan\":" << (appKitRan ? "true" : "false") << ","
        << "\"actualMetalDeviceCreated\":" << (metalDeviceCreated ? "true" : "false") << ","
        << "\"actualMetalKitViewCreated\":" << (mtkViewCreated ? "true" : "false") << ","
        << "\"actualWindowCreated\":" << (windowCreated ? "true" : "false") << ","
        << "\"actualToolbarCreated\":" << (toolbarCreated ? "true" : "false") << ","
        << "\"actualInspectorCreated\":" << (inspectorCreated ? "true" : "false") << ","
        << "\"actualDiagnosticsCreated\":" << (diagnosticsCreated ? "true" : "false") << ","
        << "\"width\":" << width << ","
        << "\"height\":" << height << ","
        << "\"title\":\"" << escapeJson(title) << "\","
        << "\"message\":\"" << escapeJson(message) << "\""
        << "}\n";
    std::cout.flush();
}

int fail(const std::string& status, const std::string& message, int width, int height, const std::string& title)
{
    printResult(status, false, false, false, false, false, false, false, false, width, height, title, message);
    return 1;
}
}

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        if (argc != 4)
            return fail("usage_error", "usage: native_human_app_workflow_probe <width> <height> <title>", 0, 0, "");

        const int width = positiveInt(argv[1], 960);
        const int height = positiveInt(argv[2], 600);
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

            NSToolbar* toolbar = [[NSToolbar alloc] initWithIdentifier:@"simple_world.toolbar"];
            [window setToolbar:toolbar];

            NSStackView* root = [[NSStackView alloc] initWithFrame:frame];
            [root setOrientation:NSUserInterfaceLayoutOrientationVertical];

            NSStackView* body = [[NSStackView alloc] initWithFrame:NSMakeRect(0, 28, width, height - 56)];
            [body setOrientation:NSUserInterfaceLayoutOrientationHorizontal];

            NSView* library = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 180, height - 56)];
            MTKView* canvas = [[MTKView alloc] initWithFrame:NSMakeRect(0, 0, width - 460, height - 56) device:device];
            NSView* inspector = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 280, height - 56)];
            NSTextField* diagnostics = [NSTextField labelWithString:@"runtime.frame.ready"];
            NSTextField* selected = [NSTextField labelWithString:@"gradient_1"];

            if (canvas == nil || inspector == nil || diagnostics == nil || selected == nil)
                return fail("blocked_native_window_unavailable", "native workflow views unavailable", width, height, title);

            [canvas setColorPixelFormat:MTLPixelFormatRGBA8Unorm];
            [canvas setPaused:YES];
            [inspector addSubview:selected];
            [body addArrangedSubview:library];
            [body addArrangedSubview:canvas];
            [body addArrangedSubview:inspector];
            [root addArrangedSubview:body];
            [root addArrangedSubview:diagnostics];
            [window setContentView:root];

            printResult(
                "human_workflow_ready",
                true,
                true,
                true,
                true,
                true,
                toolbar != nil,
                inspector != nil,
                diagnostics != nil,
                width,
                height,
                nsStringToStd([window title]),
                "human-facing workflow window created");
            std::_Exit(0);
        }
        @catch (NSException* exception)
        {
            return fail("blocked_native_window_unavailable", nsStringToStd([exception reason]), width, height, title);
        }
    }
}
