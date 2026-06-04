#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstdlib>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr int bytesPerPixel = 4;

std::string nsStringToStd (NSString* value)
{
    if (value == nil)
        return "";
    const char* text = [value UTF8String];
    return text != nullptr ? std::string (text) : "";
}

std::string errorMessage (NSError* error, const std::string& fallback)
{
    if (error == nil)
        return fallback;
    std::string message = nsStringToStd ([error localizedDescription]);
    return message.empty() ? fallback : message;
}

std::string escapeJson (const std::string& text)
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
                if (static_cast<unsigned char> (c) < 0x20)
                    out << ' ';
                else
                    out << c;
                break;
        }
    }
    return out.str();
}

void printResult (
    const std::string& status,
    bool ok,
    bool actualCompilerRan,
    bool actualMetalRan,
    int width,
    int height,
    std::size_t byteCount,
    bool nonBlack,
    bool varied,
    int nonBlackPixels,
    int uniqueColorSamples,
    const std::string& message,
    const std::string& compilerDiagnostic = "")
{
    std::cout
        << "{"
        << "\"status\":\"" << escapeJson (status) << "\","
        << "\"ok\":" << (ok ? "true" : "false") << ","
        << "\"actualCompilerRan\":" << (actualCompilerRan ? "true" : "false") << ","
        << "\"actualMetalRan\":" << (actualMetalRan ? "true" : "false") << ","
        << "\"width\":" << width << ","
        << "\"height\":" << height << ","
        << "\"byteCount\":" << byteCount << ","
        << "\"nonBlack\":" << (nonBlack ? "true" : "false") << ","
        << "\"varied\":" << (varied ? "true" : "false") << ","
        << "\"nonBlackPixels\":" << nonBlackPixels << ","
        << "\"uniqueColorSamples\":" << uniqueColorSamples << ","
        << "\"message\":\"" << escapeJson (message) << "\"";
    if (! compilerDiagnostic.empty())
        std::cout << ",\"compilerDiagnostic\":\"" << escapeJson (compilerDiagnostic) << "\"";
    std::cout << "}\n";
}

int fail (
    const std::string& status,
    bool actualCompilerRan,
    const std::string& message,
    int width,
    int height,
    const std::string& compilerDiagnostic = "")
{
    printResult (status, false, actualCompilerRan, false, width, height, 0, false, false, 0, 0, message, compilerDiagnostic);
    return 1;
}

int parsePositiveInt (const char* text, int fallback)
{
    if (text == nullptr)
        return fallback;
    const int value = std::atoi (text);
    return value > 0 ? value : fallback;
}
}

int main (int argc, char* argv[])
{
    @autoreleasepool
    {
        if (argc != 4)
            return fail ("usage_error", false, "usage: metal_explicit_msl_probe <msl_source_path> <width> <height>", 0, 0);

        const int width = parsePositiveInt (argv[2], 8);
        const int height = parsePositiveInt (argv[3], 8);

        NSError* readError = nil;
        NSString* sourcePath = [NSString stringWithUTF8String:argv[1]];
        NSString* source = [NSString stringWithContentsOfFile:sourcePath
                                                     encoding:NSUTF8StringEncoding
                                                        error:&readError];
        if (source == nil)
            return fail ("source_read_failed", false, errorMessage (readError, "MSL source read failed"), width, height);

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil)
            return fail ("blocked_metal_device_unavailable", false, "Metal device unavailable", width, height);

        id<MTLCommandQueue> commandQueue = [device newCommandQueue];
        if (commandQueue == nil)
            return fail ("blocked_metal_device_unavailable", false, "Metal command queue unavailable", width, height);

        NSError* compileError = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&compileError];
        if (library == nil)
        {
            const std::string diagnostic = errorMessage (compileError, "Metal library compile failed");
            return fail ("compile_failed", true, diagnostic, width, height, diagnostic);
        }

        id<MTLFunction> vertexFunction = [library newFunctionWithName:@"my_world_vertex"];
        if (vertexFunction == nil)
            return fail ("compile_failed", true, "MSL source must define vertex my_world_vertex", width, height);

        id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"my_world_fragment"];
        if (fragmentFunction == nil)
            return fail ("compile_failed", true, "MSL source must define fragment my_world_fragment", width, height);

        MTLRenderPipelineDescriptor* pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
        pipelineDescriptor.vertexFunction = vertexFunction;
        pipelineDescriptor.fragmentFunction = fragmentFunction;
        pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;

        NSError* pipelineError = nil;
        id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:pipelineDescriptor
                                                                                     error:&pipelineError];
        if (pipeline == nil)
            return fail ("pipeline_failed", true, errorMessage (pipelineError, "Metal render pipeline compile failed"), width, height);

        MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                                    width:static_cast<NSUInteger> (width)
                                                                                                   height:static_cast<NSUInteger> (height)
                                                                                                mipmapped:NO];
        textureDescriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        textureDescriptor.storageMode = MTLStorageModeShared;
        id<MTLTexture> renderTarget = [device newTextureWithDescriptor:textureDescriptor];
        if (renderTarget == nil)
            return fail ("render_failed", true, "Metal render target allocation failed", width, height);

        MTLRenderPassDescriptor* passDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        passDescriptor.colorAttachments[0].texture = renderTarget;
        passDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
        passDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
        passDescriptor.colorAttachments[0].clearColor = MTLClearColorMake (0.0, 0.0, 0.0, 1.0);

        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
        if (commandBuffer == nil)
            return fail ("render_failed", true, "Metal command buffer unavailable", width, height);

        id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDescriptor];
        if (encoder == nil)
            return fail ("render_failed", true, "Metal render command encoder unavailable", width, height);

        [encoder setRenderPipelineState:pipeline];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];

        if ([commandBuffer status] == MTLCommandBufferStatusError)
            return fail ("render_failed", true, errorMessage ([commandBuffer error], "Metal command buffer failed"), width, height);

        std::vector<unsigned char> rgba (static_cast<std::size_t> (width) * static_cast<std::size_t> (height) * bytesPerPixel);
        [renderTarget getBytes:rgba.data()
                   bytesPerRow:static_cast<NSUInteger> (width * bytesPerPixel)
                    fromRegion:MTLRegionMake2D (0, 0, static_cast<NSUInteger> (width), static_cast<NSUInteger> (height))
                   mipmapLevel:0];

        int nonBlackPixels = 0;
        bool varied = false;
        std::set<std::uint32_t> sampledColors;
        const std::uint32_t firstPixel = rgba.size() >= 4
            ? (static_cast<std::uint32_t> (rgba[0]) << 24)
                | (static_cast<std::uint32_t> (rgba[1]) << 16)
                | (static_cast<std::uint32_t> (rgba[2]) << 8)
                | static_cast<std::uint32_t> (rgba[3])
            : 0;

        for (std::size_t index = 0; index + 3 < rgba.size(); index += 4)
        {
            const bool pixelNonBlack = rgba[index] != 0 || rgba[index + 1] != 0 || rgba[index + 2] != 0;
            if (pixelNonBlack)
                ++nonBlackPixels;

            const std::uint32_t color =
                (static_cast<std::uint32_t> (rgba[index]) << 24)
                | (static_cast<std::uint32_t> (rgba[index + 1]) << 16)
                | (static_cast<std::uint32_t> (rgba[index + 2]) << 8)
                | static_cast<std::uint32_t> (rgba[index + 3]);
            sampledColors.insert (color);
            if (color != firstPixel)
                varied = true;
        }

        const bool nonBlack = nonBlackPixels > 0;
        printResult ("rendered",
                     true,
                     true,
                     true,
                     width,
                     height,
                     rgba.size(),
                     nonBlack,
                     varied,
                     nonBlackPixels,
                     static_cast<int> (sampledColors.size()),
                     "compiled explicit MSL, rendered offscreen RGBA8Unorm, and read back pixels");
        return 0;
    }
}
