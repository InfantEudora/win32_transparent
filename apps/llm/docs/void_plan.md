# The Void: a voice that watches the Chasm colony

The user's, 2026-10-14. The `llm` app's model reads a running Chasm through its MCP interface and advises
whoever plays it. It is a big experiment, so it lives in the llm app and Chasm is not changed for it.

## Decided (the user, 2026-10-14)

- **It is just a voice, the Void.** It is not a person in the colony.
- **It advises, and never acts.** It runs no command in the game.
- **It makes one choice a turn.**
- **It lives in the llm app.** The two apps run separately. Chasm gains only read-only MCP output for it, added
  later the same day, so the brief can stop copying Chasm's rules (see "Chasm's side").
- **Step 0 comes first**: hand-written briefs through the existing `llm_chat`, before any code is written.
  It was done the same day; the results are below.
- **The system prompt is editable in the llm app**, and the Void's voice is tuned there.
- **So is the brief, before it is asked.**

## The shape: the game's facts, the model's choice and words

A 3B model cannot do sums over Chasm's economy JSON (about 30 fields a person) or reason about coordinates.
It can weigh a few stated facts, choose between stated options and give a reason. So each turn it gets:

- a **brief**: short sections in words and days, always in the same order. They are TODAY, PEOPLE, STORES,
  FIELDS, WORK, OUT, THREATS, and NEEDS ATTENTION copied verbatim.
- a **menu** of what could be advised now, each option with its consequence spelled out. For example, "Build
  a field. Greens ripen in 8 days, after the frost: nothing would be harvested before spring."

It answers with **a letter and one or two sentences**. Because it only picks a letter, it cannot invent
advice that is not on the menu.

## Step 0: what was found (2026-10-14)

Three briefs were written by hand from three saves, read over MCP from a running Chasm. They are in
`apps/llm/void/`, along with both system prompts and every reply (`step0_results.jsonl`, 48 runs). Each brief
was sent as a fresh `llm_chat` conversation: the first run greedy, the rest at temperature 0.15.

| Brief | The situation | Best advice |
|---|---|---|
| `step0_camp` (save `camp`) | spring, day 4; 12 in the camp; water for 3 days, food for 5; no field | C, water collector (A, field, defensible) |
| `step0_camp3` (`camp_3`) | autumn, winter in 5 days; 3 left, 10 dead of thirst and hunger; food for 2 days; 24 ripe fruit at the old camp | B, pick the fruit. A field ripens after the frost, which is the trap. |
| `step0_village` (`village`) | winter, year 2; 13 healthy; food 22 days, water 18; Field 7 set to greens after greens (70 of 100) | D, Field 7 to beans, or E, nothing |

| Brief | 3B, `void_system` | 8B, `void_system` | 3B, `void_system_strict` |
|---|---|---|---|
| camp | C C C C | C C C C | C C C |
| camp_3 | B B B B | B B B B | B A B |
| village | B B B B | B B B B | A B B |

**Where there is a real danger and the brief states the consequence, it chooses right.** Every run on camp
and camp_3 picked the right option, from both models and both prompts, except one. That one picked A at
temperature 0.15 while its sentence argued for the fruit, so the letter and the words disagreed. It
avoided the frost trap every time, but only because the option's text gave the consequence. The game's
computing is doing that work.

**In a calm colony it always finds something to do, and invents the reason.**
- On the village brief it never chose Field 7 or nothing.
- It picked the provisions "because Lieve's health won't last". Lieve is the weakest at 4 days of health,
  with 22 days of food in store.
- It picked the guard tower because "water and wood are both scarce". The tower waits for 12 water; the
  colony has 18 days of it.
- Removing Lieve's line, or moving Field 7 to option A, changed nothing. It is not position bias. It reads
  any number in an option as a shortage, and does not see a 30% smaller harvest as a loss.

**Format: the letter came first on its own line in all 48 runs.** The 8B wraps it as `**C**`, which is
easy to strip.

**Voice.**
- Both models embellish under the first prompt: "before the next storm", "when the rains come",
  "Marit's hands are strong", bold text.
- The strict prompt (`void_system_strict.txt`) adds three things: something matters only if it runs out or
  is lost before it can be fixed; use only the brief's own numbers; plain text. Its replies are shorter and
  plainer ("Water is the only thing running out before it can be fixed."), but "the colony's last meal is
  saved" still gets through.
- The 8B sounds more like a Void ("thirst unites or divides"), and is no more accurate.

**Speed**, prompts of 440-650 tokens:

| | 3B alone | 3B beside a running Chasm | 8B alone |
|---|---|---|---|
| prompt | ~170 tok/s (2.6-3.7 s) | ~115 tok/s (3.8-6.7 s) | ~65-70 tok/s, one stall of 23 s |
| reply through `llm_chat` | 65-80 tok/s | 31-50 tok/s | 5-46 tok/s |
| GPU memory | 4.1 GB (2 contexts of 4096) | | 6.2 GB (1 context of 2048) |

`llm_generate`, which runs without the chat's slicing per frame, makes 100 tok/s on the 3B. A turn takes
about 5-8 s beside Chasm, well inside a 45-second game day. The 8B would not fit on the 8 GB card beside
Chasm and was not better, so **the Void runs on the 3B**. The prompt rate is a lot lower than a batched
path ought to give, and is worth a look of its own.

### What that changes

1. **The model chooses which problem to name; it does not judge whether there is one.** The llm app asks
   the Void only when the brief holds a real problem:
   - a NEEDS ATTENTION row of tier danger, countdown or stuck;
   - a store with fewer days than a threshold;
   - a death;
   - a new notice.
   On a calm day the Void says nothing, which also suits a voice that should speak rarely. "Nothing today"
   stays on the menu but is not relied on.
2. **Every option carries its weight in the brief's own terms.** An option that saves nothing says so:
   "Build the guard tower first: nothing is short, water for 18 days." The brief builder works that out,
   not the model.
3. **The strict prompt is the base** for the voice work that follows.
4. **One case of a letter and words that disagree** suggests trying the words first and the letter last, so
   the choice follows the reason.

## Where the pieces live: all in the llm app

Built in step 1 (2026-10-14):

- **`VoidBrief.h/.cpp`: the brief builder.** It reads `chasm_time`, `chasm_economy`, `chasm_issues` and
  `chasm_zombie`, and writes the brief, the menu and the reasons to speak. It uses no GL and no network.
  - **Rules copied from Chasm**, which will drift if Chasm changes them: days held (`DaysHeld`,
    `EconomyShare`, `EconomyFoodWorth`) and each crop's days. If the Void graduates, these move into Chasm
    as a `chasm_brief` tool.
  - **Rules taken from Chasm as given:** a field's `next_yields` and `frost`, so rotation and frost come
    from the game.
  - **Days held** count what `chasm_economy`'s `stored` counts, the stores and the camp. Where a NEEDS
    ATTENTION row gives its own days, which include the houses' pantries, that figure is used instead.
  - **SINCE LAST READ** is skipped after a load or a new map. It recognises one by the calendar going back,
    or moving further than Chasm can run in the time since the last read.
- **`ApplicationLLMVoid.cpp`: everything around the brief.**
  - **The MCP client:** a blocking POST to `127.0.0.1:<port>/mcp`. It stands in for a core `MCPClient`,
    which should be decided on separately if the Void graduates.
  - **Reading:** on the background worker (`LatestResult<VoidBrief>`), or on the MCP thread for
    `void_read`.
  - **The window and the `void_*` tools.**
- **Asking:** a conversation of its own (`void_chat`), started fresh for each question, in context 0 while
  the Chat keeps the last. The two never generate in the same frame, because both sample from the model's
  one logits buffer. `LLMChatSession` gained a `context` for this. The letter is read from the start of the
  reply, so `**C**` counts too.
- **The system prompt** is edited in the window, and loaded from and saved to a file. The default is
  `apps/llm/void/void_system_strict.txt`.
- **The brief is edited in the window before it is asked** (the user's, 2026-10-14).
  - The editor follows each read until someone changes it. From then on it keeps their text, and later reads
    leave it alone until "Back to the latest read".
  - The menu's letters are read from the asked text's own "A. ..." lines, so an edited menu counts as edited.
  - The editor is there with no Chasm too: a brief can be written or pasted by hand and asked.
  - `void_ask` takes `brief` for the same.

Nothing is carried out: the advice is shown, and the player acts or not.

## Chasm's side

The user, 2026-10-14: **read-only MCP output** in Chasm, so the brief takes Chasm's own figures instead of
copies of its rules. It was built the same day by another window. It is view code only: no rule, save or hash
changed. The HUD and MCP now call the same helpers (`ChasmDaysHeld`, `ChasmInCamp`, `ChasmBarGoods`,
`ChasmNeediest`).

**New in `chasm_economy` status:**
- `held{water_days, food_days, mouths}`: unrounded; NEEDS ATTENTION rounds these down.
- `bar`: the HUD's top bar.
- `worth_days`: what a unit is worth, in days.
- `firewood{homes, burn_per_dawn, dawns_at_home, dawns_with_stores, cold_today, ...}`.
- `people.in_camp`.
- `crops{<crop>: {days, ripens_at_camp}}` and `camp_growing_days_left`.
- `neediest`: MOST IN NEED.

**New op `chasm_economy op:buildings`:** every building with its kind, place, whether it stands or is a site,
`stopped`, `places` and `workers`, and the camp's own places; plus `homeless`.

**What VoidBrief uses:** `held`, `in_camp`, `crops`, `camp_growing_days_left`, `firewood` (from autumn on,
with a reason to speak when homes were cold or firewood runs short under the snow), and from op:buildings
the stopped workplaces and `homeless`. The copies remain for an older Chasm. Checked on the three saves:

- camp_3's water is now 4 days, pantries included, where the stores-only copy said 1. The false reason
  "water for 1 day" is gone.

**Flagged by that window, not changed:** Chasm's DaysHeld counts the stock of stores, camps and houses, but
not a camp's own PANTRY. That pantry is where camp families fetch food and water from the stores, so what
moved there drops out of the HUD's days. It made no difference in the three saves. It is the HUD's figure,
so it is the user's call.

**The journal, not built:**
- **Births** can be derived today: anyone with a mother was born in the colony, at `born`.
- **Deaths** are kept already.
- **Harvests and finished sites** leave no record. There are two ways to get them:
  - **View side, unsaved, covering only what one run watched:** compare each published economy with the one
    before. Getting a harvest's amount exactly would need one small read-only export from Economy.cpp.
  - **Kept by the rules, like `dead`:** saved, and left out of the hash so replays stay the same. This touches
    Economy.h/.cpp and ChasmSave. It is the user's call.

## Steps

0. **Done 2026-10-14.** Hand-written briefs through `llm_chat`, the 3B and the 8B.
1. **Done 2026-10-14.** The brief from a live Chasm, in a Void window, with the system prompt editable there.
   Asking by hand (the window's Ask, `void_ask`) came along, since an editable prompt needs something to try
   it on.
   - **Checked** against the three step-0 saves, with Chasm on 8769 and llm on 8771. The briefs match the
     hand-written ones in substance.
   - **The 3B (greedy, strict prompt) chose:**
     - camp: the water collector;
     - camp_3: Marit to pick fruit;
     - village: the guard tower, with an invented reason. That brief has no reasons to speak, so step 2
       keeps the Void silent there.
   - **Speed:** prompts of 480-690 tokens took 3.1-4.7 s, replies about 65 tok/s.
   - **The brief editable before asking** followed the same day. It was checked with `void_ask` on the
     step-0 village brief as written, and with its menu cut to two options, where A became Field 7. A
     screenshot confirmed the editor with no Chasm running.
2. **The Void speaks on its own.** It reads every few seconds, and asks when a reason to speak appears that
   it has not spoken about yet, at most once a game day. Each turn is logged as JSONL: brief, reply, choice.
3. **The voice.** The log becomes an eval set (good answers as `llm_loss` text, like
   `eval/chasm_ministral.txt`). Try words before the letter, and other small models.

## The voice

The user's, 2026-10-14: **the voice is tuned in the system prompt**, and for now it stays as set: the strict
prompt.
