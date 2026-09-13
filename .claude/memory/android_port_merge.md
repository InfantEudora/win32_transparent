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

**The merge method that worked, worth reusing in either direction:** copy the upstream file
**wholesale**, then re-apply each local adaptation from a script that **asserts on its anchor**, so
an upstream change that invalidates an adaptation fails loudly instead of silently dropping it. Ten
adaptations in `ApplicationTetris.cpp` survived a ~900-line diff that way.

**Always diff with `--strip-trailing-cr`.** This repo is CRLF in the worktree (`core.autocrlf=true`,
`.gitattributes` `text eol=lf`) and the port is LF; without it a 43-line change renders as 2,775.
Note `git diff` does NOT accept that flag — use `diff`/`git diff --no-index` or normalise first.

See [[touch_input_plan]], [[per_app_build_layout]], [[shared_build_output_coordination]].
