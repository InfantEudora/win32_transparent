#include "Texture.h"
#include "Debug.h"
#include "File.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image\stb_image.h"

static Debugger *debug = new Debugger("Texture", DEBUG_ALL);

//Creates the image handle in OpenGL
void Texture::Create2D(int w, int h, UINT _format, int target, int depth){
    width = w;
    height = h;
    if ((_format != GL_RGB8) && (_format != GL_RGBA8)){
        debug->Err("Specify GL_RGB8 or GL_RGBA8 as Create2D image format.\n");
        return;
    }
    storage_format = _format;

    glCreateTextures(target, 1, &texture_id);

    glTextureParameteri(texture_id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(texture_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(texture_id, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTextureParameteri(texture_id, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);

    //Allocates storage in GPU, but no data is transferred
    glTextureStorage2D(texture_id, 1, storage_format, width, height);
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

//Uploads entire texture. Storage should have been allocated with Create2D
void Texture::UploadTexture(UINT _format, int target){
    image_format = _format;
    if (target == GL_TEXTURE_CUBE_MAP){
        glTextureSubImage3D(texture_id,0,0,0,depth,width,height,1,image_format,GL_UNSIGNED_BYTE,img_data);
    }else{
        glTextureSubImage2D(texture_id,0,0,0,width,height,image_format,GL_UNSIGNED_BYTE,img_data);
        glGenerateTextureMipmap(texture_id);
    }
}

//Target specifies the texture target GL_TEXTURE_1D, GL_TEXTURE_2D, GL_TEXTURE_3D, GL_TEXTURE_1D_ARRAY, GL_TEXTURE_2D_ARRAY, GL_TEXTURE_RECTANGLE, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_BUFFER, GL_TEXTURE_2D_MULTISAMPLE or GL_TEXTURE_2D_MULTISAMPLE_ARRAY.
void Texture::LoadFromFile(const char* filename, int target, int depth_in){
    depth = depth_in;
    file_data_sz = 0;
    file_data = LoadFile(filename,&file_data_sz);
    if (!file_data){
        debug->Err("Unable to load Image File %s\n",filename);
    }

    int w;
	int h;
	int channels;
    stbi_set_flip_vertically_on_load(false);
    img_data = stbi_load_from_memory(file_data,file_data_sz,&w,&h,&channels,0);
    debug->Info("Loaded image file: %i x %i %i channels\n",w,h,channels);
    //TODO: Free file data. This may not be possible when it was packed in?

    //Compute image data size
    img_data_sz = channels * w * h;

    UINT format; //Note 2 different formats are used.
    if (channels == 4){
        format = GL_RGBA8;
    }else if (channels == 3){
        format = GL_RGB8;
    }else{
        debug->Err("Unsupported number of color channels.\n");
        return;
    }

    //Code for loading new image or adding to a cube map
    if (depth == -1){
        debug->Info("Not uploading texture.\n");
        return;
    }

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

    width = w;
    height = h;

    UploadTexture(format,target);

    #ifdef BINDLESS_TEXTURES
    texture_handle = glGetTextureHandleARB(texture_id);
    if (texture_handle == 0){
        debug->Fatal("Unable to get texture handle from texture ID.\n");
    }
    debug->Info("Uploading data. Texture Handle: %llu\n",texture_handle);
    glMakeTextureHandleResidentARB(texture_handle);
    #endif
}