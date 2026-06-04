#!/usr/bin/env python3
"""
Prove the source-backed TiXL mesh Draw b5 shadergraph Params native packing.

This shell consumes the b5 expansion artifact, the b0-b5 layout artifact, and
the PointLights/b5 boundary artifact. It then runs a tiny Metal compute probe
that binds the exact 16-byte b5 host buffer at Metal buffer(7).
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


RESULT_NAME = "tixl_mesh_draw_b5_native_packing_result.json"
TRACE_NAME = "tixl_mesh_draw_b5_native_packing_trace.json"
ERRORS_NAME = "tixl_mesh_draw_b5_native_packing_errors.json"
MSL_NAME = "generated_b5_shadergraph_params_packing_probe.metal"

DEFAULT_EXPANSION = "docs/runtime/artifacts/tixl_mesh_draw_b5_shadergraph_params_expansion/tixl_mesh_draw_b5_shadergraph_params_expansion_result.json"
DEFAULT_LAYOUT = "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_layout/tixl_mesh_draw_constant_buffer_layout_result.json"
DEFAULT_POINTLIGHTS = "docs/runtime/artifacts/tixl_mesh_draw_pointlights_and_b5_packing/tixl_mesh_draw_pointlights_and_b5_packing_result.json"

EXPECTED_B5_PACKING = {
    "register": "b5",
    "name": "Params",
    "semanticRole": "shadergraph_duplicate_params",
    "metalBuffer": 7,
    "sizeBytes": 16,
    "offsets": {
        "SphereSDF_nG1CBDm_Center": 0,
        "SphereSDF_nG1CBDm_Radius": 12,
    },
    "values": {
        "SphereSDF_nG1CBDm_Center": [-1.4845504, 0, 0.54366434],
        "SphereSDF_nG1CBDm_Radius": 0.5,
    },
}

EXPECTED_CLAIMS_FIXTURE = {
    "b5ShadergraphParamsExpansionArtifactConsumed": True,
    "constantBufferLayoutArtifactConsumed": True,
    "pointlightsAndB5PackingArtifactConsumed": True,
    "b3PointLightsPackingProven": True,
    "b5ShadergraphParamsExpanded": True,
    "b5FieldsSourceBacked": True,
    "actualMetalB5PackingProbeRan": False,
    "b5NativePackingProven": False,
    "constantBufferAdapterComplete": False,
    "textureSamplerMapping": False,
    "fullPbrResourceBinding": False,
    "backendReplacementReady": False,
    "hlslToMslTranslation": False,
    "tixlRuntimeParity": False,
    "pbrVisualCorrectness": False,
}

EXPECTED_EXPANSION_FIELDS = [
    {
        "name": "SphereSDF_nG1CBDm_Center",
        "type": "float3",
        "offsetBytes": 0,
        "componentCount": 3,
        "floatValueRange": [0, 3],
        "provenance": {
            "node": "SphereSDF",
            "field": "Center",
            "sourceKind": "GraphParam",
            "sourcePaths": [
                "external/tixl/Operators/Lib/field/generate/sdf/SphereSDF.cs",
                "external/tixl/Operators/examples/testing/ShaderTests.t3",
            ],
        },
    },
    {
        "name": "SphereSDF_nG1CBDm_Radius",
        "type": "float",
        "offsetBytes": 12,
        "componentCount": 1,
        "floatValueRange": [3, 4],
        "provenance": {
            "node": "SphereSDF",
            "field": "Radius",
            "sourceKind": "GraphParam",
            "sourcePaths": [
                "external/tixl/Operators/Lib/field/generate/sdf/SphereSDF.cs",
                "external/tixl/Operators/Lib/field/generate/sdf/SphereSDF.t3",
            ],
        },
    },
]

MSL_SOURCE = """#include <metal_stdlib>
using namespace metal;

struct B5Params
{
    packed_float3 SphereSDF_nG1CBDm_Center;
    float SphereSDF_nG1CBDm_Radius;
};

kernel void my_world_b5_shadergraph_params_packing_probe(
    constant B5Params& b5 [[buffer(7)]],
    device uint* out [[buffer(0)]])
{
    out[0] = uint(sizeof(B5Params));
    out[1] = as_type<uint>(b5.SphereSDF_nG1CBDm_Center.x);
    out[2] = as_type<uint>(b5.SphereSDF_nG1CBDm_Center.y);
    out[3] = as_type<uint>(b5.SphereSDF_nG1CBDm_Center.z);
    out[4] = as_type<uint>(b5.SphereSDF_nG1CBDm_Radius);
}
"""

PROBE_CPP_SOURCE = r'''#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstdint>
#include <cstring>
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
    const std::string message = nsStringToStd ([error localizedDescription]);
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

int hexValue (char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool parseHexBytes (const std::string& text, std::vector<unsigned char>& bytes)
{
    if ((text.size() % 2) != 0)
        return false;
    bytes.clear();
    bytes.reserve (text.size() / 2);
    for (std::size_t index = 0; index < text.size(); index += 2)
    {
        const int high = hexValue (text[index]);
        const int low = hexValue (text[index + 1]);
        if (high < 0 || low < 0)
            return false;
        bytes.push_back (static_cast<unsigned char> ((high << 4) | low));
    }
    return true;
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
        if (argc != 3)
            return fail ("usage_error", false, "usage: b5_shadergraph_params_packing_probe <msl_source_path> <b5_hex_bytes>");

        std::vector<unsigned char> b5;
        if (! parseHexBytes (argv[2], b5) || b5.size() != 16)
            return fail ("usage_error", false, "b5_hex_bytes must decode to exactly 16 bytes");

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

        id<MTLFunction> function = [library newFunctionWithName:@"my_world_b5_shadergraph_params_packing_probe"];
        if (function == nil)
            return fail ("compile_failed", true, "MSL source must define my_world_b5_shadergraph_params_packing_probe");

        NSError* pipelineError = nil;
        id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&pipelineError];
        if (pipeline == nil)
            return fail ("pipeline_failed", true, errorMessage (pipelineError, "Metal compute pipeline compile failed"));

        std::vector<std::uint32_t> out (8, 0);
        id<MTLBuffer> outBuffer = [device newBufferWithBytes:out.data()
                                                       length:out.size() * sizeof (std::uint32_t)
                                                      options:MTLResourceStorageModeShared];
        id<MTLBuffer> b5Buffer = [device newBufferWithBytes:b5.data() length:b5.size() options:MTLResourceStorageModeShared];
        if (outBuffer == nil || b5Buffer == nil)
            return fail ("render_failed", true, "Metal buffer allocation failed");

        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
        if (commandBuffer == nil)
            return fail ("render_failed", true, "Metal command buffer unavailable");

        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
        if (encoder == nil)
            return fail ("render_failed", true, "Metal compute encoder unavailable");

        [encoder setComputePipelineState:pipeline];
        [encoder setBuffer:outBuffer offset:0 atIndex:0];
        [encoder setBuffer:b5Buffer offset:0 atIndex:7];
        [encoder dispatchThreads:MTLSizeMake (1, 1, 1) threadsPerThreadgroup:MTLSizeMake (1, 1, 1)];
        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];

        if ([commandBuffer status] == MTLCommandBufferStatusError)
            return fail ("render_failed", true, errorMessage ([commandBuffer error], "Metal command buffer failed"));

        std::memcpy (out.data(), [outBuffer contents], out.size() * sizeof (std::uint32_t));

        std::cout
            << "{"
            << "\"status\":\"proven_b5_shadergraph_params_native_packing\","
            << "\"ok\":true,"
            << "\"actualCompilerRan\":true,"
            << "\"actualMetalRan\":true,"
            << "\"message\":\"compiled generated MSL compute probe and read b5 shadergraph Params constant buffer\","
            << "\"outputWords\":";
        printJsonArray (out);
        std::cout << "}\n";
        return 0;
    }
}
'''


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tixl_mesh_draw_b5_native_packing_shell.py <fixture.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    repo_root = Path(__file__).resolve().parents[3]
    errors: list[dict[str, Any]] = []
    trace: list[dict[str, Any]] = [{
        "op": "loadTixlMeshDrawB5NativePackingFixture",
        "fixture": display_path(fixture_path, repo_root),
    }]

    fixture = read_json(fixture_path, errors, "tixl_mesh_draw_b5_native_packing.fixture_read_failed", repo_root)
    if fixture is None:
        result = default_result(None, "blocked_missing_fixture", {}, {}, {}, False, False, False)
        write_text(out_dir / MSL_NAME, MSL_SOURCE)
        publish(out_dir, result, trace, errors)
        return 1

    result, run_trace, run_errors = run_proof(repo_root, fixture_path, fixture)
    trace.extend(run_trace)
    errors.extend(run_errors)
    trace.append({"op": "publishTixlMeshDrawB5NativePackingArtifacts", "ok": result.get("ok") is True and not errors})
    write_text(out_dir / MSL_NAME, MSL_SOURCE)
    publish(out_dir, result, trace, errors)
    return 0 if result.get("ok") is True and not errors else 1


def run_proof(repo_root: Path, fixture_path: Path, fixture: dict[str, Any]) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    graph_id = fixture.get("graphId")

    fixture_errors = validate_fixture(fixture)
    if fixture_errors:
        errors.append({
            "code": "tixl_mesh_draw_b5_native_packing.invalid_fixture_expectations",
            "message": "Fixture expectations must keep the b5 native packing proof bounded.",
            "mismatches": fixture_errors,
        })
        return default_result(graph_id, "blocked_invalid_fixture", {}, {}, {}, False, False, False), trace, errors

    expansion_path = resolve_path(repo_root, fixture_path, fixture.get("b5ShadergraphParamsExpansionArtifact"), DEFAULT_EXPANSION)
    layout_path = resolve_path(repo_root, fixture_path, fixture.get("constantBufferLayoutArtifact"), DEFAULT_LAYOUT)
    pointlights_path = resolve_path(repo_root, fixture_path, fixture.get("pointlightsAndB5PackingArtifact"), DEFAULT_POINTLIGHTS)
    trace.append({
        "op": "resolveInputArtifacts",
        "b5ShadergraphParamsExpansionArtifact": display_path(expansion_path, repo_root),
        "constantBufferLayoutArtifact": display_path(layout_path, repo_root),
        "pointlightsAndB5PackingArtifact": display_path(pointlights_path, repo_root),
    })

    expansion = read_json(expansion_path, errors, "tixl_mesh_draw_b5_native_packing.expansion_read_failed", repo_root)
    layout = read_json(layout_path, errors, "tixl_mesh_draw_b5_native_packing.layout_read_failed", repo_root)
    pointlights = read_json(pointlights_path, errors, "tixl_mesh_draw_b5_native_packing.pointlights_read_failed", repo_root)
    expansion_summary = summarize_artifact(expansion_path, expansion, repo_root)
    layout_summary = summarize_artifact(layout_path, layout, repo_root)
    pointlights_summary = summarize_artifact(pointlights_path, pointlights, repo_root)
    if expansion is None or layout is None or pointlights is None:
        return default_result(
            graph_id,
            "blocked_missing_input_artifact",
            expansion_summary,
            layout_summary,
            pointlights_summary,
            expansion is not None,
            layout is not None,
            pointlights is not None,
        ), trace, errors

    expansion_errors = validate_expansion_artifact(expansion)
    layout_errors = validate_layout_artifact(layout)
    pointlights_errors = validate_pointlights_artifact(pointlights)
    trace.append({
        "op": "validateInputArtifacts",
        "b5ShadergraphParamsExpansionValid": not expansion_errors,
        "constantBufferLayoutValid": not layout_errors,
        "pointlightsAndB5PackingValid": not pointlights_errors,
    })
    if expansion_errors:
        errors.append({
            "code": "tixl_mesh_draw_b5_native_packing.invalid_b5_shadergraph_params_expansion",
            "message": "b5 native packing requires the exact source-backed SphereSDF expansion and no widened upstream claims.",
            "mismatches": expansion_errors,
        })
    if layout_errors:
        errors.append({
            "code": "tixl_mesh_draw_b5_native_packing.invalid_layout_artifact",
            "message": "Layout artifact must keep b5 as Params mapped to candidate Metal buffer(7) without backend completion.",
            "mismatches": layout_errors,
        })
    if pointlights_errors:
        errors.append({
            "code": "tixl_mesh_draw_b5_native_packing.invalid_pointlights_verdict",
            "message": "b5 native packing must start after the b3 PointLights proof and before adapter/backend widening.",
            "mismatches": pointlights_errors,
        })
    if expansion_errors or layout_errors or pointlights_errors:
        status = "blocked_invalid_b5_shadergraph_params_expansion" if expansion_errors else "blocked_invalid_input_artifact"
        return default_result(graph_id, status, expansion_summary, layout_summary, pointlights_summary, not expansion_errors, not layout_errors, not pointlights_errors), trace, errors

    probe_payload, probe_trace, probe_errors = run_native_probe(repo_root, expansion)
    trace.extend(probe_trace)
    if probe_payload is None:
        errors.extend(probe_errors)
        return default_result(graph_id, "blocked_needs_b5_native_packing_probe", expansion_summary, layout_summary, pointlights_summary, True, True, True), trace, errors

    validation_errors = validate_probe_payload(probe_payload)
    trace.append({
        "op": "validateB5PackingProbeReadback",
        "status": probe_payload.get("status"),
        "ok": probe_payload.get("ok"),
        "valid": not validation_errors,
    })
    if validation_errors:
        errors.append({
            "code": "tixl_mesh_draw_b5_native_packing.probe_readback_mismatch",
            "message": "Native Metal b5 packing probe did not match expected size or sentinel values.",
            "mismatches": validation_errors,
        })
        return default_result(graph_id, "blocked_needs_b5_native_packing_probe", expansion_summary, layout_summary, pointlights_summary, True, True, True), trace, errors

    return success_result(graph_id, expansion_summary, layout_summary, pointlights_summary, probe_payload), trace, errors


def run_native_probe(repo_root: Path, expansion: dict[str, Any]) -> tuple[dict[str, Any] | None, list[dict[str, Any]], list[dict[str, Any]]]:
    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    build_dir = Path(tempfile.mkdtemp(prefix="tixl-b5-native-packing-"))
    try:
        b5_bytes = b5_host_bytes_from_expansion(expansion)
        trace.append({
            "op": "packB5HostBytesFromExpansionArtifact",
            "byteCount": len(b5_bytes),
            "source": "expansion.floatBuffer.values + expansion.fields.offsetBytes",
        })
        msl_path = build_dir / MSL_NAME
        source_path = build_dir / "b5_shadergraph_params_packing_probe.mm"
        probe_bin = build_dir / "b5_shadergraph_params_packing_probe"
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
        trace.append({"op": "buildGeneratedB5PackingProbe", "compiler": "xcrun clang++", "exitCode": build.returncode})
        if build.returncode != 0:
            errors.append({
                "code": "tixl_mesh_draw_b5_native_packing.probe_build_failed",
                "message": clean_text(build.stderr or build.stdout or "probe build failed"),
            })
            return None, trace, errors

        run = subprocess.run([str(probe_bin), str(msl_path), b5_bytes.hex()], cwd=repo_root, text=True, capture_output=True)
        trace.append({"op": "runGeneratedB5PackingProbe", "exitCode": run.returncode})
        payload = parse_probe_payload(run.stdout)
        if payload is None:
            errors.append({
                "code": "tixl_mesh_draw_b5_native_packing.probe_output_invalid",
                "message": clean_text(run.stderr or run.stdout or "probe did not emit JSON"),
            })
            return None, trace, errors
        if run.returncode != 0 or payload.get("ok") is not True:
            errors.append(error_from_probe(payload))
            return None, trace, errors
        return payload, trace, errors
    finally:
        shutil.rmtree(build_dir, ignore_errors=True)


def b5_host_bytes_from_expansion(expansion: dict[str, Any]) -> bytes:
    b5 = expansion.get("expansion") if isinstance(expansion.get("expansion"), dict) else {}
    float_buffer = b5.get("floatBuffer") if isinstance(b5.get("floatBuffer"), dict) else {}
    fields = b5.get("fields") if isinstance(b5.get("fields"), list) else []
    values = float_buffer.get("values") if isinstance(float_buffer.get("values"), list) else []
    out = bytearray(EXPECTED_B5_PACKING["sizeBytes"])
    for field in fields:
        if not isinstance(field, dict):
            continue
        offset = field.get("offsetBytes")
        value_range = field.get("floatValueRange")
        if not isinstance(offset, int) or not isinstance(value_range, list) or len(value_range) != 2:
            continue
        start, end = value_range
        if not isinstance(start, int) or not isinstance(end, int):
            continue
        for index, value in enumerate(values[start:end]):
            struct.pack_into("<f", out, offset + index * 4, float(value))
    return bytes(out)


def validate_fixture(fixture: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if fixture.get("graphId") != "fixture.tixl_mesh_draw_b5_native_packing":
        mismatches.append({"field": "graphId", "expected": "fixture.tixl_mesh_draw_b5_native_packing", "actual": fixture.get("graphId")})
    if fixture.get("kind") != "TixlMeshDrawB5NativePackingProof":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawB5NativePackingProof", "actual": fixture.get("kind")})
    if fixture.get("probeRegisters") != ["b5"]:
        mismatches.append({"field": "probeRegisters", "expected": ["b5"], "actual": fixture.get("probeRegisters")})
    expected = fixture.get("expected") if isinstance(fixture.get("expected"), dict) else {}
    if expected.get("status") != "proven_b5_shadergraph_params_native_packing":
        mismatches.append({"field": "expected.status", "expected": "proven_b5_shadergraph_params_native_packing", "actual": expected.get("status")})
    if expected.get("b5NativePacking") != EXPECTED_B5_PACKING:
        mismatches.append({"field": "expected.b5NativePacking", "expected": EXPECTED_B5_PACKING, "actual": expected.get("b5NativePacking")})
    if expected.get("claims") != EXPECTED_CLAIMS_FIXTURE:
        mismatches.append({"field": "expected.claims", "expected": EXPECTED_CLAIMS_FIXTURE, "actual": expected.get("claims")})
    return mismatches


def validate_expansion_artifact(expansion: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if expansion.get("kind") != "TixlMeshDrawB5ShadergraphParamsExpansionVerdict":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawB5ShadergraphParamsExpansionVerdict", "actual": expansion.get("kind")})
    if expansion.get("ok") is not True:
        mismatches.append({"field": "ok", "expected": True, "actual": expansion.get("ok")})
    if expansion.get("status") != "expanded_b5_shadergraph_params_source_backed":
        mismatches.append({"field": "status", "expected": "expanded_b5_shadergraph_params_source_backed", "actual": expansion.get("status")})

    claims = expansion.get("claims") if isinstance(expansion.get("claims"), dict) else {}
    for field in ("b5ShadergraphParamsExpanded", "b5FieldsSourceBacked", "b3PointLightsPackingProven"):
        if claims.get(field) is not True:
            mismatches.append({"field": f"claims.{field}", "expected": True, "actual": claims.get(field)})
    for field in ("b5NativePackingReady", "constantBufferAdapterComplete", "textureSamplerMapping", "fullPbrResourceBinding", "backendReplacementReady", "hlslToMslTranslation", "tixlRuntimeParity", "pbrVisualCorrectness"):
        if claims.get(field) is not False:
            mismatches.append({"field": f"claims.{field}", "expected": False, "actual": claims.get(field)})
    for field, value in claims.items():
        if field not in {
            "sourceAuditArtifactConsumed",
            "constantBufferLayoutArtifactConsumed",
            "pointlightsAndB5PackingArtifactConsumed",
            "b3PointLightsPackingProven",
            "b5ShadergraphParamsExpanded",
            "b5FieldsSourceBacked",
            "b5NativePackingReady",
            "constantBufferAdapterComplete",
            "textureSamplerMapping",
            "fullPbrResourceBinding",
            "backendReplacementReady",
            "hlslToMslTranslation",
            "tixlRuntimeParity",
            "pbrVisualCorrectness",
        } and value is True:
            mismatches.append({"field": f"claims.{field}", "expected": "no extra true claims", "actual": True})

    b5 = expansion.get("expansion") if isinstance(expansion.get("expansion"), dict) else {}
    expected_root = {
        "type": "tixl.field.generate.sdf.SphereSDF",
        "title": "my_SphereSDF",
        "tixlSymbolChildId": "04426d9c-b039-4a92-9b1f-61186b4df2e5",
        "prefix": "SphereSDF_nG1CBDm_",
    }
    if b5.get("register") != "b5" or b5.get("name") != "Params" or b5.get("semanticRole") != "shadergraph_duplicate_params":
        mismatches.append({"field": "expansion.identity", "expected": {"register": "b5", "name": "Params", "semanticRole": "shadergraph_duplicate_params"}, "actual": {"register": b5.get("register"), "name": b5.get("name"), "semanticRole": b5.get("semanticRole")}})
    if b5.get("templateHole") != "FLOAT_PARAMS" or b5.get("expanded") is not True:
        mismatches.append({"field": "expansion.templateHole", "expected": {"templateHole": "FLOAT_PARAMS", "expanded": True}, "actual": {"templateHole": b5.get("templateHole"), "expanded": b5.get("expanded")}})
    if b5.get("rootNode") != expected_root:
        mismatches.append({"field": "expansion.rootNode", "expected": expected_root, "actual": b5.get("rootNode")})
    if b5.get("fields") != EXPECTED_EXPANSION_FIELDS:
        mismatches.append({"field": "expansion.fields", "expected": EXPECTED_EXPANSION_FIELDS, "actual": b5.get("fields")})
    float_buffer = b5.get("floatBuffer") if isinstance(b5.get("floatBuffer"), dict) else {}
    if float_buffer.get("values") != [-1.4845504, 0.0, 0.54366434, 0.5]:
        mismatches.append({"field": "expansion.floatBuffer.values", "expected": [-1.4845504, 0.0, 0.54366434, 0.5], "actual": float_buffer.get("values")})
    if float_buffer.get("sizeBytes") != 16:
        mismatches.append({"field": "expansion.floatBuffer.sizeBytes", "expected": 16, "actual": float_buffer.get("sizeBytes")})
    proof_boundary = b5.get("proofBoundary") if isinstance(b5.get("proofBoundary"), dict) else {}
    if proof_boundary.get("sourceBacked") is not True:
        mismatches.append({"field": "expansion.proofBoundary.sourceBacked", "expected": True, "actual": proof_boundary.get("sourceBacked")})
    if proof_boundary.get("nativeB5PackingProven") is not False:
        mismatches.append({"field": "expansion.proofBoundary.nativeB5PackingProven", "expected": False, "actual": proof_boundary.get("nativeB5PackingProven")})
    return mismatches


def validate_layout_artifact(layout: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if layout.get("kind") != "TixlMeshDrawConstantBufferLayoutProof":
        mismatches.append({"field": "layout.kind", "expected": "TixlMeshDrawConstantBufferLayoutProof", "actual": layout.get("kind")})
    if layout.get("ok") is not True:
        mismatches.append({"field": "layout.ok", "expected": True, "actual": layout.get("ok")})
    buffers = {item.get("register"): item for item in layout.get("constantBuffers", []) if isinstance(item, dict)}
    b5 = buffers.get("b5") if isinstance(buffers.get("b5"), dict) else {}
    if b5.get("name") != "Params" or b5.get("semanticRole") != "shadergraph_duplicate_params":
        mismatches.append({"field": "constantBuffers.b5", "expected": {"name": "Params", "semanticRole": "shadergraph_duplicate_params"}, "actual": {"name": b5.get("name"), "semanticRole": b5.get("semanticRole")}})
    binding_policy = layout.get("bindingPolicy") if isinstance(layout.get("bindingPolicy"), dict) else {}
    mapping = {
        item.get("sourceRegister"): item.get("metalBuffer")
        for item in binding_policy.get("candidateMapping", [])
        if isinstance(item, dict)
    }
    if mapping.get("b5") != 7:
        mismatches.append({"field": "bindingPolicy.candidateMapping.b5", "expected": 7, "actual": mapping.get("b5")})
    if binding_policy.get("backendBindingImplemented") is not False:
        mismatches.append({"field": "bindingPolicy.backendBindingImplemented", "expected": False, "actual": binding_policy.get("backendBindingImplemented")})
    claims = layout.get("claims") if isinstance(layout.get("claims"), dict) else {}
    for field in ("constantBufferAdapterComplete", "textureSamplerMapping", "fullPbrResourceBinding", "backendReplacementReady", "hlslToMslTranslation", "tixlRuntimeParity", "pbrVisualCorrectness"):
        if claims.get(field) not in (None, False):
            mismatches.append({"field": f"layout.claims.{field}", "expected": False, "actual": claims.get(field)})
    return mismatches


def validate_pointlights_artifact(pointlights: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if pointlights.get("kind") != "TixlMeshDrawPointLightsAndB5PackingVerdict":
        mismatches.append({"field": "pointlights.kind", "expected": "TixlMeshDrawPointLightsAndB5PackingVerdict", "actual": pointlights.get("kind")})
    if pointlights.get("ok") is not True:
        mismatches.append({"field": "pointlights.ok", "expected": True, "actual": pointlights.get("ok")})
    if pointlights.get("status") != "proven_b3_pointlights_packing_b5_blocked":
        mismatches.append({"field": "pointlights.status", "expected": "proven_b3_pointlights_packing_b5_blocked", "actual": pointlights.get("status")})
    claims = pointlights.get("claims") if isinstance(pointlights.get("claims"), dict) else {}
    for field in ("actualMetalPointLightProbeRan", "b3PointLightsPackingProven", "b5RequiresShadergraphParamExpansion"):
        if claims.get(field) is not True:
            mismatches.append({"field": f"pointlights.claims.{field}", "expected": True, "actual": claims.get(field)})
    for field in ("b5DuplicateParamsPackingProven", "constantBufferAdapterComplete", "textureSamplerMapping", "fullPbrResourceBinding", "backendReplacementReady", "hlslToMslTranslation", "tixlRuntimeParity", "pbrVisualCorrectness"):
        if claims.get(field) is not False:
            mismatches.append({"field": f"pointlights.claims.{field}", "expected": False, "actual": claims.get(field)})
    for field, value in claims.items():
        if field not in {
            "priorNativePackingArtifactConsumed",
            "constantBufferLayoutArtifactConsumed",
            "actualMetalPointLightProbeRan",
            "b3PointLightsPackingProven",
            "b5DuplicateParamsPackingProven",
            "b5RequiresShadergraphParamExpansion",
            "constantBufferAdapterComplete",
            "textureSamplerMapping",
            "fullPbrResourceBinding",
            "backendReplacementReady",
            "hlslToMslTranslation",
            "tixlRuntimeParity",
            "pbrVisualCorrectness",
        } and value is True:
            mismatches.append({"field": f"pointlights.claims.{field}", "expected": "no extra true claims", "actual": True})
    return mismatches


def validate_probe_payload(payload: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    expected_words = [
        16,
        float_word(-1.4845504),
        float_word(0.0),
        float_word(0.54366434),
        float_word(0.5),
        0,
        0,
        0,
    ]
    if payload.get("status") != "proven_b5_shadergraph_params_native_packing" or payload.get("ok") is not True:
        mismatches.append({"field": "probe.status", "expected": "proven_b5_shadergraph_params_native_packing", "actual": payload.get("status")})
    if payload.get("actualCompilerRan") is not True or payload.get("actualMetalRan") is not True:
        mismatches.append({"field": "probe.actualMetalRan", "expected": True, "actual": {"actualCompilerRan": payload.get("actualCompilerRan"), "actualMetalRan": payload.get("actualMetalRan")}})
    if payload.get("outputWords") != expected_words:
        mismatches.append({"field": "probe.outputWords", "expected": expected_words, "actual": payload.get("outputWords")})
    return mismatches


def default_result(
    graph_id: str | None,
    status: str,
    expansion_summary: dict[str, Any],
    layout_summary: dict[str, Any],
    pointlights_summary: dict[str, Any],
    expansion_consumed: bool,
    layout_consumed: bool,
    pointlights_consumed: bool,
) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawB5NativePackingProof",
        "graphId": graph_id,
        "ok": False,
        "status": status,
        "inputArtifacts": {
            "b5ShadergraphParamsExpansion": expansion_summary,
            "constantBufferLayout": layout_summary,
            "pointlightsAndB5Packing": pointlights_summary,
        },
        "provenNativePacking": {},
        "claims": claims(False, expansion_consumed, layout_consumed, pointlights_consumed),
    }


def success_result(
    graph_id: str | None,
    expansion_summary: dict[str, Any],
    layout_summary: dict[str, Any],
    pointlights_summary: dict[str, Any],
    probe_payload: dict[str, Any],
) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawB5NativePackingProof",
        "graphId": graph_id,
        "ok": True,
        "status": "proven_b5_shadergraph_params_native_packing",
        "inputArtifacts": {
            "b5ShadergraphParamsExpansion": expansion_summary,
            "constantBufferLayout": layout_summary,
            "pointlightsAndB5Packing": pointlights_summary,
        },
        "provenNativePacking": EXPECTED_B5_PACKING,
        "probe": {
            "generatedMslArtifact": MSL_NAME,
            "metalBuffer": 7,
            "outputWords": probe_payload.get("outputWords"),
            "message": probe_payload.get("message"),
        },
        "claims": claims(True, True, True, True),
    }


def claims(proven: bool, expansion_consumed: bool, layout_consumed: bool, pointlights_consumed: bool) -> dict[str, Any]:
    return {
        "b5ShadergraphParamsExpansionArtifactConsumed": expansion_consumed,
        "constantBufferLayoutArtifactConsumed": layout_consumed,
        "pointlightsAndB5PackingArtifactConsumed": pointlights_consumed,
        "b3PointLightsPackingProven": pointlights_consumed,
        "b5ShadergraphParamsExpanded": expansion_consumed,
        "b5FieldsSourceBacked": expansion_consumed,
        "actualMetalB5PackingProbeRan": proven,
        "b5NativePackingProven": proven,
        "constantBufferAdapterComplete": False,
        "textureSamplerMapping": False,
        "fullPbrResourceBinding": False,
        "backendReplacementReady": False,
        "hlslToMslTranslation": False,
        "tixlRuntimeParity": False,
        "pbrVisualCorrectness": False,
    }


def summarize_artifact(path: Path, payload: dict[str, Any] | None, repo_root: Path) -> dict[str, Any]:
    summary = {"path": display_path(path, repo_root)}
    if isinstance(payload, dict):
        for key in ("kind", "ok", "status", "selectedStrategy"):
            if key in payload:
                summary[key] = payload[key]
    return summary


def resolve_path(repo_root: Path, fixture_path: Path, maybe_path: Any, default_path: str) -> Path:
    raw = maybe_path if isinstance(maybe_path, str) and maybe_path else default_path
    path = Path(raw).expanduser()
    if path.is_absolute():
        return path.resolve()
    repo_candidate = repo_root / path
    if repo_candidate.exists():
        return repo_candidate.resolve()
    return (fixture_path.parent / path).resolve()


def read_json(path: Path, errors: list[dict[str, Any]], code: str, repo_root: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": display_path(path, repo_root), "message": clean_text(str(exc))})
        return None


def parse_probe_payload(text: str) -> dict[str, Any] | None:
    try:
        payload = json.loads(text.strip())
    except Exception:
        return None
    return payload if isinstance(payload, dict) else None


def error_from_probe(payload: dict[str, Any]) -> dict[str, Any]:
    status = payload.get("status")
    code_by_status = {
        "blocked_metal_device_unavailable": "tixl_mesh_draw_b5_native_packing.device_unavailable",
        "compile_failed": "tixl_mesh_draw_b5_native_packing.compile_failed",
        "pipeline_failed": "tixl_mesh_draw_b5_native_packing.pipeline_failed",
        "render_failed": "tixl_mesh_draw_b5_native_packing.render_failed",
    }
    return {
        "code": code_by_status.get(status, "tixl_mesh_draw_b5_native_packing.probe_failed"),
        "status": status,
        "message": clean_text(str(payload.get("message") or "b5 native packing probe failed")),
    }


def float_word(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


def publish(out_dir: Path, result: dict[str, Any], trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / TRACE_NAME, trace)
    write_json(out_dir / ERRORS_NAME, errors)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf8")


def clean_text(text: str) -> str:
    return " ".join(str(text).split())


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo_root))
    except ValueError:
        return str(path)


if __name__ == "__main__":
    raise SystemExit(main())
