#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <fstream>
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

std::string readFile(const char* path)
{
    std::ifstream in(path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void emit(bool ok, const std::string& status, bool deviceCreated, bool libraryCreated, const std::string& message)
{
    std::cout
        << "{"
        << "\"ok\":" << (ok ? "true" : "false") << ","
        << "\"status\":\"" << escapeJson(status) << "\","
        << "\"actualMetalDeviceCreated\":" << (deviceCreated ? "true" : "false") << ","
        << "\"actualLibraryCreated\":" << (libraryCreated ? "true" : "false") << ","
        << "\"message\":\"" << escapeJson(message) << "\""
        << "}\n";
    std::cout.flush();
}
}

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        if (argc != 2)
        {
            emit(false, "usage_error", false, false, "usage: native_shader_ir_expression_core_compile_probe <source.metal>");
            return 2;
        }

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil)
        {
            emit(false, "blocked_metal_device_unavailable", false, false, "Metal device unavailable");
            return 1;
        }

        const std::string source = readFile(argv[1]);
        if (source.empty())
        {
            emit(false, "source_read_failed", true, false, "source is empty or unreadable");
            return 1;
        }

        NSError* error = nil;
        NSString* metalSource = [NSString stringWithUTF8String:source.c_str()];
        id<MTLLibrary> library = [device newLibraryWithSource:metalSource options:nil error:&error];
        if (library == nil)
        {
            NSString* reason = error != nil ? [error localizedDescription] : @"unknown Metal compile failure";
            emit(false, "compile_failed", true, false, [reason UTF8String]);
            return 1;
        }

        emit(true, "compiled", true, true, "Metal library compiled from ShaderExpressionIR source");
        return 0;
    }
}
