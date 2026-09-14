# `ASSET_NEEDS` — the code declares what it loads

Backlog item 91. Written 2026-09-14, after `make ship` and the packed-asset listing landed and
between them made the gap obvious. Everything called *measured* below was run on this checkout with
this toolchain; the spike is reproduced in §2 so it can be re-run.

---

## 1. What is wrong now

`tools/assetpack` walks `ASSET_ROOTS` and subtracts `--exclude` globs. So the shipped asset set is a
property of **the tree, plus a list somebody maintains by hand**. What anyone actually wants is a
property of **the code**: the assets this build of this app can ask for.

There are three candidate sources of truth today and only the third is authoritative:

| source | knows about the tree | knows about the app | knows about the build's flags |
|---|---|---|---|
| the asset tree | yes | no | no |
| `ASSET_PACK_FLAGS` in the app makefile | no | yes | **no** |
| the code that calls `LoadFile` | no | yes | yes |

That third column is not hypothetical. `apps/tetris/makefile` excludes `fonts/consola.ttf`, which is
correct for `make ship` — that is ImGui's font, `USE_IMGUI=0` drops `core/WindowImGui.cpp`, nothing
asks for it. But the exclude list is read at parse time and cannot see a flag, so it also applies to
`CONFIG=release BAKE_ASSETS=1`, which still has ImGui. Measured 2026-09-14, that build now dies
during startup:

```
[fatal] File : LoadFile failed to load [fonts/consola.ttf] - looked in: fonts/consola.ttf
```

There is no fallback, because `ASSETS_BAKED` means `main.cpp` declares no disk root. That is item 90,
and it is the smallest possible demonstration of the general problem: **an exclusion is a claim about
what the code loads, and only the code knows that.**

### Two answers that were considered and are not this one

**Feed back what a run loaded.** This is exactly `DUMP_BINARYASSETS`, removed on 2026-09-14 — the
reasoning survives in the note on `assets[]` in `core/BinaryAsset.h`. It can only ever capture what
one session happened to reach, which is a different question from what the app can ask for, and only
one of the two has a reproducible answer. A menu never opened, a sound only a tetris plays, a shader
behind a toggle: all absent, and nothing anywhere would say so. **A runtime manifest is a verifier
here, never an input.** §5 puts it to work as one.

**Scan the sources for asset-name literals.** Reproducible, and flag-aware if it scans only the
translation units the build actually compiles. But it is a heuristic sitting at arm's length from the
thing it is describing: it cannot see a name built at run time, it cannot see one asset referenced
from inside another (§4), and the day it is wrong it is wrong silently. Worth keeping as a **warning**
(item 91 step 2); not worth trusting as the list.

---

## 2. The mechanism, and it is verified

One macro, placed next to the code that loads the thing:

```cpp
#define _AN_CAT2(a,b) a##b
#define _AN_CAT(a,b) _AN_CAT2(a,b)

//Declares that this translation unit can ask LoadFile for `name`. Costs nothing at run time:
//the bytes live in their own section, which the linker drops - see below.
#define ASSET_NEEDS(name) \
    static const char _AN_CAT(_asset_need_,__COUNTER__)[] \
        __attribute__((section(".astneed"),used,aligned(1))) = name;
```

The section holds the **string bytes themselves**, NUL-terminated and concatenated — not pointers to
them. That is the whole trick, and it is what makes the section readable straight out of a `.o` with
no relocation processing and no symbol table walk.

### What was measured

A spike with three declarations — two at namespace scope, one inside a function body:

```
$ objdump -h spike.o | grep astneed
  8 .astneed      0000003c  ...  2**2

$ objcopy -O binary --only-section=.astneed spike.o names.bin && tr '\0' '\n' < names.bin
sound/click.wav
shaders/ui_overlay.vert
fonts/consola.ttf
```

Three names, exactly, including the one declared inside a function — **scope does not matter**, which
is what lets the declaration sit against the `LoadFile` call rather than at the top of the file.
`aligned(1)` is load-bearing for tidiness: without it each entry is padded to 16 bytes and the
extract comes back full of empty strings. It still parses (drop the empties), but 0x3c against 0x50
for the same three names is the difference between reading the output and squinting at it.

### It costs nothing in the shipped binary, and here is what that depends on

Linked with **exactly the flags `engine.mk` already passes** — `-Wl,--gc-sections`, no
`-fdata-sections`:

| link | `.astneed` in exe | `"fonts/consola.ttf"` in exe |
|---|---|---|
| `-Wl,--gc-sections` | absent | absent |
| no `--gc-sections` | present | **present** |

`-fdata-sections` turns out not to be needed, because the attribute has already put the data in its
own named section — which is the only thing `--gc-sections` needs to be able to drop it
independently. Nothing references these objects, so they are collected.

**That is a real dependency and it should be written down where the flag is**: if
`-Wl,--gc-sections` ever comes out of `LFLAGS`, every declared asset name starts shipping as a string
in the binary. Harmless, invisible, and precisely the sort of thing that is found two years later.

---

## 3. What the packer does with it

### 3.1 Getting the names out

A pattern rule per object, which is the part that makes this cheap:

```make
$(OBJ_DIR)/%.needs: $(OBJ_DIR)/%.o
	objcopy -O binary --only-section=.astneed $< $@
```

**An object with no declarations is not a special case.** Measured: `objcopy` exits 0 and writes a
zero-byte file when the section is absent. So the same rule covers all ~60 objects with no filtering,
no shell conditional, and no error to suppress — which matters, because most objects declare nothing.

Then `assetpack` grows one repeatable option:

```
--needs <file>     a NUL-separated list of required asset names; repeatable
```

and, when any `--needs` is given, packs the union of those names instead of walking for everything.

**`objcopy` in the makefile rather than COFF parsing inside `assetpack`** is a deliberate trade. The
tool stays dependency-free and format-agnostic — it reads bytes and splits on NUL — and the one piece
that knows about object files is a binutils call sitting in the makefile, where the toolchain already
lives. The Android port (ELF, `llvm-objcopy`) then needs a different *command*, not a different
*tool*. Parsing COFF section headers directly is about eighty lines and would have been fine on
Windows; it would have been eighty more for ELF.

It is also, deliberately, **not a shell loop**. `engine.mk` already avoids `$(shell find ...)` so the
build does not depend on which shell make picked, and one pattern rule per object is both parallel-safe
under `-j` and make-native.

### 3.2 Build order, and the circularity to not create

The packer now needs the objects, and the objects include the generated table. That is only circular
if it is written carelessly:

1. compile the app's and core's objects — none of which depend on the pack
2. extract `.needs` from each
3. run `assetpack --needs ...` → `BinaryAssetMemory.cpp`, `assets.S`, `assets.bin`
4. compile **those**
5. link

So the pack's prerequisite is `$(APP_OBJS) $(CORE_OBJS)` **minus the generated table's own object**.
Getting that `filter-out` wrong is the one way to make this eat itself, and it will present as a
mysterious make loop rather than as an error about assets.

---

## 4. The two things it cannot see

Neither is a reason not to do this. Both are reasons the verifier in §5 keeps its job.

**Names composed at run time.** `"shaders/" + variant + ".frag"`. No mechanism that reads the code
can see these — but unlike a literal scan, this one *accommodates* them: write the `ASSET_NEEDS` lines
out by hand, next to the code that composes the name, and they are as real as any other declaration.
A glob form (`ASSET_NEEDS_GLOB("shaders/variant_*.frag")`) is worth having for the cases where the set
is open-ended; `assetpack` already matches `--include`/`--exclude` globs against asset names, so the
matching half exists.

**Assets that reference other assets.** `core/Shader.cpp` implements a nested GLSL `#include` with
cycle detection, so `apps/ship/assets/shaders/*.frag` pulls in `density.glsl` by name with no C++
literal anywhere in the repo. Declaring the includee in C++ would work and would be wrong — it puts
knowledge of a shader's contents in a `.cpp` that does not include it, and it goes stale the moment
someone edits the shader.

The right answer is that **the packer follows the graph**, because it is already reading the bytes:
after resolving the declared set, scan each packed text asset for `#include "x"` and add `x`,
transitively. That mirrors exactly what `Shader.cpp` does at run time, it is deterministic, and it
needs no new source of truth. Cycle handling is a visited-set, which `Shader.cpp` already needs and
can be read off it.

---

## 5. How you know the declarations are complete

This is the central risk of the whole approach, and the reason to do it in this order. A missing
declaration is not a warning — in a baked build it is the §1 fatal, at startup, on somebody else's
machine.

Two checks, and the first is the good one:

**A debug build warns when `LoadFile` is asked for something undeclared.** The same extraction that
feeds the packer can be compiled into a non-baked build as a plain sorted name table. `LoadFile` then
checks the name against it and warns — *warns*, because a loose build has a disk to fall back on and
the app should keep running. That turns "you will find out when you ship" into "you find out the
first time you run it", during ordinary development, in the configuration people actually use. It is
the piece that makes the rest safe.

**The runtime listing is the cross-check.** `BinaryAsset::ListBinaryAssets()` already reports the
packed table and, taken at shutdown, which entries were never requested — see `apps/tetris/main.cpp`
for why it is taken there and not in `Init`. Bake with no exclusions, run to shutdown, and compare:
declared-but-never-requested is a list to read, requested-but-not-declared is the warning above. Both
directions covered, neither of them guessing.

---

## 6. Rollout

**Nothing switches to declaration-driven packing on the day the mechanism lands.** The failure mode
is an app that dies at startup, so it is earned rather than assumed:

1. Mechanism in, `ASSET_NEEDS` available, `--needs` implemented — but no app passes it. Nothing
   changes for anyone.
2. **Audit mode.** `assetpack --needs ... --audit` packs *everything* as today and reports what the
   declarations would have dropped. Run it against Tetris and compare with the shutdown listing: the
   answer is already known to be 20 packed / 16 requested, so the declarations are right when the
   audit names the same four.
3. The loose-build `LoadFile` warning from §5, so gaps surface during normal work.
4. Tetris flips first — it is the app closest to shipping and the one whose asset set is already
   measured. `apps/ui` second, being the other baking app.
5. The rest opt in individually. An app that never flips keeps working exactly as it does today; the
   directory walk stays as the default and `--needs` is what changes the behaviour.

---

## 7. Order of work

| | | |
|---|---|---|
| 1 | `ASSET_NEEDS` macro, in `core/File.h` next to `LoadFile` | done as a spike, §2 |
| 2 | `%.needs` pattern rule and `--needs` in `assetpack` | §3 |
| 3 | `--audit` | §6 step 2 |
| 4 | Declarations in `core/` — `WindowImGui`, `Renderer`, `UIOverlay`, `SoundSystem` | the flag-dependent ones are the point |
| 5 | Loose-build `LoadFile` warning | §5, and the thing that makes 6 safe |
| 6 | Tetris flips; delete its hand-written excludes | closes items 90 and 91 |
| 7 | GLSL `#include` closure in the packer | §4; needed before `apps/ship` can flip |

Steps 1-3 change nothing for any existing build and can land whenever. Step 6 is the one that needs
step 5 to have been in use for a while.

---

## 8. Decisions taken

- **Bytes in the section, not pointers.** Readable from a `.o` with no relocation or symbol handling.
- **`aligned(1)`.** Without it the extract is three names and thirteen empty strings.
- **`objcopy` in the makefile, not COFF parsing in the tool.** Keeps `assetpack` format-agnostic and
  leaves the Android port needing a different command rather than a different tool.
- **A pattern rule, not a shell loop.** Parallel-safe, and `engine.mk` already avoids depending on
  which shell make picked.
- **The packer walks the GLSL `#include` graph.** The alternative puts a shader's contents into a
  `.cpp` that does not include it.
- **Declaration-driven packing is opt-in per app, and audit mode comes first.** The failure mode is a
  startup fatal; §6.
- **`-Wl,--gc-sections` is what makes the declarations free.** Measured both ways, §2. Belongs in a
  comment beside the flag.

## 9. Open

- **Does `ASSET_NEEDS` belong in `core/File.h`?** It is about `LoadFile`, which argues yes, but that
  header is included nearly everywhere and this adds a macro to all of it. A small `AssetNeeds.h`
  costs one include line in the handful of files that declare anything.
- **What `--needs` should do when a declared name is not in any root.** Today an asset that does not
  exist is simply not packed. A *declared* one that does not exist is a different thing — almost
  certainly a typo in a string literal, which is exactly the class of bug this is meant to catch, so
  it probably deserves to fail the build. Wants a decision before step 2 rather than after.
