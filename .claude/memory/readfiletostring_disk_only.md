---
name: readfiletostring-disk-only
description: "ReadFileToString never sees baked assets - an asset read with it alone silently vanishes from the ship exe; read disk first, then fall back to LoadFile"
metadata:
  node_type: memory
  type: project
  originSessionId: 769861fc-c639-438b-83de-ddeeafeef090
  modified: 2026-09-30T22:13:25.896Z
---

`ReadFileToString` (core/File.h) reads the DISK only. In a `make ship` build there is no disk copy,
so anything loaded with it alone is simply missing, with nothing wrong in the loose build. Found
2026-09-30: the ship exe played no music ("no such score: music/jungle.json") because
MusicScore read the score and samples that way.

The rule is CueSystem::LoadTable's, now also in MusicScore's `ReadAsset`: ReadFileToString first,
so a hot reload sees edits, then `LoadFile`, which also finds the baked copy. LoadFile logs and
returns NULL on a missing file; it does not exit. File.h now documents both points.

**Why:** a loose build hides this completely, so it only shows up when someone runs the ship exe.

**How to apply:** for any new loader of a reloadable asset, use that two-step read. To check a
ship exe, run a COPY of it from an empty folder: the exe's own build/ folder can still reach the
loose assets. Related: [[ogg-vorbis-settings]].
