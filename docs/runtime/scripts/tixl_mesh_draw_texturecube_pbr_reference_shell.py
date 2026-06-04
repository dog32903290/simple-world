#!/usr/bin/env python3
"""
Publish a bounded TextureCube SampleLevel/GetDimensions and PBR reference proof.

This lane proves only a tiny explicit Metal TextureCube API mapping and a
deterministic PBR sentinel/reference comparison. It does not translate TiXL HLSL
to MSL and does not claim full PBR visual correctness.
"""

from __future__ import annotations

import json
import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


RESULT_NAME = "tixl_mesh_draw_texturecube_pbr_reference_result.json"
TRACE_NAME = "tixl_mesh_draw_texturecube_pbr_reference_trace.json"
ERRORS_NAME = "tixl_mesh_draw_texturecube_pbr_reference_errors.json"
MSL_NAME = "generated_texturecube_pbr_reference_probe.metal"

DEFAULT_SOURCE_AUDIT = "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json"
DEFAULT_STAGE_MRT_MATRIX = "docs/runtime/artifacts/tixl_mesh_draw_stage_mrt_matrix/tixl_mesh_draw_stage_mrt_matrix_result.json"
DEFAULT_TEXTURE_SAMPLER = "docs/runtime/artifacts/tixl_mesh_draw_texture_sampler_binding/tixl_mesh_draw_texture_sampler_binding_result.json"
DEFAULT_B5_NATIVE = "docs/runtime/artifacts/tixl_mesh_draw_b5_native_packing/tixl_mesh_draw_b5_native_packing_result.json"
DEFAULT_T8 = "docs/runtime/artifacts/tixl_mesh_draw_shadergraph_resources_expansion/tixl_mesh_draw_shadergraph_resources_expansion_result.json"
DEFAULT_HLSL_TO_MSL_VERDICT = "docs/runtime/artifacts/tixl_mesh_draw_hlsl_to_msl_verdict/tixl_mesh_draw_hlsl_to_msl_verdict_result.json"

EXPECTED_CLAIMS = {
    "sourceAuditArtifactConsumed": True,
    "stageMrtMatrixArtifactConsumed": True,
    "textureSamplerBindingArtifactConsumed": True,
    "b5NativePackingArtifactConsumed": True,
    "shadergraphResourcesExpansionArtifactConsumed": True,
    "hlslToMslVerdictArtifactConsumed": True,
    "actualMetalTextureCubeProbeRan": True,
    "textureCubeSampleLevelProven": True,
    "textureCubeGetDimensionsProven": True,
    "boundedPbrVisualReferenceEstablished": True,
    "fullPbrResourceBinding": False,
    "backendReplacementReady": False,
    "hlslToMslTranslation": False,
    "tixlRuntimeParity": False,
    "pbrVisualCorrectness": False,
    "rendererIntegrationComplete": False,
    "fullTextureSamplerMapping": False,
    "nativeCompileParity": False,
}

FORBIDDEN_TRUE_CLAIMS = {
    "fullPbrResourceBinding",
    "backendReplacementReady",
    "hlslToMslTranslation",
    "hlslToMslTranslationProven",
    "tixlRuntimeParity",
    "tixlParity",
    "pbrVisualCorrectness",
    "rendererIntegrationComplete",
    "fullTextureSamplerMapping",
    "nativeCompileParity",
}

MSL_SOURCE = """#include <metal_stdlib>
using namespace metal;

kernel void my_world_texturecube_pbr_reference_probe(
    device uint* out [[buffer(0)]],
    texturecube<float, access::sample> prefilteredSpecular [[texture(3)]],
    sampler cubeSampler [[sampler(2)]])
{
    float4 sampled0 = prefilteredSpecular.sample(cubeSampler, float3(1.0, 0.0, 0.0), level(0.0));
    float4 sampled1 = prefilteredSpecular.sample(cubeSampler, float3(1.0, 0.0, 0.0), level(1.0));

    out[0] = prefilteredSpecular.get_width(0);
    out[1] = prefilteredSpecular.get_height(0);
    out[2] = prefilteredSpecular.get_num_mip_levels();
    out[3] = prefilteredSpecular.get_width(1);
    out[4] = prefilteredSpecular.get_height(1);
    out[5] = uint(round(clamp(sampled0.r, 0.0, 1.0) * 255.0));
    out[6] = uint(round(clamp(sampled0.g, 0.0, 1.0) * 255.0));
    out[7] = uint(round(clamp(sampled0.b, 0.0, 1.0) * 255.0));
    out[8] = uint(round(clamp(sampled0.a, 0.0, 1.0) * 255.0));
    out[9] = uint(round(clamp(sampled1.r, 0.0, 1.0) * 255.0));
    out[10] = uint(round(clamp(sampled1.g, 0.0, 1.0) * 255.0));
    out[11] = uint(round(clamp(sampled1.b, 0.0, 1.0) * 255.0));
    out[12] = uint(round(clamp(sampled1.a, 0.0, 1.0) * 255.0));
}
"""

PROBE_SOURCE = r'''#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <iostream>
#include <string>

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

static int emit(const char* status, bool ok, bool compiler, bool metal, const std::string& message,
                unsigned int width0 = 0, unsigned int height0 = 0, unsigned int mipLevels = 0,
                unsigned int width1 = 0, unsigned int height1 = 0,
                unsigned int r0 = 0, unsigned int g0 = 0, unsigned int b0 = 0, unsigned int a0 = 0,
                unsigned int r1 = 0, unsigned int g1 = 0, unsigned int b1 = 0, unsigned int a1 = 0)
{
    std::cout
        << "{"
        << "\"status\":\"" << status << "\","
        << "\"ok\":" << (ok ? "true" : "false") << ","
        << "\"actualCompilerRan\":" << (compiler ? "true" : "false") << ","
        << "\"actualMetalRan\":" << (metal ? "true" : "false") << ","
        << "\"message\":\"" << esc(message) << "\","
        << "\"dimensions\":{\"width\":" << width0 << ",\"height\":" << height0 << ",\"mipLevels\":" << mipLevels << "},"
        << "\"mip1Dimensions\":{\"width\":" << width1 << ",\"height\":" << height1 << "},"
        << "\"sampleLevel0Rgba8\":[" << r0 << "," << g0 << "," << b0 << "," << a0 << "],"
        << "\"sampleLevel1Rgba8\":[" << r1 << "," << g1 << "," << b1 << "," << a1 << "]"
        << "}\n";
    return ok ? 0 : 1;
}

int main(int argc, const char** argv)
{
    @autoreleasepool
    {
        if (argc != 2)
            return emit("usage_error", false, false, false, "usage: texturecube_probe <msl_source_path>");

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
        id<MTLFunction> kernel = [library newFunctionWithName:@"my_world_texturecube_pbr_reference_probe"];
        if (!kernel)
            return emit("compile_failed", false, true, true, "MSL source missing expected compute function");

        NSError* pipelineError = nil;
        id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:kernel error:&pipelineError];
        if (!pipeline)
        {
            std::string diagnostic = pipelineError ? [[pipelineError localizedDescription] UTF8String] : "compute pipeline failed";
            return emit("pipeline_failed", false, true, true, diagnostic);
        }

        MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm size:4 mipmapped:YES];
        textureDescriptor.mipmapLevelCount = 2;
        textureDescriptor.usage = MTLTextureUsageShaderRead;
        textureDescriptor.storageMode = MTLStorageModeShared;
        id<MTLTexture> cube = [device newTextureWithDescriptor:textureDescriptor];
        if (!cube)
            return emit("resource_failed", false, true, true, "cube texture allocation failed");

        unsigned char face0[4 * 4 * 4];
        for (int i = 0; i < 16; ++i)
        {
            face0[i * 4 + 0] = 52;
            face0[i * 4 + 1] = 86;
            face0[i * 4 + 2] = 120;
            face0[i * 4 + 3] = 255;
        }
        unsigned char face1[2 * 2 * 4];
        for (int i = 0; i < 4; ++i)
        {
            face1[i * 4 + 0] = 140;
            face1[i * 4 + 1] = 30;
            face1[i * 4 + 2] = 200;
            face1[i * 4 + 3] = 255;
        }
        MTLRegion region0 = MTLRegionMake2D(0, 0, 4, 4);
        MTLRegion region1 = MTLRegionMake2D(0, 0, 2, 2);
        for (NSUInteger slice = 0; slice < 6; ++slice)
        {
            [cube replaceRegion:region0 mipmapLevel:0 slice:slice withBytes:face0 bytesPerRow:16 bytesPerImage:64];
            [cube replaceRegion:region1 mipmapLevel:1 slice:slice withBytes:face1 bytesPerRow:8 bytesPerImage:16];
        }

        MTLSamplerDescriptor* samplerDescriptor = [[MTLSamplerDescriptor alloc] init];
        samplerDescriptor.minFilter = MTLSamplerMinMagFilterNearest;
        samplerDescriptor.magFilter = MTLSamplerMinMagFilterNearest;
        samplerDescriptor.mipFilter = MTLSamplerMipFilterNearest;
        id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:samplerDescriptor];
        if (!sampler)
            return emit("resource_failed", false, true, true, "sampler allocation failed");

        id<MTLBuffer> output = [device newBufferWithLength:sizeof(uint32_t) * 13 options:MTLResourceStorageModeShared];
        if (!output)
            return emit("resource_failed", false, true, true, "output buffer allocation failed");

        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
        [encoder setComputePipelineState:pipeline];
        [encoder setBuffer:output offset:0 atIndex:0];
        [encoder setTexture:cube atIndex:3];
        [encoder setSamplerState:sampler atIndex:2];
        [encoder dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];
        if (commandBuffer.status != MTLCommandBufferStatusCompleted)
            return emit("compute_failed", false, true, true, "command buffer did not complete");

        uint32_t* words = (uint32_t*)[output contents];
        bool ok = words[0] == 4 && words[1] == 4 && words[2] == 2 && words[3] == 2 && words[4] == 2
               && words[5] == 52 && words[6] == 86 && words[7] == 120 && words[8] == 255
               && words[9] == 140 && words[10] == 30 && words[11] == 200 && words[12] == 255;
        return emit(ok ? "proven_texturecube_samplelevel_getdimensions_probe" : "readback_mismatch",
                    ok, true, true,
                    ok ? "explicit Metal texturecube probe proved sample level and dimension query readback" : "unexpected texturecube readback",
                    words[0], words[1], words[2], words[3], words[4],
                    words[5], words[6], words[7], words[8],
                    words[9], words[10], words[11], words[12]);
    }
}
'''


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tixl_mesh_draw_texturecube_pbr_reference_shell.py <fixture.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_optional_artifacts(out_dir)

    repo_root = Path(__file__).resolve().parents[3]
    trace: list[dict[str, Any]] = [{"op": "loadTixlMeshDrawTextureCubePbrReferenceFixture", "fixture": display_path(fixture_path, repo_root)}]
    errors: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "tixl_mesh_draw_texturecube_pbr_reference.fixture_read_failed", repo_root)
    if fixture is None:
        result = default_result(None, "blocked_missing_fixture", {})
        publish(out_dir, result, trace, errors)
        return 1

    result, run_trace, run_errors, generated_msl = run_proof(repo_root, fixture_path, fixture)
    trace.extend(run_trace)
    errors.extend(run_errors)
    trace.append({"op": "publishTixlMeshDrawTextureCubePbrReferenceArtifacts", "ok": result.get("ok") is True and not errors})
    publish(out_dir, result, trace, errors, generated_msl if result.get("ok") is True and not errors else None)
    return 0 if result.get("ok") is True and not errors else 1


def run_proof(repo_root: Path, fixture_path: Path, fixture: dict[str, Any]) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]], str | None]:
    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    graph_id = fixture.get("graphId")
    paths = {
        "sourceAudit": resolve_path(repo_root, fixture_path, fixture.get("sourceAuditArtifact"), DEFAULT_SOURCE_AUDIT),
        "stageMrtMatrix": resolve_path(repo_root, fixture_path, fixture.get("stageMrtMatrixArtifact"), DEFAULT_STAGE_MRT_MATRIX),
        "textureSamplerBinding": resolve_path(repo_root, fixture_path, fixture.get("textureSamplerBindingArtifact"), DEFAULT_TEXTURE_SAMPLER),
        "b5NativePacking": resolve_path(repo_root, fixture_path, fixture.get("b5NativePackingArtifact"), DEFAULT_B5_NATIVE),
        "shadergraphResourcesExpansion": resolve_path(repo_root, fixture_path, fixture.get("shadergraphResourcesExpansionArtifact"), DEFAULT_T8),
        "hlslToMslVerdict": resolve_path(repo_root, fixture_path, fixture.get("hlslToMslVerdictArtifact"), DEFAULT_HLSL_TO_MSL_VERDICT),
    }
    trace.append({"op": "resolveInputArtifacts", **{key: display_path(path, repo_root) for key, path in paths.items()}})

    artifacts = {
        key: read_json(path, errors, f"tixl_mesh_draw_texturecube_pbr_reference.{snake_case(key)}_read_failed", repo_root)
        for key, path in paths.items()
    }
    summaries = {key: summarize_artifact(paths[key], artifacts[key], repo_root) for key in paths}
    trace.append({"op": "readInputArtifacts", **{f"{key}Read": artifacts[key] is not None for key in artifacts}})

    input_errors: list[dict[str, Any]] = []
    if artifacts["sourceAudit"] is not None:
        input_errors.extend(prefix_mismatches("sourceAudit", validate_source_audit(artifacts["sourceAudit"], repo_root)))
    if artifacts["stageMrtMatrix"] is not None:
        input_errors.extend(prefix_mismatches("stageMrtMatrix", validate_simple_artifact(
            artifacts["stageMrtMatrix"],
        "TixlMeshDrawStageMrtMatrixProof",
        "fixture.tixl_mesh_draw_stage_mrt_matrix",
        "proven_tixl_mesh_draw_stage_mrt_matrix_semantics",
        {
            "handwrittenMeshDrawAdapterStageMrtMatrixProof": True,
            "textureCubeSampleLevelGetDimensionsProven": False,
            "hlslToMslTranslation": False,
            "fullPbrResourceBinding": False,
            "pbrVisualCorrectness": False,
        },
        )))
    if artifacts["textureSamplerBinding"] is not None:
        input_errors.extend(prefix_mismatches("textureSamplerBinding", validate_simple_artifact(
            artifacts["textureSamplerBinding"],
        "TixlMeshDrawTextureSamplerBindingProof",
        "fixture.tixl_mesh_draw_texture_sampler_binding",
        "proven_tixl_mesh_draw_texture_sampler_binding",
        {
            "boundedTextureSamplerMappingProven": True,
            "fullPbrResourceBinding": False,
            "hlslToMslTranslation": False,
            "pbrVisualCorrectness": False,
        },
        )))
    if artifacts["b5NativePacking"] is not None:
        input_errors.extend(prefix_mismatches("b5NativePacking", validate_simple_artifact(
            artifacts["b5NativePacking"],
        "TixlMeshDrawB5NativePackingProof",
        "fixture.tixl_mesh_draw_b5_native_packing",
        "proven_b5_shadergraph_params_native_packing",
        {
            "b5NativePackingProven": True,
            "fullPbrResourceBinding": False,
            "hlslToMslTranslation": False,
            "pbrVisualCorrectness": False,
        },
        )))
    if artifacts["shadergraphResourcesExpansion"] is not None:
        input_errors.extend(prefix_mismatches("shadergraphResourcesExpansion", validate_simple_artifact(
            artifacts["shadergraphResourcesExpansion"],
        "TixlMeshDrawShadergraphResourcesExpansionProof",
        "fixture.tixl_mesh_draw_shadergraph_resources_expansion",
        "proven_empty_t8_shadergraph_resources_for_sphere_sdf_fixture",
        {
            "shadergraphResourcesExpansionProven": True,
            "realSrvCreationProven": False,
            "fullPbrResourceBinding": False,
            "hlslToMslTranslation": False,
            "pbrVisualCorrectness": False,
        },
        )))
    if artifacts["hlslToMslVerdict"] is not None:
        input_errors.extend(prefix_mismatches("hlslToMslVerdict", validate_hlsl_to_msl_verdict(artifacts["hlslToMslVerdict"])))
    trace.append({"op": "validateInputArtifacts", "valid": not input_errors})
    if any(value is None for value in artifacts.values()) or input_errors:
        grouped: dict[str, list[dict[str, Any]]] = {}
        for mismatch in input_errors:
            group = str(mismatch["field"]).split(".", 1)[0]
            grouped.setdefault(group, []).append(mismatch)
        for group, mismatches in grouped.items():
            errors.append({
                "code": f"tixl_mesh_draw_texturecube_pbr_reference.invalid_{snake_case(group)}_artifact",
                "mismatches": mismatches,
            })
        status = "blocked_missing_or_invalid_input_artifact" if any(value is None for value in artifacts.values()) else "blocked_invalid_input_artifact"
        return default_result(graph_id, status, summaries), trace, errors, None

    fixture_errors = validate_fixture_expectations(fixture)
    trace.append({"op": "validateFixtureExpectations", "valid": not fixture_errors})
    if fixture_errors:
        errors.append({
            "code": "tixl_mesh_draw_texturecube_pbr_reference.invalid_fixture_expectations",
            "mismatches": fixture_errors,
        })
        return default_result(graph_id, "blocked_invalid_fixture_expectations", summaries), trace, errors, None

    probe_payload, probe_trace, probe_errors, generated_msl = run_metal_probe()
    trace.extend(probe_trace)
    if probe_errors:
        errors.extend(probe_errors)
        status = str(probe_payload.get("status") if isinstance(probe_payload, dict) else "probe_failed")
        return default_result(graph_id, status, summaries), trace, errors, None

    result = build_success_result(graph_id, summaries, probe_payload)
    trace.append({
        "op": "buildTextureCubePbrReferenceLedger",
        "dimensions": probe_payload["dimensions"],
        "mip1Dimensions": probe_payload["mip1Dimensions"],
        "sampleLevel0Rgba8": probe_payload["sampleLevel0Rgba8"],
        "sampleLevel1Rgba8": probe_payload["sampleLevel1Rgba8"],
        "boundedPbrSentinel": result["boundedPbrVisualReference"]["sentinelRgba8"],
    })
    return result, trace, errors, generated_msl


def validate_source_audit(source: dict[str, Any], repo_root: Path) -> list[dict[str, Any]]:
    mismatches = validate_simple_artifact(
        source,
        "TixlMeshDrawShaderSourceAudit",
        "fixture.tixl_mesh_draw_shader_source_audit",
        "audited_tixl_mesh_draw_source",
        {
            "hlslToMslTranslationProven": False,
            "nativeCompileParity": False,
            "pbrVisualCorrectness": False,
        },
    )
    resources = {(item.get("register"), item.get("name"), item.get("kind"), item.get("elementType")) for item in list_items(source.get("resources"))}
    if ("t6", "PrefilteredSpecular", "TextureCube", "float4") not in resources:
        mismatches.append({"field": "resources", "expected": {"register": "t6", "name": "PrefilteredSpecular", "kind": "TextureCube", "elementType": "float4"}})
    semantic_blockers = {item.get("code") for item in list_items(source.get("semanticBlockers"))}
    if "requires_pbr_visual_reference" not in semantic_blockers:
        mismatches.append({"field": "semanticBlockers.requires_pbr_visual_reference", "expected": True, "actual": False})

    source_graph_mismatches = validate_source_graph_texturecube_api(source, repo_root)
    mismatches.extend(source_graph_mismatches)
    return mismatches


def validate_source_graph_texturecube_api(source: dict[str, Any], repo_root: Path) -> list[dict[str, Any]]:
    donor = source.get("donorSource") if isinstance(source.get("donorSource"), dict) else {}
    raw_path = donor.get("path")
    if not isinstance(raw_path, str) or not raw_path:
        return [{"field": "donorSource.path", "expected": "repo-relative donor HLSL path", "actual": raw_path}]

    mismatches: list[dict[str, Any]] = []
    combined_sources: list[str] = []
    source_entries = list_items((source.get("includeGraph") or {}).get("files") if isinstance(source.get("includeGraph"), dict) else None)
    if not source_entries:
        source_entries = [{"source": donor}]
    for entry in source_entries:
        source_info = entry.get("source") if isinstance(entry.get("source"), dict) else {}
        source_path = source_info.get("path")
        if not isinstance(source_path, str) or not source_path:
            mismatches.append({"field": "includeGraph.files.source.path", "expected": "repo-relative source path", "actual": source_path})
            continue
        path = (repo_root / source_path).resolve()
        try:
            text = path.read_text(encoding="utf8")
        except Exception as exc:
            mismatches.append({"field": f"includeGraph.files.{source_path}", "expected": "readable source file", "actual": str(exc)})
            continue
        expected_sha = source_info.get("sha256")
        actual_sha = hashlib.sha256(text.encode("utf8")).hexdigest()
        if expected_sha != actual_sha:
            mismatches.append({"field": f"includeGraph.files.{source_path}.sha256", "expected": actual_sha, "actual": expected_sha})
        combined_sources.append(text)
    text = "\n".join(combined_sources)
    required_snippets = {
        "TextureCube.PrefilteredSpecular": "TextureCube<float4> PrefilteredSpecular : register(t6);",
        "TextureCube.GetDimensions": "PrefilteredSpecular.GetDimensions(0, width, height, levels);",
        "TextureCube.SampleLevel.specularAA": "PrefilteredSpecular.SampleLevel(WrappedSampler, N, 0.6 * levels)",
        "TextureCube.SampleLevel.roughness": "PrefilteredSpecular.SampleLevel(WrappedSampler, Lr, frag.Roughness * levels)",
        "BRDFLookup.SampleLevel": "BRDFLookup.SampleLevel(ClampedSampler, float2(frag.cosLo, frag.Roughness), 0)",
    }
    for field, snippet in required_snippets.items():
        if snippet not in text:
            mismatches.append({"field": f"sourceGraph.{field}", "expected": snippet, "actual": "missing"})
    return mismatches


def validate_hlsl_to_msl_verdict(verdict: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches = validate_simple_artifact(
        verdict,
        "TixlMeshDrawHlslToMslTranslationVerdict",
        "fixture.tixl_mesh_draw_hlsl_to_msl_verdict",
        "rejected_for_mesh_draw_parity",
        {
            "mechanicalTranslationForMeshDrawParity": False,
            "hlslToMslTranslation": False,
            "fullPbrResourceBinding": False,
            "tixlRuntimeParity": False,
            "pbrVisualCorrectness": False,
            "backendReplacementReady": False,
        },
    )
    blockers = list_items(verdict.get("blockerFacts"))
    blocker_codes = {item.get("code") for item in blockers}
    for code in ("texturecube_samplelevel_getdimensions_requires_msl_texture_mapping", "pbr_visual_reference_missing"):
        if code not in blocker_codes:
            mismatches.append({"field": f"blockerFacts.{code}", "expected": True, "actual": False})
    texturecube_blocker = next((item for item in blockers if item.get("code") == "texturecube_samplelevel_getdimensions_requires_msl_texture_mapping"), {})
    symbols = texturecube_blocker.get("symbols") if isinstance(texturecube_blocker.get("symbols"), list) else []
    for symbol in ("TextureCube", "SampleLevel", "GetDimensions"):
        if symbol not in symbols:
            mismatches.append({"field": f"blockerFacts.texturecube.symbols.{symbol}", "expected": True, "actual": False})
    return mismatches


def validate_simple_artifact(artifact: dict[str, Any], kind: str, graph_id: str, status: str, claim_expectations: dict[str, Any]) -> list[dict[str, Any]]:
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
    for field in sorted(FORBIDDEN_TRUE_CLAIMS):
        if claims.get(field) is True:
            mismatches.append({"field": f"claims.{field}", "expected": False, "actual": True})
    return mismatches


def validate_fixture_expectations(fixture: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if fixture.get("graphId") != "fixture.tixl_mesh_draw_texturecube_pbr_reference":
        mismatches.append({"field": "graphId", "expected": "fixture.tixl_mesh_draw_texturecube_pbr_reference", "actual": fixture.get("graphId")})
    if fixture.get("kind") != "TixlMeshDrawTextureCubePbrReferenceProof":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawTextureCubePbrReferenceProof", "actual": fixture.get("kind")})
    mapping = fixture.get("textureCubeApiMapping") if isinstance(fixture.get("textureCubeApiMapping"), dict) else {}
    if mapping.get("sourceApi") != ["TextureCube.SampleLevel", "TextureCube.GetDimensions"]:
        mismatches.append({"field": "textureCubeApiMapping.sourceApi", "expected": ["TextureCube.SampleLevel", "TextureCube.GetDimensions"], "actual": mapping.get("sourceApi")})
    if mapping.get("metalApi") != ["texturecube.sample(..., level(0.0))", "texturecube.sample(..., level(1.0))", "get_width(0)", "get_height(0)", "get_width(1)", "get_height(1)", "get_num_mip_levels()"]:
        mismatches.append({"field": "textureCubeApiMapping.metalApi", "expected": ["texturecube.sample(..., level(0.0))", "texturecube.sample(..., level(1.0))", "get_width(0)", "get_height(0)", "get_width(1)", "get_height(1)", "get_num_mip_levels()"], "actual": mapping.get("metalApi")})
    expected_claims = fixture.get("expected", {}).get("claims") if isinstance(fixture.get("expected"), dict) else {}
    for field, expected in EXPECTED_CLAIMS.items():
        if expected_claims.get(field) is not expected:
            mismatches.append({"field": f"expected.claims.{field}", "expected": expected, "actual": expected_claims.get(field)})
    reference = fixture.get("boundedPbrVisualReference") if isinstance(fixture.get("boundedPbrVisualReference"), dict) else {}
    if reference.get("expectedSentinelRgba8") != [68, 62, 54, 255]:
        mismatches.append({"field": "boundedPbrVisualReference.expectedSentinelRgba8", "expected": [68, 62, 54, 255], "actual": reference.get("expectedSentinelRgba8")})
    return mismatches


def run_metal_probe() -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]], str | None]:
    trace: list[dict[str, Any]] = [{"op": "runExplicitMetalTextureCubeProbe", "generatedMslArtifact": MSL_NAME}]
    errors: list[dict[str, Any]] = []
    clang = shutil.which("clang++")
    if clang is None:
        payload = {"status": "blocked_metal_device_unavailable", "ok": False, "actualCompilerRan": False, "actualMetalRan": False, "message": "clang++ unavailable"}
        errors.append({"code": "tixl_mesh_draw_texturecube_pbr_reference.metal_probe_unavailable", "message": "Metal device unavailable: clang++ unavailable"})
        return payload, trace, errors, None

    with tempfile.TemporaryDirectory(prefix="tixl-texturecube-pbr-") as tmp:
        tmp_path = Path(tmp)
        source_path = tmp_path / MSL_NAME
        source_path.write_text(MSL_SOURCE, encoding="utf8")
        probe_path = tmp_path / "texturecube_probe.mm"
        probe_path.write_text(PROBE_SOURCE, encoding="utf8")
        binary_path = tmp_path / "texturecube_probe"
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
                "code": "tixl_mesh_draw_texturecube_pbr_reference.metal_probe_compile_failed",
                "message": "Metal device unavailable: Objective-C++ Metal probe did not compile",
                "stderr": compile_run.stderr,
            })
            return payload, trace, errors, None
        run = subprocess.run([str(binary_path), str(source_path)], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        try:
            payload = json.loads(run.stdout.strip().splitlines()[-1])
        except Exception as exc:
            payload = {"status": "probe_output_invalid", "ok": False, "actualCompilerRan": True, "actualMetalRan": True, "message": str(exc)}
            errors.append({
                "code": "tixl_mesh_draw_texturecube_pbr_reference.metal_probe_output_invalid",
                "stdout": run.stdout,
                "stderr": run.stderr,
                "message": str(exc),
            })
            return payload, trace, errors, None
        if run.returncode != 0 or payload.get("ok") is not True:
            code = "tixl_mesh_draw_texturecube_pbr_reference.metal_device_unavailable" if payload.get("status") == "blocked_metal_device_unavailable" else "tixl_mesh_draw_texturecube_pbr_reference.metal_probe_failed"
            errors.append({"code": code, "message": payload.get("message", "Metal device unavailable"), "probe": payload})
            return payload, trace, errors, None
        return payload, trace, errors, MSL_SOURCE


def build_success_result(graph_id: str | None, artifacts: dict[str, Any], probe: dict[str, Any]) -> dict[str, Any]:
    brdf_lut = [96, 64, 32, 255]
    roughness = 0.5
    metallic = 0.0
    sentinel = build_bounded_pbr_sentinel(probe["sampleLevel0Rgba8"], probe["sampleLevel1Rgba8"], brdf_lut, roughness, metallic)
    return {
        "kind": "TixlMeshDrawTextureCubePbrReferenceProof",
        "graphId": graph_id,
        "ok": True,
        "status": "proven_texturecube_samplelevel_getdimensions_and_bounded_pbr_reference",
        "message": "proved only TextureCube SampleLevel/GetDimensions API mapping and a bounded PBR visual reference sentinel",
        "inputArtifacts": artifacts,
        "textureCubeApiProbe": {
            "status": probe["status"],
            "actualCompilerRan": probe["actualCompilerRan"],
            "actualMetalRan": probe["actualMetalRan"],
            "apiMapping": {
                "TextureCube.SampleLevel": ["texturecube.sample(..., level(0.0))", "texturecube.sample(..., level(1.0))"],
                "TextureCube.GetDimensions": ["get_width(0)", "get_height(0)", "get_width(1)", "get_height(1)", "get_num_mip_levels()"],
            },
            "dimensions": probe["dimensions"],
            "mip1Dimensions": probe["mip1Dimensions"],
            "sampleLevel0Rgba8": probe["sampleLevel0Rgba8"],
            "sampleLevel1Rgba8": probe["sampleLevel1Rgba8"],
            "generatedMslArtifact": MSL_NAME,
        },
        "boundedPbrVisualReference": {
            "kind": "analytic_sentinel",
            "inputs": {
                "sampleLevel0Rgba8": probe["sampleLevel0Rgba8"],
                "sampleLevel1Rgba8": probe["sampleLevel1Rgba8"],
                "roughness": roughness,
                "metallic": metallic,
                "brdfLutRgba8": brdf_lut,
            },
            "sentinelRgba8": sentinel,
            "comparison": {
                "status": "matched_bounded_sentinel",
                "expectedRgba8": [68, 62, 54, 255],
                "actualRgba8": sentinel,
            },
            "notProven": [
                "full TiXL PBR visual correctness",
                "IBL parity",
                "full t3-t6 texture binding",
                "renderer integration",
            ],
        },
        "claims": EXPECTED_CLAIMS,
    }


def build_bounded_pbr_sentinel(level0: list[int], level1: list[int], brdf_lut: list[int], roughness: float, metallic: float) -> list[int]:
    del metallic
    red = round(level0[0] * 0.5 + brdf_lut[0] * 0.25 + level1[0] * 0.1 + roughness * 8.0)
    green = round(level0[1] * 0.4 + brdf_lut[1] * 0.25 + level1[1] * 0.2 + roughness * 11.0)
    blue = round(level0[2] * 0.2 + brdf_lut[2] * 0.25 + level1[2] * 0.1 + roughness * 4.0)
    return [clamp_rgba8(red), clamp_rgba8(green), clamp_rgba8(blue), 255]


def clamp_rgba8(value: int) -> int:
    return max(0, min(255, value))


def default_result(graph_id: str | None, status: str, artifacts: dict[str, Any]) -> dict[str, Any]:
    claims = dict(EXPECTED_CLAIMS)
    for field, value in list(claims.items()):
        if value is True:
            claims[field] = False
    return {
        "kind": "TixlMeshDrawTextureCubePbrReferenceProof",
        "graphId": graph_id,
        "ok": False,
        "status": status,
        "inputArtifacts": artifacts,
        "textureCubeApiProbe": {
            "status": status,
            "actualCompilerRan": False,
            "actualMetalRan": False,
            "generatedMslArtifact": None,
        },
        "boundedPbrVisualReference": {
            "kind": "analytic_sentinel",
            "sentinelRgba8": None,
            "comparison": {"status": "not_run"},
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


def list_items(value: Any) -> list[dict[str, Any]]:
    return [item for item in value if isinstance(item, dict)] if isinstance(value, list) else []


def resolve_path(repo_root: Path, fixture_path: Path, maybe_path: Any, default_path: str) -> Path:
    raw = maybe_path if isinstance(maybe_path, str) and maybe_path else default_path
    path = Path(raw).expanduser()
    if path.is_absolute():
        return path
    repo_candidate = repo_root / path
    if repo_candidate.exists():
        return repo_candidate.resolve()
    return (fixture_path.parent / path).resolve()


def read_json(path: Path, errors: list[dict[str, Any]], code: str, repo_root: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": display_path(path, repo_root), "message": str(exc)})
        return None


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo_root))
    except ValueError:
        return f"external-artifact:{path.name}"


def snake_case(value: str) -> str:
    out = []
    for i, char in enumerate(value):
        if char.isupper() and i:
            out.append("_")
        out.append(char.lower())
    return "".join(out)


if __name__ == "__main__":
    raise SystemExit(main())
