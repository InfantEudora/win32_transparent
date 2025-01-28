#include "Texture.h"
#include "Debug.h"
#include "File.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image\stb_image.h"

static Debugger *debug = new Debugger("Texture", DEBUG_ALL);

Texture::Texture(){

}

Texture::~Texture(){
    if (img_data){
        free(img_data);
    }
    if (file_data){
        free(file_data);
    }
}

//Creates the image handle in OpenGL
void Texture::Create2D(int target, int depth){
    if ((storage_format != GL_RGB8) && (storage_format != GL_RGBA8)){
        debug->Err("Specify GL_RGB8 or GL_RGBA8 as Create2D image format.\n");
        return;
    }

    glCreateTextures(target, 1, &texture_id);
    debug->Info("Create2D: %s texture_id: %li\n",name.c_str(),texture_id);

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
    if (target == GL_TEXTURE_CUBE_MAP){
        glTextureSubImage3D(texture_id,0,0,0,depth,width,height,1,image_format,GL_UNSIGNED_BYTE,img_data);
    }else{
        glTextureSubImage2D(texture_id,0,0,0,width,height,image_format,GL_UNSIGNED_BYTE,img_data);
        glGenerateTextureMipmap(texture_id);
    }
}

/*
    Filename;
    Target: Specifies the texture target GL_TEXTURE_1D, GL_TEXTURE_2D, GL_TEXTURE_3D, GL_TEXTURE_1D_ARRAY, GL_TEXTURE_2D_ARRAY, GL_TEXTURE_RECTANGLE, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_BUFFER, GL_TEXTURE_2D_MULTISAMPLE or GL_TEXTURE_2D_MULTISAMPLE_ARRAY.
    Depth: -1 will not be uploaded to GPU, GL_TEXTURE_CUBE_MAP (0-6): Face index,
*/
void Texture::LoadFromFile(const char* filename, int target, int depth_in){
    depth = depth_in;

    if (file_data_sz > 0){
        debug->Info("LoadFromFile: Overwriting existing file data\n");
        free(file_data);
    }

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
    name = filename;
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
    storage_format = format;

    if (channels == 4){
        image_format = GL_RGBA;
    }else{
        image_format = GL_RGB;
    }

    width = w;
    height = h;

    //Code for loading new image or adding to a cube map
    if (depth == TEXTURE_DONT_UPLOAD){
        debug->Info("Not uploading texture.\n");
        return;
    }

    if ((target != GL_TEXTURE_CUBE_MAP) || (depth == 0)){
        Create2D(target,depth);
    }else{
        debug->Info("Re-Using Image Handle for CubeMap\n");
    }

    UploadTexture(image_format,target);

    #ifdef BINDLESS_TEXTURES
    texture_handle = glGetTextureHandleARB(texture_id);
    if (texture_handle == 0){
        debug->Fatal("Unable to get texture handle from texture ID.\n");
    }
    debug->Info("Uploading data. Texture Handle: %llu\n",texture_handle);
    glMakeTextureHandleResidentARB(texture_handle);
    #endif
}

//CPU interpolation time!
vec3 Texture::GetValueAt(float x, float y){
    vec3 p = vec3(x,y,0);

    int row = (float)height * y;
    int ypixel_index = row * width * 3;
    float pixel_index = ((float)width * x * 3) + ypixel_index;

    int index = pixel_index;
    //debug->Info("Getting data from pixel index %i\n",pixel_index);
    p = vec3(img_data[index + 0],img_data[index + 1],img_data[index + 2]);


    //Clamp output range
    p.x = clamp(p.x ,0,255);
    p.y = clamp(p.y ,0,255);
    p.z = clamp(p.z ,0,255);
    return p;
} //Returns the pixel value at 0 ... 1 interval.

bool Texture::IsEmpty(){
    return (img_data_sz == 0);
}

void Texture::CopyLine(uint8_t* line, int num_pixels, uint8_t* out, int num_color_channels){
    if (line && out)
    memcpy(out,line,num_pixels * num_color_channels);
}

//Appends the target texture to this one at position.
void Texture::AppendTexture(Texture* target, int2 at){
    //First, compare the 2 texture data types:
    debug->Info("AppendTexture: image_format %i vs %i\n",image_format,target->image_format);
    debug->Info("AppendTexture: img_data_sz  %i vs %i\n",img_data_sz,target->img_data_sz);

    if (IsEmpty()){
        image_format = target->image_format;
        storage_format = target->storage_format;
    }

    //We need the size for the new image + the old image.
    //We can only add from 0,0 onward, where 0,0 would overwrite the current image.
    at.x = width;
    at.y = 0;

    Texture new_image;
    new_image.image_format = image_format;
    new_image.width = width + target->width;
    new_image.height = max(height,target->height);
    int num_channels = (image_format == GL_RGB) ? 3 : 4;
    new_image.img_data_sz = new_image.width * new_image.height * num_channels;

    debug->Info("New image size = %i x %i: sz: %zu\n",new_image.width, new_image.height, new_image.img_data_sz);
    uint8_t* new_img_data = (uint8_t*)malloc(new_image.img_data_sz);

    int num_lines = new_image.height;

    int first_line_data_len = width * num_channels;
    int second_line_data_len = target->width * num_channels;

    uint8_t* write_ptr = new_img_data;
    uint8_t* first_data = img_data;
    uint8_t* second_data = target->img_data;

    //Concaternate line by line:
    for (int l=0;l<num_lines;l++){
        //Copy line from first image, or generate an empty line?
        //TODO

        //debug->Info("Copy 1st Line %i Linewidth = %i\n",l,width);
        CopyLine(first_data,width,write_ptr,num_channels);
        write_ptr   += width * num_channels;
        //debug->Info("Copy 2nd Line %i Linewidth = %i\n",l,target->width);
        CopyLine(second_data,target->width,write_ptr,num_channels);
        first_data  += first_line_data_len;
        second_data += second_line_data_len;
        write_ptr   += target->width * num_channels;
    }

    //We are the new image, copy some parameters
    width = new_image.width;
    height = new_image.height;
    //Image format should already match or be set.
    //Free any existing data
    if (img_data){
        free(img_data);
    }
    if (file_data){
        free(file_data);
    }
    img_data = new_img_data;
    img_data_sz = new_image.img_data_sz;
}