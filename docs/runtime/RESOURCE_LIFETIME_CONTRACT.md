# ResourceLifetime Contract

ResourceLifetime answers:

```text
when is a render resource allocated, reused, reallocated, disposed, and which views become invalid?
```

It sits below `RenderGraph` and above concrete GPU backends.

## Boundary

```text
RenderGraph resources -> ResourceLifetime -> Texture2DHandle / TextureViewHandle
```

This contract uses `native_resource_api.py` for texture and view identity. It is
still data-backed, not a Metal allocator.

## Lifetime Law

Each resource declares:

```text
id
kind
format
resolution
bindFlags
lifetime
resizePolicy
disposePolicy
```

If width, height, format, bind flags, options, array size, or sample count
change, the resource must be reallocated. Reallocation invalidates old views
before the new texture is registered.

## View Invalidation Law

Texture views do not own textures. When a texture is disposed or reallocated,
all existing views derived from that texture become invalid:

```text
ok: false
reason: source texture disposed
```

Downstream passes may not keep reading an old SRV/RTV/UAV/DSV as if it still
pointed at a valid backend object.

## First Proof

Fixture:

```text
docs/runtime/fixtures/resource_lifetime.graph.json
```

Runner:

```text
docs/runtime/scripts/resource_lifetime_shell.py
```

Artifacts:

```text
docs/runtime/artifacts/resource_lifetime/resource_lifetime_trace.json
docs/runtime/artifacts/resource_lifetime/resource_registry.json
docs/runtime/artifacts/resource_lifetime/view_invalidation_ledger.json
docs/runtime/artifacts/resource_lifetime/resource_lifetime_errors.json
```

The first proof runs three frames: allocate, reuse, resize/reallocate, then
dispose frame-lifetime resources.
