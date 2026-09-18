# `assetpack` — packing assets with a separate tool

Plan for the standalone asset packer on Windows. It closes `docs/engine_backlog.md` item 75 and
section 5 of `docs/asset_layout_plan.md`, and it answers the question those two left open: on
Android everything **has** to go into the binary, on Windows it does not, so what should this tree
actually ship?

Written 2026-09-14, and revised the same day once §9's four questions were answered — so what
follows is the agreed shape, not a menu. Every number below was measured on this machine against
this tree, not recalled; the measurement is named wherever one is quoted.

---

## 1. Where things stand

**The old way is gone but not yet replaced.** `DUMP_BINARYASSETS` still exists at
`core/BinaryAsset.cpp:139` and nothing defines it — not `engine.mk`, not any app makefile — so all
four call sites (`core/Application.cpp:170`, plus dozer, grid and ship) fall through to the `#else`
stub that just calls `ListBinaryAssets()`. The consuming side is complete and working:
`GetBinaryAsset` already searches `file_assets` and then the static `assets[]` table, `Uncompress`
already inflates, and `BinaryAssetMemoryEmpty.cpp` already provides the empty table every build
currently links. **Nothing has to be built to make a packed build possible — only the producer is
missing.**

**The self-dump was never going to work anyway**, and this is the argument for the separate exe
rather than a preference for tidiness. `DumpBinaryAssets()` packs only what *that session happened
to load*; the core call site is inside `InitGraphics`, before the app has loaded almost anything. A
directory walk packs what is *there*. Those are different answers and only one of them is
reproducible.

**The port already has the tool.** `C:/code/android/tools/pack_assets.cpp` (84 lines) plus
`pack_assets.mk`. It is the ancestor of what is proposed here, and §6 says exactly what differs and
why — including one piece of it that is **wrong for this tree** and must not be copied.

**The asset sets are not remotely the same size**, and that is what decides the output format —
anything whose cost scales with the payload has to survive a two-orders-of-magnitude spread.
Measured with `du -sk` on 2026-09-14:

| App | own `assets/` | | App | own `assets/` |
|---|---|---|---|---|
| grid | 69.0 MB | | animation | 10.5 MB |
| isoanimation | 15.3 MB | | pinball | 8.7 MB |
| dozer | 13.0 MB | | tileset | 3.7 MB |
| ship | 12.1 MB | | sim | 149 KB |
| tank | 10.1 MB | | testfx, ocpp, breakout | 64/44/16 KB |
| | | | tetris, ui | none at all |

plus `shared_assets/` at 2.3 MB (fonts 1.2, sound 0.65, meshes 0.4, shaders 0.08). So the smallest
app has 2.3 MB to account for and the largest has 71 MB — a 30x spread, and §3 is largely the story
of picking the one form that does not care.

---

## 2. The tool

`tools/assetpack/` — `assetpack.cpp`, a `makefile`, and nothing else. It follows `tools/tools.mk`
exactly as `tools/fontbake/` does:

```make
ROOT      := ../..
PROJECT   := assetpack
TOOL_SRCS += assetpack.cpp
CORE_SRCS += $(ROOT)/3rdparty/miniz/miniz.c
CORE_SRCS += $(ROOT)/3rdparty/miniz/miniz_tdef.c
CORE_SRCS += $(ROOT)/3rdparty/miniz/miniz_tinfl.c
include $(ROOT)/tools/tools.mk
```

(Two `.cpp` when this was written; three `.c` since 2026-09-18, when `3rdparty/miniz` became a
submodule and got upstream's file names back. `miniz.c` is the third because `miniz_tdef.c` calls
`mz_adler32`, which is defined there — see the note at the foot of step 3.)

**It links no core sources**, and that is a deliberate departure from the port. The port's tool
compiles in `File.cpp`, `BinaryAsset.cpp`, `Debug.cpp` and `Debug_win32.cpp` so that it can drive
`LoadFile` and then call `DumpBinaryAssets()`. That only makes sense while the dump lives in the
engine — and the whole point of item 75 is that it stops living there. A tool that owns its own
writer needs `std::filesystem` to walk, miniz to compress, and `fprintf` to emit. Four fewer
translation units, and `core/BinaryAsset.cpp` goes back to being purely a reader.

**It does `#include "BinaryAsset.h"` anyway, for the contract.** The generated file names
`BinaryAsset`'s fields with designated initialisers, so if someone renames `compressed_offset` the
*app* build breaks loudly at the generated file rather than the pack silently meaning something
else. Including the header in the tool is what lets the tool `static_assert` its own assumptions
about the struct at the point where they are made. This is cheap insurance on the one thing §1 says
is now an interface rather than a private detail.

### 2.1 Command line

```
assetpack [options] <root> [<root> ...]

  -o <dir>                  where to write; default "."
  --exclude <glob>          skip matching names; repeatable
  --include <glob>          if given, pack only matching names; repeatable
  --no-compress             store raw (for measuring, and for already-compressed data)
  --list                    walk and report, write nothing
```

**No output-form flag** — there is one form and §3 says why. `--exclude` earns its place on day one:
`apps/ocpp/assets/www/` is served over HTTP, hot-reloaded from disk and read through
`ReadFileToString`, which by design never caches, so it is not an asset in the sense this tool means
and ocpp passes `--exclude www/*`.

**`--exclude` has a second day-one use, found by the first `--list` run.** `shared_assets/` is
2.3 MB and **444 KB of it is loaded by nothing at run time** — every app's baseline, all fourteen:

| file | bytes | who loads it |
|---|---|---|
| `fonts/consola.ttf` | 459,180 | `core/Window.cpp:243`, ImGui's font |
| `fonts/mono_sdf.fnt` | 287,316 | `core/UIOverlay.h:97`, the overlay atlas |
| `fonts/CascadiaMono.ttf` | 371,352 | **nothing** |
| `fonts/mono_sdf.png` | 73,296 | **nothing** — `fontbake` writes it to be eyeballed, and says so |

That is 19% of the shared baseline, and it matters most exactly where it is least affordable:
tetris and ui own no assets at all, so `shared_assets` *is* their whole payload. It bears on
backlog items 79-82 (shipping tetris small) more than on this one. The packer's job is only to make
it visible and to be able to drop it — `--exclude "fonts/CascadiaMono.ttf" --exclude "*.png"` — not
to decide it; whether `CascadiaMono.ttf` is kept for a future font switch is not a call to make
from a grep, and `fontbake --no-png` is the tidier fix for the other one.

**Compression is on by default** and `--no-compress` exists so that measuring it is a flag rather
than a rebuild. Per-file or per-extension choice — `.glb` with embedded PNG and already-compressed
`.wav` deflate to roughly nothing while still costing inflate time at load — is a later
optimisation, not part of this. The one thing that must be right from the start is *what* gets
compressed: see §3's note on the trailing zero.

Each `<root>` is walked **recursively**, and an asset's name is its path relative to that root, in
`generic_string()` form — `sound/click.wav`, `meshes/tank.glb`. That is exactly the string the app
passes to `LoadFile`, which is the only reason a packed lookup hits at all. The port learned this
the hard way (it used to flatten to the bare filename, which both silently lost one of any two
files sharing a basename and would have meant patching every asset literal in every app, forever).

The roots are given in **the same order as the app's `main.cpp` declares them**, app first:

```bash
./build/assetpack.exe -o ../../apps/tetris/build/generated  ../../shared_assets
./build/assetpack.exe -o ../../apps/ship/build/generated    ../../apps/ship/assets ../../shared_assets
```

---

## 3. One output form: `.incbin`

**Decided 2026-09-14 (§9).** The tool emits one thing and has no mode flag. The two alternatives
were considered and declined, and both are recorded below rather than deleted — the argument for
each is short, and re-deriving it costs an afternoon.

### The byte array, what the port does — rejected

`BinaryAssetMemory.cpp` with `static const uint8_t asset_data[] = {0x1F,0x8B,...}` and the table
beside it. Self-contained, and needs no build rule beyond the file swap `engine.mk` already
documents.

**It does not scale, and the number is worse than it looks.** Every byte becomes five characters of
source. Measured on this machine, 2026-09-14, `g++ -std=c++17 -O2 -c`:

| payload | generated `.cpp` | compile |
|---|---|---|
| 2 MB | 10 MB | **3.09 s** |
| 20 MB | 100 MB | **33.7 s** |

Linear, so grid's 69 MB would be a **345 MB source file and roughly two minutes** — per compile,
on every asset change. Tetris at 2.3 MB is three seconds and perfectly liveable; grid is not.

Its one remaining argument was that it is **the format the Android port consumes**. That is gone
too: the port has its own copy of the tool anyway, and the decision is that **Android moves to
`.incbin` as well**, to be explored on that side. So nothing is left to keep it for, and the writer
does not carry a second branch for it.

### `.incbin` — what is built

The same table, but the blob is a plain `.bin` pulled in by a four-line assembler stub:

```
   assets.bin                  the blob
   assets.S                    .section .rodata / .globl / .incbin "assets.bin"
   BinaryAssetMemory.cpp       the table, pointing at &asset_blob[offset]
```

Identical result in the exe — same static bytes, same `assets[]` table, same `GetBinaryAsset` path,
no runtime change anywhere. **Verified working with this toolchain on 2026-09-14**: `g++ -c` on a
`.S` containing `.incbin`, linked against a C++ TU declaring `extern "C" const unsigned char
asset_blob[]`, read back the right bytes and the right length.

Measured against the table above: the 2 MB case assembles in **0.05 s** instead of 3.09 — 62x, and
the ratio grows with the payload because the `.cpp` side is what scales and the `.incbin` side is a
file copy.

Cost: one pattern rule in `engine.mk` (§4) and one more generated file to keep together. That is
the whole price, and it buys the bake back for every app rather than only the small ones.

**The one thing the writer must get right is `size+1` with the trailing zero.** The existing reader
already depends on it: `Uncompress` (`core/BinaryAsset.cpp:96`) inflates `size+1` bytes and
subtracts one afterwards, precisely so a decompressed asset is as safe to hand to the GLSL compiler
as a fresh disk read is — `LoadFile` `calloc`s `sz+1` for the same reason. So the tool compresses
**content plus the terminator** and records `size` without it. Get this wrong and only shaders
break, only in baked builds, which is about the worst combination available.

### The side `.pak` file — declined, for now

A pack file next to the exe, mounted at start-up and read lazily. The Windows-only option, and the
one the port cannot have: the exe stops carrying the payload, and an asset change costs zero
compiles rather than one.

**Deferred, not rejected, and the thing that will bring it back is not size.** With `.incbin` the
compile-time argument evaporates — an asset change costs a file copy and a link, not a recompile —
and exe size on its own is not worth a format, a `core/AssetPack.{h,cpp}` mount list and a third
branch in `LoadFile`'s search order. What *will* need it is **a visual loader**: a shipped game with
a large asset set wants a small exe that comes up, draws something, and then loads, and a blob the
loader cannot get in front of makes that impossible — a baked `assets[]` is simply *there* when the
process starts, with the OS paging it in behind the scenes and nothing to report progress on. So
the decision to revisit is made by the first thing that ships with a loading screen, and that is
when the option gets picked (decided 2026-09-14).

Recorded so the shape does not have to be re-derived then: index at the end of the file so the
writer never seeks back; `MountAssetPack` resolving its own name through `ResolveAssetPath`; a hit
ending in `StoreBinaryAsset` so nothing downstream learns packs exist; and **packs searched after
the roots**, so a loose file still wins and a shipping build simply declares no roots. A loader also
wants the index readable *without* the payload, which the end-of-file index already gives, and a
way to ask for one asset at a time — which lazy mounting already is.

That also settles "one bin per asset type" — it was only ever reachable through the pack mode, as a
mount list rather than a category mechanism in the engine. Not built.

### Which apps bake

`.incbin` costs essentially nothing to assemble at any size (0.05 s for 2 MB, and it scales as a
file copy), so the only consequence of baking a large app is a large exe. Grid's 69 MB is a 75 MB
binary; nothing else is above 17 MB, and the six small apps are under 3 MB.

So there is no per-app *mode* to choose, only whether an app bakes at all, and no app has to be
decided in advance — what ships is decided when something ships. Grid is the useful one to bake
*as a test*, because 69 MB is where any separation problem shows up first, but the check works on
any app and none of them is committed by it.

And **during development, none of them** — see §4.2. A loose tree with search roots is the fastest
edit loop there is, and it is what every app does today.

---

## 4. How it hooks into the build

### 4.1 The rule

Baking is **opt-in per app**, driven by one variable an app's makefile sets and `engine.mk` acts on.
With only one output form there is no mode to name, so it is a flag and a list:

```make
#apps/tetris/makefile
BAKE_ASSETS := 1                           # unset (the default) = loose, today's behaviour
ASSET_ROOTS := $(ROOT)/shared_assets       # same order as main.cpp declares them
```

`engine.mk` then, when `BAKE_ASSETS` is set:

- swaps `$(ROOT)/BinaryAssetMemoryEmpty.cpp` out of `CORE_SRCS` for
  `$(GENERATED)/BinaryAssetMemory.cpp` — the swap §5.5 of `docs/asset_layout_plan.md` recommended
  keeping as a dangling hook, now with something on the end of it;
- adds `$(GENERATED)/assets.o`, assembled from the generated `assets.S`, which needs a `.S` pattern
  rule — the one new rule in `engine.mk`;
- builds `tools/assetpack/build/assetpack.exe` if it is missing, and runs it.

The three generated files move together and there is no sense in one without the others, so
`BinaryAssetMemory.cpp` is the target the rule names and `assets.S`/`assets.bin` are written beside
it. If they were separate targets a half-finished repack could leave a table pointing into a blob
that no longer matches — offsets into the wrong bytes, which fails as garbled assets rather than as
an error.

**The generated file is per-app and per-config, and belongs in the app's own `build/generated/`** —
never in the shared `$(ROOT)/build/core` tree. Two apps pack different asset sets; a shared object
holding one app's assets and linked into another is exactly the "objects compared by timestamp, not
by the flags they were built with" failure `engine.mk` draws the `CORE_CFLAGS` line to prevent. The
generated TU is an *app* object, compiled with `CFLAGS`, sitting in `$(OBJ_DIR)`.

### 4.2 Staleness — the part that bites

The dependency has to recurse, and the port has already paid for both traps in it:

```make
rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

$(GENERATED)/BinaryAssetMemory.cpp: $(PACK_TOOL) $(ASSET_ROOTS) \
        $(foreach d,$(ASSET_ROOTS),$(call rwildcard,$(d),*))
	$(PACK_TOOL) -o $(GENERATED) $(ASSET_ROOTS)
```

- **`$(wildcard $(d)/*)` sees only the top level.** With assets in `meshes/`, `shaders/`, `sound/`
  — which is now every app — an edit would not trigger a repack and the app would run against a
  stale baked-in copy, with nothing anywhere to say so.
- **The space before `$(filter` is load-bearing.** Without it the results concatenate and make
  reports a nonsense target name like `No rule to make target 'assets/sound/probe.wavassets/sound'`.
- **The roots themselves must be prerequisites too.** `rwildcard` lists a directory's *contents*, so
  deleting a whole subdirectory removes it and its files from the list at once and nothing looks out
  of date — the deleted assets stay baked in. The parent's mtime is the only thing that changes on a
  delete.
- Pure make, not `$(shell find ...)`, so it does not depend on which shell make picked. This repo
  has both Bash and PowerShell in play and `engine.mk` is already careful about it (see the note on
  `;` in `APP_ASSET_PATH` at `core/File.cpp`).
- **`$(MAKEFILE_LIST)` has to be a prerequisite too, because the FLAGS are an input.** Added
  2026-09-14, after the first exclusion that actually mattered. The recipe passes
  `$(ASSET_PACK_FLAGS)`, which an app sets in its own makefile — and make compares timestamps, never
  the flags a thing was built with. Without it, editing that list changes nothing: the generated
  table is newer than every asset and the tool, make reports nothing to do, and the app keeps the
  blob packed under the *old* exclusions. Measured by adding an exclusion and rebuilding — the pack
  did not re-run, and the only way to see it was to count entries in the generated `.cpp`. Worse
  than the stale-object case above, because the stale artefact is data: an exclusion that silently
  did not take effect ships an asset you meant to drop, and one that silently was not *removed*
  ships a build that dies on its first `LoadFile`.

### 4.2.1 The generated directory is per-variant, not just per-app

`$(GENERATED)` is deliberately **not** per-configuration — debug and release bake identical bytes, so
one pack serves both and switching configuration costs nothing. That still holds.

It is, since 2026-09-14, **per-variant**: `$(BUILD_DIR)/generated$(VARIANT_SUFFIX)`. Once
`ASSET_PACK_FLAGS` can depend on the flag family, two builds of one app want two different packs from
one tree — `apps/tetris` excludes ImGui's font when `USE_IMGUI=0` and must keep it otherwise. With a
single shared directory the second build to run finds the first one's table newer than everything and
reuses it wholesale. Measured both ways round: `make ship` after a measurement build shipped a 254 KB
font it had explicitly excluded, and the opposite order produced the `LoadFile failed` startup death.

This is the same argument the `VARIANT_SUFFIX` block in `engine.mk` makes about object trees and exe
names, applied to a third kind of output. `$(VARIANT_SUFFIX)` is empty for a default build, so an
ordinary bake still writes `build/generated` and nothing about it changed.

### 4.3 What it does not do

`assetpack` never runs `fontbake`, `blender_glyph_meshes.py` or anything else that *produces* an
asset. Those are run by hand when their input changes and their output is committed — `fontbake`
writes `shared_assets/fonts/mono_sdf.fnt` and says so in its own header comment. `assetpack` walks
a tree of files that already exist. Keeping that line sharp is what stops the asset build from
becoming a graph nobody can reason about, and it is why the two tools do not need to know about
each other at all (§8).

---

## 5. What a baked build breaks, and it is not nothing

Four places open a file themselves rather than going through `LoadFile`. In a loose build they are
correct; in a baked one there is no file. **This is the real cost of baking, and it should be
priced in before the first `BAKE_ASSETS := 1` ships, not discovered after.**

| Where | Who | What happens |
|---|---|---|
| `core/Directory.cpp:25` `GetFiles` | sim, tileset, testfx | no directory to scan → empty list |
| `core/Texture.cpp:227` `stbi_loadf` | isoanimation | resolves, then opens; the HDR is not there |
| `core/HTTPServer.cpp` `FileWatcher` + `ReadFileToString` | ocpp | hot reload of `www/` is meaningless packed |

`GetFiles` is the sharp one, and it fails **twice**. There is no folder to enumerate — and even if
the baked table were enumerable, the names `GetFiles` returns are prefixed with the *resolved
directory* (`Directory.cpp:41`, deliberately, so the caller's later `LoadFile` hits on the as-given
step). Those are disk paths. They could never match a baked key. Making a baked build enumerate
means `GetFiles` walking `BinaryAsset::assets[]` by name prefix and returning **asset names**, which
changes what it returns in loose builds too.

Suggested order: none of this blocks the tool. Bake the apps that have no scan first — tetris is
first for other reasons anyway (§7), and it has no directory scan, no HDR and no HTTP server — and
treat `GetFiles`-over-baked-assets as its own backlog item when sim or tileset actually needs to
ship baked. `stbi_loadf_from_memory` is a two-line fix whenever isoanimation does.

---

## 6. What differs from the port, and one thing in it to *not* copy

Mostly this is a port-back, so the differences are worth listing plainly:

| | port | here | why |
|---|---|---|---|
| name | `tools/pack_assets.cpp` | `tools/assetpack/` | one word, folder-per-tool, matches `fontbake` and `tools.mk` |
| how it packs | drives `LoadFile`, calls the engine's `DumpBinaryAssets()` | walks and writes its own | the engine's dump is being deleted (§1) |
| links | `File.cpp`, `BinaryAsset.cpp`, `Debug*.cpp`, miniz | miniz | follows from the above |
| output | byte-array `.cpp` | `.incbin` blob + table | 62x faster to build at 2 MB, and it scales (§3) |
| precedence | **later root wins** | **first root wins** | see below |

**The precedence must be inverted, and taking the port's verbatim would be a real bug.** The port's
own header comment says "each dir is scanned in the order given, so a later dir's file wins on a
name collision". In *this* tree the runtime says the opposite: `AddAssetSearchRoot` appends,
`ResolveAgainstRoots` returns the first match, and every app's `main.cpp` declares its own root
*before* `shared_assets` precisely so an app's own copy of a shared shader wins (`apps/ship` is the
worked example, in `docs/asset_layout_plan.md` §6.1). `StoreBinaryAsset` agrees — a second
registration of a name keeps the copy already stored and frees the new one.

So: **first root wins, and the tool must skip a name it has already seen.** If it did not, a packed
build would resolve overrides the exact opposite way from a loose build of the same app — the two
would disagree silently, with no error and no log line, which is the single worst failure this
whole mechanism can produce.

Worth an assertion rather than a comment: the tool should *report* every name it skipped as
shadowed, so an override is visible at pack time.

---

## 7. Order of work

Each step is independently verifiable and nothing before the last one changes any current build.

1. **Delete the self-dump.** The `#ifdef` at `core/BinaryAsset.cpp:139`, `DumpBinaryAssets()` in
   `BinaryAsset.h`, and the four call sites → `BinaryAsset::ListBinaryAssets()` (keeping the
   listing: `ApplicationDozer.cpp` calls `assetmanager->ListAssets()` on the very next line, so it
   was clearly wanted). Verify: any app builds and logs the same asset list it did before.
2. **The tool, `--list` first.** The walk, the naming and the shadow report, writing nothing.
   **DONE 2026-09-14** — `tools/assetpack/`, builds clean under `-Wall`. Verified: tetris's single
   root lists 22 assets / 2,317,715 bytes, matching `du`, all forward slashes and no `./`; all
   fourteen apps walk with **zero shadowed names**; the error paths (missing root, no roots, a flag
   with no value) each fail with a message rather than an empty walk.

   Two things this step actually established, neither of them expected:

   - **There is no name shadowing anywhere in the tree**, so the precedence rule has no live test
     case. §6 says ship proves the same-category override, and it does — `shaders/default.vert`
     from `shared_assets` beside `shaders/raymarch_volume.frag` from its own root — but that is a
     shared *category*, not a colliding *name*, and the two are not the same test. The report was
     verified against a deliberately constructed collision instead: a root supplying its own
     `shaders/default.vert` ahead of `shared_assets` is packed and the shared copy reported as
     shadowed. Worth keeping as a regression check, because the day a real override appears is
     exactly the day this has to already be right.
   - **444 KB of the 2.3 MB shared baseline is loaded by nothing** — see below.
3. **The writer.** `assets.bin`, `assets.S`, `BinaryAssetMemory.cpp`.
   **DONE 2026-09-14.** Verified by hand-compiling the generated trio against the *real* engine
   reader — `core/File.cpp` + `core/BinaryAsset.cpp` + miniz, no app, no makefile — and round-tripping
   every asset: present in the baked table, served by `LoadFile` from it rather than from disk,
   bytes identical to the file, `size` the content length, and `data[size] == 0`.

   | | assets | raw | blob | |
   |---|---|---|---|---|
   | tetris (`shared_assets`) | 22 | 2,317,715 | 1,160,291 | **50%** |
   | grid (own + shared) | 72 | 72,890,388 | 61,740,843 | **85%** |

   **All 94 round-trip byte-for-byte, compressed and `--no-compress` alike**, across shaders, TTF,
   WAV, PNG, OBJ, MTL and GLB. The empty pack (everything excluded) also compiles and links, which
   is worth having because it is what an app with no assets of its own would produce.

   **Grid settles the format choice with room to spare.** End to end: **2.9 s to pack, 0.51 s to
   assemble, 0.35 s to compile the table** — under four seconds for 72 MB, against the ~115 s §3
   projects for the byte-array form. The `.incbin` side really is a file copy.

   It also shows what the deferred per-file compression decision is worth: tetris packs to 50%
   because it is shaders, TTF and WAV, while grid manages only 85% because it is mostly PNG and GLB
   that are already compressed — and pays inflate time at load for the privilege. The writer prints
   a per-asset ratio and flags entries that got *bigger*, so that optimisation now has its
   measurement and can be taken whenever it is wanted.

   **One trap found, worth knowing before anything else links miniz**: `3rdparty/miniz/miniz.h` has
   **no include guard at all** — no `#pragma once`, no `MINIZ_HEADER_INCLUDED`. A translation unit
   that includes it both directly and through `BinaryAsset.h` fails to compile with a wall of
   `conflicts with a previous declaration` on its enums, which reads like a toolchain fault rather
   than a double include. `assetpack.cpp` takes miniz *through* `BinaryAsset.h` only, which is what
   `core/BinaryAsset.cpp` already does. A one-line `#pragma once` upstream would end it.

   **Ended 2026-09-18.** It was never upstream's doing: `3rdparty/miniz` held a *modified* 3.1.0
   with the `#pragma once` commented out, along with five config defines and two edits to
   `miniz_tdef.c`. It is a submodule pinned at 3.1.2 now, upstream's guard is back, and the
   configuration lives in `3rdparty/miniz_export.h` instead — see the note in that file. This
   tool's makefile names three miniz sources rather than two for the same reason.
4. **The `engine.mk` rules.** **DONE 2026-09-14**, on `apps/ui` rather than tetris — identical
   asset shape (owns nothing, one shared root) and another agent was working in tetris at the time.
   An app opts in with `BAKE_ASSETS := 1` + `ASSET_ROOTS`, and `apps/ui/main.cpp` compiles its disk
   root out under the `-DASSETS_BAKED` that `engine.mk` then defines.

   Verified, in the order that makes each answer mean something:

   - **Runs with no search path at all.** 11 assets served from the baked table, **0 read from
     disk**, no fatals — and `InitAssetRoots` is never even reached, so the log has no search-root
     line to print. A baked build that still had its roots would have proved nothing.
   - **Renders identically.** `ui.exe` and `ui_baked.exe`, screenshotted over MCP: **byte-identical
     PNGs**, same MD5.
   - **Incremental behaviour**, which is where the staleness traps are: no changes → nothing to be
     done, no spurious repack; touching a *nested* asset (`shaders/default.vert`) → repacks, so the
     recursive wildcard works; adding a nested subdirectory → 21 assets; **deleting that whole
     subdirectory → back to 20**, which is the case that only works because the roots are
     prerequisites alongside their contents.
   - **Excludes work**: 20 assets instead of 22, the 444 KB of `CascadiaMono.ttf` and
     `mono_sdf.png` left out.
   - **All four variants coexist**: `ui.exe` 42.5 MB, `ui_baked.exe` 43.5 MB, `ui_release.exe`
     3.39 MB, **`ui_baked_release.exe` 4.32 MB** — a single file that needs no `shared_assets`
     folder at all. The baked delta is +935,424 bytes against a 932,339-byte blob.

   **A BAKED ASSET WINS OVER A FILE ON DISK — this plan said the opposite, and was wrong.** Step 4
   used to end "re-add the root and confirm the loose file still wins". It does not: `LoadFile`
   asks `GetBinaryAsset` *before* it touches the search path (`core/File.cpp`), so in a baked build
   editing a shader next to the exe does nothing at all. That sentence was pack-mode reasoning —
   §3's deferred design does put packs after the roots — leaking into the bake, where it does not
   apply. Correct behaviour for shipping, a trap during development, and the reason baking stays
   off by default and an app that bakes should stop declaring roots.

   **One real bug found by building it, worth the retelling.** Toggling `BAKE_ASSETS` changes
   `-DASSETS_BAKED`, and make compares objects by timestamp, not by the flags they were built with
   — so the first version shared one object tree and one exe name between baked and loose. Building
   loose → baked → loose then left make comparing the **baked** exe against loose objects all older
   than it: *"Nothing to be done"*, and the baked binary sat there under the name you asked for.
   Hit exactly that, and then hit its consequence — a loose build whose leftover `main.o` still had
   its roots compiled out, dying on `LoadFile failed to load [fonts/consola.ttf] - looked in:
   fonts/consola.ttf`. This is the same failure the `CONFIG` block has always described for
   debug-vs-release, and it wants the same two-part fix, not half of it: a separate object tree
   **and** a distinct exe name. Splitting only the objects is the easy step to stop at.
5. **Measure it.** **DONE 2026-09-14, on tetris.**

   **Tetris bakes and ships as one file.** `CONFIG=release BAKE_ASSETS=1`:

   | | bytes | files |
   |---|---|---|
   | `tetris_release.exe` + `shared_assets/` | 5,963,667 | **23** |
   | `tetris_baked_release.exe` | **4,581,376** | **1** |

   **23% smaller and a single file**, because the 2.3 MB asset tree becomes a 932 KB blob. The exe
   itself grows by 935,424 bytes against a 932,339-byte blob, so the table and alignment cost about
   3 KB. Build time is *lower* baked than loose (7.9 s against 13.0 s) — not a real saving, just
   the shared core objects already being warm, but it is emphatically not a cost.

   **Proven with no assets within reach, which is the only test that settles it.** Both exes copied
   alone into an empty directory far from the repo and run there:

   - `tetris_release.exe` → `Fatal: LoadFile failed to load [fonts/consola.ttf] - looked in:
     fonts/consola.ttf, <tempdir>/../../../shared_assets/fonts/consola.ttf`. The control, and it
     fails exactly as it should.
   - `tetris_baked_release.exe` → **19 assets from the baked table, 0 from disk, 0 fatals**, and it
     plays: well, ghost piece, HOLD/NEXT, and the glyph-mesh score text all rendering.

   Every category is covered by that run — TTF, the SDF `.fnt`, a GLB mesh, ten shaders including
   the `field_jfa` compute glow, and all four WAVs. Nothing in tetris reads an asset any way other
   than through `LoadFile`, which is why it is a clean case; §5's four exceptions are what makes
   other apps harder.

   Two notes for items 79-82, which this really serves:

   - The screenshots are **not** byte-comparable between runs, unlike `apps/ui`'s. `RRandom` cannot
     be seeded, so the piece sequence differs every launch. Compare asset resolution and render
     correctness, not pixels.
   - Tetris bakes 20 assets and **uses 17**. The three it never touches — `shaders/skybox.frag`,
     `shaders/skybox.vert`, `shaders/texture.comp` — are 4,660 bytes together and not worth an
     exclude. The 444 KB of dead fonts was the whole prize, and that is already taken.

---

## 8. Working alongside `fontbake`

There is no build-graph interaction and very little file overlap, so the two can proceed in
parallel.

- **`tools/tools.mk` is shared and I do not need to change it.** It already supports everything
  `assetpack` wants: `CORE_SRCS` for the miniz sources, `-I$(ROOT)/3rdparty/miniz/` is already in
  `IPATHS`. If it does turn out to need an edit, that is the one file to coordinate on.
- **`fontbake` produces an asset; `assetpack` consumes whatever is on disk.** `mono_sdf.fnt` lands
  in `shared_assets/fonts/` and is committed, so from the packer's side it is just another file.
  Neither tool needs to know the other exists, and §4.3 says why it should stay that way.
- Both will want a line in `docs/engine_backlog.md` (items 75 and the UI overlay's) and both touch
  `readme.md`'s tool list. Small, and easy to merge.
- `engine.mk` is mine to edit (§4.1); `fontbake` does not touch it — it links nothing from `core/`
  and is run by hand.

The one thing worth a word: if the overlay work ends up wanting `mono_sdf.fnt` loaded through
something other than `LoadFile`, that is a fifth entry for §5's table.

---

## 9. Decisions taken

The questions this plan was written to ask, and the answers given 2026-09-14. Kept as asked rather
than rewritten, because the shape of the plan above is the *consequence* of them and the reasoning
is worth being able to walk backwards through.

1. **Is the side-`.pak` wanted at all, or is `incbin` enough?** If every app that would ship is
   under ~15 MB, §3's third mode is work that buys nothing, and the plan collapses to steps 1–4.
   Grid and isoanimation are the only two that force it — and grid is a test bed rather than
   something that ships. *This is the one answer that changes the amount of work most.*
   <br>**Answer:** For now and the forseeable future, one incbin is enough.
2. **Should the `cpp` mode survive at all once `incbin` works?** Its only remaining reason is
   feeding the Android port, which has its own copy of the tool anyway. Keeping it costs one
   `if` in the writer; dropping it means the merge story with the port gets one more difference.
   <br>**Answer:** No reason to keep it. Android should be able to link the same way. Will be explored/fixed on the Android side.
3. **Compression: on, off, or per-extension?** `.glb` with embedded PNG/JPEG and `.wav` that is
   already ADPCM deflate to roughly nothing while costing inflate time at load. A size/time table
   over this tree's real assets would settle it in an afternoon, and `--no-compress` exists so the
   measurement is a flag rather than a rebuild.
   <br>**Answer:** I'd say per file is an eventual optimisation. For now it can default to on.
4. **Is `apps/ocpp/assets/www/` an asset at all?** It is served over HTTP, hot-reloaded from disk,
   and read with `ReadFileToString`, which by design never caches. It may simply want
   `--exclude www/*` and to stay loose forever.
   <br>**Answer:** Good point. Indeed, not really an asset. `--exclude www/*` is fine.
