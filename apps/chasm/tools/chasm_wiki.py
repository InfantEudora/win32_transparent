"""
The chasm wiki: what is in the game, how it works and where it is going - a page per thing the player
meets, built into one HTML file to click through.

    python apps/chasm/tools/chasm_wiki.py            build apps/chasm/build/wiki.html, checking every page
    python apps/chasm/tools/chasm_wiki.py --check    check only, write nothing
    python apps/chasm/tools/chasm_wiki.py --unused   also list the tuning constants no page mentions

The pages are apps/chasm/wiki/*.md; README.md there says how to write one. This script is what keeps
them true. A figure on a page is a constant's NAME, filled in from the code here, so retuning never
leaves a page wrong. And every pointer a page makes - a constant, a function in a code file, a plan's
heading - is checked, so a rename fails the build and names the page instead of leaving it quietly stale.

Exit code 1 if any page has an error. The HTML is written all the same, so it can still be looked at.
Standard library only: the Windows Python 3.9 runs it (not MSYS's - see CLAUDE.md).
"""
import argparse
import ast
import datetime
import html
import os
import re
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
APP = os.path.dirname(TOOLS)
WIKI = os.path.join(APP, "wiki")
DOCS = os.path.join(APP, "docs")
OUT = os.path.join(APP, "build", "wiki.html")

#What a page - or a line under its "Planned" - can be, in the front page's order.
STATUSES = [
    ("built", "In the game, working as the page says"),
    ("partial", "Some of it in the game; the page says what is missing"),
    ("planned", "Agreed with the user, not built yet"),
    ("proposed", "Written up for the user to decide; not agreed"),
    ("idea", "Mentioned for later, not worked out"),
    ("parked", "Put aside; may come back"),
    ("scrapped", "Dropped - kept so nobody proposes it again"),
]
STATUS_NAMES = [s for s, _ in STATUSES]
#The sidebar's groups, in its order.
KINDS = [
    ("people", "People and jobs"),
    ("buildings", "Buildings"),
    ("needs", "Needs"),
    ("goods", "Goods"),
    ("world", "The world"),
    ("systems", "Systems"),
]
KIND_NAMES = [k for k, _ in KINDS]
KEYS = ("title", "aliases", "kind", "status", "summary", "plans", "code", "tuning", "measured", "verified")
REQUIRED = ("title", "kind", "status", "summary", "verified")
#Non-blank lines a page's body may have. Past this it is a plan, or two pages.
MAX_LINES = 45
#{NAME:unit} shows a count of ticks in these, by the constant that is one of them.
UNITS = {"days": "CALENDAR_DAY_TICKS", "seasons": "CALENDAR_SEASON_TICKS",
         "years": "CALENDAR_YEAR_TICKS", "seconds": "CALENDAR_TICKS_PER_SECOND"}
#What --unused reports on: the gameplay tuning a page ought to mention.
TUNING_PREFIXES = ("ECONOMY_", "NEEDS_", "LIFE_")

DEFINE_RE = re.compile(r"^\s*#\s*define\s+([A-Z][A-Z0-9_]*)\s+(.*?)\s*$")
CAST_RE = re.compile(r"\(\s*(?:u?int(?:8|16|32|64)_t|unsigned|int|float|double|size_t)\s*\)")
LITERAL_RE = re.compile(r"\b(0[xX][0-9a-fA-F]+|\d+\.\d*(?:[eE][-+]?\d+)?|\d+)[fFuUlL]*")
LIST_RE = re.compile(r"^(\s*)([-*]|\d+[.)])\s+(.*)$")
TOKEN_RE = re.compile(r"\[\[([^\]|]+)(?:\|([^\]]+))?\]\]"           #[[Page]] or [[Page|label]]
                      r"|\[([^\]]+)\]\(([^)\s]+)\)"                  #[label](path)
                      r"|\{([A-Z][A-Z0-9_]+|[a-z_][a-z0-9_]*(?:\[[A-Za-z0-9_]+\])+)(?::(#?[a-z]+))?\}"
                                                                    #{CONSTANT}, {table[i]}, then :unit or :#noun
                      r"|\{=\s*([^{}:]+?)\s*(?::(#?[a-z]+))?\}")    #{= expression of those and measured figures}
TABLE_TOKEN_RE = re.compile(r"[a-z_][a-z0-9_]*(?:\[[A-Za-z0-9_]+\])+")
ARRAY_RE = re.compile(r"^[ \t]*(?:static[ \t]+)?const[ \t]+(?:unsigned[ \t]+)?\w+[ \t]+([a-z_][a-z0-9_]*)[ \t]*"
                      r"((?:\[[^\]\n]*\])+)[ \t]*=[ \t]*(\{.*?\});", re.M | re.S)
BOLD_RE = re.compile(r"\*\*(.+?)\*\*")
ITALIC_RE = re.compile(r"(?<![\w*])\*(?![\s*])(.+?)(?<![\s*])\*(?![\w*])")
BADGE_RE = re.compile(r"^\[([a-z]+)\]\s+(.*)$")


class Define:
    """A #define from the app's sources: its text, its comment, where it is, and its value once worked out."""
    def __init__(self, name, expr, comment, file, line):
        self.name = name
        self.expr = expr
        self.comment = comment
        self.file = file
        self.line = line
        self.f_done = False
        self.value = None


class Page:
    def __init__(self, slug, path):
        self.slug = slug
        self.path = path
        self.meta = {}
        self.meta_line = {}
        self.body = []              #(line number, text)
        self.used = []              #constants, in the order first used
        self.units = {}             #the unit a constant was shown in
        self.measured = {}          #`measured:` - figures taken off the map, not the code, for {= expressions}
        self.html = ""
        self.summary_html = ""
        self.text = ""              #plain, for the search

    def title(self):
        return self.meta.get("title", self.slug)


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def split_list(text, sep=";"):
    return [p.strip() for p in text.split(sep) if p.strip()]


def number(v):
    if isinstance(v, float):
        if v == int(v) and abs(v) < 1e15:
            return str(int(v))
        return ("%.3f" % v).rstrip("0").rstrip(".")
    return str(v)


def about(v):
    """An estimate, shown as one: whole from 10 up, a decimal below that, two below 1."""
    if abs(v) >= 10:
        return str(int(round(v)))
    return ("%.1f" % v if abs(v) >= 1 else "%.2f" % v).rstrip("0").rstrip(".")


def plain(fragment):
    return html.unescape(re.sub(r"<[^>]+>", " ", fragment))


class Wiki:
    def __init__(self, out):
        self.out_dir = os.path.dirname(os.path.abspath(out))
        self.defs = {}
        self.arrays = {}            #name -> (values, file, line): the constant tables
        self.extra = {}             #names a {= expression} may use beside the defines, while it is worked out
        self.pages = []
        self.by_slug = {}
        self.names = {}             #lower-case title or alias -> slug
        self.errors = []
        self.wanted = {}            #lower-case target -> [as first written, set of slugs linking to it]
        self.backlinks = {}         #slug -> set of slugs
        self.roadmap = []           #(status, page, html) from status-tagged bullets
        self.home = None

    #--- Reading ---------------------------------------------------------------------------------------

    def error(self, page, line, message):
        where = os.path.relpath(page.path, os.path.dirname(os.path.dirname(APP))).replace(os.sep, "/") if page else "wiki"
        self.errors.append("%s:%d: %s" % (where, line, message))

    def read_defines(self):
        for fn in sorted(os.listdir(APP)):
            if not fn.endswith((".h", ".cpp")):
                continue
            for no, line in enumerate(read(os.path.join(APP, fn)).split("\n"), 1):
                m = DEFINE_RE.match(line)
                if not m or m.group(1) in self.defs:
                    continue
                body, comment = m.group(2), ""
                c = body.find("//")
                if c >= 0:
                    body, comment = body[:c], body[c + 2:].strip()
                body = re.sub(r"/\*.*?\*/", "", body).strip()
                if body:
                    self.defs[m.group(1)] = Define(m.group(1), body, comment, fn, no)
            #Constant tables too (`const int crop_days[N] = {...};`), for {crop_days[ZONE_CROP_BEANS]}.
            text = read(os.path.join(APP, fn))
            for m in ARRAY_RE.finditer(text):
                if m.group(1) in self.arrays:
                    continue
                body = re.sub(r"//[^\n]*|/\*.*?\*/", "", m.group(3), flags=re.S)
                body = LITERAL_RE.sub(lambda x: x.group(1), body.replace("{", "[").replace("}", "]"))
                try:
                    values = ast.literal_eval(body)
                except (SyntaxError, ValueError):
                    continue
                self.arrays[m.group(1)] = (values, fn, text.count("\n", 0, m.start(1)) + 1)

    def element(self, page, no, token):
        """
        An element of a constant table, `name[i]` or `name[i][j]`, each index a number or a #define: a Define of
        its own, so it shows and goes in the tuning table like one. None, with the error made, if there is none.
        """
        if token in self.defs:
            return self.defs[token]
        name = token[:token.index("[")]
        if name not in self.arrays:
            self.error(page, no, "no table %s in apps/chasm - renamed or gone? Fix the page" % name)
            return None
        v, fn, line = self.arrays[name]
        for i in re.findall(r"\[([A-Za-z0-9_]+)\]", token):
            k = int(i) if i.isdigit() else self.value(self.defs.get(i))
            if not isinstance(k, int) or not isinstance(v, list) or not 0 <= k < len(v):
                self.error(page, no, "%s: index %s is not in the table" % (token, i))
                return None
            v = v[k]
        if isinstance(v, list):
            self.error(page, no, "%s is a row of the table, not one number - index it further" % token)
            return None
        d = Define(token, number(v), "from the table %s" % name, fn, line)
        d.f_done = True
        d.value = v
        self.defs[token] = d
        return d

    def value(self, d, seen=()):
        """A define's value, worked out as C would - integer division truncates - or None if it is not a number."""
        if d is None or d.name in seen:
            return None
        if d.f_done:
            return d.value
        try:
            expr = LITERAL_RE.sub(lambda m: m.group(1), CAST_RE.sub("", d.expr))
            d.value = self.walk(ast.parse(expr, mode="eval").body, seen + (d.name,))
        except (SyntaxError, ValueError, TypeError, ZeroDivisionError):
            d.value = None
        d.f_done = True
        return d.value

    def walk(self, n, seen):
        if isinstance(n, ast.Constant) and isinstance(n.value, (int, float)) and not isinstance(n.value, bool):
            return n.value
        if isinstance(n, ast.Name):
            if n.id in self.extra:
                return self.extra[n.id]     #a page's measured figure, or a table's element, in a {= expression}
            v = self.value(self.defs.get(n.id), seen)
            if v is None:
                raise ValueError(n.id)
            return v
        if isinstance(n, ast.UnaryOp):
            v = self.walk(n.operand, seen)
            if isinstance(n.op, ast.USub):
                return -v
            if isinstance(n.op, ast.UAdd):
                return v
            if isinstance(n.op, ast.Invert):
                return ~v
        if isinstance(n, ast.BinOp):
            a, b = self.walk(n.left, seen), self.walk(n.right, seen)
            if isinstance(n.op, ast.Div):
                if isinstance(a, int) and isinstance(b, int):
                    q = abs(a) // abs(b)
                    return q if (a >= 0) == (b >= 0) else -q
                return a / b
            ops = {ast.Add: lambda: a + b, ast.Sub: lambda: a - b, ast.Mult: lambda: a * b,
                   ast.Mod: lambda: a % b, ast.LShift: lambda: a << b, ast.RShift: lambda: a >> b,
                   ast.BitOr: lambda: a | b, ast.BitAnd: lambda: a & b, ast.BitXor: lambda: a ^ b}
            if type(n.op) in ops:
                return ops[type(n.op)]()
        raise ValueError("not a number")

    def load(self):
        self.read_defines()
        for fn in sorted(os.listdir(WIKI)):
            if not fn.endswith(".md") or fn in ("README.md", "index.md") or fn.startswith("_"):
                continue
            page = self.load_page(os.path.join(WIKI, fn), fn[:-3])
            if page:
                self.pages.append(page)
                self.by_slug[page.slug] = page
        self.pages.sort(key=lambda p: p.title().lower())
        for p in self.pages:
            names = [p.title(), p.slug.replace("_", " ")] + split_list(p.meta.get("aliases", ""), ",")
            for name in names:
                key = name.lower()
                if key in self.names and self.names[key] != p.slug:
                    self.error(p, p.meta_line.get("aliases", 1), "'%s' already names %s.md" % (name, self.names[key]))
                self.names[key] = p.slug
        index = os.path.join(WIKI, "index.md")
        self.home = Page("home", index)
        lines = read(index).split("\n") if os.path.isfile(index) else ["# Chasm wiki"]
        first = 0
        if lines and lines[0].startswith("# "):
            self.home.meta["title"] = lines[0][2:].strip()
            first = 1
        self.home.body = [(i + 1, lines[i]) for i in range(first, len(lines))]

    def load_page(self, path, slug):
        page = Page(slug, path)
        lines = read(path).split("\n")
        if not lines or lines[0].strip() != "---":
            self.error(page, 1, "no front matter: a page starts with a --- block (wiki/README.md)")
            return None
        end = None
        for i in range(1, len(lines)):
            if lines[i].strip() == "---":
                end = i
                break
            m = re.match(r"^([a-z]+):\s*(.*)$", lines[i])
            if not m:
                if lines[i].strip():
                    self.error(page, i + 1, "front matter line is not 'key: value'")
                continue
            if m.group(1) not in KEYS:
                self.error(page, i + 1, "unknown key '%s' (keys: %s)" % (m.group(1), ", ".join(KEYS)))
            page.meta[m.group(1)] = m.group(2).strip()
            page.meta_line[m.group(1)] = i + 1
        if end is None:
            self.error(page, 1, "front matter never closed with ---")
            return None
        for key in REQUIRED:
            if not page.meta.get(key):
                self.error(page, 1, "'%s:' is required" % key)
        if page.meta.get("kind") and page.meta["kind"] not in KIND_NAMES:
            self.error(page, page.meta_line["kind"], "kind '%s' is not one of %s" % (page.meta["kind"], ", ".join(KIND_NAMES)))
        if page.meta.get("status") and page.meta["status"] not in STATUS_NAMES:
            self.error(page, page.meta_line["status"], "status '%s' is not one of %s" % (page.meta["status"], ", ".join(STATUS_NAMES)))
        if page.meta.get("verified"):
            try:
                datetime.date.fromisoformat(page.meta["verified"])
            except ValueError:
                self.error(page, page.meta_line["verified"], "verified: wants a date, YYYY-MM-DD")
        page.body = [(i + 1, lines[i]) for i in range(end + 1, len(lines))]
        count = sum(1 for _, t in page.body if t.strip())
        if count > MAX_LINES:
            self.error(page, end + 2, "%d lines; a page holds %d - split it, or move the detail into its plan" % (count, MAX_LINES))
        return page

    #--- Inline text ---------------------------------------------------------------------------------

    def resolve(self, target):
        t = target.strip().lower()
        if t in self.names:
            return self.names[t]
        for cut in ("s", "es"):
            if t.endswith(cut) and t[:-len(cut)] in self.names:
                return self.names[t[:-len(cut)]]
        return None

    def label(self, text):
        text = html.escape(text, quote=False)
        return ITALIC_RE.sub(r"<em>\1</em>", BOLD_RE.sub(r"<strong>\1</strong>", text))

    def href(self, path):
        return html.escape(os.path.relpath(path, self.out_dir).replace(os.sep, "/"))

    def inline(self, page, no, text):
        held = []

        def hold(s):
            held.append(s)
            return "\x00%d\x00" % (len(held) - 1)
        text = re.sub(r"`([^`]+)`", lambda m: hold("<code>%s</code>" % html.escape(m.group(1))), text)
        text = TOKEN_RE.sub(lambda m: hold(self.token(page, no, m)), text)
        text = self.label(text)
        return re.sub("\x00(\\d+)\x00", lambda m: held[int(m.group(1))], text)

    def token(self, page, no, m):
        if m.group(1) is not None:
            return self.wikilink(page, m.group(1).strip(), m.group(2) if m.group(2) is not None else m.group(1))
        if m.group(3) is not None:
            return self.link(page, no, m.group(3), m.group(4))
        if m.group(7) is not None:
            return self.expression(page, no, m.group(7), m.group(8))
        return self.constant(page, no, m.group(5), m.group(6))

    def expression(self, page, no, expr, unit):
        """
        {= expression}: arithmetic on constants, table elements and the page's `measured:` figures - what an
        average plot holds, say - worked out at every build like a single constant, and shown rounded, since
        it is an estimate. Its constants go in the page's tuning table.
        """
        names = dict(page.measured)
        def table(m):
            d = self.element(page, no, m.group(0))
            if d is None:
                return "0"
            if m.group(0) not in page.used:
                page.used.append(m.group(0))
            key = "_t%d" % len(names)
            names[key] = d.value
            return key
        text = TABLE_TOKEN_RE.sub(table, expr)
        try:
            tree = ast.parse(text, mode="eval")
        except SyntaxError:
            self.error(page, no, "{= %s} is not an expression" % expr)
            return '<span class="tune bad">%s</span>' % html.escape(expr)
        for n in ast.walk(tree):
            if isinstance(n, ast.Name) and n.id not in names:
                if n.id not in self.defs:
                    self.error(page, no, "{= %s}: no constant or measured figure %s" % (expr, n.id))
                    return '<span class="tune bad">%s</span>' % html.escape(expr)
                if n.id not in page.used:
                    page.used.append(n.id)
        self.extra = names
        try:
            v = self.walk(tree.body, ())
        except (ValueError, TypeError, ZeroDivisionError):
            v = None
        finally:
            self.extra = {}
        if v is None:
            self.error(page, no, "{= %s} does not work out to a number" % expr)
            return '<span class="tune bad">%s</span>' % html.escape(expr)
        if unit and not unit.startswith("#"):
            per = self.value(self.defs.get(UNITS.get(unit, "")))
            if not per:
                self.error(page, no, "unknown unit '%s'" % unit)
                return '<span class="tune bad">%s</span>' % html.escape(expr)
            v = v / per
        s = about(v)
        if unit:
            word = unit.lstrip("#")
            s += " " + (word[:-1] if s == "1" and word.endswith("s") else word)
        used = ", ".join("%s = %s" % (k, number(names[k])) for k in sorted(page.measured) if k in expr)
        tip = "= %s%s" % (expr, ("\nmeasured: " + used) if used else "")
        return '<span class="tune" title="%s">%s</span>' % (html.escape(tip), html.escape(s))

    def wikilink(self, page, target, label):
        slug = self.resolve(target)
        if slug is None:
            entry = self.wanted.setdefault(target.lower(), [target, set()])
            entry[1].add(page.slug)
            return '<a class="wl missing" title="No page yet: %s">%s</a>' % (html.escape(target), self.label(label))
        if slug != page.slug and page.slug != "home":
            self.backlinks.setdefault(slug, set()).add(page.slug)
        return '<a class="wl" href="#%s">%s</a>' % (slug, self.label(label))

    def link(self, page, no, text, url):
        if re.match(r"^(https?:|#|mailto:)", url):
            return '<a href="%s">%s</a>' % (html.escape(url), self.label(text))
        path, _, anchor = url.partition("#")
        full = os.path.normpath(os.path.join(WIKI, path))
        if not os.path.exists(full):
            self.error(page, no, "link to a file that is not there: %s" % url)
        return '<a href="%s%s">%s</a>' % (self.href(full), ("#" + html.escape(anchor)) if anchor else "", self.label(text))

    def shown(self, d, unit):
        """
        What a page shows for a constant: its value; in `unit` if given (ticks converted); or, for a `#noun`, the
        value as it is with the noun after it, singular for 1 - so retuning to 1 never reads "1 days".
        None if that cannot be worked out.
        """
        v = self.value(d)
        if not unit:
            return number(v) if v is not None else d.expr
        if unit.startswith("#"):
            if v is None:
                return None
            s, noun = number(v), unit[1:]
            return "%s %s" % (s, noun[:-1] if s == "1" and noun.endswith("s") else noun)
        per = self.value(self.defs.get(UNITS.get(unit, "")))
        if v is None or not per:
            return None
        s = ("%.2f" % (v / per)).rstrip("0").rstrip(".")
        return "%s %s" % (s, unit[:-1] if s == "1" else unit)

    def constant(self, page, no, name, unit):
        d = self.element(page, no, name) if "[" in name else self.defs.get(name)
        if d is None:
            if "[" not in name:
                self.error(page, no, "no constant %s in apps/chasm - renamed or gone? Fix the page" % name)
            return '<span class="tune bad">%s</span>' % name
        if unit and not unit.startswith("#") and unit not in UNITS:
            self.error(page, no, "unknown unit '%s' (units: %s; or #noun for a count)" % (unit, ", ".join(sorted(UNITS))))
            unit = None
        if name not in page.used:
            page.used.append(name)
        shown = self.shown(d, unit)
        if shown is None:
            self.error(page, no, "%s = %s is not a number to show in %s" % (name, d.expr, unit))
            shown = d.expr
        if unit and not unit.startswith("#"):
            page.units[name] = unit     #the tuning table shows ticks in it too
        tip ="%s = %s   (%s:%d)%s" % (name, d.expr, d.file, d.line, ("\n" + d.comment) if d.comment else "")
        return '<span class="tune" title="%s">%s</span>' % (html.escape(tip), html.escape(shown))

    #--- Blocks --------------------------------------------------------------------------------------

    def blocks(self, page, lines):
        out = []
        i, n = 0, len(lines)
        while i < n:
            no, line = lines[i]
            s = line.strip()
            if not s:
                i += 1
                continue
            if s.startswith("```"):
                j = i + 1
                code = []
                while j < n and not lines[j][1].strip().startswith("```"):
                    code.append(lines[j][1])
                    j += 1
                out.append("<pre><code>%s</code></pre>" % html.escape("\n".join(code)))
                i = j + 1
                continue
            m = re.match(r"^(#{1,6})\s+(.*)$", s)
            if m:
                if len(m.group(1)) == 1:
                    self.error(page, no, "a page's title is its 'title:', not a # heading - use ##")
                tag = "h2" if len(m.group(1)) <= 2 else "h3"
                out.append("<%s>%s</%s>" % (tag, self.inline(page, no, m.group(2)), tag))
                i += 1
                continue
            if s in ("---", "***"):
                out.append("<hr>")
                i += 1
                continue
            if s.startswith("|"):
                rows = []
                while i < n and lines[i][1].strip().startswith("|"):
                    rows.append(lines[i])
                    i += 1
                out.append(self.table(page, rows))
                continue
            if s.startswith(">"):
                quote = []
                while i < n and lines[i][1].strip().startswith(">"):
                    quote.append(lines[i][1].strip()[1:].strip())
                    i += 1
                out.append("<blockquote><p>%s</p></blockquote>" % self.inline(page, no, " ".join(quote)))
                continue
            if LIST_RE.match(line):
                items = []
                while i < n:
                    no2, l2 = lines[i]
                    m2 = LIST_RE.match(l2)
                    if m2:
                        tag = "ol" if m2.group(2)[0].isdigit() else "ul"
                        items.append([len(m2.group(1).expandtabs(4)), tag, m2.group(3), no2])
                    elif l2.strip() and l2[:1] in " \t" and items:
                        items[-1][2] += " " + l2.strip()     #a continuation line
                    else:
                        break
                    i += 1
                out.append(self.list(page, items))
                continue
            para = []
            while i < n:
                l2 = lines[i][1]
                st = l2.strip()
                if not st or st.startswith(("```", "#", "|", ">")) or LIST_RE.match(l2) or st in ("---", "***"):
                    break
                para.append(st)
                i += 1
            out.append("<p>%s</p>" % self.inline(page, no, " ".join(para)))
        return "\n".join(out)

    def list(self, page, items):
        out = []
        stack = []
        for indent, tag, text, no in items:
            while stack and indent < stack[-1][0]:
                out.append("</li></%s>" % stack.pop()[1])
            if stack and indent == stack[-1][0]:
                out.append("</li>")
            else:
                out.append("<%s>" % tag)
                stack.append((indent, tag))
            #A bullet that starts with a status is a piece of the road ahead: badged, and on the front page.
            m = BADGE_RE.match(text)
            if m and m.group(1) in STATUS_NAMES:
                body = self.inline(page, no, m.group(2))
                self.roadmap.append((m.group(1), page, body))
                out.append('<li><span class="badge s-%s">%s</span> %s' % (m.group(1), m.group(1), body))
            else:
                if m:
                    self.error(page, no, "[%s] is not a status (%s)" % (m.group(1), ", ".join(STATUS_NAMES)))
                out.append("<li>" + self.inline(page, no, text))
        while stack:
            out.append("</li></%s>" % stack.pop()[1])
        return "".join(out)

    def table(self, page, rows):
        def cells(text):
            text = re.sub(r"\[\[[^\]]*\]\]", lambda m: m.group(0).replace("|", "\x01"), text.strip())
            text = text[1:] if text.startswith("|") else text
            text = text[:-1] if text.endswith("|") else text
            return [c.strip().replace("\x01", "|") for c in text.split("|")]
        head = cells(rows[0][1])
        body = rows[1:]
        if body and re.match(r"^\|?\s*:?-{2,}", body[0][1].strip()):
            body = body[1:]
        out = ["<table><thead><tr>"]
        out += ["<th>%s</th>" % self.inline(page, rows[0][0], c) for c in head]
        out.append("</tr></thead><tbody>")
        for no, line in body:
            out.append("<tr>" + "".join("<td>%s</td>" % self.inline(page, no, c) for c in cells(line)) + "</tr>")
        out.append("</tbody></table>")
        return "".join(out)

    #--- Pointers into the plans and the code ---------------------------------------------------------

    def plans(self, page):
        out = []
        no = page.meta_line.get("plans", 1)
        for part in split_list(page.meta.get("plans", "")):
            fn, _, heading = (s.strip() for s in part.partition(">"))
            path = os.path.join(DOCS, fn)
            if not os.path.isfile(path):
                self.error(page, no, "no plan docs/%s" % fn)
                out.append(html.escape(part))
                continue
            found = None
            if heading:
                for line in read(path).split("\n"):
                    m = re.match(r"^#{1,6}\s+(.*)$", line)
                    if m and m.group(1).strip().lower().startswith(heading.lower()):
                        found = m.group(1).strip()
                        break
                if not found:
                    self.error(page, no, "no heading starting '%s' in docs/%s - renamed? Fix the page" % (heading, fn))
            shown = (" &rsaquo; " + html.escape(found or heading)) if heading else ""
            out.append('<a href="%s">%s</a>%s' % (self.href(path), html.escape(fn), shown))
        return out

    def code(self, page):
        out = []
        no = page.meta_line.get("code", 1)
        for part in split_list(page.meta.get("code", "")):
            fn, _, syms = (s.strip() for s in part.partition(":"))
            path = os.path.join(APP, fn)
            if not os.path.isfile(path):
                self.error(page, no, "no code file apps/chasm/%s" % fn)
                out.append(html.escape(part))
                continue
            text = read(path)
            names = [s.strip(",") for s in syms.split() if s.strip(",")]
            for s in names:
                if not re.search(r"(?<![\w])%s(?![\w])" % re.escape(s), text):
                    self.error(page, no, "no '%s' in %s - renamed or moved? Fix the page" % (s, fn))
            out.append('<a href="%s">%s</a>%s' % (self.href(path), html.escape(fn),
                                                  "".join(" <code>%s</code>" % html.escape(s) for s in names)))
        return out

    #--- The pages ------------------------------------------------------------------------------------

    def render(self):
        for p in self.pages:
            for part in split_list(p.meta.get("measured", "")):
                m = re.match(r"^([A-Z][A-Z0-9_]*)\s*=\s*(-?\d+(?:\.\d+)?)$", part)
                if not m:
                    self.error(p, p.meta_line.get("measured", 1), "measured: wants NAME = number, ';' between: '%s'" % part)
                elif m.group(1) in self.defs:
                    self.error(p, p.meta_line.get("measured", 1), "measured: %s is a constant in the code already" % m.group(1))
                else:
                    p.measured[m.group(1)] = float(m.group(2))
            p.summary_html = self.inline(p, p.meta_line.get("summary", 1), p.meta.get("summary", ""))
            p.html = self.blocks(p, p.body)
            for name in split_list(p.meta.get("tuning", ""), ","):
                self.constant(p, p.meta_line.get("tuning", 1), name, None)
            p.plans_html = self.plans(p)
            p.code_html = self.code(p)
            p.text = " ".join([p.title(), p.meta.get("aliases", ""), plain(p.summary_html), plain(p.html)]).lower()
        self.home.html = self.blocks(self.home, self.home.body)

    def tuning_table(self, p):
        if not p.used:
            return ""
        rows = []
        for name in p.used:
            d = self.defs[name]
            v = self.value(d)
            now = number(v) if v is not None else "<code>%s</code>" % html.escape(d.expr)
            if name in p.units and v is not None:
                now = "%s <span class=\"muted\">(%s)</span>" % (html.escape(self.shown(d, p.units[name])), now)
            rows.append('<tr><td><code>%s</code></td><td class="num">%s</td><td>%s</td><td><a href="%s">%s:%d</a></td></tr>'
                        % (name, now, html.escape(d.comment), self.href(os.path.join(APP, d.file)), d.file, d.line))
        return ('<section class="tuning"><h2>Tuning on this page</h2>'
                '<table><thead><tr><th>Constant</th><th>Now</th><th>What it is</th><th>Where</th></tr></thead>'
                '<tbody>%s</tbody></table><p class="note">Read from the code when the wiki was built. '
                'To retune, change the constant - not the page.</p></section>' % "".join(rows))

    def article(self, p):
        status = p.meta.get("status", "")
        kind = dict(KINDS).get(p.meta.get("kind", ""), "")
        meta = []
        if p.plans_html:
            meta.append("<dt>Plans</dt><dd>%s</dd>" % "<br>".join(p.plans_html))
        if p.code_html:
            meta.append("<dt>Code</dt><dd>%s</dd>" % "<br>".join(p.code_html))
        if p.measured:
            meta.append("<dt>Measured</dt><dd>%s <span class=\"muted\">- taken off the map, not read from the code: measure "
                        "again when the grid changes</span></dd>" % ", ".join(
                            "<code>%s</code> %s" % (k, number(v)) for k, v in p.measured.items()))
        meta.append("<dt>Checked</dt><dd>against the code on %s</dd>" % html.escape(p.meta.get("verified", "?")))
        back = sorted(self.backlinks.get(p.slug, ()), key=lambda s: self.by_slug[s].title().lower())
        back_html = ""
        if back:
            back_html = '<section class="back"><h2>Linked from</h2><p>%s</p></section>' % " &middot; ".join(
                '<a class="wl" href="#%s">%s</a>' % (s, html.escape(self.by_slug[s].title())) for s in back)
        return ('<article class="page" id="p-%s" data-status="%s" hidden><header><div class="kind">%s</div>'
                '<h1>%s <span class="badge s-%s">%s</span></h1><p class="summary">%s</p><dl class="meta">%s</dl></header>'
                '<div class="body">%s</div>%s%s</article>'
                % (p.slug, status, html.escape(kind), html.escape(p.title()), status, status, p.summary_html,
                   "".join(meta), p.html, self.tuning_table(p), back_html))

    def front(self):
        out = ['<article class="page" id="p-home" hidden><header><h1>%s</h1></header><div class="body">%s'
               % (html.escape(self.home.title()), self.home.html)]
        counts = {s: sum(1 for p in self.pages if p.meta.get("status") == s) for s in STATUS_NAMES}
        out.append('<h2>At a glance</h2><p class="counts">%s</p>' % " &middot; ".join(
            '<span class="dot s-%s"></span>%d %s' % (s, counts[s], s) for s in STATUS_NAMES if counts[s]))
        for kind, name in KINDS:
            ps = [p for p in self.pages if p.meta.get("kind") == kind]
            if ps:
                out.append('<h3>%s</h3><p class="chips">%s</p>' % (name, "".join(
                    '<a class="chip" href="#%s" data-status="%s"><span class="dot s-%s"></span>%s</a>'
                    % (p.slug, p.meta.get("status"), p.meta.get("status"), html.escape(p.title())) for p in ps)))
        out.append("<h2>Where we're going</h2>")
        ahead = False
        for status, _ in STATUSES[2:]:
            items = ['<li><a class="wl" href="#%s">%s</a> &mdash; %s</li>' % (p.slug, html.escape(p.title()), p.summary_html)
                     for p in self.pages if p.meta.get("status") == status]
            items += ['<li>%s <span class="muted">&mdash; on</span> <a class="wl" href="#%s">%s</a></li>'
                      % (body, p.slug, html.escape(p.title())) for st, p, body in self.roadmap if st == status]
            if items:
                ahead = True
                out.append('<h3><span class="badge s-%s">%s</span></h3><ul>%s</ul>' % (status, status, "".join(items)))
        if not ahead:
            out.append("<p>Nothing yet: a page's <em>Planned</em> bullets land here.</p>")
        if self.wanted:
            items = []
            for key in sorted(self.wanted):
                target, slugs = self.wanted[key]
                items.append('<li><span class="wl missing">%s</span> <span class="muted">&mdash; linked from</span> %s</li>' % (
                    html.escape(target), ", ".join('<a class="wl" href="#%s">%s</a>' % (s, html.escape(self.by_slug[s].title()))
                                                   for s in sorted(slugs) if s in self.by_slug) or "the front page"))
            out.append("<h2>Pages wanted</h2><p>Linked to, not written yet.</p><ul>%s</ul>" % "".join(items))
        out.append('<h2>Statuses</h2><table><tbody>%s</tbody></table>' % "".join(
            '<tr><td><span class="badge s-%s">%s</span></td><td>%s</td></tr>' % (s, s, html.escape(d)) for s, d in STATUSES))
        out.append("</div></article>")
        return "".join(out)

    def nav(self):
        chips = "".join('<button type="button" class="filter" data-s="%s" aria-pressed="true"><span class="dot s-%s"></span>%s</button>'
                        % (s, s, s) for s in STATUS_NAMES if any(p.meta.get("status") == s for p in self.pages))
        groups = []
        for kind, name in KINDS:
            ps = [p for p in self.pages if p.meta.get("kind") == kind]
            if ps:
                groups.append('<div class="group"><h3>%s</h3><ul>%s</ul></div>' % (name, "".join(
                    '<li data-s="%s" data-text="%s"><a href="#%s"><span class="dot s-%s"></span>%s</a></li>'
                    % (p.meta.get("status"), html.escape(p.text), p.slug, p.meta.get("status"), html.escape(p.title()))
                    for p in ps)))
        return ('<nav id="side"><a class="brand" href="#home">%s</a>'
                '<input id="q" type="search" placeholder="Search the pages" aria-label="Search the pages">'
                '<div class="filters">%s</div>%s<p id="none" class="muted" hidden>No page matches.</p></nav>'
                % (html.escape(self.home.title()), chips, "".join(groups)))

    def document(self):
        try:
            commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=APP, capture_output=True,
                                    text=True, timeout=10).stdout.strip()
        except (OSError, subprocess.SubprocessError):
            commit = ""
        stamp = "Built %s from %d pages%s. Edit <code>apps/chasm/wiki/*.md</code>, then run <code>python apps/chasm/tools/chasm_wiki.py</code>." % (
            datetime.datetime.now().strftime("%Y-%m-%d %H:%M"), len(self.pages), (", commit " + commit) if commit else "")
        return PAGE % {"title": html.escape(self.home.title()), "css": CSS, "js": JS, "nav": self.nav(),
                       "articles": self.front() + "".join(self.article(p) for p in self.pages), "stamp": stamp}


CSS = """
:root{
  --bg:#f6f4ee; --panel:#ece9e0; --text:#23241f; --muted:#6c6a62; --line:#d9d5c9; --link:#2b5c88;
  --accent:#9a6430; --code:#e6e2d6; --missing:#b3462f;
  --s-built:#2f8a44; --s-partial:#16827d; --s-planned:#2f63b0; --s-proposed:#7b4fb3; --s-idea:#a8791c;
  --s-parked:#7d7c76; --s-scrapped:#b3462f;
}
@media (prefers-color-scheme: dark){
  :root:not([data-theme="light"]){
    --bg:#1a1b18; --panel:#22241f; --text:#e5e3da; --muted:#9b998f; --line:#383a33; --link:#8dbbe6;
    --accent:#e0a868; --code:#2b2d27; --missing:#f08a72;
    --s-built:#6cc483; --s-partial:#4cc2bb; --s-planned:#7fa8ec; --s-proposed:#b694e6; --s-idea:#e0b552;
    --s-parked:#a3a29b; --s-scrapped:#f08a72;
  }
}
:root[data-theme="dark"]{
  --bg:#1a1b18; --panel:#22241f; --text:#e5e3da; --muted:#9b998f; --line:#383a33; --link:#8dbbe6;
  --accent:#e0a868; --code:#2b2d27; --missing:#f08a72;
  --s-built:#6cc483; --s-partial:#4cc2bb; --s-planned:#7fa8ec; --s-proposed:#b694e6; --s-idea:#e0b552;
  --s-parked:#a3a29b; --s-scrapped:#f08a72;
}
*{box-sizing:border-box}
html,body{margin:0;background:var(--bg);color:var(--text)}
body{font:16px/1.6 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;display:grid;grid-template-columns:270px minmax(0,1fr);min-height:100vh}
a{color:var(--link)}
code{font:.88em/1.4 ui-monospace,Consolas,monospace;background:var(--code);padding:.05em .3em;border-radius:3px}
#side{background:var(--panel);border-right:1px solid var(--line);padding:20px 16px;position:sticky;top:0;height:100vh;overflow-y:auto}
.brand{display:block;font-weight:700;font-size:1.15rem;color:var(--text);text-decoration:none;margin-bottom:12px}
#q{width:100%;padding:7px 10px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--text);font:inherit;font-size:.92rem}
.filters{display:flex;flex-wrap:wrap;gap:4px;margin:10px 0 6px}
.filter{font:inherit;font-size:.75rem;border:1px solid var(--line);background:var(--bg);color:var(--text);border-radius:999px;padding:1px 8px;cursor:pointer}
.filter[aria-pressed="false"]{opacity:.4;text-decoration:line-through}
.group h3{font-size:.72rem;text-transform:uppercase;letter-spacing:.06em;color:var(--muted);margin:16px 0 4px}
.group ul{list-style:none;margin:0;padding:0}
.group li a{display:block;padding:3px 8px;border-radius:5px;text-decoration:none;color:var(--text)}
.group li a:hover{background:var(--bg)}
.group li a.active{background:var(--bg);font-weight:600}
.group li[data-s="scrapped"] a{color:var(--muted);text-decoration:line-through}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:7px;vertical-align:1px}
main{padding:32px 40px 60px;max-width:860px;width:100%}
.kind{font-size:.75rem;text-transform:uppercase;letter-spacing:.07em;color:var(--muted)}
h1{font-size:1.9rem;line-height:1.2;margin:.15em 0 .3em}
h2{font-size:1.2rem;margin:1.8em 0 .5em;padding-bottom:.2em;border-bottom:1px solid var(--line)}
h3{font-size:1rem;margin:1.4em 0 .4em}
.summary{font-size:1.08rem;margin:.2em 0 1em}
.meta{display:grid;grid-template-columns:max-content 1fr;gap:2px 14px;font-size:.85rem;color:var(--muted);margin:0 0 1.5em;padding:10px 14px;background:var(--panel);border-radius:8px}
.meta dt{font-weight:600}
.meta dd{margin:0;overflow-wrap:anywhere}
.badge{display:inline-block;font-size:.68rem;font-weight:600;text-transform:uppercase;letter-spacing:.05em;border:1px solid currentColor;border-radius:999px;padding:0 .55em;vertical-align:.25em;line-height:1.6}
h3 .badge{font-size:.75rem}
li .badge{vertical-align:.1em;margin-right:.2em}
""" + "".join(".s-%s.badge{color:var(--s-%s)} .s-%s.dot{background:var(--s-%s)}\n" % (s, s, s, s) for s in STATUS_NAMES) + """
.s-partial.dot{background:linear-gradient(90deg,var(--s-built) 50%,transparent 50%);box-shadow:inset 0 0 0 1.5px var(--s-built)}
.tune{border-bottom:1.5px dotted var(--accent);cursor:help;font-variant-numeric:tabular-nums;white-space:nowrap}
.tune.bad{color:var(--missing)}
a.wl.missing,span.wl.missing{color:var(--missing);text-decoration:underline dotted;cursor:default}
table{border-collapse:collapse;width:100%;font-size:.88rem;margin:.6em 0}
th,td{text-align:left;padding:5px 8px;border-bottom:1px solid var(--line);vertical-align:top}
th{font-weight:600;color:var(--muted)}
td.num{white-space:nowrap;font-variant-numeric:tabular-nums}
.tuning table td:first-child{white-space:nowrap}
.note,.muted{color:var(--muted)}
.note{font-size:.82rem}
blockquote{margin:1em 0;padding:.2em 1em;border-left:3px solid var(--accent);color:var(--muted)}
pre{background:var(--code);padding:10px 12px;border-radius:6px;overflow-x:auto}
pre code{background:none;padding:0}
.chips{display:flex;flex-wrap:wrap;gap:6px}
.chip{border:1px solid var(--line);border-radius:6px;padding:2px 10px;text-decoration:none;color:var(--text);background:var(--panel)}
.chip[data-status="scrapped"]{text-decoration:line-through;color:var(--muted)}
.counts{color:var(--muted)}
.counts .dot{margin-left:2px}
footer{margin-top:3em;font-size:.8rem;color:var(--muted)}
@media (max-width:760px){
  body{display:block}
  #side{position:static;height:auto;border-right:0;border-bottom:1px solid var(--line)}
  main{padding:20px 16px 40px}
  .meta{grid-template-columns:1fr}
  .tuning table{display:block;overflow-x:auto}
}
"""

JS = """
(function(){
  var pages = document.querySelectorAll('article.page');
  var links = document.querySelectorAll('#side .group li a');
  //Articles are p-<slug> while the address is #<slug>: no element carries the address's id, so the browser's own
  //jump to an anchor never fights this one.
  function show(){
    var id = decodeURIComponent(location.hash.slice(1)) || 'home';
    var el = document.getElementById('p-' + id);
    if (!el || !el.classList.contains('page')) el = document.getElementById('p-home');
    pages.forEach(function(p){ p.hidden = p !== el; });
    links.forEach(function(a){ a.classList.toggle('active', 'p-' + a.getAttribute('href').slice(1) === el.id); });
    var h = el.querySelector('h1');
    document.title = (el.id === 'p-home' ? '' : h.firstChild.textContent.trim() + ' - ') + document.querySelector('.brand').textContent;
    window.scrollTo(0, 0);
  }
  window.addEventListener('hashchange', show);
  show();

  var q = document.getElementById('q');
  var items = document.querySelectorAll('#side .group li');
  var hidden = {};
  try { hidden = JSON.parse(localStorage.getItem('chasm-wiki-hidden') || '{}'); } catch (e) {}
  function filter(){
    var words = q.value.toLowerCase().split(/\\s+/).filter(Boolean);
    var any = false;
    items.forEach(function(li){
      var text = li.getAttribute('data-text');
      var ok = !hidden[li.getAttribute('data-s')] && words.every(function(w){ return text.indexOf(w) >= 0; });
      li.hidden = !ok;
      any = any || ok;
    });
    document.querySelectorAll('#side .group').forEach(function(g){
      g.hidden = !g.querySelector('li:not([hidden])');
    });
    document.getElementById('none').hidden = any;
  }
  document.querySelectorAll('.filter').forEach(function(b){
    var s = b.getAttribute('data-s');
    b.setAttribute('aria-pressed', hidden[s] ? 'false' : 'true');
    b.addEventListener('click', function(){
      hidden[s] = !hidden[s];
      b.setAttribute('aria-pressed', hidden[s] ? 'false' : 'true');
      try { localStorage.setItem('chasm-wiki-hidden', JSON.stringify(hidden)); } catch (e) {}
      filter();
    });
  });
  q.addEventListener('input', filter);
  q.addEventListener('keydown', function(e){
    if (e.key === 'Enter'){
      var first = document.querySelector('#side .group li:not([hidden]) a');
      if (first) location.hash = first.getAttribute('href');
    }
  });
  filter();
})();
"""

PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%(title)s</title>
<style>%(css)s</style>
</head>
<body>
%(nav)s
<main>
%(articles)s
<footer>%(stamp)s</footer>
</main>
<script>%(js)s</script>
</body>
</html>
"""


def main():
    ap = argparse.ArgumentParser(description="Build the chasm wiki (apps/chasm/wiki/*.md) into one HTML file, checking every page.")
    ap.add_argument("--check", action="store_true", help="check the pages, write nothing")
    ap.add_argument("--unused", action="store_true", help="also list the tuning constants (%s) no page mentions" % ", ".join(TUNING_PREFIXES))
    ap.add_argument("--out", default=OUT, help="where the HTML goes (default apps/chasm/build/wiki.html)")
    args = ap.parse_args()

    wiki = Wiki(args.out)
    wiki.load()
    wiki.render()
    if not args.check:
        os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
        with open(args.out, "w", encoding="utf-8", newline="\n") as f:
            f.write(wiki.document())

    for e in wiki.errors:
        print("ERROR " + e)
    if wiki.wanted:
        print("wanted (linked, no page yet): " + ", ".join(sorted(v[0] for v in wiki.wanted.values())))
    used = set(n for p in wiki.pages for n in p.used)
    unused = sorted(n for n in wiki.defs if n.startswith(TUNING_PREFIXES) and n not in used)
    if args.unused:
        for n in unused:
            d = wiki.defs[n]
            print("unused %-30s %s:%d  %s" % (n, d.file, d.line, d.comment))
    print("chasm wiki: %d pages, %d errors, %d pages wanted, %d of %d tuning constants on a page%s" % (
        len(wiki.pages), len(wiki.errors), len(wiki.wanted), len(used & set(n for n in wiki.defs if n.startswith(TUNING_PREFIXES))),
        sum(1 for n in wiki.defs if n.startswith(TUNING_PREFIXES)),
        "" if args.check else " -> " + os.path.relpath(args.out, os.getcwd()).replace(os.sep, "/")))
    return 1 if wiki.errors else 0


if __name__ == "__main__":
    sys.exit(main())
