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

std::string nsStringToStd(NSString* value)
{
    if (value == nil)
        return "";
    const char* text = [value UTF8String];
    return text != nullptr ? std::string(text) : "";
}

std::string errorMessage(NSError* error, const std::string& fallback)
{
    if (error == nil)
        return fallback;
    std::string message = nsStringToStd([error localizedDescription]);
    return message.empty() ? fallback : message;
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

void printResult(
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
    const std::string& message)
{
    std::cout
        << "{"
        << "\"status\":\"" << escapeJson(status) << "\","
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
        << "\"message\":\"" << escapeJson(message) << "\""
        << "}\n";
}

int fail(const std::string& status, bool actualCompilerRan, const std::string& message, int width, int height)
{
    printResult(status, false, actualCompilerRan, false, width, height, 0, false, false, 0, 0, message);
    return 1;
}

int parsePositiveInt(const char* text, int fallback)
{
    if (text == nullptr)
        return fallback;
    const int value = std::atoi(text);
    return value > 0 ? value : fallback;
}

id<MTLRenderPipelineState> makePipeline(id<MTLDevice> device, id<MTLLibrary> library, NSString* fragmentName, std::string& error)
{
    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"my_world_vertex"];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:fragmentName];
    if (vertexFunction == nil || fragmentFunction == nil)
    {
        error = "MSL source missing vertex or fragment function";
        return nil;
    }

    MTLRenderPipelineDescriptor* pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.vertexFunction = vertexFunction;
    pipelineDescriptor.fragmentFunction = fragmentFunction;
    pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;

    NSError* pipelineError = nil;
    id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&pipelineError];
    if (pipeline == nil)
        error = errorMessage(pipelineError, "Metal render pipeline compile failed");
    return pipeline;
}

id<MTLTexture> makeRenderTexture(id<MTLDevice> device, int width, int height)
{
    MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                                 width:width
                                                                                                height:height
                                                                                             mipmapped:NO];
    textureDescriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    textureDescriptor.storageMode = MTLStorageModeShared;
    return [device newTextureWithDescriptor:textureDescriptor];
}

bool encodePass(
    id<MTLCommandBuffer> commandBuffer,
    id<MTLRenderPipelineState> pipeline,
    id<MTLTexture> target,
    id<MTLTexture> inputA,
    id<MTLTexture> inputB,
    id<MTLSamplerState> samplerState)
{
    MTLRenderPassDescriptor* passDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    passDescriptor.colorAttachments[0].texture = target;
    passDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    passDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    passDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);

    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDescriptor];
    if (encoder == nil)
        return false;
    [encoder setRenderPipelineState:pipeline];
    if (inputA != nil)
        [encoder setFragmentTexture:inputA atIndex:0];
    if (inputB != nil)
        [encoder setFragmentTexture:inputB atIndex:1];
    if (samplerState != nil)
        [encoder setFragmentSamplerState:samplerState atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [encoder endEncoding];
    return true;
}
}

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        if (argc != 4)
            return fail("usage_error", false, "usage: native_gpu_patch_runtime_slice_probe <msl_source_path> <width> <height>", 0, 0);

        const int width = parsePositiveInt(argv[2], 64);
        const int height = parsePositiveInt(argv[3], 64);

        NSError* readError = nil;
        NSString* sourcePath = [NSString stringWithUTF8String:argv[1]];
        NSString* source = [NSString stringWithContentsOfFile:sourcePath encoding:NSUTF8StringEncoding error:&readError];
        if (source == nil)
            return fail("source_read_failed", false, errorMessage(readError, "MSL source read failed"), width, height);

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil)
            return fail("blocked_metal_device_unavailable", false, "Metal device unavailable", width, height);

        id<MTLCommandQueue> commandQueue = [device newCommandQueue];
        if (commandQueue == nil)
            return fail("blocked_metal_device_unavailable", false, "Metal command queue unavailable", width, height);

        NSError* compileError = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&compileError];
        if (library == nil)
            return fail("compile_failed", true, errorMessage(compileError, "Metal library compile failed"), width, height);

        std::string pipelineMessage;
        id<MTLRenderPipelineState> constantPipeline = makePipeline(device, library, @"constant_bg_fragment", pipelineMessage);
        id<MTLRenderPipelineState> blobPipeline = makePipeline(device, library, @"blob_fg_fragment", pipelineMessage);
        id<MTLRenderPipelineState> blendPipeline = makePipeline(device, library, @"blend_1_fragment", pipelineMessage);
        if (constantPipeline == nil || blobPipeline == nil || blendPipeline == nil)
            return fail("pipeline_failed", true, pipelineMessage, width, height);

        id<MTLTexture> constantTexture = makeRenderTexture(device, width, height);
        id<MTLTexture> blobTexture = makeRenderTexture(device, width, height);
        id<MTLTexture> outputTexture = makeRenderTexture(device, width, height);
        if (constantTexture == nil || blobTexture == nil || outputTexture == nil)
            return fail("render_failed", true, "Metal texture allocation failed", width, height);

        MTLSamplerDescriptor* samplerDescriptor = [[MTLSamplerDescriptor alloc] init];
        samplerDescriptor.minFilter = MTLSamplerMinMagFilterLinear;
        samplerDescriptor.magFilter = MTLSamplerMinMagFilterLinear;
        id<MTLSamplerState> samplerState = [device newSamplerStateWithDescriptor:samplerDescriptor];

        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
        if (commandBuffer == nil)
            return fail("render_failed", true, "Metal command buffer creation failed", width, height);
        if (!encodePass(commandBuffer, constantPipeline, constantTexture, nil, nil, nil))
            return fail("render_failed", true, "ConstantImage pass encode failed", width, height);
        if (!encodePass(commandBuffer, blobPipeline, blobTexture, nil, nil, nil))
            return fail("render_failed", true, "Blob pass encode failed", width, height);
        if (!encodePass(commandBuffer, blendPipeline, outputTexture, constantTexture, blobTexture, samplerState))
            return fail("render_failed", true, "Blend pass encode failed", width, height);
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];
        if ([commandBuffer status] != MTLCommandBufferStatusCompleted)
            return fail("render_failed", true, "Metal command buffer did not complete", width, height);

        std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * bytesPerPixel));
        MTLRegion region = MTLRegionMake2D(0, 0, width, height);
        [outputTexture getBytes:pixels.data() bytesPerRow:width * bytesPerPixel fromRegion:region mipmapLevel:0];

        int nonBlackPixels = 0;
        std::set<unsigned int> samples;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::size_t i = static_cast<std::size_t>((y * width + x) * 4);
                const bool nonBlack = pixels[i] > 0 || pixels[i + 1] > 0 || pixels[i + 2] > 0;
                if (nonBlack)
                    ++nonBlackPixels;
                if ((x % 8 == 0) && (y % 8 == 0))
                {
                    unsigned int packed = (static_cast<unsigned int>(pixels[i]) << 24)
                        | (static_cast<unsigned int>(pixels[i + 1]) << 16)
                        | (static_cast<unsigned int>(pixels[i + 2]) << 8)
                        | static_cast<unsigned int>(pixels[i + 3]);
                    samples.insert(packed);
                }
            }
        }
        const bool nonBlack = nonBlackPixels > 0;
        const bool varied = samples.size() > 1;
        printResult("rendered", true, true, true, width, height, pixels.size(), nonBlack, varied, nonBlackPixels, static_cast<int>(samples.size()), "rendered native GPU patch slice");
        return 0;
    }
}
