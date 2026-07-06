# simple_world — Licensing & GPL boundary

sw's own code is intended to be MIT-clean (see the TiXL attribution note below). The one place a
copyleft dependency enters the tree is **Ableton Link (GPLv2)**, and it is quarantined so the main
executable does NOT become a GPL-derived work.

## Ableton Link — dynamic-link GPL boundary (照 TiXL 的 DLL 模型)

**Decision (柏為, 2026-07-06):** clone TiXL's approach — Link lives in a *separate shared library loaded
at run time*, exactly as TiXL ships it as a native DLL behind `[DllImport]` (AbletonLinkDLL). The main
`simple_world` executable never statically links the GPL code; it links only a pure POD/pimpl interface.

### How it's built
- `app/third_party/ableton-link/` (GPLv2 — Copyright 2016 Ableton AG; see its `LICENSE.md` /
  `GNU-GPL-v2.0.md`, both retained) is the vendored Link + asio-standalone.
- CMake target **`sw_link` (SHARED)** is the ONLY thing that compiles the vendored Link/asio templates
  → all GPL object code lives in `libsw_link.dylib`.
- `app/src/platform/link_sync.h` is a pure POD/pimpl seam: `LinkSnapshot` (plain doubles/ints/bools) +
  an opaque `Impl*`. **Zero `ableton::` leakage.** Every other TU in the executable includes only this
  header, so the executable's translation units include NO GPL header.
- `link_sync_selftest.cpp` stays in the executable (it drives the pimpl API only, compiles no Link).
- The exe links `sw_link` dynamically; the dylib's install_name is `@rpath/libsw_link.dylib` and the exe
  carries an `@loader_path` rpath, so the bare (no-.app-bundle) binary finds the dylib beside it in
  `app/build/`.

### GPL-boundary gates (verified — re-run after any Link/CMake change)
```
# 1) NO static Link symbols in the executable (the exe is not a GPL-derived work):
nm -gU app/build/simple_world | grep -i ableton        # → EMPTY

# 2) The exe depends on the GPL boundary DYNAMICALLY (TiXL DLL model):
otool -L app/build/simple_world | grep sw_link          # → @rpath/libsw_link.dylib

# (sanity) the GPL object code really is in the dylib:
nm app/build/libsw_link.dylib | grep -ci ableton        # → thousands
```
Result at seam/link-dynamic: gate 1 EMPTY (also zero `asio` symbols), gate 2 lists
`@rpath/libsw_link.dylib`. The executable is MIT-clean; the GPL code is isolated in `libsw_link.dylib`,
which is a mere aggregation loaded at run time (the GPLv2's own "separate work … on a volume of a storage
or distribution medium" aggregation clause; the DLL-boundary model TiXL itself relies on).

**Distribution note:** because `libsw_link.dylib` is GPLv2, when sw is distributed the dylib must be
accompanied by its source/offer per GPLv2 §3 (the vendored `third_party/ableton-link/` satisfies this).
The main executable and the rest of sw are unaffected.

## TiXL attribution (MIT)
TiXL/Tooll3 is MIT-licensed. sw is a Mac/Metal clone of it and must retain TiXL's copyright notice and a
credit when distributed (a root `LICENSE` + credit line is still TODO — tracked separately). This file
records the GPL boundary only; it does not discharge the TiXL-attribution obligation.
