#ifndef _BINARY_ASSET_H_
#define _BINARY_ASSET_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>

class BinaryAsset;

/*
    An asset, which is basically a blob of binary data with a name.
*/
class BinaryAsset{
public:
    std::string name;
    size_t size = 0;
    size_t offset = 0;
    uint8_t* data = NULL;

    static int num_memory_assets;
    static BinaryAsset assets[];
    static std::vector<BinaryAsset>file_assets;
    static BinaryAsset* FindBinaryAsset(const char* filename);
    static void StoreBinaryAsset(const char* filename, uint8_t* data, size_t sz);
    static void ListBinaryAssets();
    static void DumpBinaryAssets();
    static BinaryAsset* GetBinaryAsset(const char* filename);
};

#endif