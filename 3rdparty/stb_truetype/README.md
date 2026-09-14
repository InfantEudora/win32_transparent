# stb_truetype

`stb_truetype.h` is a **verbatim copy** of `3rdparty/imgui/imstb_truetype.h` — stb_truetype 1.26,
public domain, plus the three upstream bugfixes ImGui carries (grep `[DEAR IMGUI]` for them).

```
md5  e3dda939998273513f0494d197d91e12   (both files, 2026-09-14)
```

## Why a copy rather than an include path into `3rdparty/imgui/`

Because the point of the font work is a build that does not contain ImGui at all
(`docs/engine_backlog.md` items 81 and 82). A font system that includes a header out of ImGui's
folder has not achieved that, and the dependency would only be noticed on the day someone tried to
drop ImGui and found the link still needed it.

There is a second, smaller reason. ImGui compiles its copy inside `imgui_draw.cpp` with
`STBTT_STATIC` and its own `STBTT_malloc`/`STBTT_assert` overrides, so those symbols have internal
linkage and cannot be linked against from outside anyway. Including the header separately with
`STB_TRUETYPE_IMPLEMENTATION` gives a private copy with no clash — which is exactly what
`tools/fontbake/` does.

## Who uses it

`tools/fontbake/` only, which is an **offline tool**. So this header compiles into `fontbake.exe`
and into no app. Nothing in `core/` includes it: the engine loads a baked `.fnt`
(`core/UIFont.h`) and has no TrueType parser in it.

## Updating

Take ImGui's copy again and re-record the md5 above:

```bash
cp 3rdparty/imgui/imstb_truetype.h 3rdparty/stb_truetype/stb_truetype.h
```

Keep it verbatim. A local edit here would be invisible to anyone comparing the two files, and the
whole value of this being a copy is that the comparison is trivial.
