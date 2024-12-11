#include "Texture.h"
#include "Debug.h"
#include "File.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image\stb_image.h"

static Debugger *debug = new Debugger("Texture", DEBUG_ALL);

void Texture::Create2D(int w, int h, UINT format, int target, int depth){
    width = w;
    height = h;

    glCreateTextures(target, 1, &texture_id);

    glTextureParameteri(texture_id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(texture_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(texture_id, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTextureParameteri(texture_id, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);

    //Allocates storage in GPU, but no data is transferred
    glTextureStorage2D(texture_id, 1, format, width, height);
}

//Uses the first cubemap file to create an image. The second one uses the main image handle.
void Texture::LoadCubeMapFile(const char* filename, int depth_in, Texture* first_map){
    if (depth_in > 0){
        if (!first_map){
            debug->Fatal("Specify a first map for loading CubeMaps\n");
        }
        texture_id = first_map->texture_id;
        LoadFromFile(filename,GL_TEXTURE_CUBE_MAP,depth_in);
    }else{
        LoadFromFile(filename,GL_TEXTURE_CUBE_MAP,depth_in);
    }
}

//Target specifies the texture target GL_TEXTURE_1D, GL_TEXTURE_2D, GL_TEXTURE_3D, GL_TEXTURE_1D_ARRAY, GL_TEXTURE_2D_ARRAY, GL_TEXTURE_RECTANGLE, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_BUFFER, GL_TEXTURE_2D_MULTISAMPLE or GL_TEXTURE_2D_MULTISAMPLE_ARRAY.
void Texture::LoadFromFile(const char* filename, int target, int depth_in){
    depth = depth_in;
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
    //Code for loading new image or adding to a cube map
    if ((target != GL_TEXTURE_CUBE_MAP) || (depth == 0)){
        debug->Info("Create2D new Image Handle\n");
        Create2D(w,h,format,target,depth);
    }else{
        debug->Info("Re-Using Image Handle for CubeMap\n");
    }

    if (channels == 4){
        format = GL_RGBA;
    }else{
        format = GL_RGB;
    }

    if (target == GL_TEXTURE_CUBE_MAP){
        glTextureSubImage3D(texture_id,0,0,0,depth,w,h,1,format,GL_UNSIGNED_BYTE,img);
    }else{
        glTextureSubImage2D(texture_id,0,0,0,w,h,format,GL_UNSIGNED_BYTE,img);
        glGenerateTextureMipmap(texture_id);
    }

    #ifdef BINDLESS_TEXTURES
    texture_handle = glGetTextureHandleARB(texture_id);
    if (texture_handle == 0){
        debug->Fatal("Unable to get texture handle from texture ID.\n");
    }
    debug->Info("Uploading data. Texture Handle: %llu\n",texture_handle);
    glMakeTextureHandleResidentARB(texture_handle);
    #endif
}