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

StringView sv_chop_by_delim(StringView* sv, char delim){
    size_t i = 0;
    while (i < sv->count && (char)sv->data[i] != delim) {
        i += 1;
    }
    StringView result = sv_from_parts(sv->data, i);
    if (i < sv->count) {
        sv->count -= i + 1;
        sv->data  += i + 1;
    } else {
        sv->count -= i;
        sv->data  += i;
    }
    return result;
}

StringView sv_from_parts(uint8_t* data, size_t count){
    StringView sv;
    sv.count = count;
    sv.data = data;
    return sv;
}

//Loads binary file into memory.
uint8_t* LoadFile(const char* filename, size_t* size){
    bool store_assets = true;   //Flag if file assets need to be held in memory so they can be exported.
    BinaryAsset* memory_asset = BinaryAsset::GetBinaryAsset(filename);

    if (memory_asset){
        if (size){
            *size = memory_asset->size;
        }
        return memory_asset->data;
    }

	FILE* file;
	size_t sz = 0;

	file = fopen(filename, "rb");
	if(!file){
		debug->Fatal("LoadFile [%s] failed.\n",filename);
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
    if (!memory_asset){
        BinaryAsset::StoreBinaryAsset(filename,data,sz);
    }

	return data;
};
