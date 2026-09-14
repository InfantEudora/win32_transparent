/*
    fontbake - a monospace TrueType font to the signed-distance atlas the 2D overlay draws from.

    Step 1 of docs/ui_overlay_plan.md. Run it once when the font or the bake settings change; the
    result is an asset, and nothing in any app parses TrueType at run time.

        cd tools/fontbake && mingw32-make.exe -j8
        ./build/fontbake.exe                    #defaults: consola.ttf -> shared_assets/fonts/mono_sdf.fnt
        ./build/fontbake.exe --help

    --- WHY A DISTANCE FIELD AND NOT A COVERAGE BITMAP ------------------------------------------
    Because the size text is drawn at is not known here. The touch buttons are laid out in
    MILLIMETRES against the display's dpi (engine_backlog item 67) precisely so they survive a
    change of screen, which makes the pixel size of a label a run-time quantity on Android. A
    coverage bitmap is baked for one size; a distance field is resolution-independent, and it
    hands the shader outline and glow for a smoothstep each.

    --- WHY THE OUTPUT IS ONE BINARY FILE -------------------------------------------------------
    The obvious shape is a PNG plus a JSON sidecar, mirroring tools/blender_glyph_meshes.py and
    fonts_glyphs.json. It is rejected here because it puts a PNG decoder and a JSON parser in the
    RUNTIME load path, and engine_backlog item 74 is trying to make the JSON dependency optional.
    What the runtime needs is a header struct and some bytes, so that is what it gets: one
    LoadFile, one cast, one texture upload.

    A .png IS written beside it, and is not read by anything - it exists so the atlas can be
    eyeballed, which is the one thing the binary format costs and the cheapest possible way to buy
    back. --no-png skips it.

    --- THE ONE THING TO KNOW BEFORE CHANGING THE DEFAULTS --------------------------------------
    `padding` is the SDF's reach in pixels, and it is what every effect built on top of this has
    to fit inside. An outline of N pixels, or a glow of N, needs the field to still be meaningful
    N pixels from the edge; past `padding` it is clamped and the effect stops growing with no
    error anywhere. So padding is a budget for the shader, not a packing detail - which is why it
    travels into the header as distance_range_px rather than being forgotten here.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

//The format. Header-only and shared with the engine - see the note at the top of it about why the
//grid arithmetic in particular lives there rather than being written out twice.
#include "UIFont.h"

//---------------------------------------------------------------------------------------------
// Options
//---------------------------------------------------------------------------------------------
struct options{
    const char* font_path   = "../../shared_assets/fonts/consola.ttf";
    const char* out_path    = "../../shared_assets/fonts/mono_sdf.fnt";
    const char* png_path    = NULL;     //defaults to out_path with .png; --no-png disables
    bool        f_write_png = true;

    /*
        The em square in pixels. NOT a cell size and not a line height - the em is the font's own
        design square, which is the only one of the three that means the same thing in every font,
        so it is what the header's scale factor is expressed against.

        48 is chosen to be comfortably above the sizes a HUD actually draws (so minification does
        the resampling, which is where a distance field is at its best) without making the atlas
        large. It is not a quality ceiling the way a coverage bitmap's size would be.
    */
    float       em_px       = 48.0f;

    //The field's reach in pixels. See the note at the top of the file - this is a shader budget.
    int         padding     = 8;

    //Where the glyph edge sits on the 0..255 scale. 128 centres it, which gives equal resolution
    //to the inside and the outside; there is no reason to move it and it is exposed only because
    //the header has to record whatever it was.
    int         onedge      = 128;

    //Cells across. 16 is not arbitrary: it keeps the cell index's row/column split a shift and a
    //mask for anyone reading the atlas by eye, and 95 printable ASCII glyphs land in 6 rows with
    //exactly one cell spare for the solid texel.
    int         grid_w      = 16;

    //0 = size the cell from the glyphs (the default, and the only setting that cannot clip).
    int         cell_px     = 0;
};

static void PrintUsage(){
    printf(
        "fontbake - bake a monospace TTF into the overlay's SDF atlas\n"
        "\n"
        "  --font <path>     input .ttf            (default ../../shared_assets/fonts/consola.ttf)\n"
        "  --out <path>      output .fnt           (default ../../shared_assets/fonts/mono_sdf.fnt)\n"
        "  --png <path>      debug atlas image     (default: --out with a .png extension)\n"
        "  --no-png          skip the debug image\n"
        "  --em <px>         em square in pixels   (default 48)\n"
        "  --padding <px>    SDF reach in pixels   (default 8) - the budget for outlines and glows\n"
        "  --onedge <0-255>  edge value            (default 128)\n"
        "  --grid <cells>    cells across          (default 16)\n"
        "  --cell <px>       force the cell size   (default: sized from the glyphs, cannot clip)\n"
        "  --help\n"
        "\n"
        "Paths default to being relative to tools/fontbake/, which is where the makefile expects\n"
        "you to be standing.\n");
}

//Returns false and complains on anything unrecognised rather than ignoring it - a mistyped flag
//that silently bakes the defaults is a bad half-hour.
static bool ParseArgs(int argc, char** argv, options& opt){
    for (int i = 1; i < argc; i++){
        const char* a = argv[i];
        bool f_has_value = (i + 1 < argc);
        #define NEEDS_VALUE(flag) \
            if (!f_has_value){ printf("fontbake: %s needs a value\n", flag); return false; }

        if (!strcmp(a,"--help") || !strcmp(a,"-h")){
            PrintUsage();
            exit(0);
        }else if (!strcmp(a,"--font")){
            NEEDS_VALUE("--font"); opt.font_path = argv[++i];
        }else if (!strcmp(a,"--out")){
            NEEDS_VALUE("--out");  opt.out_path = argv[++i];
        }else if (!strcmp(a,"--png")){
            NEEDS_VALUE("--png");  opt.png_path = argv[++i];
        }else if (!strcmp(a,"--no-png")){
            opt.f_write_png = false;
        }else if (!strcmp(a,"--em")){
            NEEDS_VALUE("--em");        opt.em_px = (float)atof(argv[++i]);
        }else if (!strcmp(a,"--padding")){
            NEEDS_VALUE("--padding");   opt.padding = atoi(argv[++i]);
        }else if (!strcmp(a,"--onedge")){
            NEEDS_VALUE("--onedge");    opt.onedge = atoi(argv[++i]);
        }else if (!strcmp(a,"--grid")){
            NEEDS_VALUE("--grid");      opt.grid_w = atoi(argv[++i]);
        }else if (!strcmp(a,"--cell")){
            NEEDS_VALUE("--cell");      opt.cell_px = atoi(argv[++i]);
        }else{
            printf("fontbake: unknown argument '%s' (try --help)\n", a);
            return false;
        }
        #undef NEEDS_VALUE
    }

    if (opt.em_px < 4.0f || opt.em_px > 512.0f){
        printf("fontbake: --em %.1f is out of range (4..512)\n", opt.em_px);
        return false;
    }
    if (opt.padding < 1 || opt.padding > 64){
        printf("fontbake: --padding %d is out of range (1..64)\n", opt.padding);
        return false;
    }
    if (opt.onedge < 1 || opt.onedge > 254){
        printf("fontbake: --onedge %d is out of range (1..254)\n", opt.onedge);
        return false;
    }
    if (opt.grid_w < 1 || opt.grid_w > 64){
        printf("fontbake: --grid %d is out of range (1..64)\n", opt.grid_w);
        return false;
    }
    return true;
}

//---------------------------------------------------------------------------------------------
// Files
//---------------------------------------------------------------------------------------------
static bool ReadWholeFile(const char* path, std::vector<uint8_t>& out){
    FILE* f = fopen(path,"rb");
    if (!f){
        printf("fontbake: cannot open '%s'\n", path);
        return false;
    }
    fseek(f,0,SEEK_END);
    long size = ftell(f);
    fseek(f,0,SEEK_SET);
    if (size <= 0){
        printf("fontbake: '%s' is empty\n", path);
        fclose(f);
        return false;
    }
    out.resize((size_t)size);
    size_t got = fread(out.data(),1,(size_t)size,f);
    fclose(f);
    if (got != (size_t)size){
        printf("fontbake: short read on '%s' (%zu of %ld bytes)\n", path, got, size);
        return false;
    }
    return true;
}

//---------------------------------------------------------------------------------------------
// One generated glyph, held until the cell size is known.
//
// The cell cannot be chosen until every glyph's extent is known, and the extents are only known
// once the fields are generated - so they are all generated first and blitted afterwards, rather
// than generating twice. 95 glyphs at this size is under a megabyte.
//---------------------------------------------------------------------------------------------
struct baked_glyph{
    uint8_t* bitmap = NULL;         //stb's, freed with stbtt_FreeSDF
    int      w = 0, h = 0;
    int      xoff = 0, yoff = 0;    //of the bitmap's top-left from the pen origin, y DOWN
};

int main(int argc, char** argv){
    options opt;
    if (!ParseArgs(argc,argv,opt))
        return 1;

    std::vector<uint8_t> ttf;
    if (!ReadWholeFile(opt.font_path,ttf))
        return 1;

    stbtt_fontinfo font;
    int offset = stbtt_GetFontOffsetForIndex(ttf.data(),0);
    if (offset < 0 || !stbtt_InitFont(&font,ttf.data(),offset)){
        printf("fontbake: '%s' is not a font this can read\n", opt.font_path);
        return 1;
    }

    /*
        ScaleForMappingEmToPixels, not ScaleForPixelHeight. The latter maps ascent-descent, which
        is a different number in every font, so a bake at "48" would mean a different glyph size
        per font and the header's em_px would not be comparable across them. The em square is the
        font's own unit and is what every other tool means by a font's size.
    */
    float scale = stbtt_ScaleForMappingEmToPixels(&font,opt.em_px);
    if (!(scale > 0.0f)){
        printf("fontbake: could not derive a scale for em %.1f\n", opt.em_px);
        return 1;
    }

    int ascent_fu = 0, descent_fu = 0, linegap_fu = 0;
    stbtt_GetFontVMetrics(&font,&ascent_fu,&descent_fu,&linegap_fu);
    float line_height_px = (float)(ascent_fu - descent_fu + linegap_fu) * scale;

    /*
        The advance, and the check that this font is actually monospaced.

        Everything downstream - the whole reason there is no per-glyph table - rests on one pen
        step serving every character. A proportional font would still bake and would still look
        almost right, with the error accumulating along a line, so this is worth refusing rather
        than warning about: a "mostly aligned" HUD is harder to diagnose than a failed bake.
    */
    int advance_fu = 0, lsb_fu = 0;
    stbtt_GetCodepointHMetrics(&font,'M',&advance_fu,&lsb_fu);
    int num_mismatched = 0;
    int first_mismatch = 0;
    for (int cp = UI_FONT_FIRST_CHAR; cp <= UI_FONT_LAST_CHAR; cp++){
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&font,cp,&adv,&lsb);
        if (adv != advance_fu){
            if (!num_mismatched)
                first_mismatch = cp;
            num_mismatched++;
        }
    }
    if (num_mismatched){
        printf("fontbake: '%s' is not monospaced - %d of %d glyphs differ from 'M' "
               "(first: '%c'). This baker has no per-glyph advance table, so the result "
               "would drift along a line. Use a monospace font.\n",
               opt.font_path,num_mismatched,UI_FONT_NUM_CHARS,(char)first_mismatch);
        return 1;
    }
    float advance_px = (float)advance_fu * scale;

    //Chosen so the field spans exactly `padding` pixels before it clamps: at `padding` pixels
    //outside the edge the value reaches 0, and at `padding` inside it reaches 255 (when onedge is
    //centred). Deriving it rather than exposing it keeps the two numbers from disagreeing.
    float pixel_dist_scale = (float)opt.onedge / (float)opt.padding;

    //-----------------------------------------------------------------------------------------
    // Generate every glyph's field
    //-----------------------------------------------------------------------------------------
    std::vector<baked_glyph> glyphs(UI_FONT_NUM_CHARS);

    //Union of every glyph's extent, relative to the pen origin. Seeded from the first real glyph
    //rather than from 0, because the origin is not necessarily inside the ink - an all-descender
    //font would give a box that wrongly included the baseline.
    bool f_have_box = false;
    int box_x0 = 0, box_y0 = 0, box_x1 = 0, box_y1 = 0;

    int num_with_ink = 0;
    for (int i = 0; i < UI_FONT_NUM_CHARS; i++){
        int cp = UI_FONT_FIRST_CHAR + i;
        baked_glyph& g = glyphs[i];
        g.bitmap = stbtt_GetCodepointSDF(&font,scale,cp,opt.padding,
                                         (unsigned char)opt.onedge,pixel_dist_scale,
                                         &g.w,&g.h,&g.xoff,&g.yoff);
        //NULL is normal and is not an error: a space has no contours, so there is no field to
        //generate. Its cell stays at 0, which reads as "far outside" and draws nothing - exactly
        //what a space should do. Every character still gets a cell so the grid stays arithmetic.
        if (!g.bitmap)
            continue;

        num_with_ink++;
        int x0 = g.xoff,        y0 = g.yoff;
        int x1 = g.xoff + g.w,  y1 = g.yoff + g.h;
        if (!f_have_box){
            box_x0 = x0; box_y0 = y0; box_x1 = x1; box_y1 = y1;
            f_have_box = true;
        }else{
            if (x0 < box_x0) box_x0 = x0;
            if (y0 < box_y0) box_y0 = y0;
            if (x1 > box_x1) box_x1 = x1;
            if (y1 > box_y1) box_y1 = y1;
        }
    }

    if (!f_have_box){
        printf("fontbake: '%s' produced no glyph outlines at all for printable ASCII\n",
               opt.font_path);
        return 1;
    }

    //-----------------------------------------------------------------------------------------
    // Cell size and the pen origin inside it
    //-----------------------------------------------------------------------------------------
    int need_w = box_x1 - box_x0;
    int need_h = box_y1 - box_y0;
    int cell_w = 0, cell_h = 0;

    if (opt.cell_px > 0){
        cell_w = cell_h = opt.cell_px;
        if (cell_w < need_w || cell_h < need_h){
            printf("fontbake: --cell %d is too small - the glyphs need %dx%d at em %.1f with "
                   "padding %d. Raise it, or drop --cell and let it size itself.\n",
                   opt.cell_px,need_w,need_h,opt.em_px,opt.padding);
            return 1;
        }
    }else{
        //Round up to a multiple of 4. Nothing requires it - it just keeps the atlas dimensions
        //and every cell boundary on tidy numbers, which matters only when reading the debug PNG.
        cell_w = (need_w + 3) & ~3;
        cell_h = (need_h + 3) & ~3;
    }

    /*
        The pen origin inside a cell: where the baseline-left of a glyph sits, measured from the
        cell's top-left. The union box is placed centrally in whatever slack the cell has, so any
        rounding above is shared between the two sides rather than piling up on one.

        Note cell_w is LARGER than advance_px - by about 2*padding - so adjacent glyph quads
        overlap when a string is drawn. That is correct and necessary: the padding is the field's
        reach and it has to be rasterised for the edge to antialias at all. The overlap region is
        fully transparent in both quads, so the blend is a no-op there.
    */
    float origin_x = (float)(-box_x0 + (cell_w - need_w) / 2);
    float origin_y = (float)(-box_y0 + (cell_h - need_h) / 2);

    //-----------------------------------------------------------------------------------------
    // Atlas layout. One cell per glyph, plus at least one spare for the solid texel.
    //-----------------------------------------------------------------------------------------
    int grid_w = opt.grid_w;
    int grid_h = (UI_FONT_NUM_CHARS + 1 + grid_w - 1) / grid_w;      //+1 reserves the spare
    int atlas_w = grid_w * cell_w;
    int atlas_h = grid_h * cell_h;

    std::vector<uint8_t> atlas((size_t)atlas_w * (size_t)atlas_h, 0);

    ui_font_header header;
    memset(&header,0,sizeof(header));
    header.magic[0] = UI_FONT_MAGIC0;
    header.magic[1] = UI_FONT_MAGIC1;
    header.magic[2] = UI_FONT_MAGIC2;
    header.magic[3] = UI_FONT_MAGIC3;
    header.version          = UI_FONT_VERSION;
    header.atlas_w          = (uint32_t)atlas_w;
    header.atlas_h          = (uint32_t)atlas_h;
    header.grid_w           = (uint32_t)grid_w;
    header.grid_h           = (uint32_t)grid_h;
    header.cell_w           = (uint32_t)cell_w;
    header.cell_h           = (uint32_t)cell_h;
    header.first_char       = UI_FONT_FIRST_CHAR;
    header.num_chars        = UI_FONT_NUM_CHARS;
    header.em_px            = opt.em_px;
    header.advance_px       = advance_px;
    header.line_height_px   = line_height_px;
    header.origin_x         = origin_x;
    header.origin_y         = origin_y;
    header.onedge           = (float)opt.onedge / 255.0f;
    header.distance_range_px= 255.0f / pixel_dist_scale;
    header.pixel_offset     = (uint32_t)sizeof(ui_font_header);
    header.pixel_bytes      = (uint32_t)atlas_w * (uint32_t)atlas_h;

    //-----------------------------------------------------------------------------------------
    // Blit
    //-----------------------------------------------------------------------------------------
    int num_clipped = 0;
    for (int i = 0; i < UI_FONT_NUM_CHARS; i++){
        baked_glyph& g = glyphs[i];
        if (!g.bitmap)
            continue;

        //Through the shared helper rather than repeating the arithmetic - see UIFont.h.
        uint32_t cell_x = 0, cell_y = 0;
        if (!UIFontCell(header,(uint32_t)(UI_FONT_FIRST_CHAR + i),&cell_x,&cell_y)){
            printf("fontbake: internal error - codepoint %d has no cell\n",
                   UI_FONT_FIRST_CHAR + i);
            return 1;
        }

        int dst_x = (int)cell_x * cell_w + (int)origin_x + g.xoff;
        int dst_y = (int)cell_y * cell_h + (int)origin_y + g.yoff;

        for (int y = 0; y < g.h; y++){
            int ay = dst_y + y;
            //Cannot happen - the cell was sized from these very extents - but a silent one-pixel
            //overrun into the next cell would look like a font bug rather than a baker bug, so it
            //is counted and reported instead of trusted.
            if (ay < 0 || ay >= atlas_h){ num_clipped++; continue; }
            for (int x = 0; x < g.w; x++){
                int ax = dst_x + x;
                if (ax < 0 || ax >= atlas_w){ num_clipped++; continue; }
                atlas[(size_t)ay * (size_t)atlas_w + (size_t)ax] = g.bitmap[y * g.w + x];
            }
        }
    }

    for (int i = 0; i < UI_FONT_NUM_CHARS; i++){
        if (glyphs[i].bitmap)
            stbtt_FreeSDF(glyphs[i].bitmap,NULL);
    }

    if (num_clipped){
        printf("fontbake: %d pixels fell outside the atlas - the cell size is wrong\n",
               num_clipped);
        return 1;
    }

    /*
        The solid texel, in the first cell past the last glyph.

        The whole cell is filled rather than a single pixel, so that bilinear filtering around it
        reads the same value from every direction. A lone texel would be averaged with its
        neighbours the moment the sample landed off-centre, and a rounded rect would come out with
        its fill slightly translucent - which is exactly the kind of thing that gets blamed on the
        blend state.
    */
    {
        uint32_t solid_index = header.num_chars;
        uint32_t scell_x = solid_index % header.grid_w;
        uint32_t scell_y = solid_index / header.grid_w;
        for (uint32_t y = 0; y < header.cell_h; y++){
            uint8_t* row = &atlas[((size_t)scell_y * header.cell_h + y) * (size_t)atlas_w
                                  + (size_t)scell_x * header.cell_w];
            memset(row,255,header.cell_w);
        }
        header.solid_x = scell_x * header.cell_w + header.cell_w / 2;
        header.solid_y = scell_y * header.cell_h + header.cell_h / 2;
    }

    //The same check the loader will run, applied here so a bad bake fails at the tool rather than
    //in an app. Costs nothing and means the two agree by construction.
    size_t file_size = sizeof(ui_font_header) + atlas.size();
    if (!UIFontHeaderIsValid(header,file_size)){
        printf("fontbake: internal error - the header this produced fails its own validation\n");
        return 1;
    }

    //-----------------------------------------------------------------------------------------
    // Write
    //-----------------------------------------------------------------------------------------
    FILE* out = fopen(opt.out_path,"wb");
    if (!out){
        printf("fontbake: cannot write '%s'\n", opt.out_path);
        return 1;
    }
    bool f_wrote = (fwrite(&header,sizeof(header),1,out) == 1);
    f_wrote = f_wrote && (fwrite(atlas.data(),1,atlas.size(),out) == atlas.size());
    fclose(out);
    if (!f_wrote){
        printf("fontbake: short write on '%s'\n", opt.out_path);
        return 1;
    }

    if (opt.f_write_png){
        std::string png_path;
        if (opt.png_path){
            png_path = opt.png_path;
        }else{
            png_path = opt.out_path;
            size_t dot = png_path.find_last_of('.');
            size_t slash = png_path.find_last_of("/\\");
            if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
                png_path.resize(dot);
            png_path += ".png";
        }
        if (!stbi_write_png(png_path.c_str(),atlas_w,atlas_h,1,atlas.data(),atlas_w)){
            //Not fatal. The .fnt is the asset; the PNG is a convenience, and failing the bake
            //over it would be the tail wagging the dog.
            printf("fontbake: warning - could not write debug image '%s'\n", png_path.c_str());
        }else{
            printf("  debug image   %s\n", png_path.c_str());
        }
    }

    //-----------------------------------------------------------------------------------------
    // Report. Everything here is a number someone will want when the text looks wrong.
    //-----------------------------------------------------------------------------------------
    printf("fontbake: %s -> %s\n", opt.font_path, opt.out_path);
    printf("  atlas         %dx%d, %d cells of %dx%d (%d glyphs + 1 solid, %d spare)\n",
           atlas_w,atlas_h,grid_w * grid_h,cell_w,cell_h,
           UI_FONT_NUM_CHARS,grid_w * grid_h - UI_FONT_NUM_CHARS - 1);
    printf("  em            %.1f px  (%d of %d glyphs have ink)\n",
           opt.em_px,num_with_ink,UI_FONT_NUM_CHARS);
    printf("  advance       %.3f px    line height %.3f px\n", advance_px,line_height_px);
    printf("  pen origin    %.1f, %.1f  within the cell\n", origin_x,origin_y);
    printf("  field         +/- %d px reach, edge at %d/255, range %.2f px\n",
           opt.padding,opt.onedge,header.distance_range_px);
    printf("  solid texel   %u, %u\n", header.solid_x,header.solid_y);
    printf("  file          %zu bytes (%zu header + %zu atlas)\n",
           file_size,sizeof(ui_font_header),atlas.size());
    return 0;
}
