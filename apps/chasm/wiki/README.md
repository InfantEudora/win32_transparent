# The chasm wiki

One page per thing in the game: what it does today, what it touches, and what is planned for it. The
pages are built into a single HTML file:

```bash
python apps/chasm/tools/chasm_wiki.py            # build apps/chasm/build/wiki.html, checking every page
python apps/chasm/tools/chasm_wiki.py --unused   # ...and list the tuning constants no page mentions
```

Open `apps/chasm/build/wiki.html` in a browser. It is generated and not committed (`build/` is ignored);
these `.md` files are the source. The tool needs only the standard library, but use the Windows Python
(see "Two pythons" in CLAUDE.md).

The plans in `../docs/` stay what they are: the reasons, the decisions and the history. A page is the
present tense, what the game does now, and points into the plans for the why.

## When you change the game

| You... | Then |
|---|---|
| changed what a mechanic does | update its page, set `verified:` to today, run the tool |
| changed only a tuning value | nothing: pages read values from the code |
| renamed or removed a constant, function or plan heading | the tool fails and names the page; fix the page |
| agreed, proposed, parked or scrapped something | add or change its bullet under `## Planned` on the page it belongs to |
| added something new to the game | write its page, or at least link to it so it shows as wanted |

Run the tool before you finish. It exits with 1 on any error.

## Writing a page

**Write from the code, not from the plans.** The plans still hold decisions that were reversed later. A
page says what the code does today, so read the code first, then write.

**Keep it short, and about rules.** A page has at most 45 non-blank lines. Say what someone needs in
order to predict what happens: the order things happen in, what takes priority over what, and what else
it touches. Leave out the implementation, and leave the edge cases and history to the plan. Going over
the limit means it is two pages, or detail that belongs in the plan.

**Never type a figure.** Write the constant instead. `{ECONOMY_WOODCUTTER_REACH}` shows its value, and
`{NEEDS_WATER_FULL:days}` shows a tick count as days (`seasons`, `years` and `seconds` work too). A
constant that already counts something takes its noun with a `#`: `{ECONOMY_HOME_DAYS:#days}` shows
"2 days", or "1 day" if it is retuned to 1, so a figure followed by a countable noun never reads wrong. Values
are filled in at every build, so retuning never leaves a page wrong, and a renamed constant fails the
build. A figure that lives in a function rather than a `#define` (the food values, the skill pace) is
described in words, with the function named.

**Link everything the page touches**, as `[[Store]]` or `[[Store|stores]]`; a plural finds its page on
its own. Every page gets a "Linked from" list, so a link only has to be made in one direction. Linking to
a page that doesn't exist yet is fine: it shows in red, and on the front page under "Pages wanted".

## The page

```markdown
---
title: Woodcutter
aliases: woodcutter's hut, hut, woodpile
kind: people
status: built
summary: Fetches logs and fells trees round the hut, and carries the wood to a store.
plans: economy_plan.md > The woodcutter: logs first; people_plan.md
code: Economy.cpp: TickWorker FindTree; Economy.h
verified: 2026-10-09
---

A paragraph or two. Then ## headings, lists, tables, **bold**, *italic*, `code`,
[[Links]], [text](../docs/some_plan.md) and {CONSTANTS}.

## Planned

- [planned] Something agreed with the user and not built yet.
```

| Key | |
|---|---|
| `title` | The page's name, and what `[[...]]` finds it by. The file name is the page's address |
| `aliases` | Other names that links may use, separated by commas |
| `kind` | The sidebar group: `people`, `buildings`, `needs`, `goods`, `world`, `systems` |
| `status` | The status of the page's subject as a whole (below) |
| `summary` | One sentence, shown under the title and on the front page |
| `plans` | `file.md > start of a heading`, with `;` between entries. The heading is checked |
| `code` | `File.cpp: Symbol Symbol`, with `;` between files. Each symbol is checked to be in its file |
| `tuning` | Constants for the page's tuning table that the text does not mention, separated by commas |
| `verified` | The date the page was last checked against the code |

## Statuses

| | |
|---|---|
| `built` | In the game, working as the page says |
| `partial` | Some of it is built; the page says what is missing |
| `planned` | Agreed with the user, not built yet |
| `proposed` | Written up for the user to decide, not agreed |
| `idea` | Mentioned for later, not worked out |
| `parked` | Put aside; may come back |
| `scrapped` | Dropped |

A page has one status. A bullet that starts with one, like `- [proposed] ...`, is a piece of the road
ahead, and the front page collects it under "Where we're going". Put each such bullet on ONE page, the
one it belongs to, and link to that page from the others.

**Never delete a scrapped mechanic.** Mark its page or bullet `scrapped`, with a line saying why, so
nobody proposes it again.
