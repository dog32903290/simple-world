#!/usr/bin/env python3
"""
TextureView identity API for the native runtime lane.

This is not a GPU backend. It models Texture2D resources and typed views as
replayable data so the next Metal/DX11 layer has a stable contract to implement.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


@dataclass(frozen=True)
class Texture2DHandle:
    id: str
    width: int
    height: int
    format: str
    bindFlags: tuple[str, ...] = field(default_factory=tuple)
    optionFlags: tuple[str, ...] = field(default_factory=tuple)
    arraySize: int = 1
    sampleCount: int = 1
    owner: str = ""
    role: str = ""
    disposed: bool = False

    @staticmethod
    def from_json(payload: dict[str, Any]) -> "Texture2DHandle":
        return Texture2DHandle(
            id=str(payload["id"]),
            width=int(payload.get("width", 1)),
            height=int(payload.get("height", 1)),
            format=str(payload.get("format", "R16G16B16A16_Float")),
            bindFlags=tuple(payload.get("bindFlags", ())),
            optionFlags=tuple(payload.get("optionFlags", ())),
            arraySize=int(payload.get("arraySize", 1)),
            sampleCount=int(payload.get("sampleCount", 1)),
            owner=str(payload.get("owner", "")),
            role=str(payload.get("role", "")),
            disposed=bool(payload.get("disposed", False)),
        )

    def to_json(self) -> dict[str, Any]:
        return {
            "kind": "Texture2D",
            "id": self.id,
            "owner": self.owner,
            "role": self.role,
            "width": self.width,
            "height": self.height,
            "format": self.format,
            "arraySize": self.arraySize,
            "sampleCount": self.sampleCount,
            "bindFlags": list(self.bindFlags),
            "optionFlags": list(self.optionFlags),
            "disposed": self.disposed,
        }


@dataclass(frozen=True)
class TextureViewHandle:
    ok: bool
    textureId: str | None
    type: str
    reason: str | None = None
    format: str | None = None
    dimension: str | None = None
    firstArraySlice: int | None = None
    arraySize: int | None = None

    def to_json(self) -> dict[str, Any]:
        payload: dict[str, Any] = {
            "ok": self.ok,
            "textureId": self.textureId,
            "type": self.type,
        }
        for key in ["reason", "format", "dimension", "firstArraySlice", "arraySize"]:
            value = getattr(self, key)
            if value is not None:
                payload[key] = value
        return payload


class TextureResourceRegistry:
    """Owns Texture2DHandle identity and derived TextureViewHandle records."""

    def __init__(self) -> None:
        self.resources: dict[str, Texture2DHandle] = {}
        self.views: dict[str, TextureViewHandle] = {}

    def register_texture(self, payload: dict[str, Any] | Texture2DHandle) -> Texture2DHandle:
        texture = payload if isinstance(payload, Texture2DHandle) else Texture2DHandle.from_json(payload)
        self.resources[texture.id] = texture
        return texture

    def needs_reallocate(self, texture_id: str, payload: dict[str, Any]) -> bool:
        current = self.resources.get(texture_id)
        if current is None or current.disposed:
            return True
        candidate = Texture2DHandle.from_json(payload)
        return any([
            current.width != candidate.width,
            current.height != candidate.height,
            current.format != candidate.format,
            current.bindFlags != candidate.bindFlags,
            current.optionFlags != candidate.optionFlags,
            current.arraySize != candidate.arraySize,
            current.sampleCount != candidate.sampleCount,
        ])

    def allocate_or_reuse_texture(self, payload: dict[str, Any]) -> tuple[str, Texture2DHandle]:
        texture_id = str(payload["id"])
        if self.needs_reallocate(texture_id, payload):
            if texture_id in self.resources:
                self.dispose_texture(texture_id)
                action = "reallocate"
            else:
                action = "allocate"
            return action, self.register_texture(payload)
        return "reuse", self.resources[texture_id]

    def dispose_texture(self, texture_id: str) -> None:
        texture = self.resources.get(texture_id)
        if texture is None:
            return

        disposed = Texture2DHandle(
            id=texture.id,
            width=texture.width,
            height=texture.height,
            format=texture.format,
            bindFlags=texture.bindFlags,
            optionFlags=texture.optionFlags,
            arraySize=texture.arraySize,
            sampleCount=texture.sampleCount,
            owner=texture.owner,
            role=texture.role,
            disposed=True,
        )
        self.resources[texture_id] = disposed
        for view_id, view in list(self.views.items()):
            if view.textureId == texture_id:
                self.views[view_id] = TextureViewHandle(
                    ok=False,
                    textureId=texture_id,
                    type=view.type,
                    reason="source texture disposed",
                )

    def create_view(self, texture_id: str, view_kind: str, arrayIndex: int = 0) -> TextureViewHandle:
        texture = self.resources.get(texture_id)
        view = create_texture_view(texture, view_kind, arrayIndex=arrayIndex)
        self.views[f"{texture_id}.{view_kind.lower()}"] = view
        return view

    def to_json(self) -> dict[str, Any]:
        return {
            "resources": {
                texture_id: texture.to_json()
                for texture_id, texture in self.resources.items()
            },
            "views": {
                view_id: view.to_json()
                for view_id, view in self.views.items()
            },
        }


def create_texture_view(
    texture: Texture2DHandle | None,
    view_kind: str,
    *,
    arrayIndex: int = 0,
) -> TextureViewHandle:
    kind = view_kind.lower()
    view_type = kind.upper()

    if texture is None:
        return TextureViewHandle(ok=False, textureId=None, type=view_type, reason="missing texture")
    if texture.disposed:
        return TextureViewHandle(ok=False, textureId=texture.id, type=view_type, reason="disposed texture")

    flags = set(texture.bindFlags)
    options = set(texture.optionFlags)

    if kind == "srv":
        if "DepthStencil" in flags:
            return TextureViewHandle(ok=True, textureId=texture.id, type="SRV", format="R32_Float")
        if "ShaderResource" not in flags:
            return TextureViewHandle(ok=False, textureId=texture.id, type="SRV", reason="missing ShaderResource bind flag")
        return TextureViewHandle(ok=True, textureId=texture.id, type="SRV", format=texture.format)

    if kind == "uav":
        if "UnorderedAccess" not in flags:
            return TextureViewHandle(ok=False, textureId=texture.id, type="UAV", reason="missing UnorderedAccess bind flag")
        return TextureViewHandle(ok=True, textureId=texture.id, type="UAV")

    if kind == "rtv":
        if "RenderTarget" not in flags:
            return TextureViewHandle(ok=False, textureId=texture.id, type="RTV", reason="missing RenderTarget bind flag")
        if "TextureCube" in options:
            return TextureViewHandle(
                ok=True,
                textureId=texture.id,
                type="RTV",
                dimension="Texture2DArray",
                firstArraySlice=0,
                arraySize=6,
            )
        requested = max(0, min(int(arrayIndex), 10000))
        resolved = min(requested, max(texture.arraySize - 1, 0))
        if resolved > 0:
            return TextureViewHandle(
                ok=True,
                textureId=texture.id,
                type="RTV",
                dimension="Texture2DArray",
                firstArraySlice=resolved,
                arraySize=1,
            )
        return TextureViewHandle(ok=True, textureId=texture.id, type="RTV", dimension="Texture2D")

    if kind == "dsv":
        if "DepthStencil" not in flags:
            return TextureViewHandle(ok=False, textureId=texture.id, type="DSV", reason="missing DepthStencil bind flag")
        return TextureViewHandle(ok=True, textureId=texture.id, type="DSV", format="D32_Float", dimension="Texture2D")

    raise ValueError(f"unknown view kind: {view_kind}")


def allocate_render_target_resources(render_target: dict[str, Any]) -> TextureResourceRegistry:
    registry = TextureResourceRegistry()
    width = int(render_target.get("resolution", {}).get("width", 320))
    height = int(render_target.get("resolution", {}).get("height", 180))
    owner = str(render_target.get("id", "renderTarget"))
    color = render_target.get("colorBuffer", {})
    depth = render_target.get("depthBuffer", {})

    registry.register_texture({
        "id": color.get("id", "rt.color"),
        "owner": owner,
        "role": "ColorBuffer",
        "width": width,
        "height": height,
        "format": render_target.get("format", "R16G16B16A16_Float"),
        "arraySize": color.get("arraySize", 1),
        "sampleCount": render_target.get("multisampling", 1),
        "bindFlags": color.get("bindFlags", ["RenderTarget", "ShaderResource"]),
        "optionFlags": color.get("optionFlags", []),
    })
    registry.register_texture({
        "id": depth.get("id", "rt.depth"),
        "owner": owner,
        "role": "DepthBuffer",
        "width": width,
        "height": height,
        "format": depth.get("format", "D32_Float"),
        "arraySize": depth.get("arraySize", 1),
        "sampleCount": render_target.get("multisampling", 1),
        "bindFlags": depth.get("bindFlags", ["DepthStencil", "ShaderResource"]),
        "optionFlags": depth.get("optionFlags", []),
    })
    return registry


def create_default_texture_views(registry: TextureResourceRegistry) -> dict[str, Any]:
    for texture_id in list(registry.resources):
        registry.create_view(texture_id, "srv")
        registry.create_view(texture_id, "rtv")
        registry.create_view(texture_id, "uav")
        registry.create_view(texture_id, "dsv")
    return {"views": registry.to_json()["views"]}
