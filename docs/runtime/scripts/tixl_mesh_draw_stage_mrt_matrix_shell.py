#!/usr/bin/env python3
"""
Publish a source-backed TiXL mesh Draw stage/MRT/matrix proof.

This lane parses the donor HLSL and validates prior bounded artifacts. It does
not build Metal and does not claim HLSL-to-MSL translation.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


RESULT_NAME = "tixl_mesh_draw_stage_mrt_matrix_result.json"
TRACE_NAME = "tixl_mesh_draw_stage_mrt_matrix_trace.json"
ERRORS_NAME = "tixl_mesh_draw_stage_mrt_matrix_errors.json"
MSL_NAME = "generated_stage_mrt_matrix_probe.metal"

DEFAULT_DONOR_HLSL = "external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Draw.hlsl"
DEFAULT_SOURCE_AUDIT = "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json"
DEFAULT_T8 = "docs/runtime/artifacts/tixl_mesh_draw_shadergraph_resources_expansion/tixl_mesh_draw_shadergraph_resources_expansion_result.json"
DEFAULT_B5_NATIVE = "docs/runtime/artifacts/tixl_mesh_draw_b5_native_packing/tixl_mesh_draw_b5_native_packing_result.json"
DEFAULT_TEXTURE_SAMPLER = "docs/runtime/artifacts/tixl_mesh_draw_texture_sampler_binding/tixl_mesh_draw_texture_sampler_binding_result.json"

EXPECTED_CLAIMS = {
    "donorHlslParsed": True,
    "sourceAuditArtifactConsumed": True,
    "shadergraphResourcesExpansionArtifactConsumed": True,
    "b5NativePackingArtifactConsumed": True,
    "textureSamplerBindingArtifactConsumed": True,
    "vertexStageSemanticProven": True,
    "pixelStageSemanticProven": True,
    "mrtTarget0ColorProven": True,
    "mrtTarget1NormalProven": True,
    "fragmentDerivativeSemanticsPresent": True,
    "alphaDiscardSemanticsPresent": True,
    "d3dMulVectorMatrixConventionPresent": True,
    "explicitMetalStageMrtMatrixProbeRan": True,
    "explicitMetalStageInProven": True,
    "explicitMetalMrtWriteProven": True,
    "explicitMetalVectorMatrixConventionProven": True,
    "handwrittenMeshDrawAdapterStageMrtMatrixProof": True,
    "sourceBackedOnly": True,
    "actualMetalProbeRan": True,
    "tixlDonorHlslMetalProbeRan": False,
    "hlslToMslTranslation": False,
    "fullPbrResourceBinding": False,
    "textureCubeSampleLevelGetDimensionsProven": False,
    "backendReplacementReady": False,
    "tixlRuntimeParity": False,
    "pbrVisualCorrectness": False,
    "rendererIntegrationComplete": False,
    "constantBufferAdapterComplete": False,
    "fullTextureSamplerMapping": False,
}

FORBIDDEN_TRUE_CLAIMS = {
    "tixlDonorHlslMetalProbeRan",
    "hlslToMslTranslation",
    "hlslToMslTranslationProven",
    "fullPbrResourceBinding",
    "textureCubeSampleLevelGetDimensionsProven",
    "backendReplacementReady",
    "tixlRuntimeParity",
    "tixlParity",
    "pbrVisualCorrectness",
    "rendererIntegrationComplete",
    "constantBufferAdapterComplete",
    "fullTextureSamplerMapping",
    "nativeCompileParity",
}

MSL_SOURCE = """#include <metal_stdlib>
using namespace metal;

struct VertexOut
{
    float4 position [[position]];
    float stageSentinel;
    float matrixSentinel;
};

struct FragmentOut
{
    uint4 color [[color(0)]];
    uint4 normal [[color(1)]];
};

static float4 my_world_mul_vector_matrix(float4 v, float4x4 m)
{
    return float4(
        dot(v, float4(m[0].x, m[1].x, m[2].x, m[3].x)),
        dot(v, float4(m[0].y, m[1].y, m[2].y, m[3].y)),
        dot(v, float4(m[0].z, m[1].z, m[2].z, m[3].z)),
        dot(v, float4(m[0].w, m[1].w, m[2].w, m[3].w)));
}

vertex VertexOut my_world_stage_mrt_matrix_vertex(uint vertexId [[vertex_id]])
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };
    float4x4 matrix = float4x4(
        float4( 1.0,  2.0,  3.0,  4.0),
        float4( 5.0,  6.0,  7.0,  8.0),
        float4( 9.0, 10.0, 11.0, 12.0),
        float4(13.0, 14.0, 15.0, 16.0));
    float4 product = my_world_mul_vector_matrix(float4(1.0, 2.0, 3.0, 4.0), matrix);

    VertexOut out;
    out.position = float4(positions[vertexId], 0.0, 1.0);
    out.stageSentinel = 13.0;
    out.matrixSentinel = product.x; // 90 for row-vector * matrix convention.
    return out;
}

fragment FragmentOut my_world_stage_mrt_matrix_fragment(VertexOut in [[stage_in]])
{
    FragmentOut out;
    out.color = uint4(uint(round(in.stageSentinel)), uint(round(in.matrixSentinel)), 111u, 255u);
    out.normal = uint4(31u, 37u, 41u, 255u);
    return out;
}
"""

PROBE_CPP_SOURCE = r'''#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <iostream>
#include <sstream>
#include <string>

static int emit(const char* status, bool ok, bool compiler, bool metal, const std::string& message,
                unsigned char c0r = 0, unsigned char c0g = 0, unsigned char c0b = 0, unsigned char c0a = 0,
                unsigned char c1r = 0, unsigned char c1g = 0, unsigned char c1b = 0, unsigned char c1a = 0)
{
    std::cout
        << "{"
        << "\"status\":\"" << status << "\","
        << "\"ok\":" << (ok ? "true" : "false") << ","
        << "\"actualCompilerRan\":" << (compiler ? "true" : "false") << ","
        << "\"actualMetalRan\":" << (metal ? "true" : "false") << ","
        << "\"message\":\"" << message << "\","
        << "\"target0\":[" << int(c0r) << "," << int(c0g) << "," << int(c0b) << "," << int(c0a) << "],"
        << "\"target1\":[" << int(c1r) << "," << int(c1g) << "," << int(c1b) << "," << int(c1a) << "]"
        << "}\n";
    return ok ? 0 : 1;
}

int main(int argc, const char** argv)
{
    @autoreleasepool
    {
        if (argc != 2)
            return emit("usage_error", false, false, false, "usage: stage_mrt_matrix_probe <msl_source_path>");

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device)
            return emit("blocked_metal_device_unavailable", false, false, false, "Metal device unavailable");
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue)
            return emit("blocked_metal_device_unavailable", false, false, false, "Metal command queue unavailable");

        NSError* readError = nil;
        NSString* path = [NSString stringWithUTF8String:argv[1]];
        NSString* source = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&readError];
        if (!source)
            return emit("compile_failed", false, false, true, "MSL source read failed");

        NSError* compileError = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&compileError];
        if (!library)
        {
            std::string diagnostic = compileError ? [[compileError localizedDescription] UTF8String] : "MSL compile failed";
            return emit("compile_failed", false, true, true, diagnostic);
        }
        id<MTLFunction> vertex = [library newFunctionWithName:@"my_world_stage_mrt_matrix_vertex"];
        id<MTLFunction> fragment = [library newFunctionWithName:@"my_world_stage_mrt_matrix_fragment"];
        if (!vertex || !fragment)
            return emit("compile_failed", false, true, true, "MSL source missing expected vertex or fragment function");

        MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
        descriptor.vertexFunction = vertex;
        descriptor.fragmentFunction = fragment;
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Uint;
        descriptor.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA8Uint;
        NSError* pipelineError = nil;
        id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&pipelineError];
        if (!pipeline)
        {
            std::string diagnostic = pipelineError ? [[pipelineError localizedDescription] UTF8String] : "render pipeline failed";
            return emit("pipeline_failed", false, true, true, diagnostic);
        }

        MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Uint width:1 height:1 mipmapped:NO];
        textureDescriptor.usage = MTLTextureUsageRenderTarget;
        textureDescriptor.storageMode = MTLStorageModeShared;
        id<MTLTexture> target0 = [device newTextureWithDescriptor:textureDescriptor];
        id<MTLTexture> target1 = [device newTextureWithDescriptor:textureDescriptor];
        if (!target0 || !target1)
            return emit("render_failed", false, true, true, "render target allocation failed");

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target0;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
        pass.colorAttachments[1].texture = target1;
        pass.colorAttachments[1].loadAction = MTLLoadActionClear;
        pass.colorAttachments[1].storeAction = MTLStoreActionStore;
        pass.colorAttachments[1].clearColor = MTLClearColorMake(0, 0, 0, 0);

        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
        [encoder setRenderPipelineState:pipeline];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];
        if (commandBuffer.status != MTLCommandBufferStatusCompleted)
            return emit("render_failed", false, true, true, "command buffer did not complete");

        unsigned char a[4] = {0, 0, 0, 0};
        unsigned char b[4] = {0, 0, 0, 0};
        MTLRegion region = MTLRegionMake2D(0, 0, 1, 1);
        [target0 getBytes:a bytesPerRow:4 fromRegion:region mipmapLevel:0];
        [target1 getBytes:b bytesPerRow:4 fromRegion:region mipmapLevel:0];

        bool ok = a[0] == 13 && a[1] == 90 && a[2] == 111 && a[3] == 255
               && b[0] == 31 && b[1] == 37 && b[2] == 41 && b[3] == 255;
        return emit(ok ? "proven_explicit_metal_stage_mrt_matrix_probe" : "readback_mismatch",
                    ok, true, true,
                    ok ? "explicit Metal render probe proved vertex_id, stage_in, MRT color attachments, and vector-matrix convention" : "unexpected render target readback",
                    a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
    }
}
'''


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tixl_mesh_draw_stage_mrt_matrix_shell.py <fixture.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    repo_root = Path(__file__).resolve().parents[3]
    trace: list[dict[str, Any]] = [{"op": "loadTixlMeshDrawStageMrtMatrixFixture", "fixture": display_path(fixture_path, repo_root)}]
    errors: list[dict[str, Any]] = []

    fixture = read_json(fixture_path, errors, "tixl_mesh_draw_stage_mrt_matrix.fixture_read_failed", repo_root)
    if fixture is None:
        result = result_payload(None, "blocked_missing_fixture", {}, {}, {}, {}, {}, {}, {}, False)
        publish(out_dir, result, trace, errors)
        return 1

    result, run_trace, run_errors = run_proof(repo_root, fixture_path, fixture)
    trace.extend(run_trace)
    errors.extend(run_errors)
    trace.append({"op": "publishTixlMeshDrawStageMrtMatrixArtifacts", "ok": result.get("ok") is True and not errors})
    publish(out_dir, result, trace, errors)
    return 0 if result.get("ok") is True and not errors else 1


def run_proof(repo_root: Path, fixture_path: Path, fixture: dict[str, Any]) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    graph_id = fixture.get("graphId")

    fixture_errors = validate_fixture(fixture)
    if fixture_errors:
        errors.append({
            "code": "tixl_mesh_draw_stage_mrt_matrix.invalid_fixture_expectations",
            "message": "Fixture expectations must keep the stage/MRT/matrix proof bounded.",
            "mismatches": fixture_errors,
        })
        return result_payload(graph_id, "blocked_invalid_fixture_expectations", {}, {}, {}, {}, {}, {}, {}, False), trace, errors

    paths = {
        "donorHlsl": resolve_path(repo_root, fixture_path, fixture.get("donorHlsl"), DEFAULT_DONOR_HLSL),
        "sourceAudit": resolve_path(repo_root, fixture_path, fixture.get("sourceAuditArtifact"), DEFAULT_SOURCE_AUDIT),
        "shadergraphResourcesExpansion": resolve_path(repo_root, fixture_path, fixture.get("shadergraphResourcesExpansionArtifact"), DEFAULT_T8),
        "b5NativePacking": resolve_path(repo_root, fixture_path, fixture.get("b5NativePackingArtifact"), DEFAULT_B5_NATIVE),
        "textureSamplerBinding": resolve_path(repo_root, fixture_path, fixture.get("textureSamplerBindingArtifact"), DEFAULT_TEXTURE_SAMPLER),
    }
    trace.append({"op": "resolveInputArtifacts", **{key: display_path(value, repo_root) for key, value in paths.items()}})

    source_audit = read_json(paths["sourceAudit"], errors, "tixl_mesh_draw_stage_mrt_matrix.source_audit_read_failed", repo_root)
    t8 = read_json(paths["shadergraphResourcesExpansion"], errors, "tixl_mesh_draw_stage_mrt_matrix.shadergraph_resources_read_failed", repo_root)
    b5 = read_json(paths["b5NativePacking"], errors, "tixl_mesh_draw_stage_mrt_matrix.b5_native_packing_read_failed", repo_root)
    texture_sampler = read_json(paths["textureSamplerBinding"], errors, "tixl_mesh_draw_stage_mrt_matrix.texture_sampler_binding_read_failed", repo_root)
    donor_source = read_text(paths["donorHlsl"], errors, "tixl_mesh_draw_stage_mrt_matrix.donor_hlsl_read_failed", repo_root)

    artifacts = {
        "sourceAudit": summarize_artifact(paths["sourceAudit"], source_audit, repo_root),
        "shadergraphResourcesExpansion": summarize_artifact(paths["shadergraphResourcesExpansion"], t8, repo_root),
        "b5NativePacking": summarize_artifact(paths["b5NativePacking"], b5, repo_root),
        "textureSamplerBinding": summarize_artifact(paths["textureSamplerBinding"], texture_sampler, repo_root),
    }
    source_summary = {"path": display_path(paths["donorHlsl"], repo_root)}
    trace.append({
        "op": "readInputArtifactsAndDonorSource",
        "sourceAuditRead": source_audit is not None,
        "shadergraphResourcesExpansionRead": t8 is not None,
        "b5NativePackingRead": b5 is not None,
        "textureSamplerBindingRead": texture_sampler is not None,
        "donorHlslRead": donor_source is not None,
    })
    if None in (source_audit, t8, b5, texture_sampler, donor_source):
        return result_payload(graph_id, "blocked_missing_input", artifacts, source_summary, {}, {}, {}, {}, {}, False), trace, errors

    artifact_errors = []
    artifact_errors.extend(validate_source_audit(source_audit))
    artifact_errors.extend(validate_t8_artifact(t8))
    artifact_errors.extend(validate_b5_artifact(b5))
    artifact_errors.extend(validate_texture_sampler_artifact(texture_sampler))
    donor_eval = evaluate_donor_hlsl(donor_source)
    trace.append({
        "op": "validateInputArtifacts",
        "sourceAuditValid": not validate_source_audit(source_audit),
        "shadergraphResourcesExpansionValid": not validate_t8_artifact(t8),
        "b5NativePackingValid": not validate_b5_artifact(b5),
        "textureSamplerBindingValid": not validate_texture_sampler_artifact(texture_sampler),
    })
    trace.append({
        "op": "parseDonorHlslStageMrtMatrixSemantics",
        "valid": not donor_eval["errors"],
        "foundFacts": sorted([key for key, value in donor_eval["facts"].items() if value is True]),
    })

    if artifact_errors:
        errors.append({
            "code": "tixl_mesh_draw_stage_mrt_matrix.invalid_input_artifact",
            "message": "Consumed upstream artifact is missing or has widened claims.",
            "mismatches": artifact_errors,
        })
        return result_payload(graph_id, "blocked_invalid_input_artifact", artifacts, source_summary, donor_eval["facts"], donor_eval["stageSemantics"], donor_eval["mrtSemantics"], donor_eval["matrixSemantics"], {}, False), trace, errors
    if donor_eval["errors"]:
        errors.append({
            "code": "tixl_mesh_draw_stage_mrt_matrix.invalid_donor_stage_mrt_matrix_semantics",
            "message": "TiXL mesh Draw donor source no longer proves the required stage/MRT/matrix semantics.",
            "mismatches": donor_eval["errors"],
        })
        return result_payload(graph_id, "blocked_invalid_donor_semantics", artifacts, source_summary, donor_eval["facts"], donor_eval["stageSemantics"], donor_eval["mrtSemantics"], donor_eval["matrixSemantics"], {}, True), trace, errors

    probe_payload, probe_trace, probe_errors = run_native_probe(repo_root)
    trace.extend(probe_trace)
    if probe_payload is None:
        errors.extend(probe_errors)
        return result_payload(graph_id, "blocked_needs_explicit_metal_stage_mrt_matrix_probe", artifacts, source_summary, donor_eval["facts"], donor_eval["stageSemantics"], donor_eval["mrtSemantics"], donor_eval["matrixSemantics"], {}, True), trace, errors
    probe_validation_errors = validate_probe_payload(probe_payload)
    trace.append({
        "op": "validateExplicitMetalStageMrtMatrixProbe",
        "status": probe_payload.get("status"),
        "valid": not probe_validation_errors,
    })
    if probe_validation_errors:
        errors.append({
            "code": "tixl_mesh_draw_stage_mrt_matrix.probe_readback_mismatch",
            "message": "Explicit Metal stage/MRT/matrix probe did not return expected sentinel readback.",
            "mismatches": probe_validation_errors,
        })
        return result_payload(graph_id, "blocked_needs_explicit_metal_stage_mrt_matrix_probe", artifacts, source_summary, donor_eval["facts"], donor_eval["stageSemantics"], donor_eval["mrtSemantics"], donor_eval["matrixSemantics"], probe_payload, True), trace, errors

    trace.append({
        "op": "proveStageMrtMatrixSemanticsForHandwrittenAdapter",
        "sourceBackedOnly": True,
        "actualMetalProbeRan": True,
        "tixlDonorHlslMetalProbeRan": False,
    })
    return result_payload(
        graph_id,
        "proven_tixl_mesh_draw_stage_mrt_matrix_semantics",
        artifacts,
        source_summary,
        donor_eval["facts"],
        donor_eval["stageSemantics"],
        donor_eval["mrtSemantics"],
        donor_eval["matrixSemantics"],
        probe_payload,
        True,
        ok=True,
    ), trace, errors


def validate_fixture(fixture: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if fixture.get("graphId") != "fixture.tixl_mesh_draw_stage_mrt_matrix":
        mismatches.append({"field": "graphId", "expected": "fixture.tixl_mesh_draw_stage_mrt_matrix", "actual": fixture.get("graphId")})
    if fixture.get("kind") != "TixlMeshDrawStageMrtMatrixProof":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawStageMrtMatrixProof", "actual": fixture.get("kind")})
    expected = fixture.get("expected") if isinstance(fixture.get("expected"), dict) else {}
    if expected.get("status") != "proven_tixl_mesh_draw_stage_mrt_matrix_semantics":
        mismatches.append({"field": "expected.status", "expected": "proven_tixl_mesh_draw_stage_mrt_matrix_semantics", "actual": expected.get("status")})
    claims = expected.get("claims") if isinstance(expected.get("claims"), dict) else {}
    if claims != EXPECTED_CLAIMS:
        mismatches.append({"field": "expected.claims", "expected": EXPECTED_CLAIMS, "actual": claims})
    for field in FORBIDDEN_TRUE_CLAIMS:
        if claims.get(field) is True:
            mismatches.append({"field": f"expected.claims.{field}", "expected": False, "actual": True})
    boundary = fixture.get("adapterBoundary") if isinstance(fixture.get("adapterBoundary"), dict) else {}
    if boundary.get("route") != "handwritten_explicit_msl_adapter":
        mismatches.append({"field": "adapterBoundary.route", "expected": "handwritten_explicit_msl_adapter", "actual": boundary.get("route")})
    if boundary.get("proofType") != "source_backed_semantics_only":
        mismatches.append({"field": "adapterBoundary.proofType", "expected": "source_backed_semantics_only", "actual": boundary.get("proofType")})
    return mismatches


def validate_source_audit(artifact: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches = validate_artifact_header(artifact, "TixlMeshDrawShaderSourceAudit", "audited_tixl_mesh_draw_source", "sourceAudit")
    entry_points = artifact.get("entryPoints") if isinstance(artifact.get("entryPoints"), dict) else {}
    if entry_points.get("vsMain", {}).get("found") is not True:
        mismatches.append({"field": "sourceAudit.entryPoints.vsMain.found", "expected": True, "actual": entry_points.get("vsMain")})
    if entry_points.get("psMain", {}).get("found") is not True:
        mismatches.append({"field": "sourceAudit.entryPoints.psMain.found", "expected": True, "actual": entry_points.get("psMain")})
    mismatches.extend(validate_no_widened_claims(artifact, "sourceAudit.claims"))
    return mismatches


def validate_t8_artifact(artifact: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches = validate_artifact_header(artifact, "TixlMeshDrawShadergraphResourcesExpansionProof", "proven_empty_t8_shadergraph_resources_for_sphere_sdf_fixture", "shadergraphResourcesExpansion")
    claims = artifact.get("claims") if isinstance(artifact.get("claims"), dict) else {}
    for field in ("shadergraphResourcesExpansionProven", "sourceFilesValidated", "resourcesHookFound", "stageAppendBehaviorSourceValidated"):
        if claims.get(field) is not True:
            mismatches.append({"field": f"shadergraphResourcesExpansion.claims.{field}", "expected": True, "actual": claims.get(field)})
    mismatches.extend(validate_no_widened_claims(artifact, "shadergraphResourcesExpansion.claims"))
    return mismatches


def validate_b5_artifact(artifact: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches = validate_artifact_header(artifact, "TixlMeshDrawB5NativePackingProof", "proven_b5_shadergraph_params_native_packing", "b5NativePacking")
    claims = artifact.get("claims") if isinstance(artifact.get("claims"), dict) else {}
    for field in ("b5ShadergraphParamsExpanded", "b5FieldsSourceBacked", "b5NativePackingProven"):
        if claims.get(field) is not True:
            mismatches.append({"field": f"b5NativePacking.claims.{field}", "expected": True, "actual": claims.get(field)})
    mismatches.extend(validate_no_widened_claims(artifact, "b5NativePacking.claims"))
    return mismatches


def validate_texture_sampler_artifact(artifact: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches = validate_artifact_header(artifact, "TixlMeshDrawTextureSamplerBindingProof", "proven_tixl_mesh_draw_texture_sampler_binding", "textureSamplerBinding")
    claims = artifact.get("claims") if isinstance(artifact.get("claims"), dict) else {}
    if claims.get("boundedTextureSamplerMappingProven") is not True:
        mismatches.append({"field": "textureSamplerBinding.claims.boundedTextureSamplerMappingProven", "expected": True, "actual": claims.get("boundedTextureSamplerMappingProven")})
    mismatches.extend(validate_no_widened_claims(artifact, "textureSamplerBinding.claims"))
    return mismatches


def run_native_probe(repo_root: Path) -> tuple[dict[str, Any] | None, list[dict[str, Any]], list[dict[str, Any]]]:
    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    build_dir = Path(tempfile.mkdtemp(prefix="tixl-stage-mrt-matrix-"))
    try:
        msl_path = build_dir / MSL_NAME
        source_path = build_dir / "stage_mrt_matrix_probe.mm"
        probe_bin = build_dir / "stage_mrt_matrix_probe"
        write_text(msl_path, MSL_SOURCE)
        write_text(source_path, PROBE_CPP_SOURCE)
        trace.append({
            "op": "writeExplicitMetalStageMrtMatrixProbe",
            "msl": MSL_NAME,
            "probe": "stage_mrt_matrix_probe.mm",
        })
        compile_cmd = [
            "xcrun",
            "clang++",
            "-std=c++17",
            "-ObjC++",
            "-framework",
            "Metal",
            "-framework",
            "Foundation",
            str(source_path),
            "-o",
            str(probe_bin),
        ]
        build = subprocess.run(compile_cmd, cwd=repo_root, text=True, capture_output=True)
        trace.append({"op": "buildExplicitMetalStageMrtMatrixProbe", "compiler": "xcrun clang++", "exitCode": build.returncode})
        if build.returncode != 0:
            errors.append({
                "code": "tixl_mesh_draw_stage_mrt_matrix.probe_build_failed",
                "message": clean_message(build.stderr or build.stdout or "probe build failed", repo_root),
            })
            return None, trace, errors
        run = subprocess.run([str(probe_bin), str(msl_path)], cwd=repo_root, text=True, capture_output=True)
        trace.append({"op": "runExplicitMetalStageMrtMatrixProbe", "exitCode": run.returncode})
        payload = parse_probe_payload(run.stdout)
        if payload is None:
            errors.append({
                "code": "tixl_mesh_draw_stage_mrt_matrix.probe_output_invalid",
                "message": clean_message(run.stderr or run.stdout or "probe did not emit JSON", repo_root),
            })
            return None, trace, errors
        if run.returncode != 0 or payload.get("ok") is not True:
            errors.append(error_from_probe(payload, repo_root))
            return None, trace, errors
        return payload, trace, errors
    finally:
        pass


def validate_probe_payload(payload: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if payload.get("status") != "proven_explicit_metal_stage_mrt_matrix_probe" or payload.get("ok") is not True:
        mismatches.append({"field": "probe.status", "expected": "proven_explicit_metal_stage_mrt_matrix_probe", "actual": payload.get("status")})
    if payload.get("actualCompilerRan") is not True or payload.get("actualMetalRan") is not True:
        mismatches.append({"field": "probe.actualMetalRan", "expected": True, "actual": {"actualCompilerRan": payload.get("actualCompilerRan"), "actualMetalRan": payload.get("actualMetalRan")}})
    if payload.get("target0") != [13, 90, 111, 255]:
        mismatches.append({"field": "probe.target0", "expected": [13, 90, 111, 255], "actual": payload.get("target0")})
    if payload.get("target1") != [31, 37, 41, 255]:
        mismatches.append({"field": "probe.target1", "expected": [31, 37, 41, 255], "actual": payload.get("target1")})
    return mismatches


def parse_probe_payload(text: str) -> dict[str, Any] | None:
    try:
        payload = json.loads(text.strip().splitlines()[-1])
    except Exception:
        return None
    return payload if isinstance(payload, dict) else None


def error_from_probe(payload: dict[str, Any], repo_root: Path) -> dict[str, Any]:
    status = payload.get("status")
    code_by_status = {
        "blocked_metal_device_unavailable": "tixl_mesh_draw_stage_mrt_matrix.device_unavailable",
        "compile_failed": "tixl_mesh_draw_stage_mrt_matrix.probe_compile_failed",
        "pipeline_failed": "tixl_mesh_draw_stage_mrt_matrix.probe_pipeline_failed",
        "render_failed": "tixl_mesh_draw_stage_mrt_matrix.probe_render_failed",
        "readback_mismatch": "tixl_mesh_draw_stage_mrt_matrix.probe_readback_mismatch",
    }
    return {
        "code": code_by_status.get(status, "tixl_mesh_draw_stage_mrt_matrix.probe_failed"),
        "status": status,
        "message": clean_message(str(payload.get("message") or "explicit Metal stage/MRT/matrix probe failed"), repo_root),
    }


def validate_artifact_header(artifact: dict[str, Any], kind: str, status: str, prefix: str) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if artifact.get("kind") != kind:
        mismatches.append({"field": f"{prefix}.kind", "expected": kind, "actual": artifact.get("kind")})
    if artifact.get("ok") is not True:
        mismatches.append({"field": f"{prefix}.ok", "expected": True, "actual": artifact.get("ok")})
    if artifact.get("status") != status:
        mismatches.append({"field": f"{prefix}.status", "expected": status, "actual": artifact.get("status")})
    return mismatches


def validate_no_widened_claims(artifact: dict[str, Any], prefix: str) -> list[dict[str, Any]]:
    claims = artifact.get("claims") if isinstance(artifact.get("claims"), dict) else {}
    mismatches: list[dict[str, Any]] = []
    for field in FORBIDDEN_TRUE_CLAIMS:
        if claims.get(field) is True:
            mismatches.append({"field": f"{prefix}.{field}", "expected": False, "actual": True})
    return mismatches


def evaluate_donor_hlsl(source: str) -> dict[str, Any]:
    compact = re.sub(r"\s+", " ", source)
    facts = {
        "vsMainUsesSvVertexId": re.search(r"psInput\s+vsMain\s*\(\s*uint\s+id\s*:\s*SV_VertexID\s*\)", source) is not None,
        "vsMainComputesFaceIndex": "int faceIndex = id / 3" in source and "int faceVertexIndex = id % 3" in source,
        "vsMainReadsIndexedPbrVertex": "PbrVertices[FaceIndices[faceIndex][faceVertexIndex]]" in compact,
        "psInputPixelPositionSvPosition": re.search(r"float4\s+pixelPosition\s*:\s*SV_POSITION\s*;", source) is not None,
        "psOutputColorTarget0": re.search(r"float4\s+Color\s*:\s*SV_Target0\s*;", source) is not None,
        "psOutputNormalTarget1": re.search(r"float4\s+Normal\s*:\s*SV_Target1\s*;", source) is not None,
        "psMainReturnsPsOutput": re.search(r"psOutput\s+psMain\s*\(\s*psInput\s+pin\s*\)\s*:\s*SV_TARGET", source) is not None,
        "psMainWritesColor": "output.Color = litColor;" in source,
        "psMainWritesNormal": "output.Normal = float4(worldNormal, 1.0);" in source,
        "psMainReturnsOutput": "return output;" in source,
        "fragmentUsesDdxDdy": "ddx(" in source and "ddy(" in source,
        "fragmentUsesDiscard": re.search(r"\bdiscard\s*;", source) is not None,
        "mulPosObjectToClip": "mul(posInObject, ObjectToClipSpace)" in source,
        "mulObjectToWorld": "mul(posInObject, ObjectToWorld)" in source,
        "mulObjectToCamera": "mul(posInObject, ObjectToCamera)" in source,
        "mulEyeCameraToWorld": "mul(float4(0, 0, 0, 1), CameraToWorld)" in source,
        "mulNormalDetailTbn": "mul(normalDetail, flatTBN)" in source or "mul(N, tbnToWorld)" in source,
    }
    required = {
        "vsMainUsesSvVertexId",
        "vsMainComputesFaceIndex",
        "vsMainReadsIndexedPbrVertex",
        "psInputPixelPositionSvPosition",
        "psOutputColorTarget0",
        "psOutputNormalTarget1",
        "psMainReturnsPsOutput",
        "psMainWritesColor",
        "psMainWritesNormal",
        "psMainReturnsOutput",
        "fragmentUsesDdxDdy",
        "fragmentUsesDiscard",
        "mulPosObjectToClip",
        "mulObjectToWorld",
        "mulObjectToCamera",
        "mulEyeCameraToWorld",
        "mulNormalDetailTbn",
    }
    errors = [
        {"field": f"donorHlsl.{field}", "expected": True, "actual": facts.get(field)}
        for field in sorted(required)
        if facts.get(field) is not True
    ]
    return {
        "facts": facts,
        "errors": errors,
        "stageSemantics": {
            "vertexEntry": "vsMain(uint id : SV_VertexID)",
            "vertexAddressing": "faceIndex = id / 3, faceVertexIndex = id % 3, PbrVertices[FaceIndices[faceIndex][faceVertexIndex]]",
            "positionSemantic": "psInput.pixelPosition : SV_POSITION",
            "pixelEntry": "psMain(psInput pin) : SV_TARGET -> psOutput",
            "derivativesAndDiscard": "ddx/ddy present; discard present behind AlphaCutOff",
        },
        "mrtSemantics": {
            "target0": "psOutput.Color : SV_Target0; output.Color = litColor",
            "target1": "psOutput.Normal : SV_Target1; output.Normal = float4(worldNormal, 1.0)",
        },
        "matrixSemantics": {
            "convention": "D3D/HLSL mul(vector, matrix)",
            "examples": [
                "mul(posInObject, ObjectToClipSpace)",
                "mul(posInObject, ObjectToWorld)",
                "mul(posInObject, ObjectToCamera)",
                "mul(float4(0, 0, 0, 1), CameraToWorld)",
                "mul(N, tbnToWorld)",
            ],
        },
    }


def result_payload(
    graph_id: Any,
    status: str,
    artifacts: dict[str, Any],
    source_summary: dict[str, Any],
    source_facts: dict[str, Any],
    stage_semantics: dict[str, Any],
    mrt_semantics: dict[str, Any],
    matrix_semantics: dict[str, Any],
    metal_probe: dict[str, Any],
    consumed_inputs: bool,
    ok: bool = False,
) -> dict[str, Any]:
    proven = ok and consumed_inputs
    return {
        "kind": "TixlMeshDrawStageMrtMatrixProof",
        "graphId": graph_id,
        "ok": ok,
        "status": status,
        "message": "source-backed stage/MRT/matrix semantics for the bounded handwritten mesh draw adapter" if ok else "stage/MRT/matrix proof blocked",
        "inputArtifacts": artifacts,
        "source": source_summary,
        "sourceFacts": source_facts,
        "stageSemantics": stage_semantics,
        "mrtSemantics": mrt_semantics,
        "matrixSemantics": matrix_semantics,
        "explicitMetalProbe": {
            "status": metal_probe.get("status"),
            "actualCompilerRan": metal_probe.get("actualCompilerRan") is True,
            "actualMetalRan": metal_probe.get("actualMetalRan") is True,
            "target0": metal_probe.get("target0"),
            "target1": metal_probe.get("target1"),
            "generatedMslArtifact": MSL_NAME if metal_probe else None,
            "tixlDonorHlslMetalProbeRan": False,
        },
        "notProven": [
            "HLSL-to-MSL translation",
            "TiXL donor HLSL Metal render probe",
            "TextureCube SampleLevel/GetDimensions",
            "full PBR resource binding",
            "backend replacement",
            "TiXL runtime parity",
            "PBR visual correctness",
        ],
        "claims": claim_flags(proven),
    }


def claim_flags(proven: bool) -> dict[str, bool]:
    claims = dict(EXPECTED_CLAIMS)
    for field, value in list(claims.items()):
        if value is True:
            claims[field] = proven
    return claims


def summarize_artifact(path: Path, artifact: Any, repo_root: Path) -> dict[str, Any]:
    if not isinstance(artifact, dict):
        return {"path": display_path(path, repo_root), "kind": None, "status": None, "ok": None}
    return {
        "path": display_path(path, repo_root),
        "kind": artifact.get("kind"),
        "status": artifact.get("status") or artifact.get("overallStatus"),
        "ok": artifact.get("ok"),
    }


def resolve_path(repo_root: Path, fixture_path: Path, maybe_path: Any, default_path: str) -> Path:
    if not isinstance(maybe_path, str) or not maybe_path:
        return (repo_root / default_path).resolve()
    path = Path(maybe_path).expanduser()
    if path.is_absolute():
        return path
    repo_candidate = repo_root / path
    if repo_candidate.exists() or str(maybe_path).startswith(("docs/", "external/")):
        return repo_candidate.resolve()
    return (fixture_path.parent / path).resolve()


def read_json(path: Path, errors: list[dict[str, Any]], code: str, repo_root: Path) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": display_path(path, repo_root), "message": clean_message(str(exc), repo_root)})
        return None


def read_text(path: Path, errors: list[dict[str, Any]], code: str, repo_root: Path) -> str | None:
    try:
        return path.read_text(encoding="utf8")
    except Exception as exc:
        errors.append({"code": code, "path": display_path(path, repo_root), "message": clean_message(str(exc), repo_root)})
        return None


def publish(out_dir: Path, result: dict[str, Any], trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / TRACE_NAME, trace)
    write_json(out_dir / ERRORS_NAME, errors)
    if result.get("ok") is True:
        write_text(out_dir / MSL_NAME, MSL_SOURCE)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf8")


def clean_message(text: str, repo_root: Path) -> str:
    return " ".join(text.replace(str(repo_root), ".").split())


def display_path(path: Path | None, repo_root: Path) -> str | None:
    if path is None:
        return None
    try:
        return str(path.resolve().relative_to(repo_root))
    except ValueError:
        return f"outside_repo/{path.name}"


if __name__ == "__main__":
    raise SystemExit(main())
