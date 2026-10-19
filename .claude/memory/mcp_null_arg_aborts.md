---
name: mcp-null-arg-aborts
description: "A null JSON argument to a chasm MCP tool (e.g. add_person house:null) killed the app outright, no crash report - pass 0 or leave the key out"
metadata:
  node_type: memory
  type: reference
  originSessionId: 80068f08-caf2-4476-8759-ffd8a85ffc3a
  modified: 2026-10-18T20:24:05.327Z
---

Seen 2026-10-18 on chasm: `chasm_economy op add_person` with `"house": null` (a script passing on a refused paint's
missing id) - the app was gone, no `build/crash_*.txt`, nothing in stderr. The handler reads `args.value("house",0u)`;
nlohmann's `value()` with a key that is present but null cannot convert, and the build has `-fno-exceptions
-DJSON_NOEXCEPTION`, so the error aborts instead of throwing (likely cause - not traced in a debugger).

**Why:** it looks like a crash in whatever the call was testing, and the crash reporter does not catch it.

**How to apply:** in test scripts never pass null for a tool argument - leave the key out or pass 0; check a
previous call's id before using it. If an instance vanishes with no crash report right after a tool call, suspect
this first. Related: [[background-app-stdout-pipe]].
