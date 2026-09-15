---
name: android-port-merge
description: "The Android port at C:/code/android, what it sends back upstream (backlog items 68-77), and the copy-then-reapply-with-asserts merge method"
metadata: 
  node_type: memory
  type: project
  originSessionId: 30c1f15b-4e38-47fd-a2d2-a2246c2cea91
  modified: 2026-09-13T10:48:31.821Z
---

A parallel Android port of this engine lives at **`C:/code/android`** — inspired by this tree
rather than forked from it, with a simpler renderer, and Tetris running on a tablet (an MDT740).
Its own notes are in `C:/code/android/.claude/memory/`; the index of what belongs back here is
`upstream_merge_candidates.md` there.

Reviewed 2026-09-13 and folded into `docs/engine_backlog.md` as **items 68-77**, with a
`## Reference: merging the Android port` section at the bottom of that file. Item **67** (on-screen
touch buttons) moved from band D to band C at the same time, because the port has already built
steps 1-4 of `docs/touch_input_plan.md`.

**The point of USE_PHYSICS/USE_MCP/USE_SOUND is host BUILD TOOLS, not apps** (Dick, 2026-09-13).
Counting apps undersells it - only 3 of 12 are physics-free. The port's `sprite_packer.exe` is a
Windows GUI built on `core/Application` (window, ImGui, Renderer, Scene, Object) that has no use
for physics, sound or a JSON-RPC server, and sets all three flags to 0. This repo has never had a
non-app consumer of core, so `engine.mk` has no shape for one - its CFLAGS link opengl32/gdi32/
ws2_32/rp3d unconditionally. The thin sibling `pack_assets.exe` links only File+BinaryAsset+Debug+
miniz and needs no flags at all.

**Assets get packed by a separate exe, by preference not necessity** (Dick, 2026-09-13). Windows
*can* self-dump and recompile locally; Android cannot, which is why the port had to have a tool.
Taking the tool here anyway, and deleting `DUMP_BINARYASSETS`: the self-dump packs only what that
session loaded, and its core call site is in `InitGraphics` before anything is loaded. It is
already dead - the flag is defined nowhere, so all four call sites hit a stub.

**Three of its findings needed no backlog item, and the reason matters:**
- `app_name` being the debug panel's ImGui title is **already fixed here** by the docked-panel
  rework — `app_name` no longer exists in `core/`.
- `Object::SetPickable(bool)` is not missing; this tree has `SetPickability(bool)`. Same thing,
  different spelling. **Check for the engine's name before adding one.**
- Lambert 1/PI, `SetWritableDataDirectory`, `--no-undefined`, the `*_android` file split are
  Android-only.

**`Texture.cpp` merged (step 4b), 2026-09-16.** `Create2D`, `Create3D`, `UploadTexture` and
`LoadHDRFromFile` have GLES arms; `Texture.h` needed NO change (glad.h switches, and `GLuint64`
exists in GLES3, so the bindless member stays unguarded - the whole bindless block is already
behind `#ifdef BINDLESS_TEXTURES`, which is **defined nowhere in the tree**).

Two things this turned up that were not mechanical:
- **The cubemap face.** DSA addresses a face as a Z SLICE (`glTextureSubImage3D` with `depth` as
  the z offset); GLES names it by its own target enum, `GL_TEXTURE_CUBE_MAP_POSITIVE_X + face`.
  The only genuinely non-mechanical translation in the file.
- **The GLES arm must pin `glActiveTexture(GL_TEXTURE0)` before binding.** Bind-based calls use
  global state, so without it a texture lands on whatever unit was last active. The port hit this
  as its shadow map sampling a random material's colours. The DESKTOP arm cannot have the bug -
  `glTextureParameteri` names the texture and touches no binding - which is the point of DSA.

**Unlike Mesh.cpp, this diff removed four lines**, all pre-existing defects the port had already
fixed: `UploadTexture(UINT _format)` -> `GLenum` (the header always said `GLenum`; `UINT` is a
Windows type and was the only one leaking into a core signature), a second `UINT format;`, and two
`debug->Info(... %li ...)` for a `GLuint` -> `%u`. Type-identical on Win32, so no behaviour change.

`Create3D`'s GLES arm is **written by me, not taken from the port** - the port has no `Create3D` -
so it has never executed. Flagged as such at the site.

Verified: tetris, ship (Create3D x2 + textures), grid and animation all build and render correctly;
`make ship` clean. **The cubemap path was never exercised at runtime** - no app loaded one in these
runs - so that arm rests on the desktop side being byte-identical, not on a test.

**The merge method that worked, worth reusing in either direction:** copy the upstream file
**wholesale**, then re-apply each local adaptation from a script that **asserts on its anchor**, so
an upstream change that invalidates an adaptation fails loudly instead of silently dropping it. Ten
adaptations in `ApplicationTetris.cpp` survived a ~900-line diff that way.

**Always diff with `--strip-trailing-cr`.** This repo is CRLF in the worktree (`core.autocrlf=true`,
`.gitattributes` `text eol=lf`) and the port is LF; without it a 43-line change renders as 2,775.
Note `git diff` does NOT accept that flag — use `diff`/`git diff --no-index` or normalise first.

**APPLIED 2026-09-15 (steps 1-3 of the merge plan).** `core/glad.h` now carries the platform
switch for the whole tree; `PerfTimer` is on `std::chrono`; `Debug_win32.cpp` is `_WIN32`-guarded;
`Sprite::CalculatePixelRect`, `SpriteSheet::{AddSpriteFromWholeTexture,AddSpritesFromGrid,Clear}`
and `File::SetWritableDataDirectory` came over from the port. **33 of the 62 `core/*.cpp` now
syntax-check clean for aarch64**, from zero before. Windows debug and `make ship` both build clean
and Tetris runs.

**`Mesh.cpp` merged next (step 4a).** Four upload sites plus `InitSSBO`/`InitVBOVAO`/
`InitLineVBOVAO`/`InitSkinnedVBOVAO` now have a GLES arm using bind-then-call
(`glGenBuffers`/`glVertexAttribPointer`) beside the DSA one. **The diff removes no line at all** -
the desktop arms are byte-identical, wrapped in `#else` - which is the cheapest possible proof
that desktop behaviour did not move. Verified anyway: Tetris (normal + text meshes) and the
animation app (skinned character, correct skinning/normals/UVs/shadows) both render correctly.

Two things worth keeping from it: `type_vertex.h` is **byte-identical on both sides**, and the
desktop arm's hand-computed offsets (`11*sizeof(float)`) and the GLES arm's `offsetof` agree
exactly - now enforced by a block of `static_assert`s at the top of `Mesh.cpp`, unconditional so
an Android-only build still catches a layout change that would break desktop. GLES also has no
immutable buffer storage, so `SetMorphMeshData`'s `glNamedBufferStorage` becomes `glBufferData`
there; harmless for a write-once buffer. The port's `Mesh::ReUploadMeshData()` was NOT taken - it
belongs with `Renderer::ReUploadAllMeshes` and the context-loss item.

**`core/glad.h` is the single chokepoint, measured 2026-09-15.** `core/Mesh.h:5` includes
`"glad.h"`, whose line 4 is `#include <windows.h>` — so every file that touches `Mesh` or `Object`
dies there before reaching any real portability question. Swap `glad.h` for a
`#include <GLES3/gl31.h>` shim and **31 of the 63 `core/*.cpp` syntax-check clean for aarch64 with
the NDK, unmodified** — including `Object`, `ObjectAnimation`, `GLTFLoader`, `Shader`, all of
`physics/` and all of `skeleton/`. The Android port's own `glad.h` already opens with the right
guard; take its first four lines.

After that came **`PerfTimer.h/.cpp`**: 13 lines of `LARGE_INTEGER`/`QueryPerformanceCounter`
in a file that already included `<chrono>`, and whose `<Windows.h>` reached `Application.h` and
`Renderer.h`. Done. **Careful with what a stubbed measurement tells you**: with fake Win32 headers
in the include path, `Scene.cpp` and `ParticleEmitter.cpp` failed on nothing but `LARGE_INTEGER`,
which read as "PerfTimer is the only blocker". It was not - the stubs were satisfying
`windowsx.h`, which those two really get from `InputController.h`. Both still fail on that, and
`InputController` is the next real piece of work for them.

Measured while replacing it, because the file now claims the numbers did not move: `steady_clock`'s
tick here is **100 ns and `QueryPerformanceFrequency` reports 10 MHz - the same 100 ns**, so
libstdc++ is backing it with the same counter, and over one busy interval `PerfTimer` and a direct
QPC pair agree to **1-4 us in 8,400**.

**The arbiter is the NDK compiler, not the eye** — `aarch64-linux-android24-clang++ -std=c++17
-fsyntax-only` from `C:/code/android/sdk/ndk/27.2.12479018`. Diff line counts mislead badly here:
they conflate Android divergence with plain staleness, and for most small files
(`Camera`, `AssetManager`, `Light`, `Material.h`, `GLTFLoader.h`) the entire diff is this tree
having moved ahead, with no Android content at all.

See [[touch_input_plan]], [[per_app_build_layout]], [[shared_build_output_coordination]].
