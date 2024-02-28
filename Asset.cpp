#include <stdio.h>
#include "Asset.h"

#include "Debug.h"
static Debugger *debug = new Debugger("Asset", DEBUG_ALL);

std::vector<Asset>Asset::file_assets;

//Should be called when a file was loaded from disk and we want to make it into an asset.
void Asset::StoreAsset(const char* filename, uint8_t* data, size_t sz){
    //Find in file assets
    for (Asset& asset:file_assets){
        if (asset.name.compare(filename) == 0){
            //Already marked. File loaded twice?
            debug->Warn("Reloaded previously marked asset.\n");
            return;
        }
    }

    Asset a;
    a.name = filename;
    uint8_t* local_copy = new uint8_t[sz];
    memcpy(local_copy,data,sz);
    a.data = local_copy;
    a.size = sz;
    file_assets.push_back(a);
}

void Asset::ListAssets(){
    debug->Info("Used Assets:\n");
    for (Asset& asset:file_assets){
        debug->Info("Asset: %s\n",asset.name.c_str());
    }
}

Asset* Asset::GetAsset(const char* filename){
    for (int i =0;i<num_memory_assets;i++){
        Asset* asset = &assets[i];
        if (asset->name.compare(filename) == 0){
            debug->Info("Got Asset %s from memory\n",asset->name.c_str());
            return asset;
        }
    }
    return NULL;
}

#ifdef DUMP_ASSETS
void Asset::DumpAssets(){
    int num_file_assets = file_assets.size();
    if (num_file_assets == 0){
        return;
    }

    FILE* file;
	size_t sz = 0;
	file = fopen("AssetMemory.cpp", "wb");
    if(!file){
		debug->Fatal("DumpAssets failed.\n");
		return;
	}
    fprintf(file,"#include \"Asset.h\"\n");
    fprintf(file,"int Asset::num_memory_assets = %i;\n",num_file_assets);
    fprintf(file,"static const uint8_t asset_data[] = {\n");

    //Build the binary blob, keep track off assets and offsets.
    size_t offset = 0;
    for (Asset& asset:file_assets){
        debug->Info("Dump Asset: %s\n",asset.name.c_str());
        asset.offset = offset;
        for (size_t i=0;i<asset.size;i++){
            fprintf(file,"0x%02X,",asset.data[i]);
            offset++;
        }
    }
    fprintf(file,"};\n");
    fprintf(file,"Asset Asset::assets[] = {\n");
    for (Asset& asset:file_assets){
        fprintf(file,"{\n");
        fprintf(file,".name=\"%s\",\n",asset.name.c_str());
        fprintf(file,".size=%zu,\n",asset.size);
        fprintf(file,".offset=%zu,\n",asset.offset);
        fprintf(file,".data = (uint8_t*)&asset_data[%zu]\n",asset.offset);
        fprintf(file,"},\n");
    }
    fprintf(file,"};\n");
    fclose(file);
}
#else
void Asset::DumpAssets(){
    ListAssets();
}
#endif