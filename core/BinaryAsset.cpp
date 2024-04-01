#include <stdio.h>
#include "BinaryAsset.h"

#include "Debug.h"
static Debugger *debug = new Debugger("BinaryAsset", DEBUG_ALL);

std::vector<BinaryAsset>BinaryAsset::file_assets;

//Should be called when a file was loaded from disk and we want to make it into an BinaryAsset.
void BinaryAsset::StoreBinaryAsset(const char* filename, uint8_t* data, size_t sz){
    //Find in file BinaryAssets
    for (BinaryAsset& BinaryAsset:file_assets){
        if (BinaryAsset.name.compare(filename) == 0){
            //Already marked. File loaded twice?
            debug->Warn("Reloaded previously marked BinaryAsset.\n");
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
    for (BinaryAsset& BinaryAsset:file_assets){
        debug->Info("BinaryAsset: %s\n",BinaryAsset.name.c_str());
    }
}

BinaryAsset* BinaryAsset::GetBinaryAsset(const char* filename){
    //Find in file BinaryAssets
    for (BinaryAsset& BinaryAsset:file_assets){
        if (BinaryAsset.name.compare(filename) == 0){
            debug->Info("Got BinaryAsset %s from cache\n",BinaryAsset.name.c_str());
            return &BinaryAsset;
        }
    }

    for (int i =0;i<num_memory_assets;i++){
        BinaryAsset* BinaryAsset = &file_assets[i];
        if (BinaryAsset->name.compare(filename) == 0){
            debug->Info("Got BinaryAsset %s from memory\n",BinaryAsset->name.c_str());
            return BinaryAsset;
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
    fprintf(file,"int BinaryAsset::num_memory_BinaryAssets = %i;\n",num_file_assets);
    fprintf(file,"static const uint8_t BinaryAsset_data[] = {\n");

    //Build the binary blob, keep track off BinaryAssets and offsets.
    size_t offset = 0;
    for (BinaryAsset& BinaryAsset:file_assets){
        debug->Info("Dump BinaryAsset: %s\n",BinaryAsset.name.c_str());
        BinaryAsset.offset = offset;
        for (size_t i=0;i<BinaryAsset.size;i++){
            fprintf(file,"0x%02X,",BinaryAsset.data[i]);
            offset++;
        }
    }
    fprintf(file,"};\n");
    fprintf(file,"BinaryAsset BinaryAsset::BinaryAssets[] = {\n");
    for (BinaryAsset& BinaryAsset:file_assets){
        fprintf(file,"{\n");
        fprintf(file,".name=\"%s\",\n",BinaryAsset.name.c_str());
        fprintf(file,".size=%zu,\n",BinaryAsset.size);
        fprintf(file,".offset=%zu,\n",BinaryAsset.offset);
        fprintf(file,".data = (uint8_t*)&BinaryAsset_data[%zu]\n",BinaryAsset.offset);
        fprintf(file,"},\n");
    }
    fprintf(file,"};\n");
    fclose(file);
}
#else
void BinaryAsset::DumpBinaryAssets(){
    ListBinaryAssets();
}
#endif