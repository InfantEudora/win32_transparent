# Asset layout: where files live, and who resolves them

Plan for splitting the one shared `data/` and `shaders/` into per-app asset folders, for a
`reference/` tree of material that is *not* build input, and for removing the
`DUMP_BINARYASSETS` build stage in favour of a standalone packer.

Written 2026-09-12 from a walk through the tree. Every fact below was measured against this
source, not recalled — line numbers are as of that date.

Nothing here is implemented yet. Section 4 is the agreed first step.

---

## 1. Where things stand

### 1.1 The build system has no idea assets exist

There is no asset variable, no copy step, no manifest. Every path is a **string literal in one
`.cpp`**, resolved relative to the process working directory by `LoadFile`
(`core/File.cpp:28`). That is worth stating plainly because it is the reason this whole
reorganisation is cheap: there is no build machinery to rewrite, only literals to change, and
they are grouped one app per file.

### 1.2 Ownership is already almost entirely disjoint

The flat `data/` hides a structure that exists anyway. Who loads what, today:

| App | From `data/` | From `shaders/` beyond the defaults |
|---|---|---|
| Grid | 22 files — tiles, trees, elf, hand, arrows, skybox cube, `test_texture_4096.png` | `skybox.*` |
| Ship | `ships.glb` | `raymarch_volume.frag`, `cloud_shadow.comp`, `noise3d.comp`, `density.glsl` |
| IsoAnimation | `isoanim.glb`, `christmas_photo_studio_01_2k.hdr` | `custom.frag`, `skybox.*` |
| Tileset | `cityandroads.glb` | — |
| Tank | `tank.glb` | — |
| Animation | `gwen_anim.glb` | — |
| Tetris | `glyphs_unispace.glb`, `sound/{bleep,click,floop,hax}.wav` | — |
| Breakout | `glyphs_unispace.glb`, `sound/{bleep,click,floop,hax}.wav` | `breakout_shield.frag` |
| Sim | `icons` (directory scan) | — |
| OCPP | `www/`, `modes.json` (via `core/HTTPServer.cpp`) | — |
| Dozer, UI | — | — |

**Only the glyph mesh and the four `.wav`s are shared between two apps.** Everything else has
exactly one owner. The split is not a reorganisation so much as writing down what is already true.

### 1.3 Four folders already do it the proposed way

This is the part that settles the design question, and it was the surprise of the walk-through:

```
dozer/data/       10 wavs + dozer.glb      loaded as "dozer/data/engine_idle.wav"
galaxy/data/      meshes/{ship,sphere,plane,sunhighlight}.obj
isocity/data/     car_horn_1.wav, icons/
isoterrain/data/  tile_terrain.obj
tank/             two heightmap pngs, loose in the source folder
```

So the tree already carries **two competing conventions** — assets next to their source, and
assets in the root `data/` — with no rule saying which applies. Dozer, Sim and Tileset use both at
once. Picking one is overdue; the question is only which.

### 1.4 What nothing loads at all

`data/` is 205 MB. A large slice of it is referenced nowhere in the tree — not code, not
`tools/`, not `docs/`:

- **`data/narration/` — 42 MB, 15 wavs, zero references.**
- **`girlgun.glb` — 23 MB, zero references.** Also `plant.glb` (2.6 MB),
  `panorama_studio.png` (3 MB), `test.glb`, `ship_003.obj`, `chara.obj`.
- Most of `data/textures/` (41 MB) is reachable only through the `.mtl` files of those orphaned
  `.obj`s.
- `example_desktop.png` and `performance_2070.png` are **readme screenshots** living in the
  runtime asset tree. They belong under `docs/`.
- `win32_transparent_msvc.zip` — a zip in the asset folder. Gitignored, so local clutter only.
- `data/www_example/` — reference HTML for the OCPP web UI. Only `data/www/` is ever served.

Roughly a third of `data/` is dead before a single file moves. Deleting it is independent of
everything else here and can happen first.

---

## 2. The target layout

```
assets/
  shared/          # loaded by core/, or by two or more apps
    shaders/       default.vert  default.frag  default_skinned.vert  deferred.frag
                   skybox.vert  skybox.frag  field.vert  field.frag
                   field_jfa.comp  ssao_compute.comp  texture.comp
    fonts/         consola.ttf  CascadiaMono.ttf
    meshes/        glyphs_unispace.glb
    sound/         bleep.wav  click.wav  floop.wav  hax.wav
  grid/            tiles, trees, elf, hand, arrows, skybox/, test_texture_4096.png
  ship/            ships.glb + shaders/{raymarch_volume.frag,cloud_shadow.comp,
                                        noise3d.comp,density.glsl}
  breakout/        shaders/breakout_shield.frag
  isoanimation/    isoanim.glb, christmas_photo_studio_01_2k.hdr, shaders/custom.frag
  tank/            tank.glb, the two heightmap pngs
  tileset/         cityandroads.glb
  animation/       gwen_anim.glb
  dozer/           the ten wavs + dozer.glb   (moved out of dozer/data/)
  sim/             icons/ + galaxy meshes
  ocpp/            www/, modes.json
```

One tree, not the `assets/` + `shared_assets/` sibling pair: two top-level asset trees read as
two unrelated things, where a nested `shared/` says *same tree, wider scope* in the path itself.

### 2.1 Why keyed on `APP` and not on the source folder

The tempting shortcut is to derive the asset root from `DIR_SRC`, which `apps/*.mk` already
declares — `dozer/data/` for free, no new variable. It does not survive contact with the tree:

**the source folders are libraries, not apps.** `isoterrain/` is pulled in by Grid, IsoAnimation
*and* Tileset; Tileset also pulls `isocity/`; Tank pulls `crane/`. Six of the twelve apps
(`Animation`, `UI`, `OCPP`, `Grid`, `Tileset`, `IsoAnimation`) have no folder that is theirs
alone. `APP` is unique by construction; a source folder is not.

That leaves three real scopes, and all three already exist in the tree:

| Scope | Owner | Example |
|---|---|---|
| shared | `core/` loads it, or ≥2 apps do | `default.vert`, `consola.ttf`, `glyphs_unispace.glb` |
| library | a folder used by several apps | `isoterrain/data/tile_terrain.obj`, `isocity/data/icons/` |
| app | one app | `tank.glb`, `breakout_shield.frag` |

Library assets stay next to their library. Only app-owned files move under `assets/<app>/`.

### 2.2 The membership rule

> **If `core/` loads it, it is shared. If a library loads it, it belongs to that library.
> Otherwise it belongs to the app that loads it.**

Checkable with a grep, which is the point — a rule nobody can verify is a rule that rots.

Note the rule is slightly wider than "shared holds only the default shaders": `core/Window.cpp`
loads `fonts/consola.ttf` and `core/Renderer.cpp` loads `deferred.frag`, `field*` and
`ssao_compute.comp` unconditionally. Those are core-owned too, and a build that omits them does
not render. The second clause (≥2 apps) exists for exactly two cases — the glyph mesh and the
four bleeps — and duplicating those into Tetris and Breakout would be the alternative.

---

## 3. Search paths, mirroring `IPATHS`

Each `apps/X.mk` declares an ordered list, the way it already declares include paths:

```make
APP_ASSETS += assets/tileset
APP_ASSETS += isocity/data
APP_ASSETS += isoterrain/data
```

with `assets/shared` appended by the makefile for every app. The list is passed to the compiler
as one define next to the existing `-DAPP_HEADER` / `-DAPP_CLASS` — the same mechanism, already
proven in that file:

```make
empty :=
space := $(empty) $(empty)
CFLAGS += -DAPP_ASSET_PATH=\"$(subst $(space),;,$(APP_ASSETS) assets/shared)\"
```

(`$(space)` is not built in — make has no literal for it, so the two lines above are how you get
one. Without them the `subst` silently does nothing and the define comes out space-separated.)

App code then names an asset with **no prefix at all**: `LoadMesh("tank.glb")`,
`Shader("default.vert","breakout_shield.frag")`. First match along the path wins, so an app that
wants its own take on a shared shader drops a same-named file in its own folder and nothing else
changes. That override is a genuine feature, not a side effect of tidying.

---

## 4. Step one: the resolver in `LoadFile`

**This is the agreed first piece of work,** and it is additive — it changes no behaviour until an
asset actually moves.

`core/File.cpp:28` is the right home and its own header already says so:

> *"this function is the one thing that decides WHERE the bytes come from — a file on disk, the
> BinaryAsset cache, an asset baked into the executable, and tomorrow perhaps an archive or the
> network."*

A search path is precisely that, and putting it anywhere else re-introduces the question
`LoadFile` exists to answer.

### 4.1 Order of resolution

1. Exact name, as given — so an already-resolved or absolute path still works.
2. Each entry of `APP_ASSET_PATH`, in order.
3. **`data/` and `shaders/`, as the last two entries.**

Step 3 is the migration hinge. With those fallbacks in place every literal in the tree today keeps
resolving, so the resolver can land on its own, be verified on its own, and be committed on its
own. Apps then move one at a time. The fallbacks come out when the last app is across, and their
removal is the thing that proves the migration finished.

### 4.2 The cache key question

`LoadFile` stores into `BinaryAsset` under the name it was handed (`core/File.cpp:58`), and
`GetBinaryAsset` looks up by the same string. If the resolver caches under the **name as given**
(`"tank.glb"`) rather than the path it resolved to, then the key is stable no matter where the
file physically sits — which is what the packer in §5 needs, and what makes a baked build and a
loose-files build agree. Decide this deliberately; it is easy to get backwards and the symptom is
a baked build silently falling through to disk.

### 4.3 Fix `OBJLoader` at the same time

`core/OBJLoader.cpp:241` and `:275` build texture paths by concatenating a literal:

```cpp
std::string whole_path = "data/" + std::string(diff_name);
```

So a `.mtl` beside `galaxy/data/meshes/ship.obj` has its textures looked up in the **root**
`data/`. That is already wrong today and becomes obviously wrong the moment anything moves.
`GetBasePath()` (`core/File.cpp:9`) exists for this and makes the lookup relative to the `.obj`,
which is what the format means.

---

## 5. Step two: remove `DUMP_BINARYASSETS`

**Decided 2026-09-12.** Packing becomes the job of a standalone generator — the arrangement the
Android build already uses — instead of a two-pass build driven by a makefile flag. Until that
generator is integrated, `BinaryAsset::assets[]` is simply the empty array that
`BinaryAssetMemoryEmpty.cpp` already provides.

This is safe to do now and in any order relative to the rest: **both flags are `0` in the tree
today**, so no build currently produced depends on any of it.

### 5.1 Removal surface

| Where | What |
|---|---|
| `makefile:21` | `DUMP_BINARYASSETS = 0` |
| `makefile:129-131` | the `ifeq` adding `-DDUMP_BINARYASSETS` |
| `core/BinaryAsset.cpp:139-233` | the whole `#ifdef` / `#else` / `#endif` block — both bodies |
| `core/BinaryAsset.h:81` | the `DumpBinaryAssets()` declaration |
| `ApplicationDozer.cpp:55` | call site |
| `ApplicationGrid.cpp:579` | call site |
| `ApplicationShip.cpp:617` | call site |
| `core/Application.cpp:170` | call site |

### 5.2 Three things that must survive

- **The consuming side is untouched.** `GetBinaryAsset`, `StoreBinaryAsset`, `Uncompress` and the
  `file_assets` deque stay exactly as they are. Removing the dump removes the *producer* only, and
  the producer has no runtime callers in any current build.
- **miniz stays.** `Uncompress` (`core/BinaryAsset.cpp:96`) still needs `tinfl_decompress_mem_to_heap`
  to read a baked asset. Only `tdefl_compress_mem_to_heap` at `:185` goes with the dump.
- **The `.name` / `.iscompressed` / `.compressed_*` layout is now a contract.** It stops being
  private to a function in this file and becomes the interface the external generator must emit
  against. Section 5.4 is the spec.

### 5.3 The call sites are not what they look like

In the default build `DumpBinaryAssets()` does **not** dump — the `#else` body
(`core/BinaryAsset.cpp:230`) calls `ListBinaryAssets()`. So all four call sites are in practice
*logging* calls wearing the wrong name, and that is how they have behaved in every build anyone
has run.

So the removal is not a straight delete: decide per call site whether the listing was wanted.
`ApplicationDozer.cpp:55` calls `assetmanager->ListAssets()` on the very next line, which suggests
it was. Replacing the four with `BinaryAsset::ListBinaryAssets()` keeps the behaviour and makes
the name honest; dropping them keeps startup quieter. Either is fine — pick one and be consistent,
but do not delete them believing they were no-ops.

### 5.4 What the generator has to emit

The current dump writes `BinaryAssetMemory.cpp`, which `makefile:74-78` swaps in for
`BinaryAssetMemoryEmpty.cpp` when `COMPILE_BINARYASSETS = 1`:

```cpp
#include "BinaryAsset.h"
int BinaryAsset::num_memory_assets = N;
static const uint8_t asset_data[] = { 0x.., ... };
BinaryAsset BinaryAsset::assets[] = {
    { .name="...", .iscompressed=1, .size=0, .offset=0, .data=NULL,
      .compressed_size=.., .compressed_offset=.., .compressed_data=(uint8_t*)&asset_data[..] },
    ...
};
```

Two constraints on whatever replaces it:

1. **Compress content plus its null terminator** — `size+1` bytes, as `:185` does today. That
   terminator is what makes a decompressed asset safe to hand to the GLSL compiler as a C string,
   and `StoreBinaryAsset` guarantees it on the disk path too. Bake `size` without it and shaders
   break in baked builds only.
2. **Key on the name `LoadFile` will ask for** — see §4.2. The generator and the resolver have to
   agree on prefixing, or a baked build silently falls through to disk and nobody notices until
   there is no disk.

### 5.5 Open: keep `COMPILE_BINARYASSETS`?

The flag and the `BinaryAssetMemory.cpp` / `BinaryAssetMemoryEmpty.cpp` swap are the natural
integration point for the generator — its output *is* that file. But with the dump gone, nothing
in-tree produces `BinaryAssetMemory.cpp`, so setting the flag to `1` breaks the build with a
missing source rather than a useful message.

Recommendation: **keep the swap, with a comment saying the file is generator output and where the
generator lives.** A dangling hook with an explanation costs one `ifeq`; re-deriving the
arrangement later costs an afternoon. Revisit when the generator lands and it may turn out the
packer wants to emit a pack file rather than a `.cpp`, at which point the swap goes anyway.

---

## 6. Step three: move the assets

Mechanical and per-app, because the literals are grouped one app per file. For each app:

1. `git mv` its files under `assets/<app>/`.
2. Add `APP_ASSETS` to `apps/<App>.mk`.
3. Strip the `data/` and `shaders/` prefixes from the literals in `ApplicationX.cpp`.
4. `mingw32-make.exe APP=X -j8`, run it, `screenshot` over MCP to confirm it still looks right.

Suggested order — smallest surface first, so the resolver is proven before Grid's 22 files:
**Tank → Tileset → Animation → Breakout → Tetris → Ship → IsoAnimation → Sim → Dozer → OCPP → Grid.**

Delete the `data/` and `shaders/` fallbacks after Grid. Anything still resolving through them at
that point was never found by the audit in §1.2 and wants investigating rather than moving.

---

## 7. `reference/` — material that is not build input

Not `examples/`: the apps *are* the examples this engine ships with, which is the framing in
`readme.md`, so that name is taken. `reference/` is for the shadertoy excerpt, the snippet from
another engine, the design notes — things kept to read, never to compile.

Already in the tree and belonging there:

| Now | Why |
|---|---|
| `shaders/shadertoy_nixie_tube.hlsl` | HLSL in the GLSL folder |
| `shaders/shadertoy_smoke_lights.hsls` | ditto — and the extension is misspelled, which is good evidence nothing has ever loaded it |
| `temp/PieMenu.cpp/.h`, `RobotDogD1*`, `UsbHidIO.*` | tracked in git, built by nothing |
| `temp/glad_wgl.*`, `interp.h`, `perlin.cpp/.h`, `noisegen.cpp`, `miniz.cpp` | loose third-party sources |
| `temp/glview/` | a whole third-party viewer, with its own CMakeLists and `.sln` |
| `tools/camera_ray_test.cpp` | a lone `.cpp` among Python scripts |
| `data/www_example/` | reference HTML for the OCPP web UI; only `data/www/` is served |
| `obs_handgame/` | an Obsidian vault of design notes — reference of a different kind, still not build input |

Two rules, so it does not rot into a second `temp/`:

1. **Nothing under `reference/` ever appears in `DIR_SRC`, `IPATHS`, `SRCS` or a `LoadFile`
   call.** The build cannot reach it, so it cannot silently break, and that is the whole
   difference between this folder and `temp/`.
2. **Every file gets a provenance header** — where it came from, its licence, why it is kept.
   The two shadertoy files carry no attribution at all today, which stops being a tidiness
   question the moment any of it is pasted into a shader that ships.

---

## 8. Loose ends found on the way

Independent of everything above; each is a few minutes.

- **`apps/Grid.mk` is broken but silent.** It adds `-Iskeleton/` and `DIR_SRC += ./skeleton`, but
  there is no root `skeleton/` — it is `core/skeleton`, already in the global `DIR_SRC`. The
  wildcard expands to nothing, so it is a no-op that reads as meaningful. The file also has no
  trailing newline.
- **`core/BinaryAsset.cpp` ends `#endif` with no trailing newline.** Moot once §5 removes it.
- **`versions/`** holds five old `wind_*.exe`, gitignored, so it is a local archive that looks
  like tracked history. One line in `readme.md` fixes it.
- **`readme.md`'s folder-structure section** lists only `/core`, `/shaders`, `/apps`, `/3rdparty`.
  It needs rewriting when §6 lands, and it is the natural place to state the §2.2 rule.

---

## 9. Open questions

1. **Cache key** — name-as-given or resolved path (§4.2). Affects the generator, so decide before
   the packer is written.
2. **`COMPILE_BINARYASSETS`** — keep the hook or remove it with the dump (§5.5).
3. **The four `DumpBinaryAssets()` call sites** — become `ListBinaryAssets()`, or go (§5.3).
4. **Shared sound and glyphs** — one copy in `assets/shared/`, or duplicated into Tetris and
   Breakout so every app folder is self-contained. Self-containment matters more than the 400 KB
   if apps are ever to be extracted individually.
5. **The 65 MB of dead assets** (§1.4) — delete, or move to `reference/`? They are not reference
   material in any useful sense, but `narration/` in particular looks like it was recorded for
   something.
