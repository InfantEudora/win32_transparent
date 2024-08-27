#include "Texture.h"
#include "Debug.h"
#include "File.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image\stb_image.h"

static Debugger *debug = new Debugger("Texture", DEBUG_ALL);

void Texture::Create2D(int w, int h, UINT format){
    width = w;
    height = h;

    glCreateTextures(GL_TEXTURE_2D, 1, &texture_id);

    glTextureParameteri(texture_id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(texture_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(texture_id, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTextureParameteri(texture_id, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);

    //Allocates storage in GPU, but no data is transferred
    glTextureStorage2D(texture_id, 1, format, width, height);
}

void Texture::LoadFromFile(const char* filename){
    size_t img_data_sz = 0;
    uint8_t* img_data = LoadFile(filename,&img_data_sz);
    if (!img_data){
        debug->Err("Unable to load Image File %s\n",filename);
    }

    int w;
	int h;
	int channels;
    stbi_set_flip_vertically_on_load(false);
    uint8_t* img  = stbi_load_from_memory(img_data,img_data_sz,&w,&h,&channels,0);
    debug->Info("Loaded image file: %i x %i %i channels\n",w,h,channels);

    UINT format;
    if (channels == 4){
        format = GL_RGBA8;
    }else if (channels == 3){
        format = GL_RGB8;
    }else{
        debug->Err("Unsupported number of color channels.\n");
        return;
    }
    Create2D(w,h,format);

    if (channels == 4){
        format = GL_RGBA;
    }else{
        format = GL_RGB;
    }

    glTextureSubImage2D(texture_id,0,0,0,w,h,format,GL_UNSIGNED_BYTE,img);
    glGenerateTextureMipmap(texture_id);

    #ifdef BINDLESS_TEXTURES
    texture_handle = glGetTextureHandleARB(texture_id);
    if (texture_handle == 0){
        debug->Fatal("Unable to get texture handle from texture ID.\n");
    }
    debug->Info("Uploading data. Texture Handle: %llu\n",texture_handle);
    glMakeTextureHandleResidentARB(texture_handle);
    #endif
}