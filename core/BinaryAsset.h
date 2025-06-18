#ifndef _BINARY_ASSET_H_
#define _BINARY_ASSET_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>
#include "miniz.h"

class BinaryAsset;

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

    static int num_memory_assets;
    static BinaryAsset assets[];
    static std::vector<BinaryAsset>file_assets;
    static BinaryAsset* FindBinaryAsset(const char* filename);
    static void StoreBinaryAsset(const char* filename, uint8_t* data, size_t sz);
    static void ListBinaryAssets();
    static void DumpBinaryAssets();
    static BinaryAsset* GetBinaryAsset(const char* filename);

    void Uncompress();
};

#endif