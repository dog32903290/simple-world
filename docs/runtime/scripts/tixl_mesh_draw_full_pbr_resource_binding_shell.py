#!/usr/bin/env python3
"""
Prove the full TiXL Mesh Draw/PBR resource binding ledger.

This lane aggregates prior source-backed proofs and runs one deterministic
Metal compute probe that binds b0-b5, t0-t7, and s0-s1. It proves resource
binding only; it does not claim backend replacement, explicit adapter parity,
HLSL-to-MSL translation, TiXL runtime parity, native GPU parity, or PBR visual
correctness.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


RESULT_NAME = "tixl_mesh_draw_full_pbr_resource_binding_result.json"
TRACE_NAME = "tixl_mesh_draw_full_pbr_resource_binding_trace.json"
ERRORS_NAME = "tixl_mesh_draw_full_pbr_resource_binding_errors.json"
MSL_NAME = "generated_full_pbr_resource_binding_probe.metal"

DEFAULT_SOURCE_AUDIT = "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json"
DEFAULT_MESH_BUFFER = "docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_result.json"
DEFAULT_CONSTANT_PACKING = "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_native_packing/tixl_mesh_draw_constant_buffer_native_packing_result.json"
DEFAULT_POINTLIGHTS_B5 = "docs/runtime/artifacts/tixl_mesh_draw_pointlights_and_b5_packing/tixl_mesh_draw_pointlights_and_b5_packing_result.json"
DEFAULT_B5_NATIVE = "docs/runtime/artifacts/tixl_mesh_draw_b5_native_packing/tixl_mesh_draw_b5_native_packing_result.json"
DEFAULT_TEXTURE_SAMPLER = "docs/runtime/artifacts/tixl_mesh_draw_texture_sampler_binding/tixl_mesh_draw_texture_sampler_binding_result.json"
DEFAULT_T8 = "docs/runtime/artifacts/tixl_mesh_draw_shadergraph_resources_expansion/tixl_mesh_draw_shadergraph_resources_expansion_result.json"
DEFAULT_TEXTURECUBE_PBR = "docs/runtime/artifacts/tixl_mesh_draw_texturecube_pbr_reference/tixl_mesh_draw_texturecube_pbr_reference_result.json"

BOUND_REGISTERS = [
    "b0", "b1", "b2", "b3", "b4", "b5",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1",
]

EXPECTED_CLAIMS = {
    "sourceAuditArtifactConsumed": True,
    "meshBufferBindingArtifactConsumed": True,
    "constantBufferPackingArtifactsConsumed": True,
    "textureSamplerBindingArtifactConsumed": True,
    "shadergraphResourcesExpansionArtifactConsumed": True,
    "textureCubePbrReferenceArtifactConsumed": True,
    "actualMetalFullBindingProbeRan": True,
    "fullPbrResourceBinding": True,
    "backendReplacementReady": False,
    "explicitAdapterProof": False,
    "hlslToMslTranslation": False,
    "tixlRuntimeParity": False,
    "nativeGpuParityComplete": False,
    "pbrVisualCorrectness": False,
}

FORBIDDEN_WIDENED_CLAIMS = {
    "backendReplacementReady",
    "explicitAdapterProof",
    "hlslToMslTranslation",
    "hlslToMslTranslationProven",
    "tixlRuntimeParity",
    "nativeGpuParityComplete",
    "pbrVisualCorrectness",
    "rendererIntegrationComplete",
    "nativeCompileParity",
}

EXPECTED_WORDS = [
    0xC0DEF000,
    0xC0DEF001,
    0xC0DEF002,
    0xC0DEF003,
    0xC0DEF004,
    0xC0DEF005,
    0xC0DEF006,
    0xC0DEF007,
    0x11223344,
    0x22334455,
    0x33445566,
    0x44556677,
    0x55667788,
    0x66778899,
    0x778899AA,
    0x8899AABB,
]

MSL_SOURCE = """#include <metal_stdlib>
using namespace metal;

static uint pack_rgba8(float4 color)
{
    uint4 bytes = uint4(round(clamp(color, 0.0, 1.0) * 255.0));
    return (bytes.r << 24) | (bytes.g << 16) | (bytes.b << 8) | bytes.a;
}

kernel void full_pbr_resource_binding_probe(
    device const uint* PbrVertices [[buffer(0)]],
    device const uint* FaceIndices [[buffer(1)]],
    constant uint& b0 [[buffer(2)]],
    constant uint& b1 [[buffer(3)]],
    constant uint& b2 [[buffer(4)]],
    constant uint& b3 [[buffer(5)]],
    constant uint& b4 [[buffer(6)]],
    constant uint& b5 [[buffer(7)]],
    texture2d<float, access::sample> BaseColorMap [[texture(2)]],
    texture2d<float, access::sample> EmissiveColorMap [[texture(3)]],
    texture2d<float, access::sample> RSMOMap [[texture(4)]],
    texture2d<float, access::sample> NormalMap [[texture(5)]],
    texturecube<float, access::sample> PrefilteredSpecular [[texture(6)]],
    texture2d<float, access::sample> BRDFLookup [[texture(7)]],
    sampler WrappedSampler [[sampler(0)]],
    sampler ClampedSampler [[sampler(1)]],
    device uint* out [[buffer(8)]])
{
    out[0] = PbrVertices[0];
    out[1] = FaceIndices[0];
    out[2] = b0;
    out[3] = b1;
    out[4] = b2;
    out[5] = b3;
    out[6] = b4;
    out[7] = b5;
    out[8] = pack_rgba8(BaseColorMap.sample(WrappedSampler, float2(0.25, 0.25)));
    out[9] = pack_rgba8(EmissiveColorMap.sample(WrappedSampler, float2(0.25, 0.25)));
    out[10] = pack_rgba8(RSMOMap.sample(WrappedSampler, float2(0.25, 0.25)));
    out[11] = pack_rgba8(NormalMap.sample(WrappedSampler, float2(0.25, 0.25)));
    out[12] = pack_rgba8(PrefilteredSpecular.sample(WrappedSampler, float3(1.0, 0.0, 0.0), level(0.0)));
    out[13] = pack_rgba8(BRDFLookup.sample(ClampedSampler, float2(0.25, 0.25)));
    out[14] = pack_rgba8(BaseColorMap.sample(WrappedSampler, float2(-0.25, 0.25)));
    out[15] = pack_rgba8(BRDFLookup.sample(ClampedSampler, float2(1.25, 0.25)));
}
"""

PROBE_SOURCE = r'''#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static std::string esc(const std::string& value)
{
    std::string out;
    for (char c : value)
    {
        if (c == '"' || c == '\\') out.push_back('\\');
        if (c == '\n' || c == '\r') out.push_back(' ');
        else out.push_back(c);
    }
    return out;
}

static std::string err(NSError* error, const char* fallback)
{
    if (!error) return fallback;
    const char* text = [[error localizedDescription] UTF8String];
    return text ? std::string(text) : std::string(fallback);
}

static void jsonArray(const std::vector<uint32_t>& values)
{
    std::cout << "[";
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i) std::cout << ",";
        std::cout << values[i];
    }
    std::cout << "]";
}

static int emit(const char* status, bool ok, bool compiler, bool metal, const std::string& message,
                const std::vector<uint32_t>& expected = {}, const std::vector<uint32_t>& actual = {})
{
    std::cout << "{"
              << "\"status\":\"" << status << "\","
              << "\"ok\":" << (ok ? "true" : "false") << ","
              << "\"actualCompilerRan\":" << (compiler ? "true" : "false") << ","
              << "\"actualMetalRan\":" << (metal ? "true" : "false") << ","
              << "\"message\":\"" << esc(message) << "\","
              << "\"expectedWords\":";
    jsonArray(expected);
    std::cout << ",\"actualWords\":";
    jsonArray(actual);
    std::cout << "}\n";
    return ok ? 0 : 1;
}

static id<MTLBuffer> makeWordBuffer(id<MTLDevice> device, uint32_t value)
{
    id<MTLBuffer> buffer = [device newBufferWithLength:sizeof(uint32_t) options:MTLResourceStorageModeShared];
    if (buffer) ((uint32_t*)[buffer contents])[0] = value;
    return buffer;
}

static id<MTLTexture> makeTexture2D(id<MTLDevice> device, uint32_t rgba0, uint32_t rgba1)
{
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:2 height:1 mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModeShared;
    id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
    if (!texture) return nil;
    unsigned char pixels[8] = {
        (unsigned char)((rgba0 >> 24) & 0xff),
        (unsigned char)((rgba0 >> 16) & 0xff),
        (unsigned char)((rgba0 >> 8) & 0xff),
        (unsigned char)(rgba0 & 0xff),
        (unsigned char)((rgba1 >> 24) & 0xff),
        (unsigned char)((rgba1 >> 16) & 0xff),
        (unsigned char)((rgba1 >> 8) & 0xff),
        (unsigned char)(rgba1 & 0xff)
    };
    [texture replaceRegion:MTLRegionMake2D(0, 0, 2, 1) mipmapLevel:0 withBytes:pixels bytesPerRow:8];
    return texture;
}

static id<MTLTexture> makeTextureCube(id<MTLDevice> device, uint32_t rgba)
{
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm size:1 mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModeShared;
    id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
    if (!texture) return nil;
    unsigned char pixel[4] = {
        (unsigned char)((rgba >> 24) & 0xff),
        (unsigned char)((rgba >> 16) & 0xff),
        (unsigned char)((rgba >> 8) & 0xff),
        (unsigned char)(rgba & 0xff)
    };
    for (NSUInteger slice = 0; slice < 6; ++slice)
        [texture replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 slice:slice withBytes:pixel bytesPerRow:4 bytesPerImage:4];
    return texture;
}

int main(int argc, const char** argv)
{
    @autoreleasepool
    {
        if (argc != 2)
            return emit("usage_error", false, false, false, "usage: full_pbr_probe <msl_source_path>");

        std::vector<uint32_t> expected = {
            0xC0DEF000u, 0xC0DEF001u, 0xC0DEF002u, 0xC0DEF003u,
            0xC0DEF004u, 0xC0DEF005u, 0xC0DEF006u, 0xC0DEF007u,
            0x11223344u, 0x22334455u, 0x33445566u, 0x44556677u,
            0x55667788u, 0x66778899u, 0x778899AAu, 0x8899AABBu
        };

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device)
            return emit("blocked_metal_device_unavailable", false, false, false, "Metal device unavailable", expected);
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue)
            return emit("blocked_metal_device_unavailable", false, false, false, "Metal command queue unavailable", expected);

        NSError* readError = nil;
        NSString* path = [NSString stringWithUTF8String:argv[1]];
        NSString* source = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&readError];
        if (!source)
            return emit("source_read_failed", false, false, true, err(readError, "MSL source read failed"), expected);

        NSError* compileError = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&compileError];
        if (!library)
            return emit("compile_failed", false, true, true, err(compileError, "MSL compile failed"), expected);
        id<MTLFunction> kernel = [library newFunctionWithName:@"full_pbr_resource_binding_probe"];
        if (!kernel)
            return emit("compile_failed", false, true, true, "MSL source missing expected compute function", expected);
        NSError* pipelineError = nil;
        id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:kernel error:&pipelineError];
        if (!pipeline)
            return emit("pipeline_failed", false, true, true, err(pipelineError, "compute pipeline failed"), expected);

        std::vector<id<MTLBuffer>> buffers;
        for (uint32_t value : std::vector<uint32_t>{0xC0DEF000u, 0xC0DEF001u, 0xC0DEF002u, 0xC0DEF003u, 0xC0DEF004u, 0xC0DEF005u, 0xC0DEF006u, 0xC0DEF007u})
        {
            id<MTLBuffer> buffer = makeWordBuffer(device, value);
            if (!buffer)
                return emit("resource_failed", false, true, true, "buffer allocation failed", expected);
            buffers.push_back(buffer);
        }

        id<MTLTexture> t2 = makeTexture2D(device, 0x11223344u, 0x778899AAu);
        id<MTLTexture> t3 = makeTexture2D(device, 0x22334455u, 0x22334455u);
        id<MTLTexture> t4 = makeTexture2D(device, 0x33445566u, 0x33445566u);
        id<MTLTexture> t5 = makeTexture2D(device, 0x44556677u, 0x44556677u);
        id<MTLTexture> t6 = makeTextureCube(device, 0x55667788u);
        id<MTLTexture> t7 = makeTexture2D(device, 0x66778899u, 0x8899AABBu);
        if (!t2 || !t3 || !t4 || !t5 || !t6 || !t7)
            return emit("resource_failed", false, true, true, "texture allocation failed", expected);

        MTLSamplerDescriptor* wrappedDescriptor = [[MTLSamplerDescriptor alloc] init];
        wrappedDescriptor.minFilter = MTLSamplerMinMagFilterNearest;
        wrappedDescriptor.magFilter = MTLSamplerMinMagFilterNearest;
        wrappedDescriptor.sAddressMode = MTLSamplerAddressModeRepeat;
        wrappedDescriptor.tAddressMode = MTLSamplerAddressModeRepeat;
        id<MTLSamplerState> s0 = [device newSamplerStateWithDescriptor:wrappedDescriptor];

        MTLSamplerDescriptor* clampedDescriptor = [[MTLSamplerDescriptor alloc] init];
        clampedDescriptor.minFilter = MTLSamplerMinMagFilterNearest;
        clampedDescriptor.magFilter = MTLSamplerMinMagFilterNearest;
        clampedDescriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
        clampedDescriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
        id<MTLSamplerState> s1 = [device newSamplerStateWithDescriptor:clampedDescriptor];
        if (!s0 || !s1)
            return emit("resource_failed", false, true, true, "sampler allocation failed", expected);

        id<MTLBuffer> output = [device newBufferWithLength:sizeof(uint32_t) * expected.size() options:MTLResourceStorageModeShared];
        if (!output)
            return emit("resource_failed", false, true, true, "output allocation failed", expected);

        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
        [encoder setComputePipelineState:pipeline];
        for (NSUInteger index = 0; index < buffers.size(); ++index)
            [encoder setBuffer:buffers[index] offset:0 atIndex:index];
        [encoder setBuffer:output offset:0 atIndex:8];
        [encoder setTexture:t2 atIndex:2];
        [encoder setTexture:t3 atIndex:3];
        [encoder setTexture:t4 atIndex:4];
        [encoder setTexture:t5 atIndex:5];
        [encoder setTexture:t6 atIndex:6];
        [encoder setTexture:t7 atIndex:7];
        [encoder setSamplerState:s0 atIndex:0];
        [encoder setSamplerState:s1 atIndex:1];
        [encoder dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];
        if (commandBuffer.status != MTLCommandBufferStatusCompleted)
            return emit("compute_failed", false, true, true, "command buffer did not complete", expected);

        uint32_t* words = (uint32_t*)[output contents];
        std::vector<uint32_t> actual(words, words + expected.size());
        bool ok = actual == expected;
        return emit(ok ? "proven_full_pbr_resource_binding_probe" : "sentinel_mismatch",
                    ok, true, true,
                    ok ? "compiled generated MSL and observed b0-b5, t0-t7, s0-s1 sentinels" : "unexpected full binding sentinel readback",
                    expected, actual);
    }
}
'''


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tixl_mesh_draw_full_pbr_resource_binding_shell.py <fixture.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_optional_artifacts(out_dir)

    repo_root = Path(__file__).resolve().parents[3]
    trace: list[dict[str, Any]] = [{"op": "loadTixlMeshDrawFullPbrResourceBindingFixture", "fixture": display_path(fixture_path, repo_root)}]
    errors: list[dict[str, Any]] = []

    fixture = read_json(fixture_path, errors, "tixl_mesh_draw_full_pbr_resource_binding.fixture_read_failed", repo_root)
    if fixture is None:
        result = default_result(None, "blocked_missing_fixture", {})
        publish(out_dir, result, trace, errors)
        return 1

    result, run_trace, run_errors, generated_msl = run_proof(repo_root, fixture_path, fixture)
    trace.extend(run_trace)
    errors.extend(run_errors)
    trace.append({"op": "publishTixlMeshDrawFullPbrResourceBindingArtifacts", "ok": result.get("ok") is True and not errors})
    publish(out_dir, result, trace, errors, generated_msl if result.get("ok") is True and not errors else None)
    return 0 if result.get("ok") is True and not errors else 1


def run_proof(repo_root: Path, fixture_path: Path, fixture: dict[str, Any]) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]], str | None]:
    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    graph_id = fixture.get("graphId")
    paths = {
        "sourceAudit": resolve_path(repo_root, fixture_path, fixture.get("sourceAuditArtifact"), DEFAULT_SOURCE_AUDIT),
        "meshBufferBinding": resolve_path(repo_root, fixture_path, fixture.get("meshBufferBindingArtifact"), DEFAULT_MESH_BUFFER),
        "constantBufferNativePacking": resolve_path(repo_root, fixture_path, fixture.get("constantBufferNativePackingArtifact"), DEFAULT_CONSTANT_PACKING),
        "pointLightsAndB5Packing": resolve_path(repo_root, fixture_path, fixture.get("pointLightsAndB5PackingArtifact"), DEFAULT_POINTLIGHTS_B5),
        "b5NativePacking": resolve_path(repo_root, fixture_path, fixture.get("b5NativePackingArtifact"), DEFAULT_B5_NATIVE),
        "textureSamplerBinding": resolve_path(repo_root, fixture_path, fixture.get("textureSamplerBindingArtifact"), DEFAULT_TEXTURE_SAMPLER),
        "shadergraphResourcesExpansion": resolve_path(repo_root, fixture_path, fixture.get("shadergraphResourcesExpansionArtifact"), DEFAULT_T8),
        "textureCubePbrReference": resolve_path(repo_root, fixture_path, fixture.get("textureCubePbrReferenceArtifact"), DEFAULT_TEXTURECUBE_PBR),
    }
    trace.append({"op": "resolveInputArtifacts", **{key: display_path(value, repo_root) for key, value in paths.items()}})

    artifacts = {
        key: read_json(path, errors, f"tixl_mesh_draw_full_pbr_resource_binding.{snake_case(key)}_read_failed", repo_root)
        for key, path in paths.items()
    }
    summaries = {key: summarize_artifact(paths[key], artifacts[key], repo_root) for key in paths}
    trace.append({"op": "readInputArtifacts", **{f"{key}Read": artifacts[key] is not None for key in artifacts}})

    mismatches: list[dict[str, Any]] = []
    mismatches.extend(prefix_mismatches("sourceAudit", validate_source_audit(artifacts.get("sourceAudit"))))
    mismatches.extend(prefix_mismatches("meshBufferBinding", validate_mesh_buffer_binding(artifacts.get("meshBufferBinding"))))
    mismatches.extend(prefix_mismatches("constantBufferNativePacking", validate_constant_buffer_native_packing(artifacts.get("constantBufferNativePacking"))))
    mismatches.extend(prefix_mismatches("pointLightsAndB5Packing", validate_pointlights_b5(artifacts.get("pointLightsAndB5Packing"))))
    mismatches.extend(prefix_mismatches("b5NativePacking", validate_b5_native(artifacts.get("b5NativePacking"))))
    mismatches.extend(prefix_mismatches("textureSamplerBinding", validate_texture_sampler(artifacts.get("textureSamplerBinding"))))
    mismatches.extend(prefix_mismatches("shadergraphResourcesExpansion", validate_t8(artifacts.get("shadergraphResourcesExpansion"))))
    mismatches.extend(prefix_mismatches("textureCubePbrReference", validate_texturecube_pbr(artifacts.get("textureCubePbrReference"))))
    trace.append({"op": "validateInputArtifacts", "valid": not mismatches})
    if any(value is None for value in artifacts.values()) or mismatches:
        for group, group_mismatches in group_mismatches_by_input(mismatches).items():
            errors.append({
                "code": f"tixl_mesh_draw_full_pbr_resource_binding.invalid_{snake_case(group)}_artifact",
                "mismatches": group_mismatches,
            })
        status = "blocked_missing_or_invalid_input_artifact" if any(value is None for value in artifacts.values()) else "blocked_invalid_input_artifact"
        return default_result(graph_id, status, summaries), trace, errors, None

    fixture_errors = validate_fixture_expectations(fixture)
    trace.append({"op": "validateFixtureExpectations", "valid": not fixture_errors})
    if fixture_errors:
        errors.append({"code": "tixl_mesh_draw_full_pbr_resource_binding.invalid_fixture_expectations", "mismatches": fixture_errors})
        return default_result(graph_id, "blocked_invalid_fixture_expectations", summaries), trace, errors, None

    probe_payload, probe_trace, probe_errors, generated_msl = run_metal_probe(repo_root)
    trace.extend(probe_trace)
    if probe_errors:
        errors.extend(probe_errors)
        status = str(probe_payload.get("status") if isinstance(probe_payload, dict) else "probe_failed")
        return default_result(graph_id, status, summaries), trace, errors, None

    result = build_success_result(graph_id, summaries, probe_payload)
    trace.append({"op": "buildFullPbrResourceBindingLedger", "boundRegisters": BOUND_REGISTERS})
    return result, trace, errors, generated_msl


def validate_source_audit(artifact: Any) -> list[dict[str, Any]]:
    mismatches = validate_simple_artifact(artifact, "TixlMeshDrawShaderSourceAudit", "fixture.tixl_mesh_draw_shader_source_audit", "audited_tixl_mesh_draw_source", {})
    if not isinstance(artifact, dict):
        return mismatches
    resources = {(item.get("register"), item.get("name"), item.get("kind"), item.get("elementType")) for item in list_items(artifact.get("resources"))}
    expected_resources = [
        ("t0", "PbrVertices", "StructuredBuffer", "PbrVertex"),
        ("t1", "FaceIndices", "StructuredBuffer", "int3"),
        ("t2", "BaseColorMap", "Texture2D", "float4"),
        ("t3", "EmissiveColorMap", "Texture2D", "float4"),
        ("t4", "RSMOMap", "Texture2D", "float4"),
        ("t5", "NormalMap", "Texture2D", "float4"),
        ("t6", "PrefilteredSpecular", "TextureCube", "float4"),
        ("t7", "BRDFLookup", "Texture2D", "float4"),
    ]
    for expected in expected_resources:
        if expected not in resources:
            mismatches.append({"field": "resources", "expected": expected, "actual": "missing"})
    samplers = {(item.get("register"), item.get("name")) for item in list_items(artifact.get("samplers"))}
    for expected in (("s0", "WrappedSampler"), ("s1", "ClampedSampler")):
        if expected not in samplers:
            mismatches.append({"field": "samplers", "expected": expected, "actual": "missing"})
    mismatches.extend(validate_no_widened_claims(artifact))
    return mismatches


def validate_mesh_buffer_binding(artifact: Any) -> list[dict[str, Any]]:
    mismatches = validate_simple_artifact(artifact, "TixlMeshDrawResourceBindingProof", "fixture.tixl_mesh_draw_resource_binding", "summarized_tixl_mesh_draw_resource_binding", {"meshBufferBindingObserved": True, "fullPbrResourceBinding": False})
    if not isinstance(artifact, dict):
        return mismatches
    bound = {(item.get("sourceRegister"), item.get("sourceName"), item.get("metalBinding")) for item in list_items((artifact.get("bindingLedger") or {}).get("boundNow"))}
    for expected in (("t0", "PbrVertices", "buffer(0)"), ("t1", "FaceIndices", "buffer(1)")):
        if expected not in bound:
            mismatches.append({"field": "bindingLedger.boundNow", "expected": expected, "actual": "missing"})
    return mismatches


def validate_constant_buffer_native_packing(artifact: Any) -> list[dict[str, Any]]:
    mismatches = validate_simple_artifact(artifact, "TixlMeshDrawConstantBufferNativePackingProof", "fixture.tixl_mesh_draw_constant_buffer_native_packing", "proven_partial_native_constant_buffer_packing", {"actualMetalPackingProbeRan": True, "fullPbrResourceBinding": False})
    if not isinstance(artifact, dict):
        return mismatches
    proven = {item.get("register") for item in list_items(artifact.get("provenNativePacking"))}
    for register in ("b0", "b1", "b2", "b4"):
        if register not in proven:
            mismatches.append({"field": "provenNativePacking", "expected": register, "actual": sorted(proven)})
    return mismatches


def validate_pointlights_b5(artifact: Any) -> list[dict[str, Any]]:
    return validate_simple_artifact(artifact, "TixlMeshDrawPointLightsAndB5PackingVerdict", "fixture.tixl_mesh_draw_pointlights_and_b5_packing", "proven_b3_pointlights_packing_b5_blocked", {"b3PointLightsPackingProven": True, "b5RequiresShadergraphParamExpansion": True, "fullPbrResourceBinding": False})


def validate_b5_native(artifact: Any) -> list[dict[str, Any]]:
    mismatches = validate_simple_artifact(artifact, "TixlMeshDrawB5NativePackingProof", "fixture.tixl_mesh_draw_b5_native_packing", "proven_b5_shadergraph_params_native_packing", {"b5NativePackingProven": True, "actualMetalB5PackingProbeRan": True, "fullPbrResourceBinding": False})
    if isinstance(artifact, dict):
        packing = artifact.get("provenNativePacking") if isinstance(artifact.get("provenNativePacking"), dict) else {}
        if packing.get("register") != "b5" or packing.get("metalBuffer") != 7:
            mismatches.append({"field": "provenNativePacking", "expected": {"register": "b5", "metalBuffer": 7}, "actual": packing})
    return mismatches


def validate_texture_sampler(artifact: Any) -> list[dict[str, Any]]:
    mismatches = validate_simple_artifact(artifact, "TixlMeshDrawTextureSamplerBindingProof", "fixture.tixl_mesh_draw_texture_sampler_binding", "proven_tixl_mesh_draw_texture_sampler_binding", {"boundedTextureSamplerMappingProven": True, "fullPbrResourceBinding": False})
    if not isinstance(artifact, dict):
        return mismatches
    subset = {(item.get("sourceRegister"), item.get("metalBinding")) for item in list_items((artifact.get("bindingLedger") or {}).get("boundNow"))}
    for expected in (("t2", "texture(2)"), ("t7", "texture(7)"), ("s0", "sampler(0)"), ("s1", "sampler(1)")):
        if expected not in subset:
            mismatches.append({"field": "bindingLedger.boundNow", "expected": expected, "actual": "missing"})
    return mismatches


def validate_t8(artifact: Any) -> list[dict[str, Any]]:
    mismatches = validate_simple_artifact(artifact, "TixlMeshDrawShadergraphResourcesExpansionProof", "fixture.tixl_mesh_draw_shadergraph_resources_expansion", "proven_empty_t8_shadergraph_resources_for_sphere_sdf_fixture", {"shadergraphResourcesExpansionProven": True, "currentFixtureT8ResourcesEmpty": True, "fullPbrResourceBinding": False})
    if isinstance(artifact, dict):
        expansion = artifact.get("resourceExpansion") if isinstance(artifact.get("resourceExpansion"), dict) else {}
        if expansion.get("registerStart") != "t8" or expansion.get("currentFixtureT8ResourcesEmpty") is not True:
            mismatches.append({"field": "resourceExpansion", "expected": {"registerStart": "t8", "currentFixtureT8ResourcesEmpty": True}, "actual": expansion})
    return mismatches


def validate_texturecube_pbr(artifact: Any) -> list[dict[str, Any]]:
    return validate_simple_artifact(artifact, "TixlMeshDrawTextureCubePbrReferenceProof", "fixture.tixl_mesh_draw_texturecube_pbr_reference", "proven_texturecube_samplelevel_getdimensions_and_bounded_pbr_reference", {"textureCubeSampleLevelProven": True, "textureCubeGetDimensionsProven": True, "boundedPbrVisualReferenceEstablished": True, "fullPbrResourceBinding": False, "pbrVisualCorrectness": False})


def validate_simple_artifact(artifact: Any, kind: str, graph_id: str, status: str, claim_expectations: dict[str, Any]) -> list[dict[str, Any]]:
    if not isinstance(artifact, dict):
        return [{"field": "artifact", "expected": kind, "actual": None}]
    mismatches: list[dict[str, Any]] = []
    if artifact.get("kind") != kind:
        mismatches.append({"field": "kind", "expected": kind, "actual": artifact.get("kind")})
    if artifact.get("graphId") != graph_id:
        mismatches.append({"field": "graphId", "expected": graph_id, "actual": artifact.get("graphId")})
    if artifact.get("ok") is not True:
        mismatches.append({"field": "ok", "expected": True, "actual": artifact.get("ok")})
    if artifact.get("status") != status:
        mismatches.append({"field": "status", "expected": status, "actual": artifact.get("status")})
    claims = artifact.get("claims") if isinstance(artifact.get("claims"), dict) else {}
    for field, expected in claim_expectations.items():
        if claims.get(field) is not expected:
            mismatches.append({"field": f"claims.{field}", "expected": expected, "actual": claims.get(field)})
    mismatches.extend(validate_no_widened_claims(artifact))
    return mismatches


def validate_no_widened_claims(artifact: dict[str, Any]) -> list[dict[str, Any]]:
    claims = artifact.get("claims") if isinstance(artifact.get("claims"), dict) else {}
    return [
        {"field": f"claims.{field}", "expected": False, "actual": True}
        for field in sorted(FORBIDDEN_WIDENED_CLAIMS)
        if claims.get(field) is True
    ]


def validate_fixture_expectations(fixture: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if fixture.get("graphId") != "fixture.tixl_mesh_draw_full_pbr_resource_binding":
        mismatches.append({"field": "graphId", "expected": "fixture.tixl_mesh_draw_full_pbr_resource_binding", "actual": fixture.get("graphId")})
    if fixture.get("kind") != "TixlMeshDrawFullPbrResourceBindingProof":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawFullPbrResourceBindingProof", "actual": fixture.get("kind")})
    expected = fixture.get("expected") if isinstance(fixture.get("expected"), dict) else {}
    if expected.get("status") != "proven_full_pbr_resource_binding":
        mismatches.append({"field": "expected.status", "expected": "proven_full_pbr_resource_binding", "actual": expected.get("status")})
    if sorted(expected.get("boundRegisters") or []) != sorted(BOUND_REGISTERS):
        mismatches.append({"field": "expected.boundRegisters", "expected": BOUND_REGISTERS, "actual": expected.get("boundRegisters")})
    claims = expected.get("claims") if isinstance(expected.get("claims"), dict) else {}
    for field, value in EXPECTED_CLAIMS.items():
        if claims.get(field) is not value:
            mismatches.append({"field": f"expected.claims.{field}", "expected": value, "actual": claims.get(field)})
    return mismatches


def run_metal_probe(repo_root: Path) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]], str | None]:
    trace: list[dict[str, Any]] = [{"op": "runFullPbrResourceBindingMetalProbe", "generatedMslArtifact": MSL_NAME}]
    errors: list[dict[str, Any]] = []
    clang = shutil.which("clang++")
    if clang is None:
        payload = {"status": "blocked_metal_device_unavailable", "ok": False, "actualCompilerRan": False, "actualMetalRan": False, "message": "clang++ unavailable"}
        errors.append({"code": "tixl_mesh_draw_full_pbr_resource_binding.metal_probe_unavailable", "message": "Metal device unavailable: clang++ unavailable"})
        return payload, trace, errors, None
    with tempfile.TemporaryDirectory(prefix="tixl-full-pbr-binding-") as tmp:
        tmp_path = Path(tmp)
        source_path = tmp_path / MSL_NAME
        source_path.write_text(MSL_SOURCE, encoding="utf8")
        probe_path = tmp_path / "full_pbr_binding_probe.mm"
        probe_path.write_text(PROBE_SOURCE, encoding="utf8")
        binary_path = tmp_path / "full_pbr_binding_probe"
        compile_run = subprocess.run(
            [clang, "-std=c++17", "-x", "objective-c++", str(probe_path), "-framework", "Foundation", "-framework", "Metal", "-o", str(binary_path)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if compile_run.returncode != 0:
            payload = {"status": "blocked_metal_device_unavailable", "ok": False, "actualCompilerRan": False, "actualMetalRan": False, "message": "Metal probe compile unavailable"}
            errors.append({
                "code": "tixl_mesh_draw_full_pbr_resource_binding.metal_probe_compile_failed",
                "message": clean_message(compile_run.stderr or compile_run.stdout or "Objective-C++ Metal probe did not compile", repo_root),
            })
            return payload, trace, errors, None
        run = subprocess.run([str(binary_path), str(source_path)], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        try:
            payload = json.loads(run.stdout.strip().splitlines()[-1])
        except Exception as exc:
            payload = {"status": "probe_output_invalid", "ok": False, "actualCompilerRan": True, "actualMetalRan": True, "message": str(exc)}
            errors.append({"code": "tixl_mesh_draw_full_pbr_resource_binding.metal_probe_output_invalid", "message": clean_message(run.stderr or run.stdout or str(exc), repo_root)})
            return payload, trace, errors, None
        if run.returncode != 0 or payload.get("ok") is not True:
            code = "tixl_mesh_draw_full_pbr_resource_binding.metal_device_unavailable" if payload.get("status") == "blocked_metal_device_unavailable" else "tixl_mesh_draw_full_pbr_resource_binding.metal_probe_failed"
            errors.append({"code": code, "message": clean_message(str(payload.get("message", "Metal probe failed")), repo_root), "probe": payload})
            return payload, trace, errors, None
        if payload.get("expectedWords") != EXPECTED_WORDS or payload.get("actualWords") != EXPECTED_WORDS:
            errors.append({"code": "tixl_mesh_draw_full_pbr_resource_binding.probe_sentinel_mismatch", "expectedWords": EXPECTED_WORDS, "actualWords": payload.get("actualWords")})
            return payload, trace, errors, None
        return payload, trace, errors, MSL_SOURCE


def build_success_result(graph_id: Any, summaries: dict[str, Any], probe: dict[str, Any]) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawFullPbrResourceBindingProof",
        "graphId": graph_id,
        "ok": True,
        "status": "proven_full_pbr_resource_binding",
        "message": "proved one source-backed full TiXL Mesh Draw/PBR resource binding ledger with a deterministic Metal sentinel probe",
        "inputArtifacts": summaries,
        "bindingLedger": {
            "boundRegisters": BOUND_REGISTERS,
            "meshBuffers": [
                {"sourceRegister": "t0", "sourceName": "PbrVertices", "sourceKind": "StructuredBuffer<PbrVertex>", "metalBinding": "buffer(0)"},
                {"sourceRegister": "t1", "sourceName": "FaceIndices", "sourceKind": "StructuredBuffer<int3>", "metalBinding": "buffer(1)"},
            ],
            "constantBuffers": [
                {"sourceRegister": "b0", "sourceName": "Transforms", "sourceKind": "cbuffer", "metalBinding": "buffer(2)"},
                {"sourceRegister": "b1", "sourceName": "Params", "sourceKind": "cbuffer", "metalBinding": "buffer(3)"},
                {"sourceRegister": "b2", "sourceName": "FogParams", "sourceKind": "cbuffer", "metalBinding": "buffer(4)"},
                {"sourceRegister": "b3", "sourceName": "PointLights", "sourceKind": "cbuffer", "metalBinding": "buffer(5)"},
                {"sourceRegister": "b4", "sourceName": "PbrParams", "sourceKind": "cbuffer", "metalBinding": "buffer(6)"},
                {"sourceRegister": "b5", "sourceName": "Params", "sourceKind": "shadergraph params cbuffer", "metalBinding": "buffer(7)"},
            ],
            "textures": [
                {"sourceRegister": "t2", "sourceName": "BaseColorMap", "sourceKind": "Texture2D<float4>", "metalBinding": "texture(2)"},
                {"sourceRegister": "t3", "sourceName": "EmissiveColorMap", "sourceKind": "Texture2D<float4>", "metalBinding": "texture(3)"},
                {"sourceRegister": "t4", "sourceName": "RSMOMap", "sourceKind": "Texture2D<float4>", "metalBinding": "texture(4)"},
                {"sourceRegister": "t5", "sourceName": "NormalMap", "sourceKind": "Texture2D<float4>", "metalBinding": "texture(5)"},
                {"sourceRegister": "t6", "sourceName": "PrefilteredSpecular", "sourceKind": "TextureCube<float4>", "metalBinding": "texture(6)"},
                {"sourceRegister": "t7", "sourceName": "BRDFLookup", "sourceKind": "Texture2D<float4>", "metalBinding": "texture(7)"},
            ],
            "samplers": [
                {"sourceRegister": "s0", "sourceName": "WrappedSampler", "sourceKind": "sampler", "metalBinding": "sampler(0)"},
                {"sourceRegister": "s1", "sourceName": "ClampedSampler", "sourceKind": "sampler", "metalBinding": "sampler(1)"},
            ],
            "t8ShadergraphResources": {
                "registerStart": "t8",
                "status": "proven_empty_for_current_fixture",
                "boundRegisters": [],
            },
            "notProvenHere": [
                "backend replacement",
                "explicit adapter proof",
                "HLSL-to-MSL translation",
                "TiXL runtime parity",
                "native GPU parity completion",
                "PBR visual correctness",
            ],
        },
        "evidence": {
            "metalProbeStatus": probe.get("status"),
            "generatedMslArtifact": MSL_NAME,
            "sentinelReadback": {
                "expectedWords": probe.get("expectedWords"),
                "actualWords": probe.get("actualWords"),
            },
        },
        "claims": EXPECTED_CLAIMS,
    }


def default_result(graph_id: Any, status: str, summaries: dict[str, Any]) -> dict[str, Any]:
    claims = dict(EXPECTED_CLAIMS)
    for field, value in list(claims.items()):
        if value is True:
            claims[field] = False
    return {
        "kind": "TixlMeshDrawFullPbrResourceBindingProof",
        "graphId": graph_id,
        "ok": False,
        "status": status,
        "inputArtifacts": summaries,
        "bindingLedger": {
            "boundRegisters": [],
            "t8ShadergraphResources": {"status": "not_run"},
        },
        "claims": claims,
    }


def publish(out_dir: Path, result: dict[str, Any], trace: list[dict[str, Any]], errors: list[dict[str, Any]], generated_msl: str | None = None) -> None:
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / TRACE_NAME, trace)
    write_json(out_dir / ERRORS_NAME, errors)
    if generated_msl is not None:
        (out_dir / MSL_NAME).write_text(generated_msl, encoding="utf8")


def clear_optional_artifacts(out_dir: Path) -> None:
    generated = out_dir / MSL_NAME
    if generated.exists():
        generated.unlink()


def summarize_artifact(path: Path, artifact: Any, repo_root: Path) -> dict[str, Any]:
    if not isinstance(artifact, dict):
        return {"path": display_path(path, repo_root), "read": False}
    return {
        "path": display_path(path, repo_root),
        "kind": artifact.get("kind"),
        "graphId": artifact.get("graphId"),
        "status": artifact.get("status"),
        "ok": artifact.get("ok"),
    }


def prefix_mismatches(prefix: str, mismatches: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [{**mismatch, "field": f"{prefix}.{mismatch.get('field')}"} for mismatch in mismatches]


def group_mismatches_by_input(mismatches: list[dict[str, Any]]) -> dict[str, list[dict[str, Any]]]:
    grouped: dict[str, list[dict[str, Any]]] = {}
    for mismatch in mismatches:
        group = str(mismatch.get("field", "input")).split(".", 1)[0]
        grouped.setdefault(group, []).append(mismatch)
    return grouped


def list_items(value: Any) -> list[dict[str, Any]]:
    return [item for item in value if isinstance(item, dict)] if isinstance(value, list) else []


def resolve_path(repo_root: Path, fixture_path: Path, maybe_path: Any, default_path: str) -> Path:
    raw = maybe_path if isinstance(maybe_path, str) and maybe_path else default_path
    path = Path(raw).expanduser()
    if path.is_absolute():
        return path
    repo_candidate = repo_root / path
    if repo_candidate.exists() or raw.startswith("docs/"):
        return repo_candidate.resolve()
    return (fixture_path.parent / path).resolve()


def read_json(path: Path, errors: list[dict[str, Any]], code: str, repo_root: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": display_path(path, repo_root), "message": clean_message(str(exc), repo_root)})
        return None


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo_root))
    except ValueError:
        return f"external-artifact:{path.name}"


def clean_message(text: str, repo_root: Path) -> str:
    cleaned = " ".join(text.replace(str(repo_root), ".").split())
    try:
        cleaned = cleaned.replace(str(Path.home()), "~")
    except RuntimeError:
        pass
    return cleaned


def snake_case(value: str) -> str:
    out: list[str] = []
    for index, char in enumerate(value):
        if char.isupper() and index:
            out.append("_")
        out.append(char.lower())
    return "".join(out)


if __name__ == "__main__":
    raise SystemExit(main())
