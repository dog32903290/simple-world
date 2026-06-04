#!/usr/bin/env python3
"""
Probe native Metal constant-buffer packing for the handwritten TiXL DrawMesh MSL adapter lane.

This shell consumes the b0-b5 layout artifact, then runs a tiny generated
Objective-C++/Metal compute probe for b0, b1, b2, and b4 only. It does not map
textures/samplers, implement PBR, or replace the backend.
"""

from __future__ import annotations

import json
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


DEFAULT_LAYOUT_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_layout/tixl_mesh_draw_constant_buffer_layout_result.json"
RESULT_NAME = "tixl_mesh_draw_constant_buffer_native_packing_result.json"
TRACE_NAME = "tixl_mesh_draw_constant_buffer_native_packing_trace.json"
ERRORS_NAME = "tixl_mesh_draw_constant_buffer_native_packing_errors.json"
MSL_NAME = "generated_constant_buffer_native_packing_probe.metal"

EXPECTED_CLAIMS_FIXTURE = {
    "constantBufferLayoutArtifactConsumed": True,
    "actualMetalPackingProbeRan": False,
    "nativePackingProofComplete": False,
    "b0b5AdapterReady": False,
    "textureSamplerMapping": False,
    "fullPbrResourceBinding": False,
    "backendReplacementReady": False,
    "hlslToMslTranslation": False,
    "tixlRuntimeParity": False,
    "pbrVisualCorrectness": False,
}

EXPECTED_CONSTANT_BUFFERS = [
    {
        "register": "b0",
        "name": "Transforms",
        "semanticRole": "mesh_draw_transforms",
        "fields": [
            {"name": "CameraToClipSpace", "type": "float4x4"},
            {"name": "ClipSpaceToCamera", "type": "float4x4"},
            {"name": "WorldToCamera", "type": "float4x4"},
            {"name": "CameraToWorld", "type": "float4x4"},
            {"name": "WorldToClipSpace", "type": "float4x4"},
            {"name": "ClipSpaceToWorld", "type": "float4x4"},
            {"name": "ObjectToWorld", "type": "float4x4"},
            {"name": "WorldToObject", "type": "float4x4"},
            {"name": "ObjectToCamera", "type": "float4x4"},
            {"name": "ObjectToClipSpace", "type": "float4x4"},
        ],
    },
    {
        "register": "b1",
        "name": "Params",
        "semanticRole": "mesh_draw_material_params",
        "fields": [
            {"name": "Color", "type": "float4"},
            {"name": "AlphaCutOff", "type": "float"},
            {"name": "UseFlatShading", "type": "float"},
            {"name": "SpecularAA", "type": "float"},
        ],
    },
    {
        "register": "b2",
        "name": "FogParams",
        "semanticRole": "mesh_draw_fog_params",
        "fields": [
            {"name": "FogColor", "type": "float4"},
            {"name": "FogDistance", "type": "float"},
            {"name": "FogBias", "type": "float"},
        ],
    },
    {
        "register": "b3",
        "name": "PointLights",
        "semanticRole": "mesh_draw_point_lights",
        "fields": [
            {"name": "Lights", "type": "PointLight", "array": "[8]"},
            {"name": "ActiveLightCount", "type": "int"},
        ],
    },
    {
        "register": "b4",
        "name": "PbrParams",
        "semanticRole": "mesh_draw_pbr_params",
        "fields": [
            {"name": "BaseColor", "type": "float4"},
            {"name": "EmissiveColor", "type": "float4"},
            {"name": "Roughness", "type": "float"},
            {"name": "Specular", "type": "float"},
            {"name": "Metal", "type": "float"},
        ],
    },
    {
        "register": "b5",
        "name": "Params",
        "semanticRole": "shadergraph_duplicate_params",
        "fields": [],
    },
]

EXPECTED_PROVEN_NATIVE_PACKING = [
    {
        "register": "b0",
        "name": "Transforms",
        "metalBuffer": 2,
        "sizeBytes": 640,
        "offsets": {
            "CameraToClipSpace": 0,
            "ClipSpaceToCamera": 64,
            "WorldToCamera": 128,
            "CameraToWorld": 192,
            "WorldToClipSpace": 256,
            "ClipSpaceToWorld": 320,
            "ObjectToWorld": 384,
            "WorldToObject": 448,
            "ObjectToCamera": 512,
            "ObjectToClipSpace": 576,
        },
    },
    {
        "register": "b1",
        "name": "Params",
        "semanticRole": "mesh_draw_material_params",
        "metalBuffer": 3,
        "sizeBytes": 32,
        "offsets": {
            "Color": 0,
            "AlphaCutOff": 16,
            "UseFlatShading": 20,
            "SpecularAA": 24,
        },
    },
    {
        "register": "b2",
        "name": "FogParams",
        "metalBuffer": 4,
        "sizeBytes": 32,
        "offsets": {
            "FogColor": 0,
            "FogDistance": 16,
            "FogBias": 20,
        },
    },
    {
        "register": "b4",
        "name": "PbrParams",
        "metalBuffer": 6,
        "sizeBytes": 48,
        "offsets": {
            "BaseColor": 0,
            "EmissiveColor": 16,
            "Roughness": 32,
            "Specular": 36,
            "Metal": 40,
        },
    },
]

EXPECTED_PENDING_NATIVE_PACKING = [
    {
        "register": "b3",
        "name": "PointLights",
        "reason": "PointLight element layout is not proven in this tiny packing probe.",
    },
    {
        "register": "b5",
        "name": "Params",
        "semanticRole": "shadergraph_duplicate_params",
        "reason": "The source audit has no concrete b5 Params fields to pack.",
    },
]

MSL_SOURCE = """#include <metal_stdlib>
using namespace metal;

struct Transforms
{
    float4x4 CameraToClipSpace;
    float4x4 ClipSpaceToCamera;
    float4x4 WorldToCamera;
    float4x4 CameraToWorld;
    float4x4 WorldToClipSpace;
    float4x4 ClipSpaceToWorld;
    float4x4 ObjectToWorld;
    float4x4 WorldToObject;
    float4x4 ObjectToCamera;
    float4x4 ObjectToClipSpace;
};

struct ParamsB1
{
    float4 Color;
    float AlphaCutOff;
    float UseFlatShading;
    float SpecularAA;
};

struct FogParams
{
    float4 FogColor;
    float FogDistance;
    float FogBias;
};

struct PbrParams
{
    float4 BaseColor;
    float4 EmissiveColor;
    float Roughness;
    float Specular;
    float Metal;
};

kernel void my_world_constant_buffer_packing_probe(
    constant Transforms& b0 [[buffer(2)]],
    constant ParamsB1& b1 [[buffer(3)]],
    constant FogParams& b2 [[buffer(4)]],
    constant PbrParams& b4 [[buffer(6)]],
    device uint* out [[buffer(0)]])
{
    out[0] = uint(sizeof(Transforms));
    out[1] = uint(sizeof(ParamsB1));
    out[2] = uint(sizeof(FogParams));
    out[3] = uint(sizeof(PbrParams));

    out[10] = as_type<uint>(b0.CameraToClipSpace[0][0]);
    out[11] = as_type<uint>(b0.CameraToClipSpace[3][3]);
    out[12] = as_type<uint>(b0.ClipSpaceToCamera[0][0]);
    out[13] = as_type<uint>(b0.ClipSpaceToCamera[3][3]);
    out[14] = as_type<uint>(b0.WorldToCamera[0][0]);
    out[15] = as_type<uint>(b0.WorldToCamera[3][3]);
    out[16] = as_type<uint>(b0.CameraToWorld[0][0]);
    out[17] = as_type<uint>(b0.CameraToWorld[3][3]);
    out[18] = as_type<uint>(b0.WorldToClipSpace[0][0]);
    out[19] = as_type<uint>(b0.WorldToClipSpace[3][3]);
    out[20] = as_type<uint>(b0.ClipSpaceToWorld[0][0]);
    out[21] = as_type<uint>(b0.ClipSpaceToWorld[3][3]);
    out[22] = as_type<uint>(b0.ObjectToWorld[0][0]);
    out[23] = as_type<uint>(b0.ObjectToWorld[3][3]);
    out[24] = as_type<uint>(b0.WorldToObject[0][0]);
    out[25] = as_type<uint>(b0.WorldToObject[3][3]);
    out[26] = as_type<uint>(b0.ObjectToCamera[0][0]);
    out[27] = as_type<uint>(b0.ObjectToCamera[3][3]);
    out[28] = as_type<uint>(b0.ObjectToClipSpace[0][0]);
    out[29] = as_type<uint>(b0.ObjectToClipSpace[3][3]);

    out[30] = as_type<uint>(b1.Color.x);
    out[31] = as_type<uint>(b1.Color.y);
    out[32] = as_type<uint>(b1.Color.z);
    out[33] = as_type<uint>(b1.Color.w);
    out[34] = as_type<uint>(b1.AlphaCutOff);
    out[35] = as_type<uint>(b1.UseFlatShading);
    out[36] = as_type<uint>(b1.SpecularAA);

    out[40] = as_type<uint>(b2.FogColor.x);
    out[41] = as_type<uint>(b2.FogColor.y);
    out[42] = as_type<uint>(b2.FogColor.z);
    out[43] = as_type<uint>(b2.FogColor.w);
    out[44] = as_type<uint>(b2.FogDistance);
    out[45] = as_type<uint>(b2.FogBias);

    out[50] = as_type<uint>(b4.BaseColor.x);
    out[51] = as_type<uint>(b4.BaseColor.y);
    out[52] = as_type<uint>(b4.BaseColor.z);
    out[53] = as_type<uint>(b4.BaseColor.w);
    out[54] = as_type<uint>(b4.EmissiveColor.x);
    out[55] = as_type<uint>(b4.EmissiveColor.y);
    out[56] = as_type<uint>(b4.EmissiveColor.z);
    out[57] = as_type<uint>(b4.EmissiveColor.w);
    out[58] = as_type<uint>(b4.Roughness);
    out[59] = as_type<uint>(b4.Specular);
    out[60] = as_type<uint>(b4.Metal);
}
"""

PROBE_CPP_SOURCE = r'''#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
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

void putFloat (std::vector<unsigned char>& bytes, std::size_t offset, float value)
{
    if (offset + sizeof (float) <= bytes.size())
        std::memcpy (bytes.data() + offset, &value, sizeof (float));
}

void putFloat4 (std::vector<unsigned char>& bytes, std::size_t offset, float a, float b, float c, float d)
{
    putFloat (bytes, offset, a);
    putFloat (bytes, offset + 4, b);
    putFloat (bytes, offset + 8, c);
    putFloat (bytes, offset + 12, d);
}

void putMatrixSentinel (std::vector<unsigned char>& bytes, std::size_t offset, float firstValue)
{
    putFloat (bytes, offset, firstValue);
    putFloat (bytes, offset + 60, firstValue + 15.0f);
}

void printJsonArray (const std::vector<std::uint32_t>& values)
{
    std::cout << "[";
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        if (index != 0)
            std::cout << ",";
        std::cout << values[index];
    }
    std::cout << "]";
}

int fail (const std::string& status, bool actualCompilerRan, const std::string& message, const std::string& compilerDiagnostic = "")
{
    std::cout
        << "{"
        << "\"status\":\"" << escapeJson (status) << "\","
        << "\"ok\":false,"
        << "\"actualCompilerRan\":" << (actualCompilerRan ? "true" : "false") << ","
        << "\"actualMetalRan\":false,"
        << "\"message\":\"" << escapeJson (message) << "\"";
    if (! compilerDiagnostic.empty())
        std::cout << ",\"compilerDiagnostic\":\"" << escapeJson (compilerDiagnostic) << "\"";
    std::cout << "}\n";
    return 1;
}
}

int main (int argc, char* argv[])
{
    @autoreleasepool
    {
        if (argc != 2)
            return fail ("usage_error", false, "usage: constant_buffer_native_packing_probe <msl_source_path>");

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

        id<MTLFunction> function = [library newFunctionWithName:@"my_world_constant_buffer_packing_probe"];
        if (function == nil)
            return fail ("compile_failed", true, "MSL source must define my_world_constant_buffer_packing_probe");

        NSError* pipelineError = nil;
        id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&pipelineError];
        if (pipeline == nil)
            return fail ("pipeline_failed", true, errorMessage (pipelineError, "Metal compute pipeline compile failed"));

        std::vector<unsigned char> b0 (640, 0);
        for (int index = 0; index < 10; ++index)
            putMatrixSentinel (b0, static_cast<std::size_t> (index) * 64u, 100.0f + static_cast<float> (index) * 10.0f);

        std::vector<unsigned char> b1 (32, 0);
        putFloat4 (b1, 0, 1.0f, 2.0f, 3.0f, 4.0f);
        putFloat (b1, 16, 5.0f);
        putFloat (b1, 20, 6.0f);
        putFloat (b1, 24, 7.0f);

        std::vector<unsigned char> b2 (32, 0);
        putFloat4 (b2, 0, 8.0f, 9.0f, 10.0f, 11.0f);
        putFloat (b2, 16, 12.0f);
        putFloat (b2, 20, 13.0f);

        std::vector<unsigned char> b4 (48, 0);
        putFloat4 (b4, 0, 14.0f, 15.0f, 16.0f, 17.0f);
        putFloat4 (b4, 16, 18.0f, 19.0f, 20.0f, 21.0f);
        putFloat (b4, 32, 22.0f);
        putFloat (b4, 36, 23.0f);
        putFloat (b4, 40, 24.0f);

        std::vector<std::uint32_t> out (64, 0);
        id<MTLBuffer> outBuffer = [device newBufferWithBytes:out.data()
                                                       length:out.size() * sizeof (std::uint32_t)
                                                      options:MTLResourceStorageModeShared];
        id<MTLBuffer> b0Buffer = [device newBufferWithBytes:b0.data() length:b0.size() options:MTLResourceStorageModeShared];
        id<MTLBuffer> b1Buffer = [device newBufferWithBytes:b1.data() length:b1.size() options:MTLResourceStorageModeShared];
        id<MTLBuffer> b2Buffer = [device newBufferWithBytes:b2.data() length:b2.size() options:MTLResourceStorageModeShared];
        id<MTLBuffer> b4Buffer = [device newBufferWithBytes:b4.data() length:b4.size() options:MTLResourceStorageModeShared];
        if (outBuffer == nil || b0Buffer == nil || b1Buffer == nil || b2Buffer == nil || b4Buffer == nil)
            return fail ("render_failed", true, "Metal buffer allocation failed");

        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
        if (commandBuffer == nil)
            return fail ("render_failed", true, "Metal command buffer unavailable");

        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
        if (encoder == nil)
            return fail ("render_failed", true, "Metal compute encoder unavailable");

        [encoder setComputePipelineState:pipeline];
        [encoder setBuffer:outBuffer offset:0 atIndex:0];
        [encoder setBuffer:b0Buffer offset:0 atIndex:2];
        [encoder setBuffer:b1Buffer offset:0 atIndex:3];
        [encoder setBuffer:b2Buffer offset:0 atIndex:4];
        [encoder setBuffer:b4Buffer offset:0 atIndex:6];
        [encoder dispatchThreads:MTLSizeMake (1, 1, 1) threadsPerThreadgroup:MTLSizeMake (1, 1, 1)];
        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];

        if ([commandBuffer status] == MTLCommandBufferStatusError)
            return fail ("render_failed", true, errorMessage ([commandBuffer error], "Metal command buffer failed"));

        std::memcpy (out.data(), [outBuffer contents], out.size() * sizeof (std::uint32_t));

        std::cout
            << "{"
            << "\"status\":\"proven_partial_native_constant_buffer_packing\","
            << "\"ok\":true,"
            << "\"actualCompilerRan\":true,"
            << "\"actualMetalRan\":true,"
            << "\"message\":\"compiled generated MSL compute probe and read b0,b1,b2,b4 constant buffers\","
            << "\"outputWords\":";
        printJsonArray (out);
        std::cout << "}\n";
        return 0;
    }
}
'''


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tixl_mesh_draw_constant_buffer_native_packing_shell.py <fixture.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    repo_root = Path(__file__).resolve().parents[3]

    trace: list[dict[str, Any]] = [{
        "op": "loadTixlMeshDrawConstantBufferNativePackingFixture",
        "fixture": display_path(fixture_path, repo_root),
    }]
    errors: list[dict[str, Any]] = []

    fixture = read_json(fixture_path, errors, "tixl_mesh_draw_constant_buffer_native_packing.fixture_read_failed", repo_root)
    if fixture is None:
        result = default_result(None, "blocked_missing_fixture", {}, False)
        publish(out_dir, result, trace, errors)
        write_text(out_dir / MSL_NAME, MSL_SOURCE)
        return 1

    result, run_trace, run_errors = run_proof(repo_root, fixture_path, fixture)
    trace.extend(run_trace)
    errors.extend(run_errors)
    trace.append({
        "op": "publishTixlMeshDrawConstantBufferNativePackingArtifacts",
        "ok": result.get("ok") is True and not errors,
    })
    write_text(out_dir / MSL_NAME, MSL_SOURCE)
    publish(out_dir, result, trace, errors)
    return 0 if result.get("ok") is True and not errors else 1


def run_proof(
    repo_root: Path,
    fixture_path: Path,
    fixture: dict[str, Any],
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    graph_id = fixture.get("graphId")

    fixture_errors = validate_fixture_expectations(fixture)
    if fixture_errors:
        errors.append({
            "code": "tixl_mesh_draw_constant_buffer_native_packing.invalid_fixture_expectations",
            "message": "Fixture expectations must keep the native packing lane partial and bounded.",
            "mismatches": fixture_errors,
        })
        return default_result(graph_id, "blocked_invalid_fixture", {}, False), trace, errors

    layout_path = resolve_path(repo_root, fixture_path, fixture.get("layoutArtifact"), DEFAULT_LAYOUT_ARTIFACT)
    trace.append({
        "op": "resolveConstantBufferLayoutArtifact",
        "layoutArtifact": display_path(layout_path, repo_root),
        "layoutArtifactExists": layout_path.exists(),
    })
    layout = read_json(layout_path, errors, "tixl_mesh_draw_constant_buffer_native_packing.layout_artifact_read_failed", repo_root)
    layout_summary = summarize_layout_artifact(layout_path, layout, repo_root)
    if layout is None:
        return default_result(graph_id, "blocked_missing_layout_artifact", layout_summary, False), trace, errors

    layout_errors = validate_layout_artifact(layout)
    trace.append({
        "op": "validateConstantBufferLayoutArtifact",
        "kind": layout.get("kind"),
        "ok": layout.get("ok"),
        "status": layout.get("status"),
        "selectedStrategy": layout.get("selectedStrategy"),
        "valid": not layout_errors,
    })
    if layout_errors:
        errors.append({
            "code": "tixl_mesh_draw_constant_buffer_native_packing.invalid_layout_artifact",
            "message": "Constant-buffer layout artifact no longer matches the bounded b0-b5 adapter contract.",
            "mismatches": layout_errors,
        })
        return default_result(graph_id, "blocked_invalid_layout_artifact", layout_summary, False), trace, errors

    probe_payload, probe_trace, probe_errors = run_native_probe(repo_root)
    trace.extend(probe_trace)
    if probe_payload is None:
        errors.extend(probe_errors)
        return default_result(graph_id, "blocked_needs_native_packing_probe", layout_summary, True), trace, errors

    validation_errors = validate_probe_payload(probe_payload)
    trace.append({
        "op": "validateNativePackingProbeReadback",
        "status": probe_payload.get("status"),
        "ok": probe_payload.get("ok"),
        "valid": not validation_errors,
    })
    if validation_errors:
        errors.append({
            "code": "tixl_mesh_draw_constant_buffer_native_packing.probe_readback_mismatch",
            "message": "Native Metal packing probe did not match expected sizes or roundtrip sentinel reads.",
            "mismatches": validation_errors,
        })
        return default_result(graph_id, "blocked_needs_native_packing_probe", layout_summary, True), trace, errors

    result = success_result(graph_id, layout_summary, probe_payload)
    return result, trace, errors


def run_native_probe(repo_root: Path) -> tuple[dict[str, Any] | None, list[dict[str, Any]], list[dict[str, Any]]]:
    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    build_dir = Path(tempfile.mkdtemp(prefix="tixl-cbuffer-native-packing-"))
    try:
        msl_path = build_dir / MSL_NAME
        source_path = build_dir / "constant_buffer_native_packing_probe.mm"
        probe_bin = build_dir / "constant_buffer_native_packing_probe"
        write_text(msl_path, MSL_SOURCE)
        write_text(source_path, PROBE_CPP_SOURCE)

        compile_cmd = [
            "xcrun",
            "clang++",
            "-std=c++17",
            "-fobjc-arc",
            "-framework",
            "Metal",
            "-framework",
            "Foundation",
            str(source_path),
            "-o",
            str(probe_bin),
        ]
        build = subprocess.run(compile_cmd, cwd=repo_root, text=True, capture_output=True)
        trace.append({
            "op": "buildGeneratedConstantBufferNativePackingProbe",
            "compiler": "xcrun clang++",
            "exitCode": build.returncode,
        })
        if build.returncode != 0:
            errors.append({
                "code": "tixl_mesh_draw_constant_buffer_native_packing.probe_build_failed",
                "message": clean_text(build.stderr or build.stdout or "probe build failed"),
            })
            return None, trace, errors

        run = subprocess.run([str(probe_bin), str(msl_path)], cwd=repo_root, text=True, capture_output=True)
        trace.append({
            "op": "runGeneratedConstantBufferNativePackingProbe",
            "exitCode": run.returncode,
        })
        payload = parse_probe_payload(run.stdout)
        if payload is None:
            errors.append({
                "code": "tixl_mesh_draw_constant_buffer_native_packing.probe_output_invalid",
                "message": clean_text(run.stderr or run.stdout or "probe did not emit JSON"),
            })
            return None, trace, errors
        if run.returncode != 0 or payload.get("ok") is not True:
            errors.append(error_from_probe(payload))
            return None, trace, errors
        return payload, trace, errors
    finally:
        shutil.rmtree(build_dir, ignore_errors=True)


def validate_fixture_expectations(fixture: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if fixture.get("graphId") != "fixture.tixl_mesh_draw_constant_buffer_native_packing":
        mismatches.append({"field": "graphId", "expected": "fixture.tixl_mesh_draw_constant_buffer_native_packing", "actual": fixture.get("graphId")})
    if fixture.get("kind") != "TixlMeshDrawConstantBufferNativePackingProof":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawConstantBufferNativePackingProof", "actual": fixture.get("kind")})
    if fixture.get("probeRegisters") != ["b0", "b1", "b2", "b4"]:
        mismatches.append({"field": "probeRegisters", "expected": ["b0", "b1", "b2", "b4"], "actual": fixture.get("probeRegisters")})
    if fixture.get("pendingRegisters") != ["b3", "b5"]:
        mismatches.append({"field": "pendingRegisters", "expected": ["b3", "b5"], "actual": fixture.get("pendingRegisters")})
    expected = fixture.get("expected") if isinstance(fixture.get("expected"), dict) else {}
    if expected.get("status") != "proven_partial_native_constant_buffer_packing":
        mismatches.append({"field": "expected.status", "expected": "proven_partial_native_constant_buffer_packing", "actual": expected.get("status")})
    if expected.get("claims") != EXPECTED_CLAIMS_FIXTURE:
        mismatches.append({"field": "expected.claims", "expected": EXPECTED_CLAIMS_FIXTURE, "actual": expected.get("claims")})
    if expected.get("provenNativePacking") != EXPECTED_PROVEN_NATIVE_PACKING:
        mismatches.append({"field": "expected.provenNativePacking", "expected": EXPECTED_PROVEN_NATIVE_PACKING, "actual": expected.get("provenNativePacking")})
    return mismatches


def validate_layout_artifact(layout: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if layout.get("kind") != "TixlMeshDrawConstantBufferLayoutProof":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawConstantBufferLayoutProof", "actual": layout.get("kind")})
    if layout.get("ok") is not True:
        mismatches.append({"field": "ok", "expected": True, "actual": layout.get("ok")})
    if layout.get("status") != "classified_tixl_mesh_draw_constant_buffer_layout":
        mismatches.append({"field": "status", "expected": "classified_tixl_mesh_draw_constant_buffer_layout", "actual": layout.get("status")})
    if layout.get("selectedStrategy") != "handwritten_explicit_msl_adapter":
        mismatches.append({"field": "selectedStrategy", "expected": "handwritten_explicit_msl_adapter", "actual": layout.get("selectedStrategy")})
    if compact_constant_buffers(layout.get("constantBuffers")) != EXPECTED_CONSTANT_BUFFERS:
        mismatches.append({"field": "constantBuffers", "expected": EXPECTED_CONSTANT_BUFFERS, "actual": compact_constant_buffers(layout.get("constantBuffers"))})

    binding_policy = layout.get("bindingPolicy") if isinstance(layout.get("bindingPolicy"), dict) else {}
    if binding_policy.get("readiness") != "bounded_partial":
        mismatches.append({"field": "bindingPolicy.readiness", "expected": "bounded_partial", "actual": binding_policy.get("readiness")})
    if binding_policy.get("reservedMetalBufferRange") != [2, 7]:
        mismatches.append({"field": "bindingPolicy.reservedMetalBufferRange", "expected": [2, 7], "actual": binding_policy.get("reservedMetalBufferRange")})
    if binding_policy.get("nativePackingProofRequired") is not True:
        mismatches.append({"field": "bindingPolicy.nativePackingProofRequired", "expected": True, "actual": binding_policy.get("nativePackingProofRequired")})
    if binding_policy.get("backendBindingImplemented") is not False:
        mismatches.append({"field": "bindingPolicy.backendBindingImplemented", "expected": False, "actual": binding_policy.get("backendBindingImplemented")})
    if binding_policy.get("textureSamplerMappingIncluded") is not False:
        mismatches.append({"field": "bindingPolicy.textureSamplerMappingIncluded", "expected": False, "actual": binding_policy.get("textureSamplerMappingIncluded")})

    claims = layout.get("claims") if isinstance(layout.get("claims"), dict) else {}
    expected_claims = {
        "selectedStrategyConsumed": True,
        "constantBufferLayoutClassified": True,
        "constantBufferBindingPolicyReady": "bounded_partial",
        "b0b5LayoutNeedsNativePackingProof": True,
        "textureSamplerMapping": False,
        "fullPbrResourceBinding": False,
        "backendReplacementReady": False,
        "hlslToMslTranslation": False,
        "tixlRuntimeParity": False,
        "pbrVisualCorrectness": False,
    }
    for field, expected in expected_claims.items():
        if claims.get(field) != expected:
            mismatches.append({"field": f"claims.{field}", "expected": expected, "actual": claims.get(field)})
    for field, value in claims.items():
        if field not in expected_claims and value is True:
            mismatches.append({"field": f"claims.{field}", "expected": "not true in layout input", "actual": value})
    return mismatches


def validate_probe_payload(probe: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if probe.get("status") != "proven_partial_native_constant_buffer_packing":
        mismatches.append({"field": "status", "expected": "proven_partial_native_constant_buffer_packing", "actual": probe.get("status")})
    if probe.get("ok") is not True:
        mismatches.append({"field": "ok", "expected": True, "actual": probe.get("ok")})
    if probe.get("actualCompilerRan") is not True:
        mismatches.append({"field": "actualCompilerRan", "expected": True, "actual": probe.get("actualCompilerRan")})
    if probe.get("actualMetalRan") is not True:
        mismatches.append({"field": "actualMetalRan", "expected": True, "actual": probe.get("actualMetalRan")})

    words = probe.get("outputWords")
    if not isinstance(words, list) or len(words) < 61:
        mismatches.append({"field": "outputWords", "expected": "at least 61 uint words", "actual": len(words) if isinstance(words, list) else type(words).__name__})
        return mismatches

    expected_words = expected_probe_output_words()
    for index, expected in expected_words.items():
        actual = words[index] if index < len(words) else None
        if actual != expected:
            mismatches.append({"field": f"outputWords[{index}]", "expected": expected, "actual": actual})
    return mismatches


def success_result(graph_id: Any, layout_summary: dict[str, Any], probe: dict[str, Any]) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawConstantBufferNativePackingProof",
        "graphId": graph_id,
        "ok": True,
        "status": "proven_partial_native_constant_buffer_packing",
        "selectedStrategy": "handwritten_explicit_msl_adapter",
        "layoutArtifact": layout_summary,
        "probe": {
            "backend": "Metal",
            "generatedMslArtifact": MSL_NAME,
            "actualCompilerRan": True,
            "actualMetalRan": True,
            "roundtripReadback": True,
            "message": probe.get("message", ""),
        },
        "provenNativePacking": add_proof_metadata(EXPECTED_PROVEN_NATIVE_PACKING),
        "pendingNativePacking": EXPECTED_PENDING_NATIVE_PACKING,
        "semanticBlockers": semantic_blockers(),
        "claims": claim_flags(True, True),
    }


def default_result(graph_id: Any, status: str, layout_summary: dict[str, Any], layout_consumed: bool) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawConstantBufferNativePackingProof",
        "graphId": graph_id,
        "ok": False,
        "status": status,
        "selectedStrategy": None,
        "layoutArtifact": layout_summary,
        "probe": {
            "backend": "Metal",
            "generatedMslArtifact": MSL_NAME,
            "actualCompilerRan": False,
            "actualMetalRan": False,
            "roundtripReadback": False,
        },
        "provenNativePacking": [],
        "pendingNativePacking": EXPECTED_PENDING_NATIVE_PACKING,
        "semanticBlockers": semantic_blockers(),
        "claims": claim_flags(layout_consumed, False),
    }


def claim_flags(layout_consumed: bool, actual_probe_ran: bool) -> dict[str, bool]:
    return {
        "constantBufferLayoutArtifactConsumed": layout_consumed,
        "actualMetalPackingProbeRan": actual_probe_ran,
        "nativePackingProofComplete": False,
        "b0b5AdapterReady": False,
        "textureSamplerMapping": False,
        "fullPbrResourceBinding": False,
        "backendReplacementReady": False,
        "hlslToMslTranslation": False,
        "tixlRuntimeParity": False,
        "pbrVisualCorrectness": False,
    }


def semantic_blockers() -> list[dict[str, str]]:
    return [
        {
            "code": "b3_point_light_native_packing_pending",
            "reason": "PointLight element layout and array stride are not proven by this tiny b0/b1/b2/b4 probe.",
        },
        {
            "code": "b5_duplicate_params_fields_unknown",
            "reason": "The layout artifact records b5 Params as registered and disambiguated, but no concrete fields are known.",
        },
        {
            "code": "texture_sampler_mapping_not_in_scope",
            "reason": "This native packing proof does not map t2-t7 textures or s0-s1 samplers.",
        },
        {
            "code": "backend_replacement_not_ready",
            "reason": "Partial constant-buffer packing does not implement full resource binding or a backend.",
        },
    ]


def add_proof_metadata(buffers: list[dict[str, Any]]) -> list[dict[str, Any]]:
    enriched: list[dict[str, Any]] = []
    for buffer in buffers:
        copy = json.loads(json.dumps(buffer))
        copy["proof"] = {
            "method": "Metal compute readback from host bytes written at the listed offsets",
            "roundtripSentinelReadback": True,
        }
        enriched.append(copy)
    return enriched


def expected_probe_output_words() -> dict[int, int]:
    expected: dict[int, int] = {
        0: 640,
        1: 32,
        2: 32,
        3: 48,
    }
    b0_values: list[float] = []
    for index in range(10):
        first = 100.0 + float(index) * 10.0
        b0_values.extend([first, first + 15.0])
    for offset, value in enumerate(b0_values, start=10):
        expected[offset] = float_to_uint(value)
    for offset, value in enumerate([1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0], start=30):
        expected[offset] = float_to_uint(value)
    for offset, value in enumerate([8.0, 9.0, 10.0, 11.0, 12.0, 13.0], start=40):
        expected[offset] = float_to_uint(value)
    for offset, value in enumerate([14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0, 21.0, 22.0, 23.0, 24.0], start=50):
        expected[offset] = float_to_uint(value)
    return expected


def float_to_uint(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


def compact_constant_buffers(value: Any) -> list[dict[str, Any]]:
    buffers = value if isinstance(value, list) else []
    compacted: list[dict[str, Any]] = []
    for buffer in buffers:
        if not isinstance(buffer, dict):
            continue
        fields = []
        for field in buffer.get("fields", []) if isinstance(buffer.get("fields"), list) else []:
            if not isinstance(field, dict):
                continue
            compact = {"name": field.get("name"), "type": field.get("type")}
            if field.get("array") is not None:
                compact["array"] = field.get("array")
            fields.append(compact)
        compacted.append({
            "register": buffer.get("register"),
            "name": buffer.get("name"),
            "semanticRole": buffer.get("semanticRole"),
            "fields": fields,
        })
    return compacted


def summarize_layout_artifact(path: Path, layout: Any, repo_root: Path) -> dict[str, Any]:
    summary: dict[str, Any] = {"path": display_path(path, repo_root)}
    if isinstance(layout, dict):
        for key in ("kind", "ok", "status", "selectedStrategy"):
            if key in layout:
                summary[key] = layout[key]
        if isinstance(layout.get("bindingPolicy"), dict):
            summary["bindingPolicyReadiness"] = layout["bindingPolicy"].get("readiness")
            summary["reservedMetalBufferRange"] = layout["bindingPolicy"].get("reservedMetalBufferRange")
    return summary


def error_from_probe(probe: dict[str, Any]) -> dict[str, Any]:
    status = str(probe.get("status") or "probe_failed")
    if status == "blocked_metal_device_unavailable":
        code = "tixl_mesh_draw_constant_buffer_native_packing.device_unavailable"
    elif status == "compile_failed":
        code = "tixl_mesh_draw_constant_buffer_native_packing.compile_failed"
    elif status == "pipeline_failed":
        code = "tixl_mesh_draw_constant_buffer_native_packing.pipeline_failed"
    elif status == "render_failed":
        code = "tixl_mesh_draw_constant_buffer_native_packing.render_failed"
    else:
        code = "tixl_mesh_draw_constant_buffer_native_packing.probe_failed"
    error: dict[str, Any] = {"code": code, "message": clean_text(str(probe.get("message") or status))}
    if probe.get("compilerDiagnostic"):
        error["compilerDiagnostic"] = clean_text(str(probe.get("compilerDiagnostic")))
    return error


def resolve_path(repo_root: Path, fixture_path: Path, value: Any, default: str) -> Path:
    raw = value if isinstance(value, str) and value else default
    candidate = Path(raw).expanduser()
    if candidate.is_absolute():
        return candidate
    fixture_relative = (fixture_path.parent / candidate).resolve()
    if fixture_relative.exists():
        return fixture_relative
    return (repo_root / candidate).resolve()


def read_json(path: Path, errors: list[dict[str, Any]], code: str, repo_root: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": display_path(path, repo_root), "message": clean_message(str(exc), repo_root)})
        return None


def parse_probe_payload(stdout: str) -> dict[str, Any] | None:
    text = stdout.strip()
    if not text:
        return None
    try:
        return json.loads(text.splitlines()[-1])
    except json.JSONDecodeError:
        return None


def publish(out_dir: Path, result: dict[str, Any], trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / TRACE_NAME, trace)
    write_json(out_dir / ERRORS_NAME, errors)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf8")


def write_text(path: Path, payload: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(payload, encoding="utf8")


def clean_text(text: str) -> str:
    return " ".join(text.split())


def clean_message(text: str, repo_root: Path) -> str:
    return clean_text(text.replace(str(repo_root), "."))


def display_path(path: Path | None, repo_root: Path) -> str | None:
    if path is None:
        return None
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return f"outside_repo/{path.name}"


if __name__ == "__main__":
    raise SystemExit(main())
