#include "CubeMap.h"
#include "Debug.h"
#include "File.h"
#include <math.h>
#include <string.h>

static Debugger *debug = new Debugger("CubeMap", DEBUG_ALL);

//Uses the first cubemap file to create an image. The second one uses the main image handle.
void CubeMap::LoadFromFile(const char* filename, int depth_in){
    if ((depth_in < 0) || (depth_in > 5)){
        debug->Fatal("Invalid CubeMap index\n");
    }
    texture[depth_in] = new Texture();
    texture[depth_in]->LoadCubeMapFile(filename,depth_in,texture[0]);
}

// Basis vectors for each of the 6 cubemap faces.
// For a texel at (s, t) in [-1, 1], the world direction is:
//   normalize(forward + s*right + t*up)
// Face order matches GL_TEXTURE_CUBE_MAP_POSITIVE_X .. NEGATIVE_Z (0..5).
static const vec3 face_forward[6] = {
    vec3( 1, 0, 0),   // +X
    vec3(-1, 0, 0),   // -X
    vec3( 0, 1, 0),   // +Y
    vec3( 0,-1, 0),   // -Y
    vec3( 0, 0, 1),   // +Z
    vec3( 0, 0,-1),   // -Z
};
static const vec3 face_right[6] = {
    vec3( 0, 0,-1),   // +X
    vec3( 0, 0, 1),   // -X
    vec3( 1, 0, 0),   // +Y
    vec3( 1, 0, 0),   // -Y
    vec3( 1, 0, 0),   // +Z
    vec3(-1, 0, 0),   // -Z
};
static const vec3 face_up[6] = {
    vec3( 0,-1, 0),   // +X
    vec3( 0,-1, 0),   // -X
    vec3( 0, 0, 1),   // +Y
    vec3( 0, 0,-1),   // -Y
    vec3( 0,-1, 0),   // +Z
    vec3( 0,-1, 0),   // -Z
};

void CubeMap::LoadFromEquirectangular(const char* filename, int face_size){
    // --- Load source image to CPU only ---
    Texture* source = new Texture();
    std::string fname = filename;
    bool is_hdr = fname.size() >= 4 &&
                  fname.compare(fname.size()-4, 4, ".hdr") == 0;

    if (is_hdr){
        source->LoadHDRFromFile(filename, TEXTURE_DONT_UPLOAD);
    }else{
        source->LoadFromFile(filename, GL_TEXTURE_2D, TEXTURE_DONT_UPLOAD);
    }

    if (!source->width || !source->height){
        debug->Err("LoadFromEquirectangular: failed to load source %s\n", filename);
        delete source;
        return;
    }

    if (face_size == 0){
        face_size = source->height / 2;
    }
    debug->Info("LoadFromEquirectangular: %s  source=%ix%i  face=%i  hdr=%s\n",
                filename, source->width, source->height, face_size, is_hdr ? "yes" : "no");

    // --- Create the single GL cubemap object ---
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &cubemap_id);
    glTextureParameteri(cubemap_id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(cubemap_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(cubemap_id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(cubemap_id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    //glTextureParameteri(cubemap_id, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    GLuint storage_fmt = is_hdr ? GL_RGB16F : GL_RGB8;
    glTextureStorage2D(cubemap_id, 1, storage_fmt, face_size, face_size);


    // --- Per-face conversion ---
    const float PI = 3.14159265359f;
    const int   CH = 3;
    const size_t face_pixels = (size_t)face_size * face_size * CH;

    float*   buf_f = is_hdr ? (float*)  malloc(face_pixels * sizeof(float))   : NULL;
    uint8_t* buf_b = is_hdr ? NULL      : (uint8_t*)malloc(face_pixels);

    for (int face = 0; face < 6; face++){
        for (int y = 0; y < face_size; y++){
            for (int x = 0; x < face_size; x++){
                // Map pixel to [-1, 1]
                float s = ((x + 0.5f) / face_size) * 2.0f - 1.0f;
                float t = ((y + 0.5f) / face_size) * 2.0f - 1.0f;

                // World-space direction for this texel
                vec3 dir = face_forward[face] + face_right[face] * s + face_up[face] * t;
                dir.normalize();

                // Equirectangular UV from direction.
                // v is flipped because stb_image row 0 = top of file,
                // but the cubemap vertical axis runs bottom-to-top.
                float u =        atan2f(dir.z, dir.x) / (2.0f * PI) + 0.5f;
                float v = 1.0f - (asinf(clamp(dir.y, -1.0f, 1.0f)) / PI + 0.5f);

                int dst = (y * face_size + x) * CH;
                if (is_hdr){
                    vec3 c = source->GetValueAtF(u, v);
                    buf_f[dst]   = c.x;
                    buf_f[dst+1] = c.y;
                    buf_f[dst+2] = c.z;
                }else{
                    vec3 c = source->GetValueAt(u, v);
                    buf_b[dst]   = (uint8_t)c.x;
                    buf_b[dst+1] = (uint8_t)c.y;
                    buf_b[dst+2] = (uint8_t)c.z;
                }
            }
        }

        if (is_hdr){
            glTextureSubImage3D(cubemap_id, 0, 0, 0, face,
                                face_size, face_size, 1,
                                GL_RGB, GL_FLOAT, buf_f);
        }else{
            glTextureSubImage3D(cubemap_id, 0, 0, 0, face,
                                face_size, face_size, 1,
                                GL_RGB, GL_UNSIGNED_BYTE, buf_b);
        }
        debug->Info("LoadFromEquirectangular: face %i/6 done\n", face + 1);
    }

    if (buf_f) free(buf_f);
    if (buf_b) free(buf_b);
    delete source;
}