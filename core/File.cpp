#include <stdio.h>
#include "File.h"

#include "Debug.h"
static Debugger *debug = new Debugger("File", DEBUG_ALL);

std::string GetBasePath(const char* filename){
	std::string sname = filename;
  	std::size_t found = sname.find_last_of("/\\");
    if (found == -1){
        return "";
    }
  	return sname.substr(0,found);
}

// Loads binary file into memory. This does multiple things:
// If can load file from disk, and store it in a BinaryAsset table called memory_asset.
// If it's already stored in memory, it loads it from memory.
uint8_t* LoadFile(const char* filename, size_t* size, bool bypass_cache){
	BinaryAsset* memory_asset = NULL;
	if (bypass_cache == false){
		//We are allowed to look it up in memory cache.
		memory_asset = BinaryAsset::GetBinaryAsset(filename);
		if (memory_asset){
			if (size){
				*size = memory_asset->size;
			}
			return memory_asset->data;
		}
	}

	FILE* file;
	size_t sz = 0;

	file = fopen(filename, "rb");
	if(!file){
		debug->Fatal("LoadFile failed to load: [%s]\n",filename);
		return NULL;
	}

	/*get filesize:*/
	fseek(file , 0 , SEEK_END);
	sz = ftell(file);
	rewind(file);

	debug->Info("LoadFile: File %s is %li bytes\n",filename,sz);

	uint8_t* data = (uint8_t*)calloc(sz+1,1);
	fread(data, 1, sz, file);
	fclose(file);
    if (size){
        *size = sz;
    }
    //It was loaded from a file
    if (!memory_asset && !bypass_cache){
        BinaryAsset::StoreBinaryAsset(filename,data,sz);
    }

	return data;
};
