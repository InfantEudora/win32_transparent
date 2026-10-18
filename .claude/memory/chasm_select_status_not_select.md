---
name: chasm-select-status-not-select
description: chasm_select op select returns a STALE card after sim_step; read the card with op status
metadata:
  node_type: memory
  type: reference
  originSessionId: 26190601-4a53-4a7c-a11d-31341147a809
  modified: 2026-10-18T11:45:22.779Z
---

Found 2026-10-18 testing the person card: calling `chasm_select` op `select` (person) again after `sim_step` kept
returning the card as it was at the first select, 1450 ticks old. `op status` returns the current card. So in a
script, select once, then step and read `op status` each time.

Visual check of a card: it draws only in play mode (`chasm_view play:true`) and not while minimised - show the
test copy's window with ShowWindow(h,4) (no focus), screenshot include_ui, then ShowWindow(h,7). See
[[minimized-screenshot-no-ui]], [[chasm-goods-strip]].
