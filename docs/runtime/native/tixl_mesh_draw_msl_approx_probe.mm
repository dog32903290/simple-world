#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr int bytesPerPixel = 4;

struct PbrVertex80
{
    float position[3];
    float normal[3];
    float tangent[3];
    float bitangent[3];
    float texCoord[2];
    float texCoord2[2];
    float selected;
    float colorRGB[3];
};

struct FaceIndices12
{
    int indices[3];
};

static_assert (sizeof (PbrVertex80) == 80, "PbrVertex80 must stay 80 bytes");
static_assert (sizeof (FaceIndices12) == 12, "FaceIndices12 must stay 12 bytes");

struct MeshPayload
{
    std::vector<PbrVertex80> vertices;
    std::vector<FaceIndices12> faces;
};

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
    int vertexCount,
    int faceCount,
    int drawVertexCount,
    std::uint64_t frameDigest,
    int opaquePixels,
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
        << "\"vertexCount\":" << vertexCount << ","
        << "\"faceCount\":" << faceCount << ","
        << "\"drawVertexCount\":" << drawVertexCount << ","
        << "\"pbrVertexStrideBytes\":80,"
        << "\"faceIndicesStrideBytes\":12,"
        << "\"frameDigest\":\"" << std::hex << frameDigest << std::dec << "\","
        << "\"opaquePixels\":" << opaquePixels << ","
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
    int vertexCount = 0,
    int faceCount = 0,
    int drawVertexCount = 0,
    const std::string& compilerDiagnostic = "")
{
    printResult (status,
                 false,
                 actualCompilerRan,
                 false,
                 width,
                 height,
                 0,
                 false,
                 false,
                 0,
                 0,
                 vertexCount,
                 faceCount,
                 drawVertexCount,
                 0,
                 0,
                 message,
                 compilerDiagnostic);
    return 1;
}

int parsePositiveInt (const char* text, int fallback)
{
    if (text == nullptr)
        return fallback;
    const int value = std::atoi (text);
    return value > 0 ? value : fallback;
}

float numberAt (NSArray* array, NSUInteger index)
{
    if (array == nil || index >= [array count])
        return 0.0f;
    id value = [array objectAtIndex:index];
    return [value respondsToSelector:@selector(floatValue)] ? [value floatValue] : 0.0f;
}

int intAt (NSArray* array, NSUInteger index)
{
    if (array == nil || index >= [array count])
        return 0;
    id value = [array objectAtIndex:index];
    return [value respondsToSelector:@selector(intValue)] ? [value intValue] : 0;
}

void copyFloatArray (float* destination, NSDictionary* source, NSString* key, NSUInteger length)
{
    NSArray* values = [source objectForKey:key];
    for (NSUInteger index = 0; index < length; ++index)
        destination[index] = numberAt (values, index);
}

bool readMeshPayload (NSString* meshPath, MeshPayload& mesh, std::string& message)
{
    NSData* data = [NSData dataWithContentsOfFile:meshPath];
    if (data == nil)
    {
        message = "mesh payload read failed";
        return false;
    }

    NSError* jsonError = nil;
    id payload = [NSJSONSerialization JSONObjectWithData:data options:0 error:&jsonError];
    if (![payload isKindOfClass:[NSDictionary class]])
    {
        message = errorMessage (jsonError, "mesh payload must be a JSON object");
        return false;
    }

    NSDictionary* root = static_cast<NSDictionary*> (payload);
    NSArray* vertices = [root objectForKey:@"vertices"];
    NSArray* faces = [root objectForKey:@"faces"];
    if (![vertices isKindOfClass:[NSArray class]] || ![faces isKindOfClass:[NSArray class]] || [vertices count] < 3 || [faces count] < 1)
    {
        message = "mesh payload requires at least 3 vertices and 1 face";
        return false;
    }

    mesh.vertices.reserve ([vertices count]);
    for (id item in vertices)
    {
        if (![item isKindOfClass:[NSDictionary class]])
        {
            message = "mesh vertex must be an object";
            return false;
        }
        NSDictionary* vertex = static_cast<NSDictionary*> (item);
        PbrVertex80 packed {};
        copyFloatArray (packed.position, vertex, @"position", 3);
        copyFloatArray (packed.normal, vertex, @"normal", 3);
        copyFloatArray (packed.tangent, vertex, @"tangent", 3);
        copyFloatArray (packed.bitangent, vertex, @"bitangent", 3);
        copyFloatArray (packed.texCoord, vertex, @"texCoord", 2);
        copyFloatArray (packed.texCoord2, vertex, @"texCoord2", 2);
        id selected = [vertex objectForKey:@"selected"];
        packed.selected = [selected respondsToSelector:@selector(floatValue)] ? [selected floatValue] : 0.0f;
        copyFloatArray (packed.colorRGB, vertex, @"colorRGB", 3);
        mesh.vertices.push_back (packed);
    }

    mesh.faces.reserve ([faces count]);
    for (id item in faces)
    {
        if (![item isKindOfClass:[NSArray class]] || [item count] != 3)
        {
            message = "mesh face must be an array of 3 indices";
            return false;
        }
        NSArray* face = static_cast<NSArray*> (item);
        FaceIndices12 packed {};
        for (NSUInteger index = 0; index < 3; ++index)
        {
            packed.indices[index] = intAt (face, index);
            if (packed.indices[index] < 0 || packed.indices[index] >= static_cast<int> (mesh.vertices.size()))
            {
                message = "mesh face index out of range";
                return false;
            }
        }
        mesh.faces.push_back (packed);
    }
    return true;
}
}

int main (int argc, char* argv[])
{
    @autoreleasepool
    {
        if (argc != 5)
            return fail ("usage_error", false, "usage: tixl_mesh_draw_msl_approx_probe <msl_source_path> <mesh_payload_path> <width> <height>", 0, 0);

        const int width = parsePositiveInt (argv[3], 16);
        const int height = parsePositiveInt (argv[4], 16);

        MeshPayload mesh;
        std::string meshMessage;
        NSString* meshPath = [NSString stringWithUTF8String:argv[2]];
        if (! readMeshPayload (meshPath, mesh, meshMessage))
            return fail ("mesh_payload_invalid", false, meshMessage, width, height);

        const int vertexCount = static_cast<int> (mesh.vertices.size());
        const int faceCount = static_cast<int> (mesh.faces.size());
        const int drawVertexCount = faceCount * 3;

        NSError* readError = nil;
        NSString* sourcePath = [NSString stringWithUTF8String:argv[1]];
        NSString* source = [NSString stringWithContentsOfFile:sourcePath
                                                     encoding:NSUTF8StringEncoding
                                                        error:&readError];
        if (source == nil)
            return fail ("source_read_failed", false, errorMessage (readError, "MSL source read failed"), width, height, vertexCount, faceCount, drawVertexCount);

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil)
            return fail ("blocked_metal_device_unavailable", false, "Metal device unavailable", width, height, vertexCount, faceCount, drawVertexCount);

        id<MTLCommandQueue> commandQueue = [device newCommandQueue];
        if (commandQueue == nil)
            return fail ("blocked_metal_device_unavailable", false, "Metal command queue unavailable", width, height, vertexCount, faceCount, drawVertexCount);

        NSError* compileError = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&compileError];
        if (library == nil)
        {
            const std::string diagnostic = errorMessage (compileError, "Metal library compile failed");
            return fail ("compile_failed", true, diagnostic, width, height, vertexCount, faceCount, drawVertexCount, diagnostic);
        }

        id<MTLFunction> vertexFunction = [library newFunctionWithName:@"my_world_mesh_vertex"];
        if (vertexFunction == nil)
            return fail ("compile_failed", true, "MSL source must define vertex my_world_mesh_vertex", width, height, vertexCount, faceCount, drawVertexCount);

        id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"my_world_mesh_fragment"];
        if (fragmentFunction == nil)
            return fail ("compile_failed", true, "MSL source must define fragment my_world_mesh_fragment", width, height, vertexCount, faceCount, drawVertexCount);

        MTLRenderPipelineDescriptor* pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
        pipelineDescriptor.vertexFunction = vertexFunction;
        pipelineDescriptor.fragmentFunction = fragmentFunction;
        pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;

        NSError* pipelineError = nil;
        id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:pipelineDescriptor
                                                                                     error:&pipelineError];
        if (pipeline == nil)
            return fail ("pipeline_failed", true, errorMessage (pipelineError, "Metal render pipeline compile failed"), width, height, vertexCount, faceCount, drawVertexCount);

        id<MTLBuffer> vertexBuffer = [device newBufferWithBytes:mesh.vertices.data()
                                                         length:mesh.vertices.size() * sizeof (PbrVertex80)
                                                        options:MTLResourceStorageModeShared];
        id<MTLBuffer> faceBuffer = [device newBufferWithBytes:mesh.faces.data()
                                                       length:mesh.faces.size() * sizeof (FaceIndices12)
                                                      options:MTLResourceStorageModeShared];
        if (vertexBuffer == nil || faceBuffer == nil)
            return fail ("render_failed", true, "Metal buffer allocation failed", width, height, vertexCount, faceCount, drawVertexCount);

        MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                                    width:static_cast<NSUInteger> (width)
                                                                                                   height:static_cast<NSUInteger> (height)
                                                                                                mipmapped:NO];
        textureDescriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        textureDescriptor.storageMode = MTLStorageModeShared;
        id<MTLTexture> renderTarget = [device newTextureWithDescriptor:textureDescriptor];
        if (renderTarget == nil)
            return fail ("render_failed", true, "Metal render target allocation failed", width, height, vertexCount, faceCount, drawVertexCount);

        MTLRenderPassDescriptor* passDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        passDescriptor.colorAttachments[0].texture = renderTarget;
        passDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
        passDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
        passDescriptor.colorAttachments[0].clearColor = MTLClearColorMake (0.0, 0.0, 0.0, 1.0);

        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
        if (commandBuffer == nil)
            return fail ("render_failed", true, "Metal command buffer unavailable", width, height, vertexCount, faceCount, drawVertexCount);

        id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDescriptor];
        if (encoder == nil)
            return fail ("render_failed", true, "Metal render command encoder unavailable", width, height, vertexCount, faceCount, drawVertexCount);

        [encoder setRenderPipelineState:pipeline];
        [encoder setVertexBuffer:vertexBuffer offset:0 atIndex:0];
        [encoder setVertexBuffer:faceBuffer offset:0 atIndex:1];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:static_cast<NSUInteger> (drawVertexCount)];
        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];

        if ([commandBuffer status] == MTLCommandBufferStatusError)
            return fail ("render_failed", true, errorMessage ([commandBuffer error], "Metal command buffer failed"), width, height, vertexCount, faceCount, drawVertexCount);

        std::vector<unsigned char> rgba (static_cast<std::size_t> (width) * static_cast<std::size_t> (height) * bytesPerPixel);
        [renderTarget getBytes:rgba.data()
                   bytesPerRow:static_cast<NSUInteger> (width * bytesPerPixel)
                    fromRegion:MTLRegionMake2D (0, 0, static_cast<NSUInteger> (width), static_cast<NSUInteger> (height))
                   mipmapLevel:0];

        int nonBlackPixels = 0;
        int opaquePixels = 0;
        bool varied = false;
        std::uint64_t frameDigest = 1469598103934665603ull;
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
            if (rgba[index + 3] == 255)
                ++opaquePixels;

            const std::uint32_t color =
                (static_cast<std::uint32_t> (rgba[index]) << 24)
                | (static_cast<std::uint32_t> (rgba[index + 1]) << 16)
                | (static_cast<std::uint32_t> (rgba[index + 2]) << 8)
                | static_cast<std::uint32_t> (rgba[index + 3]);
            sampledColors.insert (color);
            if (color != firstPixel)
                varied = true;
            for (int component = 0; component < bytesPerPixel; ++component)
            {
                frameDigest ^= static_cast<std::uint64_t> (rgba[index + component]);
                frameDigest *= 1099511628211ull;
            }
        }

        const bool nonBlack = nonBlackPixels > 0;
        printResult ("rendered_tixl_mesh_draw_msl_approximation",
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
                     vertexCount,
                     faceCount,
                     drawVertexCount,
                     frameDigest,
                     opaquePixels,
                     "compiled explicit MSL approximation, drew packed PbrVertex/FaceIndices mesh, and read back pixels");
        return 0;
    }
}
