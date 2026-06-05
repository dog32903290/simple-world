#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
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

void emitResult(bool ok, const std::string& status, bool deviceCreated, bool heapCreated, bool texturesCreated, unsigned long long heapSize, unsigned long long minimumAlignedSize, int textureCount, const std::string& message)
{
    std::cout
        << "{"
        << "\"ok\":" << (ok ? "true" : "false") << ","
        << "\"status\":\"" << escapeJson(status) << "\","
        << "\"actualMetalDeviceCreated\":" << (deviceCreated ? "true" : "false") << ","
        << "\"actualHeapCreated\":" << (heapCreated ? "true" : "false") << ","
        << "\"actualHeapBackedTexturesCreated\":" << (texturesCreated ? "true" : "false") << ","
        << "\"heapSize\":" << heapSize << ","
        << "\"minimumAlignedSize\":" << minimumAlignedSize << ","
        << "\"textureCount\":" << textureCount << ","
        << "\"message\":\"" << escapeJson(message) << "\""
        << "}\n";
    std::cout.flush();
}
}

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        const int width = argc > 1 ? positiveInt(argv[1], 64) : 64;
        const int height = argc > 2 ? positiveInt(argv[2], 64) : 64;
        const int textureCount = argc > 3 ? positiveInt(argv[3], 3) : 3;

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil)
        {
            emitResult(false, "blocked_metal_device_unavailable", false, false, false, 0, 0, textureCount, "Metal device unavailable");
            return 1;
        }

        MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                                      width:width
                                                                                                     height:height
                                                                                                  mipmapped:NO];
        textureDescriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        textureDescriptor.storageMode = MTLStorageModePrivate;

        const MTLSizeAndAlign sizeAndAlign = [device heapTextureSizeAndAlignWithDescriptor:textureDescriptor];
        const unsigned long long alignedSize = ((sizeAndAlign.size + sizeAndAlign.align - 1) / sizeAndAlign.align) * sizeAndAlign.align;
        const unsigned long long heapSize = alignedSize * static_cast<unsigned long long>(textureCount);

        MTLHeapDescriptor* heapDescriptor = [[MTLHeapDescriptor alloc] init];
        heapDescriptor.storageMode = MTLStorageModePrivate;
        heapDescriptor.size = static_cast<NSUInteger>(heapSize);

        id<MTLHeap> heap = [device newHeapWithDescriptor:heapDescriptor];
        if (heap == nil)
        {
            emitResult(false, "heap_create_failed", true, false, false, heapSize, alignedSize, textureCount, "MTLHeap creation failed");
            return 1;
        }

        bool texturesOk = true;
        for (int index = 0; index < textureCount; ++index)
        {
            id<MTLTexture> texture = [heap newTextureWithDescriptor:textureDescriptor];
            if (texture == nil)
            {
                texturesOk = false;
                break;
            }
            if ([texture heap] != heap)
            {
                texturesOk = false;
                break;
            }
        }

        emitResult(texturesOk, texturesOk ? "metal_heap_ready" : "heap_texture_create_failed", true, true, texturesOk, heapSize, alignedSize, textureCount, texturesOk ? "heap-backed textures created" : "heap-backed texture creation failed");
        return texturesOk ? 0 : 1;
    }
}
