---
name: worktree-ab-build
description: "Building HEAD in a git worktree for an A/B check - untracked files to copy, MAX_PATH trap under the scratchpad, CPLUS_INCLUDE_PATH fix"
metadata:
  node_type: memory
  type: reference
  originSessionId: 4c59e560-7c23-462c-8e8d-4fa67026fe08
  modified: 2026-10-18T19:46:38.686Z
---

An A/B (does HEAD without my change behave the same?) can be built in a `git worktree add --detach <scratchpad>/ab HEAD` without touching the shared build tree, so no `#build` claim is needed. It doesn't build as checked out:

- **Copy what git doesn't track:** `libs/*.a`, ALL of `3rdparty/` minus `build/` (robocopy /E /XD build; the rp3d fork headers, miniaudio etc. are ignored, and imgui is a submodule a worktree leaves empty), and for archer `apps/archer/recordings/archer_test.rec`.
- **MAX_PATH:** the scratchpad path is ~141 chars, and `apps\archer\..\..\3rdparty\reactphysics3d\collision\narrowphase\ConvexPolyhedronVsConvexPolyhedronAlgorithm.h` on top of it is over 260, so native cc1plus reports "No such file" for a file that ls and PowerShell can see. `subst` doesn't help (it resolves to the long path). What worked: `subst W:` plus `export CPLUS_INCLUDE_PATH="W:/3rdparty"`, a normalized fallback searched after the `-I` dirs.
- **Clean-up:** `git worktree remove --force` fails with "Filename too long"; delete with `cmd /c 'rd /s /q "\\?\<path>"'` and then `git worktree prune`. Also `subst W: /D`.

**When the tree carries other sessions' UNCOMMITTED work, HEAD is the wrong A** - it differs by far more than your
change. Used 2026-10-18 for chasm instead: copy the app folder minus `build/` to a sibling (`apps/chasm_ab/`, so
`../../engine.mk`, core and shared_assets resolve; same PROJECT, its own build/obj, saves and recordings), undo ONLY
your edits there with a script that asserts each edit matches exactly once, `diff -r` it against the original to show
nothing else differs, build it (claim `#build`; app objects only, core is shared), run it on its own port, delete the
folder after. Put the saves' `zz_in_*` copies in BOTH apps' `saves/`, and pause both instances before the first load.

Related: [[archer-test-baseline-stale]], [[per-app-build-layout]].
