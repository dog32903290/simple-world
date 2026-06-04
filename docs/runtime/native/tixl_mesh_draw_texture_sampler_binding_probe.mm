#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr std::uint32_t expectedBaseColor = 0x11223344u;
constexpr std::uint32_t expectedBrdfLookup = 0xa1b2c3d4u;
constexpr std::uint32_t expectedWrappedSampler = 0x778899aau;
constexpr std::uint32_t expectedClampedSampler = 0x55667788u;

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
    const std::vector<std::uint32_t>& actualWords,
    const std::string& message,
    const std::string& compilerDiagnostic = "")
{
    const std::uint32_t actual0 = actualWords.size() > 0 ? actualWords[0] : 0u;
    const std::uint32_t actual1 = actualWords.size() > 1 ? actualWords[1] : 0u;
    const std::uint32_t actual2 = actualWords.size() > 2 ? actualWords[2] : 0u;
    const std::uint32_t actual3 = actualWords.size() > 3 ? actualWords[3] : 0u;
    std::cout
        << "{"
        << "\"status\":\"" << escapeJson (status) << "\","
        << "\"ok\":" << (ok ? "true" : "false") << ","
        << "\"actualCompilerRan\":" << (actualCompilerRan ? "true" : "false") << ","
        << "\"actualMetalRan\":" << (actualMetalRan ? "true" : "false") << ","
        << "\"textureBindings\":{\"BaseColorMap\":2,\"BRDFLookup\":7},"
        << "\"samplerBindings\":{\"WrappedSampler\":0,\"ClampedSampler\":1},"
        << "\"expectedWords\":[" << expectedBaseColor << "," << expectedBrdfLookup << "," << expectedWrappedSampler << "," << expectedClampedSampler << "],"
        << "\"actualWords\":[" << actual0 << "," << actual1 << "," << actual2 << "," << actual3 << "],"
        << "\"message\":\"" << escapeJson (message) << "\"";
    if (! compilerDiagnostic.empty())
        std::cout << ",\"compilerDiagnostic\":\"" << escapeJson (compilerDiagnostic) << "\"";
    std::cout << "}\n";
}

int fail (
    const std::string& status,
    bool actualCompilerRan,
    const std::string& message,
    const std::string& compilerDiagnostic = "")
{
    printResult (status, false, actualCompilerRan, false, {}, message, compilerDiagnostic);
    return 1;
}

void fillTexture (id<MTLTexture> texture, const std::vector<unsigned char>& rgba)
{
    [texture replaceRegion:MTLRegionMake2D (0, 0, 2, 2)
               mipmapLevel:0
                 withBytes:rgba.data()
               bytesPerRow:8];
}
}

int main (int argc, char* argv[])
{
    @autoreleasepool
    {
        if (argc != 2)
            return fail ("usage_error", false, "usage: tixl_mesh_draw_texture_sampler_binding_probe <msl_source_path>");

        NSError* readError = nil;
        NSString* sourcePath = [NSString stringWithUTF8String:argv[1]];
        NSString* source = [NSString stringWithContentsOfFile:sourcePath
                                                     encoding:NSUTF8StringEncoding
                                                        error:&readError];
        if (source == nil)
            return fail ("source_read_failed", false, errorMessage (readError, "MSL source read failed"));

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil)
            return fail ("blocked_metal_device_unavailable", false, "Metal device unavailable");

        id<MTLCommandQueue> commandQueue = [device newCommandQueue];
        if (commandQueue == nil)
            return fail ("blocked_metal_device_unavailable", false, "Metal command queue unavailable");

        NSError* compileError = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&compileError];
        if (library == nil)
        {
            const std::string diagnostic = errorMessage (compileError, "Metal library compile failed");
            return fail ("compile_failed", true, diagnostic, diagnostic);
        }

        id<MTLFunction> function = [library newFunctionWithName:@"texture_sampler_probe"];
        if (function == nil)
            return fail ("compile_failed", true, "MSL source must define kernel texture_sampler_probe");

        NSError* pipelineError = nil;
        id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&pipelineError];
        if (pipeline == nil)
            return fail ("pipeline_failed", true, errorMessage (pipelineError, "Metal compute pipeline compile failed"));

        MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                                    width:2
                                                                                                   height:2
                                                                                                mipmapped:NO];
        textureDescriptor.usage = MTLTextureUsageShaderRead;
        textureDescriptor.storageMode = MTLStorageModeShared;

        id<MTLTexture> baseColor = [device newTextureWithDescriptor:textureDescriptor];
        id<MTLTexture> brdfLookup = [device newTextureWithDescriptor:textureDescriptor];
        if (baseColor == nil || brdfLookup == nil)
            return fail ("probe_setup_failed", true, "Metal texture allocation failed");

        fillTexture (baseColor, {
            0x11, 0x22, 0x33, 0x44, 0x77, 0x88, 0x99, 0xaa,
            0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff,
        });
        fillTexture (brdfLookup, {
            0x55, 0x66, 0x77, 0x88, 0xa1, 0xb2, 0xc3, 0xd4,
            0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff,
        });

        MTLSamplerDescriptor* wrappedDescriptor = [[MTLSamplerDescriptor alloc] init];
        wrappedDescriptor.sAddressMode = MTLSamplerAddressModeRepeat;
        wrappedDescriptor.tAddressMode = MTLSamplerAddressModeRepeat;
        wrappedDescriptor.minFilter = MTLSamplerMinMagFilterNearest;
        wrappedDescriptor.magFilter = MTLSamplerMinMagFilterNearest;
        id<MTLSamplerState> wrappedSampler = [device newSamplerStateWithDescriptor:wrappedDescriptor];

        MTLSamplerDescriptor* clampedDescriptor = [[MTLSamplerDescriptor alloc] init];
        clampedDescriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
        clampedDescriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
        clampedDescriptor.minFilter = MTLSamplerMinMagFilterNearest;
        clampedDescriptor.magFilter = MTLSamplerMinMagFilterNearest;
        id<MTLSamplerState> clampedSampler = [device newSamplerStateWithDescriptor:clampedDescriptor];
        if (wrappedSampler == nil || clampedSampler == nil)
            return fail ("probe_setup_failed", true, "Metal sampler allocation failed");

        id<MTLBuffer> output = [device newBufferWithLength:sizeof (std::uint32_t) * 4 options:MTLResourceStorageModeShared];
        if (output == nil)
            return fail ("probe_setup_failed", true, "Metal output buffer allocation failed");
        std::memset ([output contents], 0, sizeof (std::uint32_t) * 4);

        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
        if (commandBuffer == nil)
            return fail ("probe_run_failed", true, "Metal command buffer unavailable");
        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
        if (encoder == nil)
            return fail ("probe_run_failed", true, "Metal compute encoder unavailable");

        [encoder setComputePipelineState:pipeline];
        [encoder setTexture:baseColor atIndex:2];
        [encoder setTexture:brdfLookup atIndex:7];
        [encoder setSamplerState:wrappedSampler atIndex:0];
        [encoder setSamplerState:clampedSampler atIndex:1];
        [encoder setBuffer:output offset:0 atIndex:0];
        [encoder dispatchThreadgroups:MTLSizeMake (1, 1, 1) threadsPerThreadgroup:MTLSizeMake (1, 1, 1)];
        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];

        if ([commandBuffer status] == MTLCommandBufferStatusError)
            return fail ("probe_run_failed", true, errorMessage ([commandBuffer error], "Metal command buffer failed"));

        const std::uint32_t* words = static_cast<const std::uint32_t*> ([output contents]);
        std::vector<std::uint32_t> actualWords { words[0], words[1], words[2], words[3] };
        const bool ok = actualWords[0] == expectedBaseColor
            && actualWords[1] == expectedBrdfLookup
            && actualWords[2] == expectedWrappedSampler
            && actualWords[3] == expectedClampedSampler;
        if (! ok)
        {
            printResult ("sentinel_mismatch",
                         false,
                         true,
                         true,
                         actualWords,
                         "Metal texture/sampler readback did not match sentinel words");
            return 1;
        }

        printResult ("proven",
                     true,
                     true,
                     true,
                     actualWords,
                     "Metal compute probe read t2/t7 textures and s0/s1 samplers at selected indices");
        return 0;
    }
}
