---
name: per-app-build-layout
description: "DONE 2026-09-12 - one exe per app under apps/<name>/ with its own makefile+main.cpp+assets; root makefile, main.cpp, data/ and shaders/ all deleted; core/ is app-agnostic and compiles once into build/core"
metadata: 
  node_type: memory
  type: project
  originSessionId: 321ef6c3-8b60-4cdc-8703-73b7f520e3b5
  modified: 2026-09-12T20:34:54.603Z
---

The whole build and asset reorganisation landed on 2026-09-12. Supersedes the old
"one wind.exe, `make APP=X`" arrangement described in [[project-overview]] and
[[build-toolchain-location]]. Full write-up in `docs/asset_layout_plan.md`.

**Layout:** `apps/<name>/` holds an app's `makefile`, `main.cpp`, gameplay classes, `assets/` and
`build/<name>.exe`. Twelve of them: animation breakout dozer grid isoanimation ocpp ship sim tank
tetris tileset ui. **`data/`, `shaders/`, `fonts/`, the root `makefile`, the root `main.cpp`, every
`apps/*.mk` and `.current_app` are all deleted.** `isoterrain/` stays at the root as a genuine
shared library (Grid, IsoAnimation, Tileset) declared via `LIB_DIRS`.

**Building:** `cd apps/<name> && mingw32-make.exe -j8`, run `./build/<name>.exe`. **One app at a
time** - `build/core` is shared, so concurrent builds race on the same objects.

**Why core could be shared:** only `core/File.cpp` was ever app-coupled (via `APP_ASSET_PATH`), and
`main.cpp` (via `APP_HEADER`/`APP_CLASS`). Both are gone - each app's own `main.cpp` names its class
directly and calls `AddAssetSearchRootFromExe()` for its roots. `engine.mk` freezes the
app-invariant flags as `CORE_CFLAGS` at a marked line; **a flag added above that line silently
corrupts every other app's objects, because make compares timestamps and not flags.**

**Asset naming:** `<category>/<file>` - `meshes/tank.glb`, `shaders/default.vert`. Roots are
searched in order, app's own first, so an app overrides a shared shader by having one of the same
name. Cache key is the name as given, never the resolved path (the packed build depends on that).

**The trap that costs a session:** every app binds MCP to 127.0.0.1:8765. A second app starts fine
while its server silently fails to bind, so `screenshot` returns the FIRST app's window. **A
screenshot showing the wrong game is always this.** `netstat -ano | grep 8765` names the holder.

**Anything that opens a file itself** rather than via `LoadFile` must resolve first with
`ResolveAssetPath` / `ResolveAssetDirectory`. Three do: `Directory::GetFiles`, `HTTPServer`'s
hot-reload watcher, `Texture::LoadHDRFromFile` (`stbi_loadf` opens its own file). A fourth would
present as a missing file that is plainly on disk.

`reference/` now holds the non-compilable material (`shaders/` for the shadertoy excerpts,
`code_snippets/` for what was in `temp/`); `temp/` and `obs_handgame/` are gone and `.blend`
sources live in `art_source/`. Authoring sources that belong to one app stayed with it -
`apps/dozer/audio_source/`, `apps/sim/icon_source/`.

Still open: `DUMP_BINARYASSETS` removal (see [[asset-layout-plan]] §5) and deleting `OBJLoader`
itself (§5b - still used by Grid and Sim, needs 14 .obj converted to .glb).
