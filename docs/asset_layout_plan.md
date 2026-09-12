# Asset layout: where files live, and who resolves them

Plan for splitting the one shared `data/` and `shaders/` into per-app asset folders, for a
`reference/` tree of material that is *not* build input, and for removing the
`DUMP_BINARYASSETS` build stage in favour of a standalone packer.

Written 2026-09-12 from a walk through the tree. Every fact below was measured against this
source, not recalled — line numbers are as of that date.

**Status: the migration is complete.** All twelve apps are under `apps/<name>/` with their own
makefile, `main.cpp`, assets and exe. `core/` is app-agnostic and compiles once into `build/core`.
**`data/` is gone**, `shaders/` is gone, the root `makefile` and `main.cpp` are gone. `OBJLoader`'s
hardcoded `"data/"` prefix is fixed (§4.3). Still open: §5 (`DUMP_BINARYASSETS`), §5b (removing
`OBJLoader` itself), and the rest of §7's move into `reference/`.

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

### 1.4 What nothing loads at all — DONE 2026-09-12

`data/` was 205 MB, of which **65 MB was referenced nowhere in the tree** — not code, not
`tools/`, not `docs/`. Moved to `stale/` (not deleted: `narration/` in particular looks like it
was recorded for something), to be taken out of the repo entirely:

- **`narration/` — 42 MB, 15 wavs.**
- **`girlgun.glb` — 23 MB.** Also `plant.glb`, `panorama_studio.png`, `test.glb`,
  `ship_003.obj`+`.mtl`, `chara.obj`, `sphere.obj`, `outline.png`, `preact.html`.
- **17 of the 28 files in `data/textures/`** — `dirt_001`, `grass_001`, `concrete*`, `stone_001`,
  `metal*`, `bone_001`, `bricks`, `checkerboard`, `noise`, `planks`, `pinetexture*`, the `.avif`.
- `win32_transparent_msvc.zip`, `www_example/`.

`example_desktop.png` and `performance_2070.png` were readme screenshots living in the runtime
asset tree; they went to `docs/images/` instead, and `readme.md`'s link was updated.

**Correction to an earlier draft of this section:** it claimed most of `data/textures/` was
reachable only through orphaned `.obj`s. Wrong — **11 of those textures are live**, reached
through the `.mtl` files of Grid's ten `.obj` meshes (`brickwall`, `Rock030_*`, `mossy-ground*`,
`broken_down_concrete2_*`, `pinebarklogs`, `pineleaves001`, `selection`), plus
`test_texture_4096.png` loaded directly. The `.mtl` chain has to be walked, not assumed — a
basename grep gives false positives in both directions. Verified after the move: every remaining
`.mtl` names a texture that still exists.

**Deliberately not moved:** the four `*_original.psd` in `data/icons/` (1.1 MB). `ApplicationSim`
scans that folder for `*.png` only, so the `.psd`s are never loaded — but they are *authoring
sources*, not stale assets, and losing a layered source is worse than losing a render. They want
an art repo, not `stale/`. Open call.

**Noticed while verifying:** `core/HTTPServer.cpp:480` reads `data/modes.json`, which **does not
exist and never did** in this tree — the only `modes.json` was `data/www_example/modes.json`, now
in `stale/`. It fails silently because the read goes through `ReadFileToString`, which tolerates
a missing file by design. Whether the OCPP web UI is meant to serve one is an open question.

---

## 2. The target layout

An app owns a **folder**, and its assets live inside it next to its code. Only what genuinely
serves more than one app is shared, and that sits at the root under its own name:

```
shared_assets/     # loaded by core/, or by two or more apps - nothing else
  shaders/         default.vert  default.frag  default_skinned.vert  deferred.frag
                   skybox.vert  skybox.frag  field.vert  field.frag
                   field_jfa.comp  ssao_compute.comp  texture.comp
  fonts/           consola.ttf  CascadiaMono.ttf
  meshes/          glyphs_unispace.glb        (Tetris + Breakout)
  sound/           bleep  click  floop  hax   (Tetris + Breakout)

apps/tank/         makefile, main.cpp, ApplicationTank + its gameplay classes
  assets/
    meshes/tank.glb
    textures/export_terrain_photoshop.png
  build/tank.exe

apps/ship/         ships.glb + shaders/{raymarch_volume,cloud_shadow,noise3d,density}
apps/grid/         tiles, trees, elf, hand, arrows, skybox/, test_texture_4096.png
apps/breakout/     shaders/breakout_shield.frag
apps/isoanimation/ isoanim.glb, christmas_photo_studio_01_2k.hdr, shaders/custom.frag
apps/tileset/      cityandroads.glb
apps/animation/    gwen_anim.glb
apps/dozer/        the ten wavs + dozer.glb
apps/sim/          icons/ + the galaxy meshes
apps/ocpp/         www/, modes.json
```

The earlier draft of this section proposed one `assets/` tree with a nested `shared/`. Superseded
2026-09-12: an app folder that holds its code, its assets, its makefile and its exe is
self-contained in a way a split tree is not — you can read one app without knowing the repo, and
the membership question answers itself, because an asset only one app uses has nowhere else to go.
`shared_assets/` earns a name of its own precisely because it is the exception.

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

Library assets stay next to their library. Only app-owned files move under `apps/<app>/assets/`.

Note that §6.3 shrinks this question: once each app owns a folder, a "library" used by exactly one
app (`crane/` was, for Tank) simply moves into that app. Only a folder genuinely shared between
apps — `isoterrain/`, `isocity/` — stays a library with assets of its own.

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
proven in that file. As built:

```make
empty :=
space := $(empty) $(empty)
comma := ,
APP_ASSETS += assets/shared
CFLAGS += -DAPP_ASSET_PATH=\"$(subst $(space),$(comma),$(strip $(APP_ASSETS)))\"
```

(`$(space)` is not built in — make has no literal for it, so those two lines are how you get one.
Without them the `subst` silently matches nothing and the define arrives space-separated, making
the whole path one unusable root. On the comma, see §4.2b.)

**An asset is named `<category>/<file>`** — `shaders/default.vert`, `meshes/tank.glb`,
`sound/bleep.wav` — and the roots decide which copy of that name the process gets. Keeping the
category *in the name* rather than folding it into the roots is what keeps the path two entries
long instead of eight, and what makes every pre-existing literal resolve unchanged (§4.1).

First match wins and the app's own root comes first, so an app that wants its own take on a shared
shader drops a same-named file in its own folder and nothing else changes. That override is a
genuine feature, not a side effect of tidying.

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

**Implemented 2026-09-12.** `AddAssetSearchRoot` / `ResolveAssetPath` in `core/File.{h,cpp}`,
with `LoadFile` and `ReadFileToString` both going through it.

### 4.1 Order of resolution

1. **Exact name, as given.**
2. Each root of `APP_ASSET_PATH`, in order, as `<root>/<name>`.

The planned third step — `data/` and `shaders/` as explicit fallback entries — **turned out to be
unnecessary**, and that is worth understanding because it is what made this safe to land in one
go. Keep the *category* in the asset name (`shaders/default.vert`, not `default.vert`) and let the
search path hold only **roots**, and then step 1 already is the fallback: `"shaders/default.vert"`
is found in `./shaders/` exactly as it always was, before any root is consulted. No fallback
entry, no removal step later, and nothing to forget to take out.

It also means **moving a file can require no code change at all.** Ship's four cloud shaders moved
to `assets/ship/shaders/` with `ApplicationShip.cpp` untouched — the name `shaders/noise3d.comp`
still names them, the root just changed which copy answers. Only a name whose *category* changes
(`data/tank.glb` → `meshes/tank.glb`) needs the literal edited.

Verified on both sides: `APP=Ship` logs all four resolving through `assets/ship`, including
`density.glsl` reached through the GLSL `#include` mechanism, and the clouds render. `APP=Tetris`,
which declares no root of its own, loads all 14 of its files with zero resolution failures.

### 4.2 The cache key — settled: the name as given

`LoadFile` keys `BinaryAsset` on the name it was handed, never on the path resolution produced.
The reason is the packed build: an asset baked into the executable can only be found by the name
the caller asked for, because there is no disk to have resolved against. Key on the resolved path
and `"shaders/default.vert"` keys as `assets/shared/shaders/default.vert` in a loose build and as
itself in a packed one — the two builds then disagree about what is already loaded, silently.
Keying on the name as given also keeps `ReleaseFile`, which only ever sees the name, able to find
what `LoadFile` stored. This is the constraint the generator in §5.4 has to match.

### 4.2b Two traps this hit, both now commented in place

- **`;` cannot separate the roots.** make hands the whole compile and link command to `sh`, which
  reads a `;` in the define as a command separator and cuts the path in half. It fails at the
  *link* step with `assets/shared": No such file or directory` and `Error 127`, which points
  nowhere near a `-D` flag. The separator is `,`; `core/File.cpp` parses both, because `;` is what
  anyone used to a Windows `PATH` will reach for.
- **`core/File.o` needs the `.current_app` marker**, for the same reason `main.o` already did: it
  bakes `APP_ASSET_PATH` in at compile time, and that string changes with `APP` while
  `core/File.cpp` does not. A stale one links and runs and merely searches the *previous* app's
  roots — so the symptom is a missing asset, or worse, silently loading another app's copy of a
  name they both define. Added next to the existing `main.o` rule.

### 4.3 `OBJLoader`'s hardcoded `data/` — fixed

`core/OBJLoader.cpp` built texture paths by bolting a literal on the front:

```cpp
std::string whole_path = "data/" + std::string(diff_name);   //map_Kd, and the same for map_Bump
```

So a `.mtl` beside `galaxy/data/meshes/ship.obj` had its textures looked up in the **root**
`data/` — wrong before any of this, and fatal once `data/` stopped existing.

The fix turned out to need no path arithmetic at all: **a `.mtl` already writes its maps as
`textures/brickwall.jpg`**, which is exactly the `<category>/<file>` form every asset here is
named in. So the prefix simply comes off and the name goes to the loader as-is; the search path
finds it in whichever app root owns it. `GetBasePath()` was not needed.

Verified on Grid's test scene: all eleven `.mtl`-referenced textures load, including the
`map_Bump` normal maps, and the geometry renders textured and normal-mapped.

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

## 5b. Removing `OBJLoader` — a conversion job, not a delete

Intent (2026-09-12): `OBJLoader` goes, superseded by glTF. `readme.md` has carried
*"Will we be using OBJLoader ever again? Maybe remove it."* for a long time.

**It is still in use.** That is the thing to know before planning it out — measured, not assumed:

| Caller | How |
|---|---|
| `ApplicationGrid.cpp` | **10 direct `OBJLoader::ParseOBJFile` calls** (`:246`–`:540`) |
| `ApplicationSim.cpp` `:123`–`:126` | 4 calls via `AssetManager::AddNewAssetFromOBJFile` |
| `core/AssetManager.cpp:26` | `AddNewAssetFromOBJFile` — the only core API that touches it, and Sim is its only caller |

So removing it costs **14 `.obj` meshes converted to `.glb`**, plus their `.mtl` materials and the
11 textures those `.mtl`s name:

- Grid: `tile_001`, `tile_002`, `border_rock`, `editor_camera`, `tile_gate`, `arrows`,
  `test_cube`, `tree_001`, `wall_segment`, `selection_tile`
- Sim: `galaxy/data/meshes/{sphere,sunhighlight,ship,plane}.obj`

That is a Blender export pass and a re-check of Grid's materials, not a code change. It is also
the natural moment to do it — the meshes have to be touched by §6 anyway.

### 5b.1 Done already: the dead includes

Five files included `OBJLoader.h` without using a single symbol from it. Removed 2026-09-12,
`APP=Grid`, `APP=Sim` and `APP=Ship` all verified to build clean afterwards:

`core/Application.cpp`, `core/Scene.cpp`, `isoterrain/IsoTerrain.cpp`, `ApplicationSim.cpp`, and
`core/AssetManager.h` — the last one was a transitive-include crutch in a *header*, so every
translation unit that touched `AssetManager.h` was dragging `OBJLoader.h`, `Mesh.h`,
`Material.h` and the vector/int3/vec2/vec3 types in behind it. The include moved down into
`core/AssetManager.cpp`, which is the only file that actually calls the parser.

This shrinks the removal surface from seven files to three and costs nothing. The remaining
three all genuinely call into it.

### 5b.2 Order

Do it **after** §4 (the resolver) and alongside §6, per app: convert Sim's four meshes with Sim's
move, Grid's ten with Grid's. `AddNewAssetFromOBJFile` is deleted with Sim's conversion — it is
the only core-side caller and nothing else uses it. `core/OBJLoader.{cpp,h}` (360 lines) goes
when Grid's last `.obj` does.

The `"data/" + name` bug at `core/OBJLoader.cpp:241`/`:275` (§4.3) then needs **no fix at all** —
the file it lives in is gone. If §4 lands well before the conversion, patch it; if the
conversion is close behind, skip it.

---

## 6. Step three: move the assets

Mechanical and per-app, because the literals are grouped one app per file. For each app:

1. `git mv` its files under `assets/<app>/<category>/`.
2. Add `APP_ASSETS += assets/<app>` to `apps/<App>.mk`.
3. Re-prefix the literals in `ApplicationX.cpp` to `<category>/<file>` — **and only where the
   category actually changes.** A `shaders/...` name is already in the right shape and needs no
   edit at all (§4.1); `data/tank.glb` → `meshes/tank.glb` does.
4. Build, run, `screenshot` over MCP to confirm it still looks right.

Suggested order — smallest surface first, so the resolver is proven before Grid's 22 files:
**Tank → Tileset → Animation → Breakout → Tetris → Ship → IsoAnimation → Sim → Dozer → OCPP → Grid.**

There is no fallback entry to delete afterwards — see §4.1, step 1 does that job and keeps doing
it harmlessly. What marks the end of the migration is `data/` being empty, not a makefile edit.

### 6.1 Progress — all twelve done

| App | Own assets | Notes |
|---|---|---|
| **Tank** | mesh + heightmap texture | first one across; `crane/` moved in with it |
| **Ship** | `ships.glb` + 4 cloud shaders | proved the same-category override (`shaders/raymarch_volume.frag` vs `shaders/default.vert`) |
| **Breakout** | 1 shader | 14 of its 15 assets are shared; first `USE_SOUND` app, found the `CORE_CFLAGS` trap |
| **Tetris** | none | owns nothing at all — one root, no `assets/` folder |
| **UI** | none | owns nothing |
| **OCPP** | `www/` | found the `FileWatcher` resolved-path bug |
| **Animation** | `gwen_anim.glb` | |
| **Dozer** | mesh + 9 sounds | 42 MB of raw audio split out to `audio_source/` |
| **Tileset** | mesh, sound, icon folder | `isocity/` moved in; first `LIB_DIRS` user |
| **IsoAnimation** | mesh, HDR, `custom.frag` | found the `stbi_loadf` direct-`fopen` bug |
| **Sim** | 4 meshes + icon folder | `galaxy/` moved in; `.psd` sources split to `icon_source/` |
| **Grid** | 22 — 10 `.obj`+`.mtl`, 5 `.glb`, skybox, 12 textures | the `OBJLoader` `map_Kd`/`map_Bump` path |

Verified the same way each time: every asset resolving through the intended root, zero resolution
failures, and a screenshot matching the pre-migration render.

Grid needed one extra step. Its `Init` selects a scene by commenting a line, and the active one
(`CreateHandTestScene`) loads a single `.glb` — so the ten `.obj` meshes and the eleven textures
their `.mtl`s name were never touched, and neither was the `OBJLoader` fix. Sim exercises
`OBJLoader` but its `.mtl` files declare no maps, so nothing in the tree covered `map_Kd` at all.
`CreateTestScene` was switched on temporarily to verify (35 assets, 0 failures, textured and
normal-mapped geometry rendering), then switched back.

### 6.2 What the old system left behind

Removed once the last app was across: the root `makefile`, the root `main.cpp`, every
`apps/<Name>.mk`, the `.current_app` sentinel, and the `data/`, `shaders/`, `fonts/`, `tank/`,
`ship/`, `tetris/`, `breakout/`, `dozer/`, `galaxy/`, `isocity/` and `crane/` folders.

`isoterrain/` stays at the root: Grid, IsoAnimation and Tileset all build it, so it is a genuine
shared library, declared with `LIB_DIRS` and compiled once into the shared object tree alongside
core. It has no runtime assets of its own — `isoterrain/data/tile_terrain.obj` is referenced by
nothing, so the "library assets" problem §2.1 anticipated never actually arose.

**`apps/dozer/audio_source/` — 42 MB that is not build input.** `dozer/data/` held 22 files and
the app loads 10. The rest is the raw multitrack the clips were cut from (`Bulldozer sounds.wav`
at 25 MB, `Hydraulics.wav` at 16 MB), two unused takes, the `.pk`/`.asd` files the audio editor
left behind, and a `desktop.ini`. Kept — deleting someone's source audio is not a call to make
from a grep — but moved out of `assets/` and named so nothing mistakes it for runtime input.
Same category as the four `*_original.psd` below: it wants a media repo, not this one.

**`tank/heightmap_roundtrip_test.png` was deliberately left where it is.** It is not an asset —
`ApplicationTank::TestHeightmapRoundTrip` **writes** it with `SaveHeightmapPNG` and reads it back
to check the round trip. Moving it under `assets/` would put generated test output in the asset
tree, and the resolver cannot help a *write* anyway: it finds files that already exist. It wants
a scratch directory, or to stop being committed at all — not an asset root.

### 6.2 Blocked on the shared build output

**Another agent builds apps in this same working copy.** With one `wind.exe` and one makefile for
all twelve apps, concurrent builds collide: the linker cannot overwrite a running binary, whoever
builds last owns it regardless of the other's `APP=`, and the `taskkill //F //IM wind.exe` that
precedes a build kills the other agent's running app. This is not hypothetical — it took out a
verified-good Tank run mid-session, with a half-black screenshot and nothing wrong in its own log.

So **ask before building** until §6.3 lands.

### 6.3 Per-app executable and makefile

Agreed direction 2026-09-12: each app gets **its own output binary and its own makefile**, instead
of one `wind.exe` selected by `APP=`.

It is worth doing for its own sake and not only for the collision above:

- Two people, or two agents, can build different apps at once with nothing shared to fight over.
- `main.o` and `core/File.o` currently need the `.current_app` sentinel purely because they bake
  `APP`-dependent values in (`APP_HEADER`/`APP_CLASS`, and now `APP_ASSET_PATH`) while their
  `.cpp` never changes. Per-app object and output directories make that whole mechanism
  unnecessary rather than merely correct.
- It removes the stale-object class of failure the makefile already documents — the one whose
  symptom was heap corruption (`c0000374`) after an `APP` switch.

**Built and proven on Tank, 2026-09-12.** The layout:

```
engine.mk                  the shared engine: toolchain, CFLAGS, core sources, rules
build/core/**.o            core objects, compiled ONCE and shared by every app
shared_assets/
  shaders/                 default.*, deferred, skybox, field*, ssao_compute, texture
  fonts/                   consola.ttf, CascadiaMono.ttf
apps/tank/
  makefile                 ROOT, PROJECT, APP_SRCS, then `include $(ROOT)/engine.mk`
  main.cpp                 names ApplicationTank directly; declares the asset roots
  ApplicationTank.cpp/.h   moved from the repo root
  TankCharacter/BuggyCharacter/Heightmap/CraneCharacter   moved from tank/ and crane/
  assets/
    meshes/tank.glb
    textures/export_terrain_photoshop.png
  build/
    tank.exe               and this app's own objects
```

An app makefile is now four lines of actual content:

```make
ROOT     := ../..
PROJECT  := tank
APP_SRCS += main.cpp ApplicationTank.cpp TankCharacter.cpp ...
include $(ROOT)/engine.mk
```

#### What made the shared core objects possible

**Only `core/File.cpp` was app-coupled**, across all 51 core sources — through `APP_ASSET_PATH`.
(`main.cpp` was the other coupling, through `APP_HEADER`/`APP_CLASS`, and a per-app `main.cpp`
removes it by definition.) Move the roots from a compile-time define to a **runtime call in the
app's own `main.cpp`** and `core/` stops knowing which app is building at all — which is what lets
its objects be compiled once into `build/core` and shared.

That also **retires the `.current_app` sentinel completely.** It existed only to force `main.o`
(and later `core/File.o`) to rebuild when `APP` changed while their `.cpp` had not. Nothing
app-dependent is compiled into a shared object any more, so there is nothing left to go stale —
the failure class whose symptom was `c0000374` heap corruption after an `APP` switch is gone by
construction rather than guarded against.

#### Roots are relative to the executable, not the working directory

`main.cpp` calls `AddAssetSearchRootFromExe("../assets")` and
`AddAssetSearchRootFromExe("../../../shared_assets")`. With each app in its own `build/` folder,
resolving against the working directory would mean the binary only found its assets when launched
from one particular place — a debugger, a shortcut or a script elsewhere would fail with nothing
obviously wrong. `GetExecutableDirectory()` in `core/File.h` is the new primitive;
`AddAssetSearchRoot` stays for a root that really is working-directory relative.

Verified: `apps/tank/build/tank.exe` resolves all eight of its assets — five shared shaders and
the font out of `shared_assets`, the mesh and heightmap out of `apps/tank/assets` — with zero
failures, and renders identically to the pre-restructure build.

#### The two systems coexist during the migration

The old root `makefile` still builds the other eleven apps into `wind.exe` via `APP=`. Its
`APP_ASSETS` now appends `shared_assets` instead of `assets/shared`, so those apps find the moved
default shaders and fonts through the search path. Verified: `APP=Ship` resolves its own four
cloud shaders from `assets/ship` and the six shared ones from `shared_assets`, with no failures.

An app leaves the old system when it gains an `apps/<name>/makefile`; `apps/<Name>.mk` is deleted
at that point (`apps/Tank.mk` and `apps/Ship.mk` are gone, and the root makefile's `APP ?=` default
moved off Ship to Grid, since a default naming a deleted fragment fails a bare `make`). The root
`makefile` disappears when the last app moves.

#### One gotcha per app: include prefixes

Moving `ship/*.cpp` up into `apps/ship/` broke `ApplicationShip.h`, which included its neighbours
as `"ship/Asteroid.h"` — a prefix that made sense when the app sat at the repo root and the
makefile added `-Iship/`. Now they are siblings and `-I.` finds them by plain name, so the prefix
has to come off. Tank had no such problem because `ApplicationTank.h` already included
`"CraneCharacter.h"` unprefixed.

Worth grepping for `#include "<oldfolder>/` as the first step of each remaining app.

#### `ResolveAssetPath` earning its keep: the HTML hot-reload

OCPP surfaced the first real caller for the *exposed* resolver rather than `LoadFile`.
`HTTPServer::LoadHTMLFromFile` stored the name it was given in `m_htmlFilePath` and handed that
straight to `FileWatcher`. A watcher needs a real directory and file on disk — an asset name like
`"www/index.html"` means nothing to it — so keeping the name would have left the page loading
correctly and then **silently never hot-reloading again**, which is the whole point of that
watcher.

It now stores the resolved path. Verified: the watcher reports
`shared_assets/www/index.html`, not `www/index.html`. This is also a small fix to pre-existing
behaviour — the old literal `data/www/index.html` was working-directory relative, so hot-reload
only ever worked when the exe was launched from the repo root.

That is exactly the case §4 anticipated when it made `ResolveAssetPath` public: *"not every kind
of asset access can go through LoadFile."*

#### `CORE_CFLAGS`: the line between shared and per-app flags

Breakout is the first app with `USE_SOUND := 1`, and it exposed a trap in the shared core
objects. The old makefile put `-DUSE_SOUND` into `CFLAGS`, which compiles **every** object —
so under the new arrangement a sound app and a silent app would compile the *same shared core
object* with different flags. Make compares timestamps, not flags: whichever app built first
would win, every other app would link objects compiled for someone else, and nothing would
rebuild or warn.

It was harmless today only by luck — **nothing in the tree actually tests `USE_SOUND`** (checked;
zero `#ifdef USE_SOUND` anywhere). The first person to write one would have hit a genuinely
baffling bug.

`engine.mk` now draws the line structurally. `CORE_CFLAGS` freezes the app-invariant flags at a
marked point; core objects compile with `CORE_CFLAGS` and nothing else, while `CFLAGS` carries
anything app-specific and reaches only the app's own objects and the link. `-DAL_LIBTYPE_STATIC`
moved *above* the line and is now unconditional — it is needed by `core/SoundSystem.cpp` and
`core/WaveFile.cpp`, which are core sources, and is inert everywhere else.

Note `USE_SOUND` *adds two objects* to the shared core directory rather than changing any
existing one, so a sound app and a silent app coexist there cleanly — the silent one simply never
links the two it did not ask for.

#### The `.current_app` sentinel is itself a casualty of the shared output

Verifying Tetris hit a link failure in the **old** makefile: `main.o` was still compiled for
`ApplicationGrid` while `.current_app` read `Sim`, so `-DAPP_CLASS` and the object disagreed and
the link died on `undefined reference to ApplicationGrid::ApplicationGrid()`.

The sentinel only compares the `APP` of *this* invocation against the file. It cannot survive two
builds interleaving, because there is a single `main.o` and a single marker for all of them —
which is the same shared-output problem as §6.2, one level down. It is not worth fixing: a
migrated app has its own `main.o` in its own `build/`, named its class directly, and needs no
sentinel at all. The mechanism disappears with the root makefile.

Symptom to recognise: a link error naming a **different app's** constructor than the one you asked
for. The fix in the meantime is `rm main.o .current_app` and build again.

#### Known limitation

`build/core` is shared deliberately, so **two apps must not be built at the same time** — they
would race on the same object files. This is written at the top of `engine.mk`. It is a smaller
problem than the old one: the collision is now limited to concurrent builds, rather than every
build overwriting the single `wind.exe` that someone else was running.

---

## 7. `reference/` — material that is not build input — DONE

Not `examples/`: the apps under `/apps` *are* the examples this engine ships with, so that name is
taken. `reference/` is for the shadertoy excerpt, the snippet from another engine, the technique
worth keeping — things kept to read, never to compile.

Created and filled 2026-09-12:

- `reference/shaders/` — the two shadertoy files that had been sitting among the real GLSL, one
  of them with its extension misspelled `.hsls`, which was decent evidence nothing had ever
  loaded it.
- `reference/code_snippets/` — what used to be loose in `temp/`: `PieMenu`, `glad_wgl`,
  `interp.h`, `perlin`, `noisegen`, plus the `ImCurveEdit`/`ImSequencer` widgets that sat in the
  repo root wired into nothing. All tracked in git, all built by no app.

Two rules keep it from rotting into another `temp/`, and they are written in
`reference/readme.md`:

1. **Nothing here ever appears in `APP_SRCS`, `LIB_DIRS`, `IPATHS` or a `LoadFile` call.** The
   build cannot reach it, so it cannot silently break — that is the whole difference between this
   folder and the one it replaces.
2. **Every file gets a provenance header** — where it came from, its licence, why it is kept.
   Most of it still has none; the two shadertoy files in particular carry no attribution at all,
   which stops being a tidiness question the moment any of it is pasted into something that ships.

`temp/` and `obs_handgame/` are gone. The `.blend` sources moved to `art_source/`, alongside the
same distinction the apps make internally with `apps/dozer/audio_source/` and
`apps/sim/icon_source/`: authoring sources are kept, but never where a build could mistake them
for input.

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
5. ~~**The 65 MB of dead assets**~~ — settled: moved to `stale/`, on their way out of the repo.
6. **The four `*_original.psd` in `data/icons/`** — authoring sources, never loaded. An art repo
   rather than `stale/`, but that is a call about where art lives, not about this tree.
7. **`core/HTTPServer.cpp:480` reads `data/modes.json`, which does not exist** and never did here.
   It fails silently by design (`ReadFileToString`). Should the OCPP web UI be serving one?
8. **Per-app exe and makefile (§6.3)** — agreed in principle; the shape above is a sketch, not a
   decision. Worth settling before the remaining ten apps migrate.
