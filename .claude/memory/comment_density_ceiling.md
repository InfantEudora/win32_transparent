---
name: comment-density-ceiling
description: House comment style has a ceiling - prose blocks per enum value are too much; one-liners on the declaration, block comments only for the type
metadata:
  type: feedback
---

CLAUDE.md's "this codebase comments unusually heavily and unusually well" has an upper bound the
user enforces. On 2026-09-17 they called `apps/bomber/Maze.h` "almost unreadable" and asked for:

- A **one-liner on the declaration** where the name nearly says it already
  (`MAZE_DECOR_LILLY, //only goes on water`), not a paragraph.
- A **short block comment on the type** saying what the thing is - what "decor" means - rather than
  per-value essays.
- **Exceptions get a sentence**, not a story: bridge is decor that makes water crossable, and that
  is the whole comment.

**Why:** the *why* rule still holds, but a multi-paragraph rationale per enum value buries the
code it is attached to. The user's phrasing: "code should document itself."

**How to apply:** keep the hard-won fact, drop the narrative around it. "on the walker, not the
game - 35 of 60 boards ended with an enemy in the exit" keeps the measurement and loses three
paragraphs. Verify a comment-only edit with `g++ -fpreprocessed -dD -E -P` on both versions and
diff - it proves no code moved. Related: [[bomber-app]].
