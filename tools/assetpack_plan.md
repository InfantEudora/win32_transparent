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
CORE_SRCS += $(ROOT)/3rdparty/miniz/miniz_tdef.cpp
CORE_SRCS += $(ROOT)/3rdparty/miniz/miniz_tinfl.cpp
include $(ROOT)/tools/tools.mk
```

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
   Verify against tetris: `--list` over `shared_assets` names every file with forward slashes and
   no `./`, and reports no shadowed names. Against ship, which has two roots, it reports the one
   real override.
3. **The writer.** `assets.bin`, `assets.S`, `BinaryAssetMemory.cpp`. Verify by compiling the
   generated trio by hand and dumping the table — before any makefile is touched, so a failure here
   is the tool's and not the build's.
4. **The `engine.mk` rules.** Verify: `apps/tetris` builds with `BAKE_ASSETS := 1`, runs with its
   asset root **removed from `main.cpp`**, and screenshots identically. That is the real test — a
   baked build that still has its roots proves nothing, because every lookup falls through to the
   disk and succeeds. Then re-add the root and confirm the loose file still wins.
5. **Measure it.** Exe size and build time for tetris baked vs loose, recorded in
   `docs/engine_backlog_done.md` with item 75. §3's numbers are the argument for the shape; this is
   the evidence that the shape delivered.

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
