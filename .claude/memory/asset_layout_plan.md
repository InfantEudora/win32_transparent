---
name: asset-layout-plan
description: "AGREED DIRECTION 2026-09-12 — split flat data//shaders/ into per-app assets/<app>/ + assets/shared/, resolver in LoadFile first, strip DUMP_BINARYASSETS entirely; plan lives at docs/asset_layout_plan.md"
metadata: 
  node_type: memory
  type: project
  originSessionId: 321ef6c3-8b60-4cdc-8703-73b7f520e3b5
  modified: 2026-09-12T13:43:30.603Z
---

Agreed with the user on 2026-09-12. Full plan (measured facts, line numbers, migration order,
open questions) is in `docs/asset_layout_plan.md` — read that before touching any of it; this
note only records the decisions so they are not re-litigated.

**Direction:** one `assets/` tree with `assets/shared/` inside it (not a sibling
`shared_assets/`), keyed on `APP` and not on the source folder — the source folders are shared
*libraries* (`isoterrain/` serves Grid + IsoAnimation + Tileset), so they cannot key an app's
assets. Three scopes: core/shared, library, app. Membership rule: *if `core/` loads it it is
shared; if a library loads it it belongs to that library; otherwise it belongs to the app.*

**Step order (agreed):** the `LoadFile` search-path resolver comes FIRST, with `data/` and
`shaders/` as the last two fallback entries so every existing literal keeps resolving and apps
can then move one at a time. The fallbacks coming out is what proves the migration finished.

**`DUMP_BINARYASSETS` is to be stripped entirely** — the user's Android build already packs
binaries with a separate generator, and that generator will be integrated here instead of the
two-pass makefile-flag stage. Until then `BinaryAsset::assets[]` is the empty array
`BinaryAssetMemoryEmpty.cpp` already provides. Safe in any order: both flags are `0` in-tree
today, so no current build depends on it. Removing it is *producer-only* — `GetBinaryAsset` /
`StoreBinaryAsset` / `Uncompress` / the `file_assets` deque all stay, and miniz stays for
`tinfl_decompress_mem_to_heap`.

**Trap worth remembering:** in the default build `DumpBinaryAssets()` does not dump — the
`#else` body calls `ListBinaryAssets()`. The four call sites (Dozer, Grid, Ship,
`core/Application.cpp`) are therefore *logging* calls wearing the wrong name. Do not delete them
believing they were no-ops.

Related: [[project-overview]] (describes the two-stage bake as if it were live — now superseded),
[[build-toolchain-location]].
