#!/usr/bin/env python3
"""
Run ResourceLifetime allocation/reuse/reallocation/dispose proof.

This shell uses native_resource_api.py for Texture2D and TextureView identity.
It is not a GPU allocator.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

from native_resource_api import TextureResourceRegistry


def main() -> int:
    if len(sys.argv) not in {3, 4}:
        print("usage: resource_lifetime_shell.py <resource_lifetime.graph.json> <out_dir> [resource_access_ledger.json]", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    resource_access_ledger_path = Path(sys.argv[3]).expanduser().resolve() if len(sys.argv) == 4 else None
    out_dir.mkdir(parents=True, exist_ok=True)

    errors: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "resource_lifetime.fixture_read_failed")
    resource_access_ledger = read_json(resource_access_ledger_path, errors, "resource_lifetime.resource_access_ledger_read_failed") if resource_access_ledger_path else None
    if fixture is None or errors:
        write_artifacts(out_dir, [], {}, {}, errors)
        return 1

    trace, registry_json, invalidation_ledger, run_errors = run_lifetime(fixture, resource_access_ledger)
    errors.extend(run_errors)
    write_artifacts(out_dir, trace, registry_json, invalidation_ledger, errors)
    return 0 if not errors else 1


def run_lifetime(
    fixture: dict[str, Any],
    resource_access_ledger: dict[str, Any] | None = None,
) -> tuple[list[dict[str, Any]], dict[str, Any], dict[str, Any], list[dict[str, Any]]]:
    registry = TextureResourceRegistry()
    trace: list[dict[str, Any]] = [{"op": "loadResourceLifetimeGraph", "graphId": fixture.get("graphId")}]
    errors: list[dict[str, Any]] = []
    invalidated: list[dict[str, Any]] = []
    frames = frames_from_render_graph_ledger(resource_access_ledger, trace, errors) if resource_access_ledger else fixture.get("frames", [])

    for frame in frames:
        frame_index = frame.get("frameIndex")
        trace.append({"op": "frame.begin", "frameIndex": frame_index})
        frame_resources = []
        for resource in frame.get("resources", []):
            payload = texture_payload(resource)
            before_views = registry.to_json()["views"]
            action, texture = registry.allocate_or_reuse_texture(payload)
            after_dispose_views = registry.to_json()["views"]
            invalidated.extend(find_invalidated_views(frame_index, resource["id"], before_views, after_dispose_views))
            registry.create_view(texture.id, "srv")
            if "RenderTarget" in texture.bindFlags:
                registry.create_view(texture.id, "rtv")
            if "DepthStencil" in texture.bindFlags:
                registry.create_view(texture.id, "dsv")
            trace.append({
                "op": "resource." + action,
                "frameIndex": frame_index,
                "resourceId": texture.id,
                "resolution": {"width": texture.width, "height": texture.height},
                "format": texture.format,
            })
            frame_resources.append(resource)
        for resource in frame_resources:
            if resource.get("disposePolicy") == "endOfFrame":
                before_views = registry.to_json()["views"]
                registry.dispose_texture(resource["id"])
                after_views = registry.to_json()["views"]
                invalidated.extend(find_invalidated_views(frame_index, resource["id"], before_views, after_views))
                trace.append({
                    "op": "resource.dispose",
                    "frameIndex": frame_index,
                    "resourceId": resource["id"],
                    "reason": "endOfFrame",
                })
        trace.append({"op": "frame.end", "frameIndex": frame_index})

    registry_json = registry.to_json()
    invalidation_ledger = {
        "invalidatedViews": invalidated,
        "count": len(invalidated),
    }
    trace.append({"op": "publishResourceLifetimeArtifacts", "ok": not errors})
    return trace, registry_json, invalidation_ledger, errors


def frames_from_render_graph_ledger(
    resource_access_ledger: dict[str, Any],
    trace: list[dict[str, Any]],
    errors: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    resources = resource_access_ledger.get("resources", {})
    latest_access = resource_access_ledger.get("latestAccess", {})
    if not resources:
        errors.append({"code": "resource_lifetime.render_graph_resources_missing"})
        return []
    frame_resources = []
    for resource_id, resource in resources.items():
        if resource.get("kind") != "Texture2D":
            errors.append({
                "code": "resource_lifetime.unsupported_render_graph_resource_kind",
                "resourceId": resource_id,
                "kind": resource.get("kind"),
            })
            continue
        frame_resources.append({
            **resource,
            "id": resource_id,
            "role": infer_role(resource_id, latest_access.get(resource_id, "")),
            "bindFlags": infer_bind_flags(resource_id, latest_access.get(resource_id, ""), resource_access_ledger),
            "lifetime": resource.get("lifetime", "frame"),
            "disposePolicy": "afterFramePublish",
        })
    trace.append({
        "op": "deriveResourcesFromRenderGraph",
        "resourceCount": len(frame_resources),
        "resourceIds": [resource["id"] for resource in frame_resources],
    })
    return [{
        "frameIndex": resource_access_ledger.get("frame", {}).get("frameIndex", 0),
        "resources": frame_resources,
    }]


def infer_role(resource_id: str, latest_access: str) -> str:
    if latest_access == "DepthStencilWrite" or resource_id.endswith(".depth"):
        return "DepthBuffer"
    return "ColorBuffer"


def infer_bind_flags(resource_id: str, latest_access: str, resource_access_ledger: dict[str, Any]) -> list[str]:
    access_values = [
        access
        for access_resource, access in resource_access_ledger.get("latestAccess", {}).items()
        if access_resource == resource_id
    ]
    access_values.extend([
        barrier.get("before")
        for barrier in resource_access_ledger.get("resourceBarriers", [])
        if barrier.get("resource") == resource_id
    ])
    access_values.extend([
        barrier.get("after")
        for barrier in resource_access_ledger.get("resourceBarriers", [])
        if barrier.get("resource") == resource_id
    ])
    access_set = set(filter(None, access_values + [latest_access]))
    flags: list[str] = []
    if "RenderTargetWrite" in access_set:
        flags.append("RenderTarget")
    if "DepthStencilWrite" in access_set:
        flags.append("DepthStencil")
    if access_set.intersection({"ShaderResourceRead", "FrameOutputRead"}):
        flags.append("ShaderResource")
    if not flags:
        flags.append("ShaderResource")
    return flags


def texture_payload(resource: dict[str, Any]) -> dict[str, Any]:
    resolution = resource.get("resolution", {})
    return {
        "id": resource["id"],
        "owner": resource.get("ownerPass", ""),
        "role": resource.get("role", ""),
        "width": resolution.get("width", 1),
        "height": resolution.get("height", 1),
        "format": resource.get("format", "R16G16B16A16_Float"),
        "bindFlags": resource.get("bindFlags", []),
        "optionFlags": resource.get("optionFlags", []),
        "arraySize": resource.get("arraySize", 1),
        "sampleCount": resource.get("sampleCount", 1),
    }


def find_invalidated_views(
    frame_index: int,
    resource_id: str,
    before_views: dict[str, Any],
    after_views: dict[str, Any],
) -> list[dict[str, Any]]:
    rows = []
    for view_id, before in before_views.items():
        after = after_views.get(view_id)
        if before.get("ok") is True and after and after.get("ok") is False:
            rows.append({
                "frameIndex": frame_index,
                "resourceId": resource_id,
                "viewId": view_id,
                "reason": after.get("reason"),
            })
    return rows


def read_json(path: Path, errors: list[dict[str, Any]], code: str) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": str(path), "message": str(exc)})
        return None


def write_artifacts(
    out_dir: Path,
    trace: list[dict[str, Any]],
    registry_json: dict[str, Any],
    invalidation_ledger: dict[str, Any],
    errors: list[dict[str, Any]],
) -> None:
    write_json(out_dir / "resource_lifetime_trace.json", trace)
    write_json(out_dir / "resource_registry.json", registry_json)
    write_json(out_dir / "view_invalidation_ledger.json", invalidation_ledger)
    write_json(out_dir / "resource_lifetime_errors.json", errors)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


if __name__ == "__main__":
    raise SystemExit(main())
