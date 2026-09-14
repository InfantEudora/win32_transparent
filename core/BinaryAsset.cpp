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

void BinaryAsset::ListBinaryAssets(){
    debug->Info("Used BinaryAssets:\n");
    for (BinaryAsset& asset:file_assets){
        debug->Info("BinaryAsset: %s\n",asset.name.c_str());
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
