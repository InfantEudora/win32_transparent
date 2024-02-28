#ifndef _ASSET_H_
#define _ASSET_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>

class Asset;

class Asset{
public:
    std::string name;
    size_t size = 0;
    size_t offset = 0;
    uint8_t* data = NULL;

    static int num_memory_assets;
    static Asset assets[];
    static std::vector<Asset>file_assets;
    static Asset* FindAsset(const char* filename);
    static void StoreAsset(const char* filename, uint8_t* data, size_t sz);
    static void ListAssets();
    static void DumpAssets();
    static Asset* GetAsset(const char* filename);
};

#endif