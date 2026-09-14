#ifndef _BINARY_ASSET_H_
#define _BINARY_ASSET_H_

#include <stddef.h>
#include <stdint.h>
#include <deque>
#include <string>
#include "miniz.h"

class BinaryAsset;

/*
    What came of asking for an asset to be let go of. See ReleaseFile in File.h, which is the
    interface this is really for.

    The three answers exist because "free this" and "read this again" are only the same request
    in a build that still has files on a disk. With assets packed into the executable - which is
    the normal way this engine ships, and the only way the Android port can work - there is no
    disk copy to go back to, and the honest answer to "drop it so I can re-read it" is that there
    is nothing to drop and nothing to re-read.
*/
enum FileRelease{
    FILE_RELEASED = 0,      //it was ours, it is freed, and the next load reads the file again
    FILE_RELEASE_FAILED,    //nothing is held under that name - nothing happened
    FILE_RELEASE_EMBEDDED   //baked into the executable: it is not ours to free, and re-reading
                            //it could only ever produce the same bytes
};

/*
    An asset, which is basically a blob of binary data with a name.
*/
class BinaryAsset{
public:
    std::string name;
    bool iscompressed = false;

    size_t size = 0;
    size_t offset = 0;  //Data offset when loaded in a contiguous memory block
    uint8_t* data = NULL;

    size_t compressed_size = 0;
    size_t compressed_offset = 0;
    uint8_t* compressed_data = NULL;

    /*
        THE BAKED-IN ASSETS, AND THE ONE THING IN THIS HEADER THAT IS AN EXTERNAL INTERFACE.

        Nothing in this engine writes these. They are defined by a GENERATED translation unit -
        BinaryAssetMemoryEmpty.cpp in a loose build, which is the empty array, or the packer's
        output in a baked one. So the field names and their meanings below are a contract with
        tools/assetpack rather than a private arrangement of this class, and renaming one breaks
        a build somewhere this file cannot see. See tools/assetpack_plan.md.

        The engine used to produce this itself, from a DUMP_BINARYASSETS build that dumped its own
        loaded assets back out as a .cpp to be compiled in on a second pass. That went on
        2026-09-14, and the reason is worth keeping: it could only ever bake what that particular
        session happened to have loaded by the time it was called, which is not the same question
        as what is in the asset tree, and only one of the two has a reproducible answer.

        `size` is the CONTENT length, but a compressed entry's blob holds size+1 bytes - the
        trailing zero StoreBinaryAsset is promised, baked in so a decompressed asset is as safe to
        hand to the GLSL compiler as a fresh disk read is. Uncompress() is the other half of that
        and subtracts the one back off.
    */
    static int num_memory_assets;
    static BinaryAsset assets[];
    /*
        Assets loaded from disk this run. A DEQUE, and the choice is load-bearing:
        GetBinaryAsset hands back &asset - a pointer INTO this container - and
        StoreBinaryAsset appends to it. A vector's push_back may reallocate, which would
        leave every pointer handed out before it dangling. That was only ever safe by
        timing: LoadFile is the one caller, and it copies out of the asset and drops the
        pointer before it can store anything new. Timing is not a property you can read off
        the code, and the next person to rearrange LoadFile has no way to know they are
        holding one.

        A deque never moves an element once it is in: push_back invalidates iterators but
        NOT references or pointers to existing elements. Nothing is ever erased from here
        either, which is the other half of why a handed-out pointer stays good for the life
        of the process. So the arrangement is correct by construction rather than by
        inspection, which is what this container is bought with - the sequential access the
        vector was giving up is all anything here does (range-for, push_back, size).
    */
    static std::deque<BinaryAsset>file_assets;

    /*
        Hands the cache a buffer read from disk. It ADOPTS the pointer - no copy is made - so
        `data` must come from malloc/calloc and the caller must forget it the moment this
        returns. `sz` is the content length, and the buffer must be sz+1 bytes with a zero in
        the last one, which is what keeps a cached asset safe to treat as a C string (GLSL
        source does) and what the packer bakes into the compressed blob - see the note on
        `assets[]` above.

        Returns the stored asset, or the one already held under that name - in which case the
        buffer passed in is freed, because the asset already here is what earlier callers are
        pointing at and it is not going to be swapped out from under them. The exception is an
        entry ReleaseBinaryAsset has emptied: that one is REFILLED in place, keeping one entry
        per name and, more to the point, keeping the entry at the same address.
    */
    static BinaryAsset* StoreBinaryAsset(const char* filename, uint8_t* data, size_t sz);
    static void ListBinaryAssets();
    static BinaryAsset* GetBinaryAsset(const char* filename);

    /*
        Frees what this table holds for `filename`, so the next load goes back to the file.

        THIS IS THE ONE THING THAT CAN INVALIDATE A POINTER LoadFile HANDED OUT - the whole rest
        of the design promises those stay good for the life of the process. Only call it for an
        asset you know nobody is still holding. Shader source is the case it exists for: Shader
        copies it into a std::string during compilation and keeps nothing.

        The entry itself is emptied, never erased, because erasing from the middle of a deque
        would move the other elements and undo exactly the guarantee file_assets is a deque for.
    */
    static FileRelease ReleaseBinaryAsset(const char* filename);

    void Uncompress();
};

#endif