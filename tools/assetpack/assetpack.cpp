/*
    assetpack - walks an app's asset roots and bakes what it finds into the table the engine
    already reads. Step 2 of tools/assetpack_plan.md; this file currently does the walk, the
    naming and the shadow report, and --list is the only thing it can be asked to do.

        cd tools/assetpack && mingw32-make.exe -j8
        ./build/assetpack.exe --list ../../shared_assets
        ./build/assetpack.exe --help

    It replaces the engine's own DUMP_BINARYASSETS self-dump, removed 2026-09-14. The reason that
    had to go is the reason this walks a directory: the dump could only ever bake what the running
    process happened to have loaded by the time it was called, which is not the same question as
    what is in the asset tree, and only one of the two has a reproducible answer.

    --- WHAT AN ASSET IS CALLED, AND WHY IT IS NOT NEGOTIABLE ------------------------------------
    An asset's name is its path relative to the root it was found under, with forward slashes:
    "shaders/default.vert", "meshes/tank.glb", "fonts/consola.ttf". That is exactly the string the
    app passes to LoadFile, and a baked lookup is a string compare against it (BinaryAsset.cpp,
    GetBinaryAsset). Get the name wrong and nothing fails loudly - the lookup simply misses and a
    loose build falls through to the disk, so it only breaks once there is no disk.

    Two mechanical details carry that, both learned on the Android port:
      - lexically_relative strips the iterator's "./" prefix.
      - generic_string() forces forward slashes. Without it a name packed on Windows carries
        backslashes and never matches a lookup.

    --- FIRST ROOT WINS, AND THE PORT DOES THE OPPOSITE ------------------------------------------
    Roots are given in the same order the app's main.cpp declares them, app first, and the FIRST
    root to supply a name is the one that is packed. That matches the runtime exactly:
    AddAssetSearchRoot appends, ResolveAgainstRoots returns the first match, and every app declares
    its own root before shared_assets precisely so its own copy of a shared shader wins.

    C:/code/android/tools/pack_assets.cpp documents the opposite rule ("a later dir's file wins").
    Taking it would make a baked build resolve overrides the other way round from a loose build of
    the same app - no error, no log line, just a different shader. Hence the shadow report below:
    every name a later root also had is printed, so an override is visible at pack time rather than
    inferred from a screenshot.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
#include <type_traits>
#include <filesystem>

/*
    miniz arrives THROUGH BinaryAsset.h, and must not also be included directly here.

    3rdparty/miniz/miniz.h has no include guard - no #pragma once, no MINIZ_HEADER_INCLUDED - so a
    translation unit that includes it twice, once directly and once transitively, fails to compile
    with a wall of "conflicts with a previous declaration" on its enums. core/BinaryAsset.cpp gets
    tdefl_compress_mem_to_heap the same way, so this is the arrangement the engine already relies
    on rather than a dodge invented here.
*/

/*
    Included for the CONTRACT, not for any code. Nothing here constructs a BinaryAsset - the
    generated file does, with designated initialisers naming these fields - so this header is the
    one place the tool and the engine have to agree, and the asserts below are the only part of
    that agreement a compiler can check for us. See core/BinaryAsset.h, which says the same thing
    from the other side.
*/
#include "BinaryAsset.h"

static_assert(std::is_same<decltype(BinaryAsset::name),std::string>::value,
    "BinaryAsset::name changed type - the generated table writes a string literal into it");
static_assert(std::is_same<decltype(BinaryAsset::iscompressed),bool>::value,
    "BinaryAsset::iscompressed changed type");
static_assert(std::is_same<decltype(BinaryAsset::size),size_t>::value,
    "BinaryAsset::size changed type - the generated table writes %zu into it");
static_assert(std::is_same<decltype(BinaryAsset::compressed_size),size_t>::value,
    "BinaryAsset::compressed_size changed type");
static_assert(std::is_same<decltype(BinaryAsset::data),uint8_t*>::value,
    "BinaryAsset::data changed type - the generated table casts the blob to this");
static_assert(std::is_same<decltype(BinaryAsset::compressed_data),uint8_t*>::value,
    "BinaryAsset::compressed_data changed type");

namespace fs = std::filesystem;

/*
    THE FLAGS MUST MATCH core/BinaryAsset.cpp's Uncompress(), which passes 1500 to
    tinfl_decompress_mem_to_heap. 1500 is below 0x1000 (TDEFL_WRITE_ZLIB_HEADER), so both ends are
    RAW DEFLATE with no zlib header, and the low bits are the probe count - the compression effort.

    If these two ever disagree the failure is not subtle but it is remote from its cause: every
    baked asset inflates to nothing and the app dies on its first shader. Written here rather than
    passed as an option for that reason - it is not a knob, it is the other half of a format.
*/
static const int DEFLATE_FLAGS = 1500;

//The blob symbol the generated .S defines and the generated .cpp points into. Prefixed rather than
//named 'asset_data' like the old self-dump's static array, because this one is a GLOBAL: it has to
//be visible across the two translation units, so its name is in the link namespace of every app.
static const char* BLOB_SYMBOL = "assetpack_blob";

//---------------------------------------------------------------------------------------------
// Options
//---------------------------------------------------------------------------------------------
struct options{
    std::vector<std::string> roots;         //in declaration order; first to supply a name wins
    std::vector<std::string> include_globs;
    std::vector<std::string> exclude_globs;
    std::string out_dir     = ".";
    bool        f_compress  = true;
    bool        f_list_only = false;
};

static void PrintUsage(const char* argv0){
    fprintf(stderr,
        "Usage: %s [options] <root> [<root> ...]\n"
        "\n"
        "  Roots are given in the SAME ORDER the app's main.cpp declares them, app first.\n"
        "  The first root to supply a name is the one packed; later ones are reported as\n"
        "  shadowed.\n"
        "\n"
        "  -o <dir>           where to write; default \".\"\n"
        "  --exclude <glob>   skip matching asset names; repeatable\n"
        "  --include <glob>   if given, pack ONLY matching asset names; repeatable\n"
        "  --no-compress      store raw, for measuring and for already-compressed data\n"
        "  --list             walk and report, write nothing\n"
        "  --help\n"
        "\n"
        "  Globs match the ASSET NAME, not the disk path, and '*' crosses '/' - so\n"
        "  --exclude \"www/*\" drops that whole subtree. '?' is one character.\n",
        argv0);
}

//---------------------------------------------------------------------------------------------
// Glob matching
//
// '*' deliberately crosses '/', unlike a shell. The names being matched are asset names whose
// first segment is a category, so the thing anyone actually wants to write is "www/*" or
// "*.wav" - a shell's rule would make the first of those match nothing useful and need "www/**"
// instead, which is a second syntax to explain for no gain here.
//
// Recursive rather than the usual two-pointer backtracking loop: the strings are short, this runs
// once per file in an offline tool, and the recursive form is the one that is obviously correct.
//---------------------------------------------------------------------------------------------
static bool GlobMatch(const char* pattern, const char* text){
    if (*pattern == '\0'){
        return *text == '\0';
    }
    if (*pattern == '*'){
        //Match nothing here, or one more character and try again.
        if (GlobMatch(pattern + 1,text)){
            return true;
        }
        return (*text != '\0') && GlobMatch(pattern,text + 1);
    }
    if (*text == '\0'){
        return false;
    }
    if ((*pattern == '?') || (*pattern == *text)){
        return GlobMatch(pattern + 1,text + 1);
    }
    return false;
}

static bool MatchesAny(const std::vector<std::string>& globs, const std::string& name){
    for (const std::string& g:globs){
        if (GlobMatch(g.c_str(),name.c_str())){
            return true;
        }
    }
    return false;
}

//---------------------------------------------------------------------------------------------
// The walk
//---------------------------------------------------------------------------------------------
struct asset_entry{
    std::string name;           //what LoadFile will ask for
    std::string path;           //where it actually is, for opening
    std::string root;           //which root answered, for the report
    uintmax_t   size = 0;
};

struct shadowed_entry{
    std::string name;
    std::string kept_root;      //the root whose copy is packed
    std::string hidden_root;    //the root whose copy is not
};

/*
    Sorted, and that is not cosmetic. recursive_directory_iterator yields in whatever order the
    filesystem hands back, so an unsorted walk would put assets at different offsets in the blob
    from one run to the next and make two packs of identical inputs differ byte for byte. A pack
    that is reproducible is worth having for its own sake, and it is the only way a "did the assets
    actually change?" check can ever be cheap.
*/
static bool WalkRoot(const std::string& root, std::vector<asset_entry>& out){
    std::error_code ec;
    fs::path root_path = fs::absolute(root,ec);
    if (ec){
        fprintf(stderr,"assetpack: cannot resolve root '%s': %s\n",root.c_str(),ec.message().c_str());
        return false;
    }
    if (!fs::is_directory(root_path,ec)){
        //Named but absent is an error, never an empty walk. A mistyped root that silently packed
        //nothing would produce a build whose assets are simply missing, and the first thing anyone
        //would suspect is the engine.
        fprintf(stderr,"assetpack: root does not exist or is not a directory: %s\n",root_path.string().c_str());
        return false;
    }

    for (fs::recursive_directory_iterator it(root_path,ec), end;(it != end) && !ec;it.increment(ec)){
        if (!it->is_regular_file(ec)){
            continue;
        }
        asset_entry e;
        e.name = it->path().lexically_relative(root_path).generic_string();
        e.path = it->path().string();
        e.root = root;
        e.size = it->file_size(ec);
        if (ec){
            fprintf(stderr,"assetpack: cannot size %s: %s\n",e.path.c_str(),ec.message().c_str());
            return false;
        }
        out.push_back(e);
    }
    if (ec){
        fprintf(stderr,"assetpack: error walking %s: %s\n",root_path.string().c_str(),ec.message().c_str());
        return false;
    }

    std::sort(out.begin(),out.end(),[](const asset_entry& a, const asset_entry& b){
        return a.name < b.name;
    });
    return true;
}

//---------------------------------------------------------------------------------------------
// The writer
//---------------------------------------------------------------------------------------------

//What ends up in the table for one asset, once its bytes are in the blob.
struct baked_entry{
    std::string name;
    bool        f_compressed = false;
    size_t      content_size = 0;       //length WITHOUT the trailing zero - what LoadFile reports
    size_t      stored_size  = 0;       //bytes occupying the blob
    size_t      offset       = 0;       //where they start in the blob
};

static bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out){
    FILE* f = fopen(path.c_str(),"rb");
    if (!f){
        fprintf(stderr,"assetpack: cannot open %s\n",path.c_str());
        return false;
    }
    fseek(f,0,SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz < 0){
        fclose(f);
        fprintf(stderr,"assetpack: cannot size %s\n",path.c_str());
        return false;
    }
    /*
        sz+1 with a zero in the last byte. THIS IS THE HALF OF THE CONTRACT THE ENGINE CANNOT
        CHECK - see core/BinaryAsset.cpp's Uncompress(), which inflates size+1 and subtracts the
        one back off, and core/File.cpp's LoadFile, which callocs sz+1 on the disk path for the
        same reason. The terminator is what makes an asset safe to hand to the GLSL compiler as a
        C string. Compress size+1 and shaders work; compress size and only shaders break, only in
        baked builds.
    */
    out.assign((size_t)sz + 1,0);
    if (sz > 0){
        size_t got = fread(out.data(),1,(size_t)sz,f);
        if (got != (size_t)sz){
            fclose(f);
            fprintf(stderr,"assetpack: short read on %s (%zu of %ld)\n",path.c_str(),got,sz);
            return false;
        }
    }
    fclose(f);
    return true;
}

//C string literal escaping for the generated table. Asset names are built by generic_string() and
//so contain neither quote nor backslash today - this exists so that a name which someday does
//cannot quietly emit a .cpp that will not compile, or worse, one that compiles differently.
static std::string EscapeForC(const std::string& s){
    std::string out;
    for (char c:s){
        if ((c == '"') || (c == '\\')){
            out += '\\';
        }
        out += c;
    }
    return out;
}

static bool WritePack(const options& opt, const std::vector<asset_entry>& packed){
    std::error_code ec;
    fs::path out_dir = fs::absolute(opt.out_dir,ec);
    if (ec){
        fprintf(stderr,"assetpack: cannot resolve -o '%s': %s\n",opt.out_dir.c_str(),ec.message().c_str());
        return false;
    }
    fs::create_directories(out_dir,ec);     //ec ignored: the open below is the real test

    fs::path bin_path   = out_dir / "assets.bin";
    fs::path s_path     = out_dir / "assets.S";
    fs::path table_path = out_dir / "BinaryAssetMemory.cpp";

    std::vector<uint8_t>    blob;
    std::vector<baked_entry> baked;
    size_t raw_total = 0;

    for (const asset_entry& e:packed){
        std::vector<uint8_t> content;       //content + the trailing zero
        if (!ReadWholeFile(e.path,content)){
            return false;
        }

        baked_entry b;
        b.name         = e.name;
        b.content_size = content.size() - 1;
        raw_total += b.content_size;

        /*
            Aligned to 8. Compressed entries do not need it - tinfl writes to a fresh malloc, which
            is aligned for anything - but a RAW entry is handed to the caller as a pointer straight
            into this blob, and some of what reads an asset (the glTF binary chunk in particular)
            expects its data to be at least 4-byte aligned. Eight costs a handful of padding bytes
            across the whole pack and removes the question.
        */
        while ((blob.size() % 8) != 0){
            blob.push_back(0);
        }
        b.offset = blob.size();

        if (opt.f_compress){
            size_t clen = 0;
            void* cdata = tdefl_compress_mem_to_heap(content.data(),content.size(),&clen,DEFLATE_FLAGS);
            if (!cdata){
                fprintf(stderr,"assetpack: deflate failed for %s\n",e.name.c_str());
                return false;
            }
            b.f_compressed = true;
            b.stored_size  = clen;
            blob.insert(blob.end(),(uint8_t*)cdata,(uint8_t*)cdata + clen);
            free(cdata);
        }else{
            /*
                The trailing zero goes into the blob too, and `content_size` stays the length
                without it, so a raw baked asset keeps exactly the C-string guarantee a compressed
                one does. The old self-dump's raw branch wrote only `size` bytes and would have
                lost that - it never mattered because that branch was unreachable (`if (1)`), but
                it is the sort of thing that is only ever found by a shader losing its last line.
            */
            b.f_compressed = false;
            b.stored_size  = content.size();
            blob.insert(blob.end(),content.begin(),content.end());
        }
        baked.push_back(b);
    }

    //--- the blob ---------------------------------------------------------------------------
    FILE* fb = fopen(bin_path.string().c_str(),"wb");
    if (!fb){
        fprintf(stderr,"assetpack: cannot write %s\n",bin_path.string().c_str());
        return false;
    }
    if (!blob.empty() && (fwrite(blob.data(),1,blob.size(),fb) != blob.size())){
        fclose(fb);
        fprintf(stderr,"assetpack: short write on %s\n",bin_path.string().c_str());
        return false;
    }
    fclose(fb);

    //--- the assembler stub -----------------------------------------------------------------
    /*
        .incbin takes a path resolved by the ASSEMBLER, whose working directory is whatever make
        happened to be in - not this directory. So the path is written ABSOLUTE and with forward
        slashes: gas on Windows accepts '/' and would read '\' as an escape.

        Absolute is safe here in a way it would not be in a checked-in file, because this file is
        generated output regenerated whenever the assets change. Moving the tree leaves a stale
        .S naming a path that no longer exists, which fails loudly at assembly time rather than
        producing a wrong build. The alternative is -Wa,-I<dir> on the app's compile line, which
        works too and is one more thing for an app makefile to get right.
    */
    FILE* fs_out = fopen(s_path.string().c_str(),"w");
    if (!fs_out){
        fprintf(stderr,"assetpack: cannot write %s\n",s_path.string().c_str());
        return false;
    }
    fprintf(fs_out,"/* Generated by tools/assetpack - do not edit. */\n");
    fprintf(fs_out,"    .section .rodata\n");
    fprintf(fs_out,"    .balign 16\n");
    fprintf(fs_out,"    .globl %s\n",BLOB_SYMBOL);
    fprintf(fs_out,"%s:\n",BLOB_SYMBOL);
    if (!blob.empty()){
        fprintf(fs_out,"    .incbin \"%s\"\n",bin_path.generic_string().c_str());
    }
    fprintf(fs_out,"    .globl %s_end\n",BLOB_SYMBOL);
    fprintf(fs_out,"%s_end:\n",BLOB_SYMBOL);
    fclose(fs_out);

    //--- the table --------------------------------------------------------------------------
    FILE* ft = fopen(table_path.string().c_str(),"w");
    if (!ft){
        fprintf(stderr,"assetpack: cannot write %s\n",table_path.string().c_str());
        return false;
    }
    fprintf(ft,"/*\n");
    fprintf(ft,"    Generated by tools/assetpack - do not edit.\n\n");
    fprintf(ft,"    This file DEFINES BinaryAsset::assets[] and ::num_memory_assets, replacing\n");
    fprintf(ft,"    BinaryAssetMemoryEmpty.cpp in a baked build. The bytes live in assets.bin,\n");
    fprintf(ft,"    pulled in by assets.S - all three are one unit and none is useful alone.\n");
    fprintf(ft,"*/\n");
    fprintf(ft,"#include \"BinaryAsset.h\"\n\n");
    fprintf(ft,"extern \"C\" const unsigned char %s[];\n\n",BLOB_SYMBOL);
    fprintf(ft,"int BinaryAsset::num_memory_assets = %zu;\n\n",baked.size());
    fprintf(ft,"BinaryAsset BinaryAsset::assets[] = {\n");
    for (const baked_entry& b:baked){
        fprintf(ft,"    {\n");
        fprintf(ft,"        .name = \"%s\",\n",EscapeForC(b.name).c_str());
        fprintf(ft,"        .iscompressed = %s,\n",b.f_compressed ? "true" : "false");
        if (b.f_compressed){
            /*
                size and data MUST be zero and NULL for a compressed entry: Uncompress() treats
                anything else as an asset that has already been decompressed and calls Fatal. The
                content length is not stored at all - tinfl recovers it, and Uncompress subtracts
                the trailing zero back off.
            */
            fprintf(ft,"        .size = 0,\n");
            fprintf(ft,"        .offset = 0,\n");
            fprintf(ft,"        .data = NULL,\n");
            fprintf(ft,"        .compressed_size = %zu,\n",b.stored_size);
            fprintf(ft,"        .compressed_offset = %zu,\n",b.offset);
            fprintf(ft,"        .compressed_data = (uint8_t*)&%s[%zu]\n",BLOB_SYMBOL,b.offset);
        }else{
            fprintf(ft,"        .size = %zu,\n",b.content_size);
            fprintf(ft,"        .offset = %zu,\n",b.offset);
            fprintf(ft,"        .data = (uint8_t*)&%s[%zu],\n",BLOB_SYMBOL,b.offset);
            fprintf(ft,"        .compressed_size = 0,\n");
            fprintf(ft,"        .compressed_offset = 0,\n");
            fprintf(ft,"        .compressed_data = NULL\n");
        }
        fprintf(ft,"    },\n");
    }
    fprintf(ft,"};\n");
    fclose(ft);

    //--- what happened ----------------------------------------------------------------------
    printf("\n%-52s %10s %10s\n","BAKED","STORED","OF RAW");
    for (const baked_entry& b:baked){
        double pct = (b.content_size > 0) ? (100.0 * (double)b.stored_size / (double)b.content_size) : 0.0;
        //Flagged where compression made it bigger, which is what already-compressed data does.
        //Not acted on - per-file choice is a later optimisation (tools/assetpack_plan.md) - but
        //this is the measurement that optimisation would be decided from.
        printf("%-52s %10zu %9.0f%%%s\n",b.name.c_str(),b.stored_size,pct,
            (b.stored_size > b.content_size) ? "  <- bigger" : "");
    }
    printf("\n%zu assets: %zu bytes raw -> %zu bytes in the blob (%.0f%%)\n",
        baked.size(),raw_total,blob.size(),
        (raw_total > 0) ? (100.0 * (double)blob.size() / (double)raw_total) : 0.0);
    printf("Wrote %s\n      %s\n      %s\n",
        bin_path.string().c_str(),s_path.string().c_str(),table_path.string().c_str());
    return true;
}

int main(int argc, char** argv){
    options opt;

    for (int i = 1;i < argc;i++){
        std::string arg = argv[i];
        //A helper so the three flags that take a value all fail the same way rather than reading
        //past the end of argv when someone leaves the value off.
        auto TakeValue = [&](const char* flag)->const char*{
            if (i + 1 >= argc){
                fprintf(stderr,"assetpack: %s needs a value\n",flag);
                return NULL;
            }
            return argv[++i];
        };

        if ((arg == "--help") || (arg == "-h")){
            PrintUsage(argv[0]);
            return 0;
        }else if (arg == "-o"){
            const char* v = TakeValue("-o");
            if (!v){
                return 1;
            }
            opt.out_dir = v;
        }else if (arg == "--exclude"){
            const char* v = TakeValue("--exclude");
            if (!v){
                return 1;
            }
            opt.exclude_globs.push_back(v);
        }else if (arg == "--include"){
            const char* v = TakeValue("--include");
            if (!v){
                return 1;
            }
            opt.include_globs.push_back(v);
        }else if (arg == "--no-compress"){
            opt.f_compress = false;
        }else if (arg == "--list"){
            opt.f_list_only = true;
        }else if (!arg.empty() && (arg[0] == '-')){
            fprintf(stderr,"assetpack: unknown option '%s'\n",arg.c_str());
            PrintUsage(argv[0]);
            return 1;
        }else{
            opt.roots.push_back(arg);
        }
    }

    if (opt.roots.empty()){
        fprintf(stderr,"assetpack: no asset roots given\n");
        PrintUsage(argv[0]);
        return 1;
    }

    /*
        Collected root by root, then merged, rather than one flat walk. The merge is where the
        precedence rule lives and it wants to see which root each name came from, which a single
        walk would have thrown away.
    */
    std::vector<asset_entry>    packed;
    std::vector<shadowed_entry> shadowed;
    std::vector<std::string>    filtered;      //dropped by --include/--exclude, reported as a count

    for (const std::string& root:opt.roots){
        std::vector<asset_entry> found;
        if (!WalkRoot(root,found)){
            return 1;
        }

        for (const asset_entry& e:found){
            //Filtering before the precedence check, deliberately: an excluded file is not an asset
            //at all, so it must not shadow a copy of the same name in a later root either.
            if (!opt.include_globs.empty() && !MatchesAny(opt.include_globs,e.name)){
                filtered.push_back(e.name);
                continue;
            }
            if (MatchesAny(opt.exclude_globs,e.name)){
                filtered.push_back(e.name);
                continue;
            }

            bool f_already = false;
            for (const asset_entry& kept:packed){
                if (kept.name == e.name){
                    shadowed.push_back({e.name,kept.root,e.root});
                    f_already = true;
                    break;
                }
            }
            if (!f_already){
                packed.push_back(e);
            }
        }
    }

    //Sorted across roots too, so the final order does not depend on which root a name came from.
    std::sort(packed.begin(),packed.end(),[](const asset_entry& a, const asset_entry& b){
        return a.name < b.name;
    });

    uintmax_t total = 0;
    for (const asset_entry& e:packed){
        total += e.size;
    }

    printf("Roots, in precedence order:\n");
    for (size_t i = 0;i < opt.roots.size();i++){
        printf("  %zu. %s\n",i + 1,opt.roots[i].c_str());
    }
    printf("\n%-52s %10s  %s\n","ASSET","BYTES","FROM");
    for (const asset_entry& e:packed){
        printf("%-52s %10ju  %s\n",e.name.c_str(),e.size,e.root.c_str());
    }
    printf("\n%zu assets, %ju bytes\n",packed.size(),total);

    if (!filtered.empty()){
        printf("%zu excluded by --include/--exclude\n",filtered.size());
    }

    /*
        The shadow report. Not a warning - an override is a supported and deliberate thing to do,
        and apps/ship exists to do it - but it is printed every time, because the one way this
        mechanism goes wrong is silently: a name shadowed by accident looks exactly like a name
        shadowed on purpose, and neither is visible in the packed output.
    */
    if (!shadowed.empty()){
        printf("\n%zu name(s) supplied by more than one root - FIRST WINS:\n",shadowed.size());
        for (const shadowed_entry& s:shadowed){
            printf("  %-46s packed from %s, shadowing %s\n",s.name.c_str(),s.kept_root.c_str(),s.hidden_root.c_str());
        }
    }

    if (opt.f_list_only){
        return 0;
    }

    if (!WritePack(opt,packed)){
        return 1;
    }
    return 0;
}
