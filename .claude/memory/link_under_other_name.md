---
name: link-under-other-name
description: "When the user's game holds the exe, compile with make then re-run its own link line with another -o; make refuses EXE= overrides"
metadata:
  node_type: memory
  type: reference
  originSessionId: 69bfd5e2-d3af-47c9-869a-da8e0eaa395a
  modified: 2026-10-21T20:02:22.733Z
---

engine.mk rejects unknown command-line overrides ("not a build setting: EXE"), so a test build can't be renamed through make. When the user is playing the app (its exe locked, `Permission denied` at link), run `mingw32-make.exe -j8 > log 2>&1` anyway (everything compiles, only the link fails), then take the `g++ ... -o build/<exe>.exe ...` line from the log, swap the output name (e.g. `build/chasm_slope.exe`) and run it. That leaves the user's game running; the next plain `make` after they close it is just a relink. Used 2026-10-21 for chasm's slope grid. Related: [[ok-to-stop-running-apps]], [[per-app-build-layout]].
