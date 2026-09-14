#ifndef _UI_FONT_H_
#define _UI_FONT_H_

#include <stdint.h>
#include <stddef.h>

/*
    The baked monospace SDF font: the FILE FORMAT, and nothing else.

    This header describes what tools/fontbake writes and what the 2D overlay reads. It is
    deliberately the only thing the two share, and it is header-only with no dependency beyond
    <stdint.h> - so the offline tool includes it without linking a single core object, and the
    runtime includes it without gaining a TrueType parser.

    See docs/ui_overlay_plan.md section 2 for why the font is baked offline at all, and why the
    result is one binary file rather than a PNG plus a JSON sidecar.

    --- WHY A GRID, AND WHY THERE IS NO GLYPH TABLE ---------------------------------------------
    The font is MONOSPACE and the atlas is a FIXED GRID, so every per-glyph quantity that a
    proportional font would need a table for - bearing, width, advance - is either a constant or
    arithmetic on the codepoint. What is left is this header.

    A glyph is drawn as its WHOLE CELL rather than as a tight box around its ink. That costs a
    little overdraw on transparent margins and removes the last reason to store anything per
    glyph. It is also what makes UIFontCellRect below total: every character has a cell, including
    the ones with no ink in them.

    --- WHY THE DISTANCE FIELD NEEDS TWO NUMBERS AND NOT ONE ------------------------------------
    A sampled texel is a distance, not a coverage, and turning it back into pixels needs to know
    both where the edge sits in the 0..1 range (`onedge`) and how many pixels that range spans
    (`distance_range_px`). Both are properties of how it was baked, so both travel with it - a
    shader that hardcodes 0.5 and a guess is a shader that breaks silently when the bake settings
    change. UIFontDistanceScale turns the second one into what a draw at an arbitrary size needs.

    --- THE SOLID TEXEL -------------------------------------------------------------------------
    One texel of the atlas holds the maximum "inside" value, and untextured geometry points its
    UVs at it. That is what lets a rounded rect and a glyph go through one shader with no branch
    and no second texture: the rect's glyph term reads as far-inside everywhere and never wins the
    max(). See docs/ui_overlay_plan.md section 4.

    It lives in a spare grid cell, so a bake whose grid exactly fits its glyphs is refused rather
    than silently overwriting '~'.
*/

//Printable ASCII, matching core/TextMesh.h's glyph set so the two text paths agree on what a font
//contains. Everything else draws nothing and advances the pen.
#define UI_FONT_FIRST_CHAR  0x20
#define UI_FONT_LAST_CHAR   0x7E
#define UI_FONT_NUM_CHARS   (UI_FONT_LAST_CHAR - UI_FONT_FIRST_CHAR + 1)

//'F','N','T','1'. Checked on load - a truncated or unrelated file is worth refusing loudly,
//because the failure mode without it is a texture full of garbage rather than an error.
#define UI_FONT_MAGIC0      'F'
#define UI_FONT_MAGIC1      'N'
#define UI_FONT_MAGIC2      'T'
#define UI_FONT_MAGIC3      '1'

//Bumped whenever a field below changes meaning. The loader refuses a version it does not know
//rather than reinterpreting old bytes as new fields.
#define UI_FONT_VERSION     1

/*
    The file is this struct, then `atlas_w * atlas_h` bytes of single-channel coverage-distance,
    top row first. Every field is 4 bytes and naturally aligned, so the struct has no padding and
    the file layout is the struct layout on every compiler this repo uses (see the static_assert).

    All pixel-valued fields are AT BAKE SCALE - that is, they describe the atlas as it sits on
    disk. Drawing at another size scales them; see UIFontDistanceScale.
*/
struct ui_font_header{
    char     magic[4];
    uint32_t version;

    uint32_t atlas_w;           //pixels
    uint32_t atlas_h;
    uint32_t grid_w;            //cells across
    uint32_t grid_h;            //cells down
    uint32_t cell_w;            //pixels per cell
    uint32_t cell_h;

    uint32_t first_char;        //normally UI_FONT_FIRST_CHAR; stored so the loader need not assume
    uint32_t num_chars;

    float    em_px;             //the size the field was generated at - the reference for every scale
    float    advance_px;        //pen step per character. ONE number, because monospace
    float    line_height_px;    //baseline to baseline

    /*
        Where the pen sits INSIDE a cell: pixels right and down from the cell's top-left corner to
        the glyph's origin (left sidebearing edge, on the baseline).

        This is the field that makes layout trivial. A glyph drawn with its pen at screen position
        P occupies the cell-sized box whose top-left is P - (origin_x, origin_y), so positioning a
        glyph needs no per-glyph data at all - see UIFontCellRect's counterpart in the overlay.
    */
    float    origin_x;
    float    origin_y;

    //Turning a sampled texel back into a signed distance. See the header note above.
    float    onedge;            //normalised 0..1 value the glyph edge sits at (typically ~0.502)
    float    distance_range_px; //pixels spanned by the full 0..1 stored range, at bake scale

    //The all-inside texel, in atlas pixels. See the header note above.
    uint32_t solid_x;
    uint32_t solid_y;

    uint32_t pixel_offset;      //byte offset from the start of the file to the atlas bytes
    uint32_t pixel_bytes;       //atlas_w * atlas_h. Redundant, and therefore worth checking
};

//The file layout IS the struct layout, so a compiler that pads this would silently read a
//different file than the tool wrote.
static_assert(sizeof(ui_font_header) == 84, "ui_font_header must be exactly 84 bytes and unpadded");

/*
    Which grid cell a character occupies. Returns false for anything outside the baked range,
    including every control character and everything above ASCII.

    Shared between the tool and the runtime ON PURPOSE. It is two lines, which is exactly what
    makes it tempting to write twice - and a bake that packed glyphs by one rule while the drawing
    code unpacked them by another would render as plausible-looking wrong letters rather than as
    an obvious failure.
*/
inline bool UIFontCell(const ui_font_header& h, uint32_t codepoint, uint32_t* out_x, uint32_t* out_y){
    if (codepoint < h.first_char || codepoint >= h.first_char + h.num_chars)
        return false;
    uint32_t index = codepoint - h.first_char;
    *out_x = index % h.grid_w;
    *out_y = index / h.grid_w;
    return true;
}

/*
    What to multiply the header's *_px fields by when drawing at `size_px` (an em size, matching
    em_px's meaning).

    distance_range_px scales by this too, and that is the part worth stating because it is easy to
    miss: the field stores distances measured in BAKE pixels, so text drawn at twice the bake size
    has its edges twice as far apart in screen pixels. A shader handed an unscaled range gets its
    antialiasing ramp wrong by that factor - too soft when magnified, too hard when minified - and
    it looks like a bad font rather than a missing multiply.
*/
inline float UIFontDistanceScale(const ui_font_header& h, float size_px){
    return (h.em_px > 0.0f) ? (size_px / h.em_px) : 0.0f;
}

/*
    Whether a header is self-consistent: magic, version, and the sizes agreeing with each other.
    Cheap, and the one call that stands between a corrupt file and a GL upload sized from it.
*/
inline bool UIFontHeaderIsValid(const ui_font_header& h, size_t file_size){
    if (h.magic[0] != UI_FONT_MAGIC0 || h.magic[1] != UI_FONT_MAGIC1 ||
        h.magic[2] != UI_FONT_MAGIC2 || h.magic[3] != UI_FONT_MAGIC3)
        return false;
    if (h.version != UI_FONT_VERSION)
        return false;
    if (h.atlas_w == 0 || h.atlas_h == 0 || h.grid_w == 0 || h.grid_h == 0)
        return false;
    if (h.grid_w * h.cell_w != h.atlas_w || h.grid_h * h.cell_h != h.atlas_h)
        return false;
    //A spare cell is required, not merely likely - the solid texel lives in one.
    if (h.num_chars >= h.grid_w * h.grid_h)
        return false;
    if (h.solid_x >= h.atlas_w || h.solid_y >= h.atlas_h)
        return false;
    if (h.pixel_bytes != h.atlas_w * h.atlas_h)
        return false;
    if (h.pixel_offset < sizeof(ui_font_header))
        return false;
    if ((size_t)h.pixel_offset + (size_t)h.pixel_bytes > file_size)
        return false;
    return true;
}

#endif
