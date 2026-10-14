---
name: void-plan
description: "The Void - llm app's model reads a running Chasm over MCP and advises; decisions, step 0 findings, apps/llm/docs/void_plan.md"
metadata:
  node_type: memory
  type: project
  originSessionId: 92a7bfa9-85a6-4a81-8340-4d0cb2b9d582
  modified: 2026-10-14T11:24:52.412Z
---

The user's experiment, started 2026-10-14: the Void, a voice only (not a colonist), advises the Chasm player.
Plan and step-0 results: `apps/llm/docs/void_plan.md`; briefs, prompts and replies in `apps/llm/void/`.

Decided by the user: advice only (never acts), one choice a turn, lives entirely in the llm app (Chasm not
changed for it), apps run separately; voice is tuned in the system prompt, left as the strict one for now.
Step 0 (hand-written briefs via llm_chat) and step 1 (VoidBrief.cpp + ApplicationLLMVoid.cpp: MCP client,
brief from a live Chasm, Void window with editable system prompt, manual Ask, void_read/void_ask/void_system
tools) done 2026-10-14, plus the brief editable before asking. Step 2 (speak on its own + JSONL log) is
next, not yet asked for. The user then allowed READ-ONLY MCP output in Chasm for the Void (days held,
in_camp, crops, buildings, most in need); built by window win32-transparent-02 the same day and VoidBrief
now uses it (copies kept as fallback). Open for the user: camp pantry missing from Chasm's DaysHeld, and
whether to build a journal (view-side diff vs rules-kept) - both in void_plan.md "Chasm's side".

Step 0 findings: the 3B picks right when there is a real danger and the menu spells out consequences, but in
a calm colony it always "does something" and invents shortages - so the app decides WHETHER to ask, the
model only WHICH. 8B no better, 6.2 GB VRAM won't fit beside Chasm on the 8 GB card: 3B it is.

**Why:** the user wants it kept separate from the game while it is an experiment.
**How to apply:** the Void itself stays in apps/llm; Chasm gets read-only MCP output only, nothing else. Run test apps on
their own ports (8770 is taken by an unrelated win32can.exe; 8771 was used for llm). Related: [[chasm-game-plan]].
