# Bug Report Plan: sending a report from the game

Proposed 2026-10-16; nothing built in the game yet. It follows on from `done/demo_plan.md` step 4 ("As built:
recording for testers"): Report a bug writes `reports/<date>_<time>/`, and today the tester has to get that
folder to the user by themselves. The receiving end now exists - a page on the user's website that takes a
report folder and a message, checks them and stores them. This plan is the game's half: sending the report
from the game, to the same page. It was written in the website's session, so that an agent here can pick it up.

## What the user asked (2026-10-16)

- Testers post their bug reports on the user's website, which can only run PHP: a message, plus the report
  folder. Before anything is posted, it is checked that the folder holds only the files a report has and stays
  under a size limit, and the tester is told plainly if not. The message is stored with the files, and the
  whole report is zipped. **Built, on the website** (below).
- The game sending the report itself, as a second way in: this plan. "I will look into option 3."

## Decided (the user, 2026-10-16)

1. **The web page is the first way in, and stays.** A tester can always drag the folder onto it.
2. **`settings.json` is optional** in a report. The game copies it only if there is one.
3. **The host was moved from PHP 5.5 to PHP 8.5**, with 16 MB upload limits, so a report fits whole,
   screenshot and all.

## What there is today

### In the game

- **Report a bug** (pause menu, F12): `StartReport` and `WriteReport` in `ApplicationChasmRecord.cpp` write the
  folder on a worker thread. Then `ServiceRecording` opens the `CHASM_ASK_REPORT` pop-up
  (`ApplicationChasmTitle.cpp`): "Bug report written", the folder's path, and **Open the folder** (which
  starts `explorer.exe`) or OK.
- `report.txt` ends with "Say what happened, and what you expected instead, in the message you send this folder
  with." It does not say where to send it.
- **No HTTP client.** Core's networking (`Socket`, `TCPClient`, `HTTPServer`, under `USE_NET`) is plain sockets
  for the MCP server and apps/ocpp. **`make ship` builds with `USE_NET=0`**, so the tester's exe has none of it.
- **No typing.** `InputController` receives `WM_CHAR` and drops it (the empty `WM_CHAR` branch in
  `InputController.cpp`). Nothing in the UI takes text.
- Full screen is a borderless `WS_POPUP` window (`Window::SetFullscreen`), not a change of display mode, so
  another window can show over it.

### The website

- **`https://nattesok.nl/bugreport.php`.** Its source is in the website's own repo,
  `C:\IDE-E\Mijn Documenten\Website\Nattesok\bugreport.php`, not in this checkout. It is one PHP file: GET shows
  the page, POST takes a report and answers in JSON.
- **The page:**
  - The tester picks or drops the report folder.
  - Each file is checked in the browser - its name, its size, a PNG or minidump signature, text without zero
    bytes - and listed green or red.
  - There is a message (10 characters at least) and an optional name or e-mail.
  - Send stays disabled until all is well. The server then checks everything again.
- **Stored** as one zip per report, `<arrival time>_<random>.zip`.
  - The zip holds the files and `message.txt`: the message, the name, the original folder's name, the file
    list, and the sender's User-Agent.
  - Reports go in `domains/nattesok.nl/bugreports/`, beside `public_html`. That is outside the web root; the
    user fetches them over FTP.
- **Also:**
  - at most 10 reports an hour from one address;
  - nothing more is stored once 2000 MB is;
  - an optional mail per report, off for now.
- **The host:** PHP 8.5 on LiteSpeed. 16 MB a file and 16 MB a post, which leaves 15.9 MB for a report after
  64 KB kept for the form. 20 files a post.

## The endpoint

The game sends exactly what the page sends:

`POST https://nattesok.nl/bugreport.php`, as `multipart/form-data`.

| field | what |
|---|---|
| `message` | what happened, as the tester wrote it: UTF-8, 10 to 10,000 characters after trimming |
| `contact` | optional: a name or an e-mail, one line, cut at 200 characters |
| `folder` | the report folder's name, like `20261016_194321`: `[A-Za-z0-9_.-]`, up to 64 characters; anything else is stored as "(loose files)" |
| `leftout` | optional: the names of files left out, written into `message.txt` |
| `files[]` | one part per file, with the bare name as its filename (`filename="chasm.log"`), no path |
| `website` | **never sent.** It is a trap for bots: a post with anything in it is answered "ok" and thrown away |

Set the **User-Agent** to the game and its build, for example `Chasm (release build, Oct 16 2026)` from
`BuildName()`. It lands in `message.txt` as "Browser:", which is how a report sent by the game is told apart
from one sent through the page.

The files a report may hold are listed in `rules()`, at the top of `bugreport.php`:

| file | in a report | content |
|---|---|---|
| `report.txt` | required | text |
| `chasm.log` | required | text |
| `now.json` | required | text |
| `settings.json` | optional | text |
| `chasm_previous.log` | optional | text |
| `screenshot.png` | optional | PNG signature |
| `chasm_auto_<date>_<time>.rec` or `chasm_<date>_<time>.rec` | at most 1 | text |
| `crash_<date>_<time>.txt` | up to 10 | text |
| `crash_<date>_<time>.dmp` | up to 10 | minidump (`MDMP`) |

"Text" means no zero bytes. **Any other file refuses the whole report.** So a new file in `WriteReport` needs
its line in the website's `rules()` first, or every report from that build is turned away - by the page and
by this upload alike.

The answer is JSON:

- **`200 {"ok":true,"id":"20261016_204128_9e1218"}`**: stored. The id names the zip; show it as the reference.
- **`422 {"ok":false,"problems":["...", "..."]}`**: a check failed. Each problem is a sentence fit to show the
  tester.
- **The other refusals** carry `problems` the same way:
  - `413`: the report is too large;
  - `429`: more than 10 in the last hour from this address;
  - `507`: the store is full;
  - `500`: the server could not store it;
  - `400`: nothing arrived.

Two quirks:

- **HTTPS only.** The site redirects `http://` to `https://`, and a redirected POST arrives as a GET, which
  returns the page instead of an answer. Use the https address, with redirects off.
- **A post over the host's limit** can get a PHP warning printed before the JSON, and the status is then 200.
  Read the answer from its first `{"ok":` onwards, as the page does, and go by `ok` rather than the status.

## In the game

### On screen

1. **The pop-up.** Report a bug writes the folder as it does now. The pop-up gains **Send it**, beside Open
   the folder and OK.
2. **The message screen.** Send it opens a screen with:
   - the question "What were you doing, what went wrong, and what did you expect instead?" and a text box;
   - an optional name or e-mail, remembered in `settings.json` for next time;
   - the files that will go, with their sizes;
   - a line saying what is sent: the logs hold folder paths from the tester's PC, which can include their
     Windows user name;
   - Send and Cancel. Send waits for 10 characters, as on the page.
3. **Sending.** A bar and a percentage, with Cancel. The game stays paused underneath, as under every pop-up.
4. **The outcome.**
   - **Sent:** "Report sent - thank you", and the reference.
   - **Not sent:** the problems as the server gave them, or "no connection". **Open the web page** opens the
     browser at the page and the folder in Explorer, so the tester can drag it in. OK closes.
   - The folder stays either way (see "Open questions").

### Underneath

- **WinHTTP** (`winhttp.h`, linked with `-lwinhttp`).
  - It is part of Windows and does HTTPS itself. It uses none of core's sockets, so it works in the ship build
    with `USE_NET=0`.
  - Synchronous calls on a worker thread are enough:
    - `WinHttpOpen`, with the User-Agent;
    - `WinHttpConnect`, port 443;
    - `WinHttpOpenRequest`, with `WINHTTP_FLAG_SECURE`;
    - `WinHttpSendRequest`, with the total length;
    - `WinHttpWriteData`, in chunks of 64 KB - each chunk moves the bar;
    - `WinHttpReceiveResponse`, `WinHttpQueryHeaders` for the status, and `WinHttpReadData` for the body.
  - Redirects off: `WINHTTP_OPTION_DISABLE_FEATURE` with `WINHTTP_DISABLE_REDIRECTS`.
  - Timeouts set with `WinHttpSetTimeouts`.
  - Cancel is a flag checked between chunks. The wait for the answer is bounded by the timeouts.
  - The library goes on the link line from the app's makefile, the way `engine.mk` adds `-lws2_32` under
    `USE_NET`: libraries ride on `CFLAGS` here.
- **The body** is written while it is sent, not built in memory first.
  - Its order: the text parts; then for each file, its part header, its bytes read in chunks, and a CRLF; then
    the closing boundary.
  - Its length is known before the first byte (the header lines plus the file sizes), so `Content-Length` is
    exact and the bar can count.
  - The boundary is a long run of random hex.
- **Threads.** The same pattern as `WriteReport`:
  - a detached `std::thread`;
  - its state in atomics and under `menu_text_mutex`;
  - the outcome picked up by `ServiceRecording` on the physics thread, which opens the next pop-up.

  Quitting while a report is being sent cancels it and goes. The folder is still there.
- **Where the code goes.** In the app: `ApplicationChasmReportSend.cpp`, or a small `ReportUpload.cpp` that
  knows nothing of the app. An HTTP client is a new engine feature, so by the house rules it is flagged rather
  than put in core quietly. The app-side code stands in until it is decided whether core wants one; Android
  would need its own.
- **The address** is one `#define` in the app. There should be a way to point it elsewhere for testing - a
  `--report-url` argument, or a field in `settings.json` - so that a test run does not fill the real box.

### The message screen needs typing

Nothing in the game takes text today. There are three ways to get the message; the user chooses (see "Open
questions").

- **A. A text field in the game's own UI.**
  - What it takes: `WM_CHAR` passed through to the app, and a field drawn in the overlay with a caret,
    Backspace, Enter for a new line, wrapping, and Ctrl+V from the clipboard.
  - For: it looks like the game, and serves other things later, such as naming a save or entering a seed.
  - Against: it is the larger job, and a core change to flag.
- **B. A Windows text box.**
  - What it takes: a small modal window with a multi-line `EDIT` control, made by the app. Windows does the
    typing, pasting, selection and IME, and the box shows over the borderless full screen.
  - For: quick and robust.
  - Against: it looks like Windows rather than the game.
- **C. No typing in the game.**
  - What it takes: the game uploads the files without a message, then opens the browser at the page to write
    it. The website would need a second step: adding a message to a stored report, with a secret handed back
    by the upload.
  - Against: the tester ends up in the browser anyway.

**Recommended:** A, if a text field is wanted for other things soon. Otherwise B now, and A later. C only if
the game is to have no message screen at all.

## Steps, in order

1. **The web page from the pop-up.** Small, needs no core change, and is useful at once.
   - "Open the web page" on the report pop-up opens `https://nattesok.nl/bugreport.php` in the browser and the
     folder beside it. `explorer.exe "<url>"` opens the default browser, the way Open the folder already
     starts Explorer.
   - `report.txt`'s last line names the page.
   - A note in `WriteReport` that a new file needs the website's `rules()` first.
   - This plan added to the table in `docs/README.md`.
2. **Typing**, by the user's choice of A or B.
3. **The message screen**, without sending yet: the text, the name kept in `settings.json`, the list of files,
   and what is sent.
4. **Sending:** the WinHTTP upload on a worker thread, the bar, Cancel, and the answers shown, with Open the
   web page as the way out when it fails.
5. **The ship build:** `-lwinhttp` linked and the real address in place, checked from the ship exe, which has
   `USE_NET=0`.

Steps 1, 3 and 4 change nothing the rules do, so replays and save and load are untouched.

## Checking it

- **A local copy of the endpoint**, so tests do not land in the real box.
  - Get PHP for Windows: the "NTS x64" zip from windows.php.net, unpacked anywhere.
  - From that folder, run:

    ```
    php.exe -n -d extension_dir=ext -d extension=zip -d extension=mbstring -d upload_max_filesize=16M -d post_max_size=16M -S 127.0.0.1:8099 -t <a folder holding a copy of bugreport.php>
    ```

  - Reports land in `bugreports/`, beside that folder: the script stores them one level up, as on the host.
  - The local server is plain http, so the test address is `http://127.0.0.1:8099/bugreport.php`, without
    `WINHTTP_FLAG_SECURE`.
- **What to check:**
  - A report sent, then its zip (`unzip -l`): the files, and `message.txt` with the message, the name and the
    game's User-Agent.
  - A stray file put in the folder: the 422's problems shown on screen.
  - The server stopped: "no connection", and Open the web page.
  - A 15 MB file: the bar moving.
  - Cancel, halfway.
- **On the real address:** one report sent from the ship exe, then deleted from `bugreports/` over FTP.

## Open questions

1. **Typing: A, B or C** (above).
2. **The folder after sending:** kept, as now, or deleted once the server has said ok? Keeping it costs a few
   MB a report.
3. **The tester's user name in paths.** The logs and `report.txt` hold paths under `C:\Users\<name>\...`. Should
   the copies `WriteReport` makes replace the profile folder with `%USERPROFILE%`? Or should the message screen
   just say so and leave the paths, as the web page does today?
4. **Asking after a crash.** On the next start after a crash: "The game crashed last time. Send a report?"
   Wanted now, or later?
