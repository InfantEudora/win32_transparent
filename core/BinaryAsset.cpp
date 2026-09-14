#include <stdio.h>
#include <vector>
#include "BinaryAsset.h"

#include "Debug.h"
static Debugger *debug = new Debugger("BinaryAsset", DEBUG_ALL);

std::deque<BinaryAsset>BinaryAsset::file_assets;

//Should be called when a file was loaded from disk and we want to make it into an asset.
BinaryAsset* BinaryAsset::StoreBinaryAsset(const char* filename, uint8_t* data, size_t sz){
    //Find in file assets
    for (BinaryAsset& asset:file_assets){
        if (asset.name.compare(filename) == 0){
            if (!asset.data){
                //An entry ReleaseBinaryAsset emptied, being filled back in. Re-used rather than
                //appended to so there is still exactly one entry per name, which is what lets
                //GetBinaryAsset stop at the first match instead of looking for a later one.
                asset.data = data;
                asset.size = sz;
                return &asset;
            }
            //Already here. Whoever read the file again went round LoadFile to do it, so the only
            //question is which buffer survives, and it has to be the one already in the cache:
            //every caller served since it was stored is pointing at it.
            debug->Warn("StoreBinaryAsset: %s is already stored, keeping the copy we handed out.\n",filename);
            free(data);
            return &asset;
        }
    }

    BinaryAsset a;
    a.name = filename;
    // ADOPTED, not copied. This is the only copy of the file that will exist, and LoadFile
    // hands out a pointer to it rather than to a duplicate - see File.h on who owns what.
    // The sz+1-with-a-zero shape is the caller's promise (LoadFile calloc's it) and it is what
    // makes an asset safe to treat as a C string: GLSL source is loaded this way, and the packer
    // compresses size+1 bytes so a baked-in asset keeps the same guarantee - see BinaryAsset.h.
    a.data = data;
    a.size = sz;
    file_assets.push_back(a);
    return &file_assets.back();
}

/*
    Searched in the same order as GetBinaryAsset, and that ordering is the whole answer here: a
    packed build finds the embedded copy first and so never reads the file from disk at all, which
    is precisely why "I have released it, now re-read it" cannot mean anything there.
*/
FileRelease BinaryAsset::ReleaseBinaryAsset(const char* filename){
    for (BinaryAsset& asset:file_assets){
        if (asset.name.compare(filename) == 0){
            if (!asset.data){
                debug->Trace("ReleaseBinaryAsset: %s is already released\n",filename);
                return FILE_RELEASE_FAILED;
            }
            free(asset.data);
            asset.data = NULL;
            asset.size = 0;
            debug->Info("Released BinaryAsset %s; it will be read from file again\n",filename);
            return FILE_RELEASED;
        }
    }

    for (int i = 0;i < num_memory_assets;i++){
        if (assets[i].name.compare(filename) == 0){
            /*
                Embedded, so no. Note that this says no even once Uncompress has put a heap buffer
                in front of it: that buffer could be freed, but doing so would reclaim memory
                rather than pick up an edit - the bytes behind it are a static array in this
                executable and cannot have changed. Dropping decompressed copies under memory
                pressure is a real thing to want and a different decision from this one.
            */
            debug->Info("BinaryAsset %s is embedded in the executable - nothing to release\n",filename);
            return FILE_RELEASE_EMBEDDED;
        }
    }

    debug->Trace("ReleaseBinaryAsset: nothing held for %s\n",filename);
    return FILE_RELEASE_FAILED;
}

/*
    Everything this process can load, from the two places it can come from - kept as two lists
    rather than one, because which side a name falls on is the whole point.

    PACKED is what the BUILD shipped with: tools/assetpack's table, fixed at link time, and in a
    baked build the only source there is. It is the answer to "what is actually inside this exe",
    and nothing on disk can answer that - shared_assets/ is whatever the tree holds today, which
    is not what some exe was linked against.

    FILE is what THIS RUN read off a disk. In a baked build that list should be empty, because
    LoadFile asks GetBinaryAsset first and a packed copy always wins; a name showing up there
    anyway means the bake missed it and the exe is quietly depending on the tree it was built
    beside. That is a finding rather than noise, so it is worth a line of its own even when there
    is nothing in it.

    WHETHER A PACKED ASSET HAS BEEN USED is readable here, and it is the reason this prints more
    than names. A compressed entry is expanded on first request and Uncompress clears
    `iscompressed` as it does, so that flag still being set means nothing has asked for the asset
    YET - and "yet" is doing real work in that sentence. This is a snapshot taken when it is
    called, not a verdict: called from Init it will list assets that are merely loaded later in
    the same Init, which reads like dead weight and is not. Only a listing taken at SHUTDOWN can
    say an asset was never wanted, which is why apps/tetris/main.cpp takes one there as well.

    Read at shutdown, it earns its keep. Tetris ships twenty and finishes a session having never
    touched four: shaders/skybox.vert, shaders/skybox.frag and shaders/texture.comp, none of which
    it draws, plus fonts/consola.ttf, which is ImGui's font and so is pure weight in a USE_IMGUI=0
    build specifically. That last one is the kind of thing no makefile can tell you, because it is
    not a property of the asset tree - it is a property of this build of this app.

    An entry packed with --no-compress is the one case where that cannot be read: there is nothing
    to expand, `data` points into the blob from the start, and a request leaves no trace. Said
    plainly below rather than guessed at, so the listing never claims to know something it cannot.
*/
void BinaryAsset::ListBinaryAssets(){
    size_t packed_bytes = 0;
    int num_untouched = 0;

    debug->Info("Packed BinaryAssets - baked into the executable:\n");
    for (int i = 0;i < num_memory_assets;i++){
        BinaryAsset& asset = assets[i];
        packed_bytes += asset.compressed_size ? asset.compressed_size : asset.size;

        if (!asset.compressed_size){
            //Stored raw. Usable immediately and indistinguishable used from unused - see above.
            debug->Info("  %-34s %8zu stored\n",asset.name.c_str(),asset.size);
        }else if (asset.iscompressed){
            num_untouched++;
            debug->Info("  %-34s %8zu packed   not requested yet\n",asset.name.c_str(),asset.compressed_size);
        }else{
            debug->Info("  %-34s %8zu packed   in use, %zu bytes\n",asset.name.c_str(),asset.compressed_size,asset.size);
        }
    }
    if (num_memory_assets){
        debug->Info("  %i packed, %zu bytes, %i not requested yet\n",num_memory_assets,packed_bytes,num_untouched);
    }else{
        debug->Info("  (none - this build reads its assets from disk; see BAKE_ASSETS in engine.mk)\n");
    }

    debug->Info("File BinaryAssets - read from disk this run:\n");
    size_t file_bytes = 0;
    int num_held = 0;
    for (BinaryAsset& asset:file_assets){
        if (!asset.data){
            //ReleaseBinaryAsset emptied this one. The entry has to stay where it is (BinaryAsset.h
            //explains what the deque is guaranteeing), but it holds nothing, so say so rather than
            //list it as a zero-byte asset.
            debug->Info("  %-34s          released\n",asset.name.c_str());
            continue;
        }
        num_held++;
        file_bytes += asset.size;
        debug->Info("  %-34s %8zu bytes\n",asset.name.c_str(),asset.size);
    }
    if (num_held){
        debug->Info("  %i from disk, %zu bytes\n",num_held,file_bytes);
    }else{
        debug->Info("  (none)\n");
    }
}

void BinaryAsset::Uncompress(){
    if (iscompressed){
        debug->Info("Asset requires decompression ... \n");
        if (size > 0 || data){
            debug->Fatal("Uncompressing asset with exising data\n");
        }
        data = (uint8_t*)tinfl_decompress_mem_to_heap(compressed_data,compressed_size,&size,1500);
        // The packer compresses size+1 bytes (content + the null terminator
        // StoreBinaryAsset guarantees), so what comes back is one byte longer
        // than the logical content -- correct for that so `size` keeps meaning
        // "content length" everywhere else, same as the disk-read path.
        //
        // THIS IS ONE HALF OF A CONTRACT WITH tools/assetpack, which has no way
        // to check it: a packer that compressed only `size` bytes would leave
        // every asset here one byte short, and the only thing that would notice
        // is a shader losing its last character. See BinaryAsset.h.
        size -= 1;
        iscompressed = false;
        debug->Info(" Done.\n");
    }
}

BinaryAsset* BinaryAsset::GetBinaryAsset(const char* filename){
    //Find in file assets
    for (BinaryAsset& asset:file_assets){
        if (asset.name.compare(filename) == 0){
            if (!asset.data){
                //Released, and not read again since. The ENTRY stays (emptying it in place is
                //what keeps every other element of the deque where it is), but it is no longer an
                //answer to anything: say we have nothing so the caller goes back to the file, and
                //StoreBinaryAsset will fill this same entry back in. Names are unique here, so
                //there is no second one to keep looking for.
                debug->Trace("BinaryAsset %s was released; going back to the file\n",asset.name.c_str());
                break;
            }
            debug->Info("Got BinaryAsset %s from cache\n",asset.name.c_str());
            //A pointer into file_assets, which outlives every caller. That it survives the
            //next StoreBinaryAsset is the deque's doing - see BinaryAsset.h.
            return &asset;
        }
    }

    for (int i =0;i<num_memory_assets;i++){
        BinaryAsset* asset = &assets[i];
        if (asset->name.compare(filename) == 0){
            debug->Info("Got BinaryAsset %s from memory\n",asset->name.c_str());
            asset->Uncompress();
            return asset;
        }
    }
    return NULL;
}
