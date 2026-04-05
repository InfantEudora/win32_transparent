#include "Texture.h"
#include "Debug.h"
#include "File.h"

#include "stb_image/stb_image.h"

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
    if (hdr_data){
        stbi_image_free(hdr_data);
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

    glTextureParameteri(texture_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(texture_id, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTextureParameteri(texture_id, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(texture_id, GL_TEXTURE_WRAP_T, GL_REPEAT);

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

//Load a decoded (PNG, JPG etc. from memory.)
void Texture::LoadFromMemory(uint8_t* data, size_t length, int target, int depth_in){
    depth = depth_in;

    int w;
	int h;
	int channels;
    stbi_set_flip_vertically_on_load(false);
    img_data = stbi_load_from_memory(data,length,&w,&h,&channels,0);
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

/*
    Filename;
    Target: Specifies the texture target GL_TEXTURE_1D, GL_TEXTURE_2D, GL_TEXTURE_3D, GL_TEXTURE_1D_ARRAY, GL_TEXTURE_2D_ARRAY, GL_TEXTURE_RECTANGLE, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_BUFFER, GL_TEXTURE_2D_MULTISAMPLE or GL_TEXTURE_2D_MULTISAMPLE_ARRAY.
    Depth: -1 will not be uploaded to GPU, GL_TEXTURE_CUBE_MAP (0-6): Face index,
*/
void Texture::LoadFromFile(const char* filename, int target, int depth_in){
    if (file_data_sz > 0){
        debug->Info("LoadFromFile: Overwriting existing file data\n");
        free(file_data);
    }

    file_data_sz = 0;
    file_data = LoadFile(filename,&file_data_sz);
    if (!file_data){
        debug->Err("Unable to load Image File %s\n",filename);
        return;
    }

    name = filename;
    LoadFromMemory(file_data,file_data_sz,target,depth_in);
}

//CPU interpolation time!
vec3 Texture::GetValueAt(float x, float y){
    x = clamp(x, 0.0f, 1.0f);
    y = clamp(y, 0.0f, 1.0f);

    int stride = (image_format == GL_RGBA) ? 4 : 3;
    int row    = (int)((float)height * y);
    int col    = (int)((float)width  * x);
    int index  = (row * width + col) * stride;

    vec3 p = vec3(img_data[index], img_data[index+1], img_data[index+2]);
    p.x = clamp(p.x, 0, 255);
    p.y = clamp(p.y, 0, 255);
    p.z = clamp(p.z, 0, 255);
    return p;
} //Returns the pixel value at 0 ... 1 interval.

bool Texture::IsEmpty(){
    return (img_data_sz == 0);
}

void Texture::CopyLine(uint8_t* line, int num_pixels, uint8_t* out, int num_color_channels){
    if (line && out)
    memcpy(out,line,num_pixels * num_color_channels);
}

// Load a Radiance HDR (.hdr) file into float CPU data.
// depth_in = TEXTURE_DONT_UPLOAD: CPU only (for cubemap conversion).
// depth_in = 0: upload as a plain GL_TEXTURE_2D.
void Texture::LoadHDRFromFile(const char* filename, int depth_in){
    f_is_hdr = true;
    name     = filename;
    depth    = depth_in;

    int w, h, channels;
    stbi_set_flip_vertically_on_load(false);
    hdr_data = stbi_loadf(filename, &w, &h, &channels, 3); // force RGB
    if (!hdr_data){
        debug->Err("LoadHDRFromFile: failed to load %s: %s\n", filename, stbi_failure_reason());
        return;
    }
    width       = w;
    height      = h;
    hdr_data_sz = (size_t)w * h * 3 * sizeof(float);
    storage_format = GL_RGB16F;
    image_format   = GL_RGB;
    debug->Info("LoadHDRFromFile: %s  %i x %i\n", filename, w, h);

    if (depth_in == TEXTURE_DONT_UPLOAD){
        return;
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &texture_id);
    glTextureParameteri(texture_id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(texture_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(texture_id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(texture_id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureStorage2D(texture_id, 1, GL_RGB16F, width, height);
    glTextureSubImage2D(texture_id, 0, 0, 0, width, height, GL_RGB, GL_FLOAT, hdr_data);
}

// Bilinear-filtered HDR sample. x, y in [0, 1].
vec3 Texture::GetValueAtF(float x, float y){
    x = clamp(x, 0.0f, 1.0f);
    y = clamp(y, 0.0f, 1.0f);

    float px = x * (width  - 1);
    float py = y * (height - 1);
    int x0 = (int)px,  y0 = (int)py;
    int x1 = min(x0 + 1, width  - 1);
    int y1 = min(y0 + 1, height - 1);
    float fx = px - x0,  fy = py - y0;

    auto s = [&](int cx, int cy) -> vec3 {
        int i = (cy * width + cx) * 3;
        return vec3(hdr_data[i], hdr_data[i+1], hdr_data[i+2]);
    };
    return s(x0,y0).lerp(s(x1,y0), fx).lerp(
           s(x0,y1).lerp(s(x1,y1), fx), fy);
}

void Texture::AppendTexture(Texture* target, int2 at){
    debug->Info("Adding texture of size (%i x %i) at texture (%i,%i)\n",target->width,target->height, at.x,at.y);
    //At would provide the top left of where you want to put it.
    int new_image_width  = max(at.x + target->width,width);
    int new_image_height = max(at.y + target->height,height);

    if (IsEmpty()){
        //Just take in the image format
        image_format = target->image_format;
        storage_format = target->storage_format;
    }

    int num_channels = (image_format == GL_RGB) ? 3 : 4;
    size_t new_image_data_sz = new_image_width * new_image_height * num_channels;

    debug->Info("New image size = %i x %i: sz: %zu\n",new_image_width, new_image_height, new_image_data_sz);
    uint8_t* new_img_data = (uint8_t*)malloc(new_image_data_sz);

    uint8_t* write_ptr = new_img_data;
    uint8_t* first_data = img_data;
    uint8_t* second_data = target->img_data;
    int num_lines = new_image_height;

    int current_line_length = width;
    int line_length_inc = new_image_width - width;

    uint8_t* empty_line = (uint8_t*)calloc(new_image_width * num_channels,1);

    //Now we scan the old image into the new image first:
    for (int l=0;l<num_lines;l++){
        //debug->Info("Copy 1st Line %i Linewidth = %i\n",l,width);
        CopyLine(first_data,width,write_ptr,num_channels);
        write_ptr   += width * num_channels;
        first_data  += width * num_channels;

        //Add empty line
        CopyLine(empty_line,line_length_inc,write_ptr,num_channels);
        write_ptr   += line_length_inc * num_channels;
    }

    //Then we overwrite with the new image.
    //Scan the line from at.y to at.y + target->height
    for (int l=at.y;l<at.y+target->height;l++){
        write_ptr = new_img_data;
        write_ptr += l * new_image_width * num_channels; //start of current line

        //Increment pointer with x offset
        write_ptr   += at.x * num_channels;

        CopyLine(second_data,target->width,write_ptr,num_channels);
        second_data += target->width * num_channels;
        write_ptr   += target->width * num_channels;

        int remain = new_image_width - target->width - at.x;

        //Perhaps we fill this line with some more emptyness?
        CopyLine(empty_line,remain,write_ptr,num_channels);
    }

    //Finalise
    width = new_image_width;
    height = new_image_height;
    //Image format should already match or be set.
    //Free any existing data
    free(img_data);
    img_data = NULL;
    free(file_data);
    file_data = NULL;
    file_data_sz = 0;
    free(empty_line);
    img_data = new_img_data;
    img_data_sz = new_image_data_sz;
}
