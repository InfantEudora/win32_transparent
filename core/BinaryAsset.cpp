#include <stdio.h>
#include "BinaryAsset.h"

#include "Debug.h"
static Debugger *debug = new Debugger("BinaryAsset", DEBUG_ALL);

std::vector<BinaryAsset>BinaryAsset::file_assets;

//Should be called when a file was loaded from disk and we want to make it into an asset.
void BinaryAsset::StoreBinaryAsset(const char* filename, uint8_t* data, size_t sz){
    //Find in file assets
    for (BinaryAsset& asset:file_assets){
        if (asset.name.compare(filename) == 0){
            //Already marked. File loaded twice?
            debug->Warn("Reloaded previously marked asset.\n");
            return;
        }
    }

    BinaryAsset a;
    a.name = filename;
    uint8_t* local_copy = new uint8_t[sz];
    memcpy(local_copy,data,sz);
    a.data = local_copy;
    a.size = sz;
    file_assets.push_back(a);
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
        iscompressed = false;
        debug->Info(" Done.\n");
    }
}

//Test miniz compression of the first binary asset
/*
void BinaryAsset::DeflateBinaryAssets(){
    for (BinaryAsset& asset:file_assets){
        //if (asset.name.compare("data/hand.glb") == 0){
            uint32_t crc_initial = mz_crc32(MZ_CRC32_INIT,asset.data,asset.size);
            debug->Info("Deflating %s. Uncompressed size: %i kB (%i Bytes) CRC = %08X\n",asset.name.c_str(),asset.size / 1024,asset.size,crc_initial);


            size_t compressed_size = 0;
            uint8_t* compressed_data = (uint8_t*)tdefl_compress_mem_to_heap(asset.data,asset.size,&compressed_size,1500);
            debug->Info("Done. Compressed size: %zu kB (%zu Bytes)\n",compressed_size / 1024,compressed_size);

            uint8_t* data = (uint8_t*)malloc(assets->size);
            debug->Info("Uncompressing...\n");
            size_t uncompressed_size = 0;
            uint8_t* uncompressed_data = (uint8_t*)tinfl_decompress_mem_to_heap(compressed_data,compressed_size,&uncompressed_size,1500);

            uint32_t crc_final = mz_crc32(MZ_CRC32_INIT,uncompressed_data,uncompressed_size);
            debug->Info("Done. Uncompressed size: %zu kB (%zu Bytes)CRC = %08X\n",uncompressed_size / 1024,uncompressed_size,crc_final);

        //}
    }
}*/

BinaryAsset* BinaryAsset::GetBinaryAsset(const char* filename){
    //Find in file assets
    for (BinaryAsset& asset:file_assets){
        if (asset.name.compare(filename) == 0){
            debug->Info("Got BinaryAsset %s from cache\n",asset.name.c_str());
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

#ifdef DUMP_BINARYASSETS
void BinaryAsset::DumpBinaryAssets(){
    int num_file_assets = file_assets.size();
    if (num_file_assets == 0){
        return;
    }

    FILE* file;
	size_t sz = 0;
	file = fopen("BinaryAssetMemory.cpp", "wb");
    if(!file){
		debug->Fatal("DumpBinaryAssets failed.\n");
		return;
	}
    fprintf(file,"#include \"BinaryAsset.h\"\n");
    fprintf(file,"int BinaryAsset::num_memory_assets = %i;\n",num_file_assets);
    fprintf(file,"static const uint8_t asset_data[] = {\n");

    //Build the binary blob, keep track off assets and offsets.
    size_t offset = 0;
    for (BinaryAsset& asset:file_assets){
        debug->Info("Dump BinaryAsset: %s\n",asset.name.c_str());
        //We compress them
        if (1){
            if (asset.iscompressed){
                debug->Fatal("Duping an already compressed asset?\n");
            }
            asset.compressed_data = (uint8_t*)tdefl_compress_mem_to_heap(asset.data,asset.size,&asset.compressed_size,1500);
            float ratio = (100.0f / asset.size) * asset.compressed_size;
            debug->Info(" Compressed from %zu to %zu (%.0f%%)\n",asset.size,asset.compressed_size,ratio);
            asset.iscompressed = true;
            asset.compressed_offset = offset;
            for (size_t i=0;i<asset.compressed_size;i++){
                fprintf(file,"0x%02X,",asset.compressed_data[i]);
                offset++;
            }
        }else{
            asset.offset = offset;
            for (size_t i=0;i<asset.size;i++){
                fprintf(file,"0x%02X,",asset.data[i]);
                offset++;
            }
        }
    }
    fprintf(file,"};\n");
    fprintf(file,"BinaryAsset BinaryAsset::assets[] = {\n");
    for (BinaryAsset& asset:file_assets){
        fprintf(file,"{\n");
        fprintf(file,".name=\"%s\",\n",asset.name.c_str());
        fprintf(file,".iscompressed=%d,\n",asset.iscompressed);
        if (asset.iscompressed){
            fprintf(file,".size = 0,\n");
            fprintf(file,".offset = 0,\n");
            fprintf(file,".data = NULL,\n");
            fprintf(file,".compressed_size = %zu,\n",asset.compressed_size);
            fprintf(file,".compressed_offset = %zu,\n",asset.compressed_offset);
            fprintf(file,".compressed_data = (uint8_t*)&asset_data[%zu]\n",asset.compressed_offset);
        }else{
            fprintf(file,".size=%zu,\n",asset.size);
            fprintf(file,".offset=%zu,\n",asset.offset);
            fprintf(file,".data = (uint8_t*)&asset_data[%zu],\n",asset.offset);
            fprintf(file,".compressed_size = 0,\n");
            fprintf(file,".compressed_offset = 0,\n");
            fprintf(file,".compressed_data = NULL\n");
        }
        fprintf(file,"},\n");
    }
    fprintf(file,"};\n");
    fclose(file);
}
#else
void BinaryAsset::DumpBinaryAssets(){
    ListBinaryAssets();
    //DeflateBinaryAssets();
}
#endif