#!/usr/bin/env python3
"""
Close the TiXL mesh Draw b3/b5 constant-buffer packing lane conservatively.

This shell consumes the previous native packing artifact for b0/b1/b2/b4, then
runs a tiny Metal compute probe for b3 PointLights only. b5 remains blocked
until shadergraph parameter expansion produces concrete source fields.
"""

from __future__ import annotations

import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


DEFAULT_LAYOUT_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_layout/tixl_mesh_draw_constant_buffer_layout_result.json"
DEFAULT_PRIOR_NATIVE_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_native_packing/tixl_mesh_draw_constant_buffer_native_packing_result.json"
DEFAULT_SOURCE_AUDIT_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json"
DEFAULT_POINTLIGHT_SHADER_SOURCE = "external/tixl/Operators/Lib/Assets/shaders/shared/point-light.hlsl"
DEFAULT_POINTLIGHT_HOST_SOURCE = "external/tixl/Core/Rendering/PointLightStack.cs"
RESULT_NAME = "tixl_mesh_draw_pointlights_and_b5_packing_result.json"
TRACE_NAME = "tixl_mesh_draw_pointlights_and_b5_packing_trace.json"
ERRORS_NAME = "tixl_mesh_draw_pointlights_and_b5_packing_errors.json"
MSL_NAME = "generated_pointlights_b3_packing_probe.metal"

EXPECTED_CLAIMS_FIXTURE = {
    "priorNativePackingArtifactConsumed": True,
    "constantBufferLayoutArtifactConsumed": True,
    "actualMetalPointLightProbeRan": False,
    "b3PointLightsPackingProven": False,
    "b5DuplicateParamsPackingProven": False,
    "b5RequiresShadergraphParamExpansion": True,
    "constantBufferAdapterComplete": False,
    "textureSamplerMapping": False,
    "fullPbrResourceBinding": False,
    "backendReplacementReady": False,
    "hlslToMslTranslation": False,
    "tixlRuntimeParity": False,
    "pbrVisualCorrectness": False,
}
EXPECTED_POINTLIGHT_SHADER_FIELDS = [
    ("position", "float3"),
    ("intensity", "float"),
    ("color", "float4"),
    ("range", "float"),
    ("decay", "float"),
    ("__padding", "float2"),
]

EXPECTED_PRIOR_PROVEN_NATIVE_PACKING = [
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

EXPECTED_B3_PACKING = {
    "register": "b3",
    "name": "PointLights",
    "semanticRole": "mesh_draw_point_lights",
    "metalBuffer": 5,
    "sizeBytes": 400,
    "arrayStrideBytes": 48,
    "arrayLength": 8,
    "activeLightCountOffset": 384,
    "offsets": {
        "Lights[0].position": 0,
        "Lights[0].intensity": 12,
        "Lights[0].color": 16,
        "Lights[0].range": 32,
        "Lights[0].decay": 36,
        "Lights[0].__padding": 40,
        "Lights[1].position": 48,
        "Lights[7].position": 336,
        "ActiveLightCount": 384,
    },
}

MSL_SOURCE = """#include <metal_stdlib>
using namespace metal;

struct PointLight
{
    packed_float3 position;
    float intensity;
    float4 color;
    float range;
    float decay;
    packed_float2 padding;
};

struct PointLightsB3
{
    PointLight Lights[8];
    int ActiveLightCount;
};

kernel void my_world_pointlights_b3_packing_probe(
    constant PointLightsB3& b3 [[buffer(5)]],
    device uint* out [[buffer(0)]])
{
    out[0] = uint(sizeof(PointLight));
    out[1] = uint(sizeof(PointLightsB3));
    out[2] = uint(b3.ActiveLightCount);

    out[10] = as_type<uint>(b3.Lights[0].position.x);
    out[11] = as_type<uint>(b3.Lights[0].position.y);
    out[12] = as_type<uint>(b3.Lights[0].position.z);
    out[13] = as_type<uint>(b3.Lights[0].intensity);
    out[14] = as_type<uint>(b3.Lights[0].color.x);
    out[15] = as_type<uint>(b3.Lights[0].color.y);
    out[16] = as_type<uint>(b3.Lights[0].color.z);
    out[17] = as_type<uint>(b3.Lights[0].color.w);
    out[18] = as_type<uint>(b3.Lights[0].range);
    out[19] = as_type<uint>(b3.Lights[0].decay);
    out[20] = as_type<uint>(b3.Lights[0].padding.x);
    out[21] = as_type<uint>(b3.Lights[0].padding.y);

    out[30] = as_type<uint>(b3.Lights[1].position.x);
    out[31] = as_type<uint>(b3.Lights[1].intensity);
    out[32] = as_type<uint>(b3.Lights[1].color.x);
    out[33] = as_type<uint>(b3.Lights[1].range);

    out[40] = as_type<uint>(b3.Lights[7].position.x);
    out[41] = as_type<uint>(b3.Lights[7].intensity);
    out[42] = as_type<uint>(b3.Lights[7].color.x);
    out[43] = as_type<uint>(b3.Lights[7].range);
}
"""

PROBE_CPP_SOURCE = r'''#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstdint>
#include <cstring>
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

void putInt (std::vector<unsigned char>& bytes, std::size_t offset, std::int32_t value)
{
    if (offset + sizeof (std::int32_t) <= bytes.size())
        std::memcpy (bytes.data() + offset, &value, sizeof (std::int32_t));
}

void putFloat3 (std::vector<unsigned char>& bytes, std::size_t offset, float x, float y, float z)
{
    putFloat (bytes, offset, x);
    putFloat (bytes, offset + 4, y);
    putFloat (bytes, offset + 8, z);
}

void putFloat4 (std::vector<unsigned char>& bytes, std::size_t offset, float x, float y, float z, float w)
{
    putFloat (bytes, offset, x);
    putFloat (bytes, offset + 4, y);
    putFloat (bytes, offset + 8, z);
    putFloat (bytes, offset + 12, w);
}

void putPointLight (std::vector<unsigned char>& bytes, std::size_t offset, float base)
{
    putFloat3 (bytes, offset, base + 1.0f, base + 2.0f, base + 3.0f);
    putFloat (bytes, offset + 12, base + 4.0f);
    putFloat4 (bytes, offset + 16, base + 5.0f, base + 6.0f, base + 7.0f, base + 8.0f);
    putFloat (bytes, offset + 32, base + 9.0f);
    putFloat (bytes, offset + 36, base + 10.0f);
    putFloat (bytes, offset + 40, base + 11.0f);
    putFloat (bytes, offset + 44, base + 12.0f);
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
            return fail ("usage_error", false, "usage: pointlights_b3_packing_probe <msl_source_path>");

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

        id<MTLFunction> function = [library newFunctionWithName:@"my_world_pointlights_b3_packing_probe"];
        if (function == nil)
            return fail ("compile_failed", true, "MSL source must define my_world_pointlights_b3_packing_probe");

        NSError* pipelineError = nil;
        id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&pipelineError];
        if (pipeline == nil)
            return fail ("pipeline_failed", true, errorMessage (pipelineError, "Metal compute pipeline compile failed"));

        std::vector<unsigned char> b3 (400, 0);
        putPointLight (b3, 0, 0.0f);
        putPointLight (b3, 48, 20.0f);
        putPointLight (b3, 336, 70.0f);
        putInt (b3, 384, 7);

        std::vector<std::uint32_t> out (48, 0);
        id<MTLBuffer> outBuffer = [device newBufferWithBytes:out.data()
                                                       length:out.size() * sizeof (std::uint32_t)
                                                      options:MTLResourceStorageModeShared];
        id<MTLBuffer> b3Buffer = [device newBufferWithBytes:b3.data() length:b3.size() options:MTLResourceStorageModeShared];
        if (outBuffer == nil || b3Buffer == nil)
            return fail ("render_failed", true, "Metal buffer allocation failed");

        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
        if (commandBuffer == nil)
            return fail ("render_failed", true, "Metal command buffer unavailable");

        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
        if (encoder == nil)
            return fail ("render_failed", true, "Metal compute encoder unavailable");

        [encoder setComputePipelineState:pipeline];
        [encoder setBuffer:outBuffer offset:0 atIndex:0];
        [encoder setBuffer:b3Buffer offset:0 atIndex:5];
        [encoder dispatchThreads:MTLSizeMake (1, 1, 1) threadsPerThreadgroup:MTLSizeMake (1, 1, 1)];
        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];

        if ([commandBuffer status] == MTLCommandBufferStatusError)
            return fail ("render_failed", true, errorMessage ([commandBuffer error], "Metal command buffer failed"));

        std::memcpy (out.data(), [outBuffer contents], out.size() * sizeof (std::uint32_t));

        std::cout
            << "{"
            << "\"status\":\"proven_b3_pointlights_packing\","
            << "\"ok\":true,"
            << "\"actualCompilerRan\":true,"
            << "\"actualMetalRan\":true,"
            << "\"message\":\"compiled generated MSL compute probe and read b3 PointLights constant buffer\","
            << "\"outputWords\":";
        printJsonArray (out);
        std::cout << "}\n";
        return 0;
    }
}
'''


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tixl_mesh_draw_pointlights_and_b5_packing_shell.py <fixture.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    repo_root = Path(__file__).resolve().parents[3]

    trace: list[dict[str, Any]] = [{
        "op": "loadTixlMeshDrawPointLightsAndB5PackingFixture",
        "fixture": display_path(fixture_path, repo_root),
    }]
    errors: list[dict[str, Any]] = []

    fixture = read_json(fixture_path, errors, "tixl_mesh_draw_pointlights_and_b5_packing.fixture_read_failed", repo_root)
    if fixture is None:
        result = default_result(None, "blocked_missing_fixture", {}, {}, {}, {}, False, False)
        publish(out_dir, result, trace, errors)
        write_text(out_dir / MSL_NAME, MSL_SOURCE)
        return 1

    result, run_trace, run_errors = run_proof(repo_root, fixture_path, fixture)
    trace.extend(run_trace)
    errors.extend(run_errors)
    trace.append({
        "op": "publishTixlMeshDrawPointLightsAndB5PackingArtifacts",
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
            "code": "tixl_mesh_draw_pointlights_and_b5_packing.invalid_fixture_expectations",
            "message": "Fixture expectations must keep the b3/b5 packing verdict bounded.",
            "mismatches": fixture_errors,
        })
        return default_result(graph_id, "blocked_invalid_fixture", {}, {}, {}, {}, False, False), trace, errors

    source_audit_path = resolve_path(repo_root, fixture_path, fixture.get("sourceAuditArtifact"), DEFAULT_SOURCE_AUDIT_ARTIFACT)
    layout_path = resolve_path(repo_root, fixture_path, fixture.get("layoutArtifact"), DEFAULT_LAYOUT_ARTIFACT)
    prior_path = resolve_path(repo_root, fixture_path, fixture.get("priorNativePackingArtifact"), DEFAULT_PRIOR_NATIVE_ARTIFACT)
    pointlight_shader_path = resolve_path(repo_root, fixture_path, fixture.get("pointLightShaderSource"), DEFAULT_POINTLIGHT_SHADER_SOURCE)
    pointlight_host_path = resolve_path(repo_root, fixture_path, fixture.get("pointLightHostSource"), DEFAULT_POINTLIGHT_HOST_SOURCE)
    trace.append({
        "op": "resolveInputArtifacts",
        "sourceAuditArtifact": display_path(source_audit_path, repo_root),
        "layoutArtifact": display_path(layout_path, repo_root),
        "priorNativePackingArtifact": display_path(prior_path, repo_root),
        "pointLightShaderSource": display_path(pointlight_shader_path, repo_root),
        "pointLightHostSource": display_path(pointlight_host_path, repo_root),
    })

    source_audit = read_json(source_audit_path, errors, "tixl_mesh_draw_pointlights_and_b5_packing.source_audit_read_failed", repo_root)
    layout = read_json(layout_path, errors, "tixl_mesh_draw_pointlights_and_b5_packing.layout_artifact_read_failed", repo_root)
    prior = read_json(prior_path, errors, "tixl_mesh_draw_pointlights_and_b5_packing.prior_native_packing_read_failed", repo_root)
    source_audit_summary = summarize_source_audit(source_audit_path, source_audit, repo_root)
    layout_summary = summarize_layout_artifact(layout_path, layout, repo_root)
    prior_summary = summarize_prior_artifact(prior_path, prior, repo_root)
    if source_audit is None or layout is None or prior is None:
        return default_result(
            graph_id,
            "blocked_missing_input_artifact",
            source_audit_summary,
            layout_summary,
            prior_summary,
            {},
            layout is not None,
            prior is not None,
        ), trace, errors

    source_audit_errors = validate_source_audit(source_audit)
    layout_errors = validate_layout_artifact(layout)
    prior_errors = validate_prior_native_artifact(prior)
    trace.append({
        "op": "validateInputArtifacts",
        "sourceAuditValid": not source_audit_errors,
        "layoutValid": not layout_errors,
        "priorNativePackingValid": not prior_errors,
    })
    if source_audit_errors:
        errors.append({
            "code": "tixl_mesh_draw_pointlights_and_b5_packing.invalid_source_audit_artifact",
            "message": "Source audit must still record b3 PointLights and fieldless b5 Params.",
            "mismatches": source_audit_errors,
        })
    if layout_errors:
        errors.append({
            "code": "tixl_mesh_draw_pointlights_and_b5_packing.invalid_layout_artifact",
            "message": "Layout artifact must keep b3 concrete and b5 fieldless until shadergraph expansion.",
            "mismatches": layout_errors,
        })
    if prior_errors:
        errors.append({
            "code": "tixl_mesh_draw_pointlights_and_b5_packing.invalid_prior_native_packing_artifact",
            "message": "Prior native packing artifact must prove b0/b1/b2/b4 with a real Metal probe before this lane can continue.",
            "mismatches": prior_errors,
        })
    if source_audit_errors or layout_errors or prior_errors:
        return default_result(graph_id, "blocked_invalid_input_artifact", source_audit_summary, layout_summary, prior_summary, {}, False, False), trace, errors

    source_evidence, source_errors = validate_pointlight_sources(pointlight_shader_path, pointlight_host_path, repo_root)
    trace.append({
        "op": "validatePointLightSourceEvidence",
        "valid": not source_errors,
        "shaderSource": display_path(pointlight_shader_path, repo_root),
        "hostSource": display_path(pointlight_host_path, repo_root),
    })
    if source_errors:
        errors.append({
            "code": "tixl_mesh_draw_pointlights_and_b5_packing.invalid_pointlight_source_evidence",
            "message": "PointLight native packing needs both TiXL shader struct and host constant-buffer layout evidence.",
            "mismatches": source_errors,
        })
        return default_result(graph_id, "blocked_invalid_pointlight_source_evidence", source_audit_summary, layout_summary, prior_summary, source_evidence, True, True), trace, errors

    probe_payload, probe_trace, probe_errors = run_native_probe(repo_root)
    trace.extend(probe_trace)
    if probe_payload is None:
        errors.extend(probe_errors)
        return default_result(graph_id, "blocked_needs_pointlight_native_probe", source_audit_summary, layout_summary, prior_summary, source_evidence, True, True), trace, errors

    validation_errors = validate_probe_payload(probe_payload)
    trace.append({
        "op": "validatePointLightPackingProbeReadback",
        "status": probe_payload.get("status"),
        "ok": probe_payload.get("ok"),
        "valid": not validation_errors,
    })
    if validation_errors:
        errors.append({
            "code": "tixl_mesh_draw_pointlights_and_b5_packing.probe_readback_mismatch",
            "message": "Native Metal point-light packing probe did not match expected size, stride, active count, or sentinels.",
            "mismatches": validation_errors,
        })
        return default_result(graph_id, "blocked_needs_pointlight_native_probe", source_audit_summary, layout_summary, prior_summary, source_evidence, True, True), trace, errors

    result = success_result(graph_id, source_audit_summary, layout_summary, prior_summary, source_evidence, probe_payload)
    return result, trace, errors


def run_native_probe(repo_root: Path) -> tuple[dict[str, Any] | None, list[dict[str, Any]], list[dict[str, Any]]]:
    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    build_dir = Path(tempfile.mkdtemp(prefix="tixl-pointlights-b3-packing-"))
    try:
        msl_path = build_dir / MSL_NAME
        source_path = build_dir / "pointlights_b3_packing_probe.mm"
        probe_bin = build_dir / "pointlights_b3_packing_probe"
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
            "op": "buildGeneratedPointLightsB3PackingProbe",
            "compiler": "xcrun clang++",
            "exitCode": build.returncode,
        })
        if build.returncode != 0:
            errors.append({
                "code": "tixl_mesh_draw_pointlights_and_b5_packing.probe_build_failed",
                "message": clean_text(build.stderr or build.stdout or "probe build failed"),
            })
            return None, trace, errors

        run = subprocess.run([str(probe_bin), str(msl_path)], cwd=repo_root, text=True, capture_output=True)
        trace.append({
            "op": "runGeneratedPointLightsB3PackingProbe",
            "exitCode": run.returncode,
        })
        payload = parse_probe_payload(run.stdout)
        if payload is None:
            errors.append({
                "code": "tixl_mesh_draw_pointlights_and_b5_packing.probe_output_invalid",
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
    if fixture.get("graphId") != "fixture.tixl_mesh_draw_pointlights_and_b5_packing":
        mismatches.append({"field": "graphId", "expected": "fixture.tixl_mesh_draw_pointlights_and_b5_packing", "actual": fixture.get("graphId")})
    if fixture.get("kind") != "TixlMeshDrawPointLightsAndB5PackingVerdict":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawPointLightsAndB5PackingVerdict", "actual": fixture.get("kind")})
    if fixture.get("probeRegisters") != ["b3"]:
        mismatches.append({"field": "probeRegisters", "expected": ["b3"], "actual": fixture.get("probeRegisters")})
    if fixture.get("blockedRegisters") != ["b5"]:
        mismatches.append({"field": "blockedRegisters", "expected": ["b5"], "actual": fixture.get("blockedRegisters")})
    expected = fixture.get("expected") if isinstance(fixture.get("expected"), dict) else {}
    if expected.get("status") != "proven_b3_pointlights_packing_b5_blocked":
        mismatches.append({"field": "expected.status", "expected": "proven_b3_pointlights_packing_b5_blocked", "actual": expected.get("status")})
    if expected.get("claims") != EXPECTED_CLAIMS_FIXTURE:
        mismatches.append({"field": "expected.claims", "expected": EXPECTED_CLAIMS_FIXTURE, "actual": expected.get("claims")})
    if expected.get("b3PointLightsPacking") != EXPECTED_B3_PACKING:
        mismatches.append({"field": "expected.b3PointLightsPacking", "expected": EXPECTED_B3_PACKING, "actual": expected.get("b3PointLightsPacking")})
    return mismatches


def validate_source_audit(source: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if source.get("kind") != "TixlMeshDrawShaderSourceAudit":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawShaderSourceAudit", "actual": source.get("kind")})
    if source.get("ok") is not True:
        mismatches.append({"field": "ok", "expected": True, "actual": source.get("ok")})
    if source.get("status") != "audited_tixl_mesh_draw_source":
        mismatches.append({"field": "status", "expected": "audited_tixl_mesh_draw_source", "actual": source.get("status")})

    buffers = {
        (item.get("register"), item.get("name")): item
        for item in source.get("requiredBuffers", [])
        if isinstance(item, dict)
    }
    b3 = buffers.get(("b3", "PointLights"), {})
    b5 = buffers.get(("b5", "Params"), {})
    expected_b3_fields = [
        {"type": "PointLight", "name": "Lights", "array": "[8]"},
        {"type": "int", "name": "ActiveLightCount", "array": None},
    ]
    if b3 == {}:
        mismatches.append({"field": "requiredBuffers.b3", "expected": {"register": "b3", "name": "PointLights"}})
    elif b3.get("fields") != expected_b3_fields:
        mismatches.append({"field": "requiredBuffers.b3.fields", "expected": expected_b3_fields, "actual": b3.get("fields")})
    if b5 == {}:
        mismatches.append({"field": "requiredBuffers.b5", "expected": {"register": "b5", "name": "Params"}})
    elif b5.get("fields") != []:
        mismatches.append({"field": "requiredBuffers.b5.fields", "expected": [], "actual": b5.get("fields")})

    constants = [
        item for item in source.get("constants", [])
        if isinstance(item, dict) and item.get("register") in {"b3", "b5"}
    ]
    expected_constants = [
        {"buffer": "PointLights", "register": "b3", "name": "Lights", "type": "PointLight", "array": "[8]"},
        {"buffer": "PointLights", "register": "b3", "name": "ActiveLightCount", "type": "int", "array": None},
    ]
    if constants != expected_constants:
        mismatches.append({"field": "constants.b3_b5", "expected": expected_constants, "actual": constants})

    claims = source.get("claims") if isinstance(source.get("claims"), dict) else {}
    for field in ("hlslToMslTranslationProven", "tixlParity", "nativeCompileParity", "pbrVisualCorrectness"):
        if claims.get(field) is not False:
            mismatches.append({"field": f"claims.{field}", "expected": False, "actual": claims.get(field)})
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

    buffers = {buffer.get("register"): buffer for buffer in layout.get("constantBuffers", []) if isinstance(buffer, dict)}
    b3 = buffers.get("b3") if isinstance(buffers.get("b3"), dict) else {}
    b5 = buffers.get("b5") if isinstance(buffers.get("b5"), dict) else {}
    expected_b3_fields = [
        {"name": "Lights", "type": "PointLight", "array": "[8]"},
        {"name": "ActiveLightCount", "type": "int"},
    ]
    if compact_fields(b3.get("fields")) != expected_b3_fields:
        mismatches.append({"field": "constantBuffers.b3.fields", "expected": expected_b3_fields, "actual": compact_fields(b3.get("fields"))})
    if b3.get("name") != "PointLights" or b3.get("semanticRole") != "mesh_draw_point_lights":
        mismatches.append({"field": "constantBuffers.b3", "expected": {"name": "PointLights", "semanticRole": "mesh_draw_point_lights"}, "actual": {"name": b3.get("name"), "semanticRole": b3.get("semanticRole")}})
    if b5.get("name") != "Params" or b5.get("semanticRole") != "shadergraph_duplicate_params":
        mismatches.append({"field": "constantBuffers.b5", "expected": {"name": "Params", "semanticRole": "shadergraph_duplicate_params"}, "actual": {"name": b5.get("name"), "semanticRole": b5.get("semanticRole")}})
    if b5.get("fields") != []:
        mismatches.append({"field": "constantBuffers.b5.fields", "expected": [], "actual": b5.get("fields")})

    duplicate_policy = layout.get("duplicateNamePolicy") if isinstance(layout.get("duplicateNamePolicy"), dict) else {}
    b5_policy = duplicate_policy.get("b5") if isinstance(duplicate_policy.get("b5"), dict) else {}
    if b5_policy.get("fieldsKnownFromSourceAudit") is not False:
        mismatches.append({"field": "duplicateNamePolicy.b5.fieldsKnownFromSourceAudit", "expected": False, "actual": b5_policy.get("fieldsKnownFromSourceAudit")})

    binding_policy = layout.get("bindingPolicy") if isinstance(layout.get("bindingPolicy"), dict) else {}
    expected_mapping = {"b3": 5, "b5": 7}
    actual_mapping = {
        item.get("sourceRegister"): item.get("metalBuffer")
        for item in binding_policy.get("candidateMapping", [])
        if isinstance(item, dict) and item.get("sourceRegister") in expected_mapping
    }
    if actual_mapping != expected_mapping:
        mismatches.append({"field": "bindingPolicy.candidateMapping.b3_b5", "expected": expected_mapping, "actual": actual_mapping})
    if binding_policy.get("backendBindingImplemented") is not False:
        mismatches.append({"field": "bindingPolicy.backendBindingImplemented", "expected": False, "actual": binding_policy.get("backendBindingImplemented")})
    if binding_policy.get("textureSamplerMappingIncluded") is not False:
        mismatches.append({"field": "bindingPolicy.textureSamplerMappingIncluded", "expected": False, "actual": binding_policy.get("textureSamplerMappingIncluded")})

    claims = layout.get("claims") if isinstance(layout.get("claims"), dict) else {}
    for field in ("textureSamplerMapping", "fullPbrResourceBinding", "backendReplacementReady", "hlslToMslTranslation", "tixlRuntimeParity", "pbrVisualCorrectness"):
        if claims.get(field) is not False:
            mismatches.append({"field": f"claims.{field}", "expected": False, "actual": claims.get(field)})
    return mismatches


def validate_prior_native_artifact(prior: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if prior.get("kind") != "TixlMeshDrawConstantBufferNativePackingProof":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawConstantBufferNativePackingProof", "actual": prior.get("kind")})
    if prior.get("ok") is not True:
        mismatches.append({"field": "ok", "expected": True, "actual": prior.get("ok")})
    if prior.get("status") != "proven_partial_native_constant_buffer_packing":
        mismatches.append({"field": "status", "expected": "proven_partial_native_constant_buffer_packing", "actual": prior.get("status")})
    claims = prior.get("claims") if isinstance(prior.get("claims"), dict) else {}
    if claims.get("actualMetalPackingProbeRan") is not True:
        mismatches.append({"field": "claims.actualMetalPackingProbeRan", "expected": True, "actual": claims.get("actualMetalPackingProbeRan")})
    if claims.get("constantBufferLayoutArtifactConsumed") is not True:
        mismatches.append({"field": "claims.constantBufferLayoutArtifactConsumed", "expected": True, "actual": claims.get("constantBufferLayoutArtifactConsumed")})
    for field in ("nativePackingProofComplete", "b0b5AdapterReady", "textureSamplerMapping", "fullPbrResourceBinding", "backendReplacementReady", "hlslToMslTranslation", "tixlRuntimeParity", "pbrVisualCorrectness"):
        if claims.get(field) is not False:
            mismatches.append({"field": f"claims.{field}", "expected": False, "actual": claims.get(field)})
    if compact_proven_native_packing(prior.get("provenNativePacking")) != EXPECTED_PRIOR_PROVEN_NATIVE_PACKING:
        mismatches.append({"field": "provenNativePacking", "expected": EXPECTED_PRIOR_PROVEN_NATIVE_PACKING, "actual": compact_proven_native_packing(prior.get("provenNativePacking"))})
    pending_registers = [item.get("register") for item in prior.get("pendingNativePacking", []) if isinstance(item, dict)]
    if pending_registers != ["b3", "b5"]:
        mismatches.append({"field": "pendingNativePacking.registers", "expected": ["b3", "b5"], "actual": pending_registers})
    return mismatches


def validate_pointlight_sources(shader_path: Path, host_path: Path, repo_root: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    mismatches: list[dict[str, Any]] = []
    shader_text = read_text(shader_path)
    host_text = read_text(host_path)
    if shader_text is None:
        mismatches.append({"field": "pointLightShaderSource", "expected": "readable source file", "actual": display_path(shader_path, repo_root)})
        shader_text = ""
    if host_text is None:
        mismatches.append({"field": "pointLightHostSource", "expected": "readable source file", "actual": display_path(host_path, repo_root)})
        host_text = ""

    shader_fields = parse_hlsl_struct_fields(shader_text, "PointLight")
    if shader_fields != EXPECTED_POINTLIGHT_SHADER_FIELDS:
        mismatches.append({
            "field": "pointLightShaderSource.structFields",
            "expected": [{"name": name, "type": type_name} for name, type_name in EXPECTED_POINTLIGHT_SHADER_FIELDS],
            "actual": [{"name": name, "type": type_name} for name, type_name in shader_fields],
        })

    host_needles = [
        "[StructLayout(LayoutKind.Explicit, Size = 3 * 16)]",
        "[FieldOffset(0)]",
        "[FieldOffset(3*4)]",
        "[FieldOffset(4*4)]",
        "[FieldOffset(8*4)]",
        "[FieldOffset(9*4)]",
        "public const int MaxPointLights = 8;",
        "Marshal.SizeOf<PointLight>() * MaxPointLights + 16",
        "data.Write(activeLightCount);",
    ]
    for needle in host_needles:
        if needle not in host_text:
            mismatches.append({"field": "pointLightHostSource", "expected": needle, "actual": "missing"})

    evidence = {
        "shaderStruct": {
            "path": display_path(shader_path, repo_root),
            "fields": [
                {"name": "position", "type": "float3", "offset": 0},
                {"name": "intensity", "type": "float", "offset": 12},
                {"name": "color", "type": "float4", "offset": 16},
                {"name": "range", "type": "float", "offset": 32},
                {"name": "decay", "type": "float", "offset": 36},
                {"name": "__padding", "type": "float2", "offset": 40},
            ],
        },
        "hostConstantBuffer": {
            "path": display_path(host_path, repo_root),
            "pointLightStructSizeBytes": 48,
            "maxPointLights": 8,
            "activeLightCountOffset": 384,
            "constantBufferSizeBytes": 400,
        },
    }
    return evidence, mismatches


def validate_probe_payload(probe: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if probe.get("status") != "proven_b3_pointlights_packing":
        mismatches.append({"field": "status", "expected": "proven_b3_pointlights_packing", "actual": probe.get("status")})
    if probe.get("ok") is not True:
        mismatches.append({"field": "ok", "expected": True, "actual": probe.get("ok")})
    if probe.get("actualCompilerRan") is not True:
        mismatches.append({"field": "actualCompilerRan", "expected": True, "actual": probe.get("actualCompilerRan")})
    if probe.get("actualMetalRan") is not True:
        mismatches.append({"field": "actualMetalRan", "expected": True, "actual": probe.get("actualMetalRan")})

    words = probe.get("outputWords")
    if not isinstance(words, list) or len(words) < 44:
        mismatches.append({"field": "outputWords", "expected": "at least 44 uint words", "actual": len(words) if isinstance(words, list) else type(words).__name__})
        return mismatches
    expected_words = expected_probe_output_words()
    for index, expected in expected_words.items():
        actual = words[index] if index < len(words) else None
        if actual != expected:
            mismatches.append({"field": f"outputWords[{index}]", "expected": expected, "actual": actual})
    return mismatches


def success_result(
    graph_id: Any,
    source_audit_summary: dict[str, Any],
    layout_summary: dict[str, Any],
    prior_summary: dict[str, Any],
    source_evidence: dict[str, Any],
    probe: dict[str, Any],
) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawPointLightsAndB5PackingVerdict",
        "graphId": graph_id,
        "ok": True,
        "status": "proven_b3_pointlights_packing_b5_blocked",
        "selectedStrategy": "handwritten_explicit_msl_adapter",
        "inputArtifacts": {
            "sourceAudit": source_audit_summary,
            "constantBufferLayout": layout_summary,
            "priorNativePacking": prior_summary,
        },
        "sourceEvidence": source_evidence,
        "probe": {
            "backend": "Metal",
            "generatedMslArtifact": MSL_NAME,
            "actualCompilerRan": True,
            "actualMetalRan": True,
            "roundtripReadback": True,
            "message": probe.get("message", ""),
        },
        "provenNativePacking": [add_proof_metadata(EXPECTED_B3_PACKING)],
        "b5Verdict": b5_verdict(),
        "semanticBlockers": semantic_blockers(),
        "claims": claim_flags(True, True, True),
    }


def default_result(
    graph_id: Any,
    status: str,
    source_audit_summary: dict[str, Any],
    layout_summary: dict[str, Any],
    prior_summary: dict[str, Any],
    source_evidence: dict[str, Any],
    layout_consumed: bool,
    prior_consumed: bool,
) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawPointLightsAndB5PackingVerdict",
        "graphId": graph_id,
        "ok": False,
        "status": status,
        "selectedStrategy": None,
        "inputArtifacts": {
            "sourceAudit": source_audit_summary,
            "constantBufferLayout": layout_summary,
            "priorNativePacking": prior_summary,
        },
        "sourceEvidence": source_evidence,
        "probe": {
            "backend": "Metal",
            "generatedMslArtifact": MSL_NAME,
            "actualCompilerRan": False,
            "actualMetalRan": False,
            "roundtripReadback": False,
        },
        "provenNativePacking": [],
        "b5Verdict": b5_verdict(),
        "semanticBlockers": semantic_blockers(),
        "claims": claim_flags(layout_consumed, prior_consumed, False),
    }


def claim_flags(layout_consumed: bool, prior_consumed: bool, b3_proven: bool) -> dict[str, bool]:
    return {
        "priorNativePackingArtifactConsumed": prior_consumed,
        "constantBufferLayoutArtifactConsumed": layout_consumed,
        "actualMetalPointLightProbeRan": b3_proven,
        "b3PointLightsPackingProven": b3_proven,
        "b5DuplicateParamsPackingProven": False,
        "b5RequiresShadergraphParamExpansion": True,
        "constantBufferAdapterComplete": False,
        "textureSamplerMapping": False,
        "fullPbrResourceBinding": False,
        "backendReplacementReady": False,
        "hlslToMslTranslation": False,
        "tixlRuntimeParity": False,
        "pbrVisualCorrectness": False,
    }


def parse_hlsl_struct_fields(source: str, struct_name: str) -> list[tuple[str, str]]:
    without_block_comments = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    without_comments = re.sub(r"//.*", "", without_block_comments)
    match = re.search(rf"\bstruct\s+{re.escape(struct_name)}\s*\{{(?P<body>.*?)\}}\s*;", without_comments, flags=re.S)
    if match is None:
        return []
    fields: list[tuple[str, str]] = []
    for statement in match.group("body").split(";"):
        text = " ".join(statement.strip().split())
        if not text:
            continue
        field = re.match(r"^(?P<type>[A-Za-z_][A-Za-z0-9_<>,]*)\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)(?:\s*\[[^\]]+\])?$", text)
        if field is None:
            return []
        fields.append((field.group("name"), field.group("type")))
    return fields


def b5_verdict() -> dict[str, Any]:
    return {
        "register": "b5",
        "name": "Params",
        "semanticRole": "shadergraph_duplicate_params",
        "status": "b5_packing_blocked_until_shadergraph_param_expansion",
        "fieldsFromLayout": [],
        "fieldsKnownFromSourceAudit": False,
        "provenNativePacking": False,
        "reason": "The current layout/source-audit artifact records duplicate shadergraph Params at b5 but no concrete fields.",
    }


def semantic_blockers() -> list[dict[str, str]]:
    return [
        {
            "code": "b5_packing_blocked_until_shadergraph_param_expansion",
            "reason": "b5 duplicate Params has no concrete source fields, so native packing cannot be invented in this lane.",
        },
        {
            "code": "constant_buffer_adapter_still_incomplete",
            "reason": "b3 PointLights can be proven independently, but b5 keeps the b0-b5 adapter incomplete.",
        },
        {
            "code": "texture_sampler_mapping_not_in_scope",
            "reason": "This verdict does not map t2-t7 textures or s0-s1 samplers.",
        },
        {
            "code": "backend_replacement_not_ready",
            "reason": "A b3 packing probe plus a b5 boundary verdict is not full resource binding or a backend.",
        },
    ]


def add_proof_metadata(buffer: dict[str, Any]) -> dict[str, Any]:
    copy = json.loads(json.dumps(buffer))
    copy["proof"] = {
        "method": "Metal compute readback from host bytes written at PointLight offsets 0, 48, 336 and ActiveLightCount offset 384",
        "roundtripSentinelReadback": True,
        "sourceBackedBy": [
            "external/tixl/Operators/Lib/Assets/shaders/shared/point-light.hlsl",
            "external/tixl/Core/Rendering/PointLightStack.cs",
        ],
    }
    return copy


def expected_probe_output_words() -> dict[int, int]:
    expected: dict[int, int] = {
        0: 48,
        1: 400,
        2: 7,
    }
    for offset, value in enumerate([1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0], start=10):
        expected[offset] = float_to_uint(value)
    expected[30] = float_to_uint(21.0)
    expected[31] = float_to_uint(24.0)
    expected[32] = float_to_uint(25.0)
    expected[33] = float_to_uint(29.0)
    expected[40] = float_to_uint(71.0)
    expected[41] = float_to_uint(74.0)
    expected[42] = float_to_uint(75.0)
    expected[43] = float_to_uint(79.0)
    return expected


def float_to_uint(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


def compact_fields(value: Any) -> list[dict[str, Any]]:
    fields = value if isinstance(value, list) else []
    compacted: list[dict[str, Any]] = []
    for field in fields:
        if not isinstance(field, dict):
            continue
        compact = {"name": field.get("name"), "type": field.get("type")}
        if field.get("array") is not None:
            compact["array"] = field.get("array")
        compacted.append(compact)
    return compacted


def compact_proven_native_packing(value: Any) -> list[dict[str, Any]]:
    buffers = value if isinstance(value, list) else []
    compacted: list[dict[str, Any]] = []
    for buffer in buffers:
        if not isinstance(buffer, dict):
            continue
        compact = {
            "register": buffer.get("register"),
            "name": buffer.get("name"),
            "metalBuffer": buffer.get("metalBuffer"),
            "sizeBytes": buffer.get("sizeBytes"),
            "offsets": buffer.get("offsets"),
        }
        if buffer.get("semanticRole") is not None:
            compact["semanticRole"] = buffer.get("semanticRole")
        compacted.append(compact)
    return compacted


def summarize_source_audit(path: Path, source: Any, repo_root: Path) -> dict[str, Any]:
    summary: dict[str, Any] = {"path": display_path(path, repo_root)}
    if isinstance(source, dict):
        for key in ("kind", "ok", "status"):
            if key in source:
                summary[key] = source[key]
        summary["requiredRegisters"] = [
            item.get("register")
            for item in source.get("requiredBuffers", [])
            if isinstance(item, dict)
        ]
        summary["b5FieldCount"] = 0
        for item in source.get("requiredBuffers", []):
            if isinstance(item, dict) and item.get("register") == "b5" and item.get("name") == "Params":
                fields = item.get("fields")
                summary["b5FieldCount"] = len(fields) if isinstance(fields, list) else None
    return summary


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


def summarize_prior_artifact(path: Path, prior: Any, repo_root: Path) -> dict[str, Any]:
    summary: dict[str, Any] = {"path": display_path(path, repo_root)}
    if isinstance(prior, dict):
        for key in ("kind", "ok", "status", "selectedStrategy"):
            if key in prior:
                summary[key] = prior[key]
        if isinstance(prior.get("claims"), dict):
            summary["actualMetalPackingProbeRan"] = prior["claims"].get("actualMetalPackingProbeRan")
        summary["provenRegisters"] = [
            item.get("register")
            for item in prior.get("provenNativePacking", [])
            if isinstance(item, dict)
        ]
    return summary


def error_from_probe(probe: dict[str, Any]) -> dict[str, Any]:
    status = str(probe.get("status") or "probe_failed")
    if status == "blocked_metal_device_unavailable":
        code = "tixl_mesh_draw_pointlights_and_b5_packing.device_unavailable"
    elif status == "compile_failed":
        code = "tixl_mesh_draw_pointlights_and_b5_packing.compile_failed"
    elif status == "pipeline_failed":
        code = "tixl_mesh_draw_pointlights_and_b5_packing.pipeline_failed"
    elif status == "render_failed":
        code = "tixl_mesh_draw_pointlights_and_b5_packing.render_failed"
    else:
        code = "tixl_mesh_draw_pointlights_and_b5_packing.probe_failed"
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


def read_text(path: Path) -> str | None:
    try:
        return path.read_text(encoding="utf8")
    except Exception:
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
