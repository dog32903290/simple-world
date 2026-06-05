#!/usr/bin/env python3
"""
Publish the TiXL mesh Draw explicit adapter proof.

This lane proves only the selected handwritten explicit MSL adapter scope. It
consumes prior selected-strategy/MSL evidence, compiles a generated adapter MSL
through real Metal, renders a tiny frame, and keeps backend/parity flags false.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


RESULT_NAME = "tixl_mesh_draw_explicit_adapter_result.json"
TRACE_NAME = "tixl_mesh_draw_explicit_adapter_trace.json"
ERRORS_NAME = "tixl_mesh_draw_explicit_adapter_errors.json"
MSL_NAME = "generated_explicit_adapter.metal"
FRAME_STATS_NAME = "frame_stats.json"

DEFAULT_STRATEGY = "docs/runtime/artifacts/tixl_mesh_draw_explicit_translation_strategy/tixl_mesh_draw_explicit_translation_strategy_result.json"
DEFAULT_MSL_APPROX = "docs/runtime/artifacts/tixl_mesh_draw_msl_approx/tixl_mesh_draw_msl_approx_result.json"
DEFAULT_METAL_EXPLICIT = "docs/runtime/artifacts/metal_explicit_msl_proof/metal_explicit_msl_result.json"

EXPECTED_CLAIMS = {
    "explicitTranslationStrategyArtifactConsumed": True,
    "mslApproxArtifactConsumed": True,
    "metalExplicitMslArtifactConsumed": True,
    "selectedHandwrittenAdapterStrategyConsumed": True,
    "mslApproxEvidenceConsumed": True,
    "metalExplicitMslEvidenceConsumed": True,
    "actualCompilerRan": True,
    "actualMetalRan": True,
    "explicitAdapterProof": True,
    "explicitAdapterProofPresent": True,
    "fullPbrResourceBinding": False,
    "backendReplacementReady": False,
    "hlslToMslTranslation": False,
    "tixlRuntimeParity": False,
    "nativeGpuParityComplete": False,
    "pbrVisualCorrectness": False,
}

FORBIDDEN_TRUE_CLAIMS = {
    "fullPbrResourceBinding",
    "backendReplacementReady",
    "hlslToMslTranslation",
    "tixlRuntimeParity",
    "nativeGpuParityComplete",
    "pbrVisualCorrectness",
    "pbrParity",
    "rendererIntegration",
    "drawMeshRuntime",
    "nativeDrawShaderCompileProofIntegration",
}

MSL_SOURCE = """#include <metal_stdlib>
using namespace metal;

struct AdapterVertexOut
{
    float4 position [[position]];
    float2 adapterUv;
};

vertex AdapterVertexOut my_world_explicit_adapter_vertex(uint vertexId [[vertex_id]])
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };

    AdapterVertexOut out;
    out.position = float4(positions[vertexId], 0.0, 1.0);
    out.adapterUv = positions[vertexId] * 0.5 + 0.5;
    return out;
}

fragment uint4 my_world_explicit_adapter_fragment(AdapterVertexOut in [[stage_in]])
{
    const uint x = uint(clamp(in.position.x, 0.0, 255.0));
    const uint y = uint(clamp(in.position.y, 0.0, 255.0));
    return uint4(17u + x, 113u + y, 191u + x + y, 255u);
}
"""

PROBE_CPP_SOURCE = r'''#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

static std::string jsonEscape(const std::string& input)
{
    std::ostringstream out;
    for (char c : input)
    {
        if (c == '"' || c == '\\')
            out << '\\' << c;
        else if (c == '\n' || c == '\r' || c == '\t')
            out << ' ';
        else
            out << c;
    }
    return out.str();
}

static std::string digestBytes(const std::vector<unsigned char>& bytes)
{
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char byte : bytes)
    {
        hash ^= uint64_t(byte);
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

static int emit(const char* status, bool ok, bool compiler, bool metal, const std::string& message,
                int width, int height, const std::vector<unsigned char>& pixels)
{
    int byteCount = int(pixels.size());
    int nonBlackPixels = 0;
    int opaquePixels = 0;
    std::set<std::string> unique;
    for (int i = 0; i + 3 < byteCount; i += 4)
    {
        unsigned char r = pixels[i + 0];
        unsigned char g = pixels[i + 1];
        unsigned char b = pixels[i + 2];
        unsigned char a = pixels[i + 3];
        if (r || g || b)
            nonBlackPixels++;
        if (a == 255)
            opaquePixels++;
        unique.insert(std::to_string(int(r)) + "," + std::to_string(int(g)) + "," + std::to_string(int(b)) + "," + std::to_string(int(a)));
    }
    unsigned char c0r = byteCount >= 4 ? pixels[0] : 0;
    unsigned char c0g = byteCount >= 4 ? pixels[1] : 0;
    unsigned char c0b = byteCount >= 4 ? pixels[2] : 0;
    unsigned char c0a = byteCount >= 4 ? pixels[3] : 0;
    std::cout
        << "{"
        << "\"status\":\"" << status << "\","
        << "\"ok\":" << (ok ? "true" : "false") << ","
        << "\"actualCompilerRan\":" << (compiler ? "true" : "false") << ","
        << "\"actualMetalRan\":" << (metal ? "true" : "false") << ","
        << "\"message\":\"" << jsonEscape(message) << "\","
        << "\"width\":" << width << ","
        << "\"height\":" << height << ","
        << "\"byteCount\":" << byteCount << ","
        << "\"nonBlack\":" << (nonBlackPixels > 0 ? "true" : "false") << ","
        << "\"varied\":" << (unique.size() > 1 ? "true" : "false") << ","
        << "\"nonBlackPixels\":" << nonBlackPixels << ","
        << "\"uniqueColorSamples\":" << unique.size() << ","
        << "\"opaquePixels\":" << opaquePixels << ","
        << "\"frameDigest\":\"" << digestBytes(pixels) << "\","
        << "\"color0\":[" << int(c0r) << "," << int(c0g) << "," << int(c0b) << "," << int(c0a) << "]"
        << "}\n";
    return ok ? 0 : 1;
}

int main(int argc, const char** argv)
{
    @autoreleasepool
    {
        if (argc != 4)
            return emit("usage_error", false, false, false, "usage: explicit_adapter_probe <msl> <width> <height>", 0, 0, {});

        int width = std::max(1, atoi(argv[2]));
        int height = std::max(1, atoi(argv[3]));
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device)
            return emit("blocked_metal_device_unavailable", false, false, false, "Metal device unavailable", width, height, {});
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue)
            return emit("blocked_metal_device_unavailable", false, false, false, "Metal command queue unavailable", width, height, {});

        NSError* readError = nil;
        NSString* path = [NSString stringWithUTF8String:argv[1]];
        NSString* source = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&readError];
        if (!source)
            return emit("compile_failed", false, false, true, "MSL source read failed", width, height, {});

        NSError* compileError = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&compileError];
        if (!library)
        {
            std::string diagnostic = compileError ? [[compileError localizedDescription] UTF8String] : "MSL compile failed";
            return emit("compile_failed", false, true, true, diagnostic, width, height, {});
        }
        id<MTLFunction> vertex = [library newFunctionWithName:@"my_world_explicit_adapter_vertex"];
        id<MTLFunction> fragment = [library newFunctionWithName:@"my_world_explicit_adapter_fragment"];
        if (!vertex || !fragment)
            return emit("compile_failed", false, true, true, "MSL source missing explicit adapter vertex or fragment", width, height, {});

        MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
        descriptor.vertexFunction = vertex;
        descriptor.fragmentFunction = fragment;
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Uint;
        NSError* pipelineError = nil;
        id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&pipelineError];
        if (!pipeline)
        {
            std::string diagnostic = pipelineError ? [[pipelineError localizedDescription] UTF8String] : "render pipeline failed";
            return emit("pipeline_failed", false, true, true, diagnostic, width, height, {});
        }

        MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Uint width:width height:height mipmapped:NO];
        textureDescriptor.usage = MTLTextureUsageRenderTarget;
        textureDescriptor.storageMode = MTLStorageModeShared;
        id<MTLTexture> target = [device newTextureWithDescriptor:textureDescriptor];
        if (!target)
            return emit("render_failed", false, true, true, "render target allocation failed", width, height, {});

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);

        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
        [encoder setRenderPipelineState:pipeline];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];
        if (commandBuffer.status != MTLCommandBufferStatusCompleted)
            return emit("render_failed", false, true, true, "command buffer did not complete", width, height, {});

        std::vector<unsigned char> pixels(size_t(width) * size_t(height) * 4u, 0);
        MTLRegion region = MTLRegionMake2D(0, 0, width, height);
        [target getBytes:pixels.data() bytesPerRow:width * 4 fromRegion:region mipmapLevel:0];

        bool ok = pixels.size() >= 4
               && pixels[0] == 17 && pixels[1] == 113 && pixels[2] == 191 && pixels[3] == 255;
        return emit(ok ? "proven_explicit_mesh_draw_adapter" : "readback_mismatch",
                    ok, true, true,
                    ok ? "compiled explicit adapter MSL, rendered offscreen, and read back adapter frame stats" : "unexpected adapter readback",
                    width, height, pixels);
    }
}
'''


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tixl_mesh_draw_explicit_adapter_proof_shell.py <fixture.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_optional_artifacts(out_dir)

    repo_root = Path(__file__).resolve().parents[3]
    trace: list[dict[str, Any]] = [{
        "op": "loadTixlMeshDrawExplicitAdapterFixture",
        "fixture": display_path(fixture_path, repo_root),
    }]
    errors: list[dict[str, Any]] = []

    fixture = read_json(fixture_path, errors, "tixl_mesh_draw_explicit_adapter.fixture_read_failed", repo_root)
    if fixture is None:
        result = default_result(None, "blocked_missing_fixture")
        publish(out_dir, result, trace, errors, None)
        return 1

    result, run_trace, run_errors, frame_stats = run_proof(repo_root, fixture_path, fixture)
    trace.extend(run_trace)
    errors.extend(run_errors)
    write_text(out_dir / MSL_NAME, MSL_SOURCE)
    trace.append({"op": "publishTixlMeshDrawExplicitAdapterArtifacts", "ok": result.get("ok") is True and not errors})
    publish(out_dir, result, trace, errors, frame_stats if result.get("ok") is True and not errors else None)
    return 0 if result.get("ok") is True and not errors else 1


def run_proof(repo_root: Path, fixture_path: Path, fixture: dict[str, Any]) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]], dict[str, Any] | None]:
    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    graph_id = fixture.get("graphId")
    viewport = fixture.get("viewport") if isinstance(fixture.get("viewport"), dict) else {}
    width = int(viewport.get("width", 4))
    height = int(viewport.get("height", 4))

    fixture_errors = validate_fixture(fixture)
    if fixture_errors:
        errors.append({
            "code": "tixl_mesh_draw_explicit_adapter.invalid_fixture_expectations",
            "message": "Fixture expectations must keep the explicit adapter proof bounded.",
            "mismatches": fixture_errors,
        })
        return default_result(graph_id, "blocked_invalid_fixture_expectations"), trace, errors, None

    paths = {
        "strategy": resolve_path(repo_root, fixture_path, fixture.get("explicitTranslationStrategyArtifact"), DEFAULT_STRATEGY),
        "mslApprox": resolve_path(repo_root, fixture_path, fixture.get("mslApproxArtifact"), DEFAULT_MSL_APPROX),
        "metalExplicitMsl": resolve_path(repo_root, fixture_path, fixture.get("metalExplicitMslArtifact"), DEFAULT_METAL_EXPLICIT),
    }
    trace.append({"op": "resolveInputArtifacts", **{key: display_path(path, repo_root) for key, path in paths.items()}})

    strategy = read_json(paths["strategy"], errors, "tixl_mesh_draw_explicit_adapter.strategy_read_failed", repo_root)
    msl_approx = read_json(paths["mslApprox"], errors, "tixl_mesh_draw_explicit_adapter.msl_approx_read_failed", repo_root)
    metal_explicit = read_json(paths["metalExplicitMsl"], errors, "tixl_mesh_draw_explicit_adapter.metal_explicit_read_failed", repo_root)
    trace.append({
        "op": "readInputArtifacts",
        "strategyRead": strategy is not None,
        "mslApproxRead": msl_approx is not None,
        "metalExplicitMslRead": metal_explicit is not None,
    })
    if None in (strategy, msl_approx, metal_explicit):
        return default_result(graph_id, "blocked_missing_input_artifact"), trace, errors, None

    artifact_errors: list[dict[str, Any]] = []
    artifact_errors.extend(validate_strategy(strategy))
    artifact_errors.extend(validate_msl_approx(msl_approx))
    artifact_errors.extend(validate_metal_explicit(metal_explicit))
    trace.append({
        "op": "validateInputArtifacts",
        "strategyKind": strategy.get("kind"),
        "mslApproxKind": msl_approx.get("kind"),
        "metalExplicitKind": metal_explicit.get("kind"),
        "valid": not artifact_errors,
    })
    if artifact_errors:
        errors.append({
            "code": "tixl_mesh_draw_explicit_adapter.invalid_input_artifact",
            "message": "Input artifacts must prove selected explicit strategy/MSL evidence without widened parity claims.",
            "mismatches": artifact_errors,
        })
        return default_result(graph_id, "blocked_invalid_input_artifact"), trace, errors, None

    if width < 1 or height < 1:
        errors.append({"code": "tixl_mesh_draw_explicit_adapter.invalid_viewport", "width": width, "height": height})
        return default_result(graph_id, "blocked_invalid_viewport"), trace, errors, None

    build_dir = Path(tempfile.mkdtemp(prefix="tixl-explicit-adapter-build-"))
    try:
        msl_path = build_dir / MSL_NAME
        probe_source_path = build_dir / "explicit_adapter_probe.mm"
        probe_bin = build_dir / "explicit_adapter_probe"
        write_text(msl_path, MSL_SOURCE)
        write_text(probe_source_path, PROBE_CPP_SOURCE)
        trace.append({"op": "writeExplicitAdapterMsl", "artifact": MSL_NAME})

        build = subprocess.run(
            [
                "xcrun",
                "clang++",
                "-std=c++17",
                "-fobjc-arc",
                "-framework",
                "Metal",
                "-framework",
                "Foundation",
                str(probe_source_path),
                "-o",
                str(probe_bin),
            ],
            cwd=repo_root,
            text=True,
            capture_output=True,
        )
        trace.append({"op": "buildExplicitAdapterMetalProbe", "compiler": "xcrun clang++", "exitCode": build.returncode})
        if build.returncode != 0:
            errors.append({
                "code": "tixl_mesh_draw_explicit_adapter.probe_build_failed",
                "message": clean_message(build.stderr or build.stdout or "probe build failed", repo_root),
            })
            return default_result(graph_id, "blocked_probe_build_failed"), trace, errors, None

        run = subprocess.run(
            [str(probe_bin), str(msl_path), str(width), str(height)],
            cwd=repo_root,
            text=True,
            capture_output=True,
        )
    finally:
        shutil.rmtree(build_dir, ignore_errors=True)

    trace.append({"op": "runExplicitAdapterMetalProbe", "exitCode": run.returncode})
    probe = parse_probe_payload(run.stdout)
    if probe is None:
        errors.append({
            "code": "tixl_mesh_draw_explicit_adapter.probe_output_invalid",
            "message": clean_message(run.stderr or run.stdout or "probe did not emit JSON", repo_root),
        })
        return default_result(graph_id, "blocked_probe_output_invalid"), trace, errors, None

    if run.returncode != 0 or probe.get("ok") is not True:
        errors.append(error_from_probe(probe))
        return result_payload(graph_id, str(probe.get("status") or "probe_failed"), paths, strategy, msl_approx, metal_explicit, probe, False, repo_root), trace, errors, None

    frame_stats = frame_stats_from_probe(probe)
    result = result_payload(graph_id, "proven_explicit_mesh_draw_adapter", paths, strategy, msl_approx, metal_explicit, probe, True, repo_root)
    result["frameStats"] = frame_stats
    trace.append({"op": "validateExplicitAdapterMetalProbe", "status": probe.get("status"), "valid": True})
    trace.append({"op": "proveExplicitAdapterScopeOnly", "scope": "handwritten_explicit_msl_adapter", "backendReplacementReady": False})
    return result, trace, errors, frame_stats


def validate_fixture(fixture: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if fixture.get("kind") != "TixlMeshDrawExplicitAdapterProof":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawExplicitAdapterProof", "actual": fixture.get("kind")})
    adapter_scope = fixture.get("adapterScope") if isinstance(fixture.get("adapterScope"), dict) else {}
    if adapter_scope.get("name") != "handwritten_explicit_msl_adapter":
        mismatches.append({"field": "adapterScope.name", "expected": "handwritten_explicit_msl_adapter", "actual": adapter_scope.get("name")})
    if adapter_scope.get("proofScope") != "explicit_adapter_only":
        mismatches.append({"field": "adapterScope.proofScope", "expected": "explicit_adapter_only", "actual": adapter_scope.get("proofScope")})
    if adapter_scope.get("fullPbrResourceBindingConsumed") is not False:
        mismatches.append({"field": "adapterScope.fullPbrResourceBindingConsumed", "expected": False, "actual": adapter_scope.get("fullPbrResourceBindingConsumed")})
    expected_claims = fixture.get("expected", {}).get("claims") if isinstance(fixture.get("expected"), dict) else None
    if expected_claims != EXPECTED_CLAIMS:
        mismatches.append({"field": "expected.claims", "expected": EXPECTED_CLAIMS, "actual": expected_claims})
        if isinstance(expected_claims, dict):
            for key, expected in EXPECTED_CLAIMS.items():
                if expected_claims.get(key) != expected:
                    mismatches.append({"field": f"expected.claims.{key}", "expected": expected, "actual": expected_claims.get(key)})
    return mismatches


def validate_strategy(strategy: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    expect_field(mismatches, "strategy.kind", "TixlMeshDrawExplicitTranslationStrategy", strategy.get("kind"))
    expect_field(mismatches, "strategy.ok", True, strategy.get("ok"))
    expect_field(mismatches, "strategy.status", "selected_handwritten_explicit_msl_adapter", strategy.get("status"))
    expect_field(mismatches, "strategy.selectedStrategy", "handwritten_explicit_msl_adapter", strategy.get("selectedStrategy"))
    claims = strategy.get("claims") if isinstance(strategy.get("claims"), dict) else {}
    expect_field(mismatches, "strategy.claims.hlslToMslTranslation", False, claims.get("hlslToMslTranslation"))
    expect_field(mismatches, "strategy.claims.backendReplacementReady", False, claims.get("backendReplacementReady"))
    expect_field(mismatches, "strategy.claims.fullPbrResourceBinding", False, claims.get("fullPbrResourceBinding"))
    expect_field(mismatches, "strategy.claims.tixlRuntimeParity", False, claims.get("tixlRuntimeParity"))
    expect_field(mismatches, "strategy.claims.pbrVisualCorrectness", False, claims.get("pbrVisualCorrectness"))
    observed = strategy.get("evidence", {}).get("observedAdapter") if isinstance(strategy.get("evidence"), dict) else {}
    if observed.get("boundRegisters") != ["t0", "t1"]:
        mismatches.append({"field": "strategy.evidence.observedAdapter.boundRegisters", "expected": ["t0", "t1"], "actual": observed.get("boundRegisters")})
    return mismatches


def validate_msl_approx(msl_approx: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    expect_field(mismatches, "mslApprox.kind", "TixlMeshDrawMslApproxProof", msl_approx.get("kind"))
    expect_field(mismatches, "mslApprox.ok", True, msl_approx.get("ok"))
    expect_field(mismatches, "mslApprox.status", "rendered_tixl_mesh_draw_msl_approximation", msl_approx.get("status"))
    claims = msl_approx.get("claims") if isinstance(msl_approx.get("claims"), dict) else {}
    expect_field(mismatches, "mslApprox.claims.actualCompilerRan", True, claims.get("actualCompilerRan"))
    expect_field(mismatches, "mslApprox.claims.actualMetalRan", True, claims.get("actualMetalRan"))
    expect_field(mismatches, "mslApprox.claims.mslApproximationRendered", True, claims.get("mslApproximationRendered"))
    expect_field(mismatches, "mslApprox.claims.mslApproxBufferPackingObserved", True, claims.get("mslApproxBufferPackingObserved"))
    expect_field(mismatches, "mslApprox.claims.hlslToMslTranslation", False, claims.get("hlslToMslTranslation"))
    expect_field(mismatches, "mslApprox.claims.tixlRuntimeParity", False, claims.get("tixlRuntimeParity"))
    expect_field(mismatches, "mslApprox.claims.pbrVisualCorrectness", False, claims.get("pbrVisualCorrectness"))
    expect_field(mismatches, "mslApprox.claims.drawMeshRuntime", False, claims.get("drawMeshRuntime"))
    frame = msl_approx.get("frameStats") if isinstance(msl_approx.get("frameStats"), dict) else {}
    expect_field(mismatches, "mslApprox.frameStats.nonBlack", True, frame.get("nonBlack"))
    expect_field(mismatches, "mslApprox.frameStats.varied", True, frame.get("varied"))
    return mismatches


def validate_metal_explicit(metal_explicit: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    expect_field(mismatches, "metalExplicit.kind", "MetalExplicitMslProof", metal_explicit.get("kind"))
    expect_field(mismatches, "metalExplicit.ok", True, metal_explicit.get("ok"))
    expect_field(mismatches, "metalExplicit.status", "rendered", metal_explicit.get("status"))
    claims = metal_explicit.get("claims") if isinstance(metal_explicit.get("claims"), dict) else {}
    expect_field(mismatches, "metalExplicit.claims.actualCompilerRan", True, claims.get("actualCompilerRan"))
    expect_field(mismatches, "metalExplicit.claims.actualMetalRan", True, claims.get("actualMetalRan"))
    for key in FORBIDDEN_TRUE_CLAIMS:
        if claims.get(key) is True:
            mismatches.append({"field": f"metalExplicit.claims.{key}", "expected": False, "actual": True})
    frame = metal_explicit.get("frameStats") if isinstance(metal_explicit.get("frameStats"), dict) else {}
    expect_field(mismatches, "metalExplicit.frameStats.nonBlack", True, frame.get("nonBlack"))
    expect_field(mismatches, "metalExplicit.frameStats.varied", True, frame.get("varied"))
    return mismatches


def expect_field(mismatches: list[dict[str, Any]], field: str, expected: Any, actual: Any) -> None:
    if actual != expected:
        mismatches.append({"field": field, "expected": expected, "actual": actual})


def result_payload(
    graph_id: Any,
    status: str,
    paths: dict[str, Path],
    strategy: dict[str, Any],
    msl_approx: dict[str, Any],
    metal_explicit: dict[str, Any],
    probe: dict[str, Any],
    proven: bool,
    repo_root: Path,
) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawExplicitAdapterProof",
        "graphId": graph_id,
        "ok": proven,
        "status": status,
        "backend": "Metal",
        "message": "compiled explicit adapter MSL, rendered offscreen, and proved only the named handwritten adapter scope" if proven else clean_text(str(probe.get("message", status))),
        "inputArtifacts": {
            "explicitTranslationStrategy": summarize_artifact(paths.get("strategy"), strategy, repo_root),
            "mslApprox": summarize_artifact(paths.get("mslApprox"), msl_approx, repo_root),
            "metalExplicitMsl": summarize_artifact(paths.get("metalExplicitMsl"), metal_explicit, repo_root),
        },
        "adapterScope": {
            "name": "handwritten_explicit_msl_adapter",
            "proofScope": "explicit_adapter_only",
            "fullPbrResourceBindingConsumed": False,
            "notBackendReplacement": True,
        },
        "actualMetalProbe": {
            "status": probe.get("status"),
            "actualCompilerRan": bool(probe.get("actualCompilerRan")),
            "actualMetalRan": bool(probe.get("actualMetalRan")),
            "generatedMslArtifact": MSL_NAME,
            "color0": probe.get("color0"),
        },
        "frameStats": frame_stats_from_probe(probe) if proven else None,
        "claims": claim_flags(proven),
    }


def default_result(graph_id: Any, status: str) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawExplicitAdapterProof",
        "graphId": graph_id,
        "ok": False,
        "status": status,
        "backend": "Metal",
        "claims": claim_flags(False),
    }


def claim_flags(proven: bool) -> dict[str, bool]:
    return {
        "explicitTranslationStrategyArtifactConsumed": proven,
        "mslApproxArtifactConsumed": proven,
        "metalExplicitMslArtifactConsumed": proven,
        "selectedHandwrittenAdapterStrategyConsumed": proven,
        "mslApproxEvidenceConsumed": proven,
        "metalExplicitMslEvidenceConsumed": proven,
        "actualCompilerRan": proven,
        "actualMetalRan": proven,
        "explicitAdapterProof": proven,
        "explicitAdapterProofPresent": proven,
        "fullPbrResourceBinding": False,
        "backendReplacementReady": False,
        "hlslToMslTranslation": False,
        "tixlRuntimeParity": False,
        "nativeGpuParityComplete": False,
        "pbrVisualCorrectness": False,
    }


def frame_stats_from_probe(probe: dict[str, Any]) -> dict[str, Any]:
    return {
        "width": probe.get("width"),
        "height": probe.get("height"),
        "byteCount": probe.get("byteCount"),
        "nonBlack": probe.get("nonBlack"),
        "varied": probe.get("varied"),
        "nonBlackPixels": probe.get("nonBlackPixels"),
        "uniqueColorSamples": probe.get("uniqueColorSamples"),
        "opaquePixels": probe.get("opaquePixels"),
        "frameDigest": probe.get("frameDigest"),
    }


def summarize_artifact(path: Path | None, artifact: dict[str, Any], repo_root: Path) -> dict[str, Any]:
    return {
        "path": display_path(path, repo_root),
        "kind": artifact.get("kind"),
        "ok": artifact.get("ok"),
        "status": artifact.get("status"),
    }


def error_from_probe(probe: dict[str, Any]) -> dict[str, Any]:
    status = str(probe.get("status") or "probe_failed")
    code = {
        "blocked_metal_device_unavailable": "tixl_mesh_draw_explicit_adapter.device_unavailable",
        "compile_failed": "tixl_mesh_draw_explicit_adapter.compile_failed",
        "pipeline_failed": "tixl_mesh_draw_explicit_adapter.pipeline_failed",
        "render_failed": "tixl_mesh_draw_explicit_adapter.render_failed",
        "readback_mismatch": "tixl_mesh_draw_explicit_adapter.readback_mismatch",
    }.get(status, "tixl_mesh_draw_explicit_adapter.probe_failed")
    return {"code": code, "message": clean_text(str(probe.get("message") or status))}


def parse_probe_payload(stdout: str) -> dict[str, Any] | None:
    text = stdout.strip()
    if not text:
        return None
    try:
        return json.loads(text.splitlines()[-1])
    except json.JSONDecodeError:
        return None


def resolve_path(repo_root: Path, fixture_path: Path, maybe_path: Any, default_path: str) -> Path:
    if not isinstance(maybe_path, str) or not maybe_path:
        return (repo_root / default_path).resolve()
    path = Path(maybe_path).expanduser()
    if path.is_absolute():
        return path
    repo_candidate = repo_root / path
    if repo_candidate.exists() or str(maybe_path).startswith("docs/"):
        return repo_candidate.resolve()
    return (fixture_path.parent / path).resolve()


def read_json(path: Path, errors: list[dict[str, Any]], code: str, repo_root: Path) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": display_path(path, repo_root), "message": clean_message(str(exc), repo_root)})
        return None


def publish(
    out_dir: Path,
    result: dict[str, Any],
    trace: list[dict[str, Any]],
    errors: list[dict[str, Any]],
    frame_stats: dict[str, Any] | None,
) -> None:
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / TRACE_NAME, trace)
    write_json(out_dir / ERRORS_NAME, errors)
    if frame_stats is not None:
        write_json(out_dir / FRAME_STATS_NAME, frame_stats)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


def write_text(path: Path, payload: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(payload, encoding="utf8")


def clear_optional_artifacts(out_dir: Path) -> None:
    for name in (FRAME_STATS_NAME,):
        target = out_dir / name
        if target.exists():
            target.unlink()


def clean_text(text: str) -> str:
    return " ".join(text.split())


def clean_message(text: str, repo_root: Path) -> str:
    return clean_text(text.replace(str(repo_root), "."))


def display_path(path: Path | None, repo_root: Path) -> str | None:
    if path is None:
        return None
    try:
        return str(path.resolve().relative_to(repo_root))
    except ValueError:
        return f"outside_repo/{path.name}"


if __name__ == "__main__":
    raise SystemExit(main())
