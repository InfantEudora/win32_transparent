#include "Heightmap.h"
#include "stb_image_write.h"
#include "type_helpers.h"
#include "Texture.h"
#include "Debug.h"

static Debugger *debug = new Debugger("Heightmap", DEBUG_ALL);

bool SaveHeightmapPNG(const char* filename, const float* heights, int width, int height, float min_height, float max_height){
    float range = max_height - min_height;
    std::vector<uint8_t> pixels((size_t)width * height * 3);
    for (int i = 0; i < width * height; i++){
        float t = (range != 0.0f) ? (heights[i] - min_height) / range : 0.0f;
        t = clamp(t,0.0f,1.0f);
        uint8_t byte = (uint8_t)(t * 255.0f + 0.5f);
        pixels[i*3+0] = byte;
        pixels[i*3+1] = byte;
        pixels[i*3+2] = byte;
    }
    if (!stbi_write_png(filename,width,height,3,pixels.data(),width*3)){
        debug->Err("SaveHeightmapPNG: failed to write %s\n",filename);
        return false;
    }
    return true;
}

bool LoadHeightmapPNG(const char* filename, std::vector<float>& out_heights, int& out_width, int& out_height, float min_height, float max_height,Texture* tex_out){
    Texture tex;
    Texture* used_tex = &tex;
    if (tex_out){
        used_tex = tex_out;
    }

    tex_out->LoadFromFile(filename,GL_TEXTURE_2D,TEXTURE_DONT_UPLOAD);
    if (tex_out->IsEmpty()){
        debug->Err("LoadHeightmapPNG: failed to load %s\n",filename);
        return false;
    }

    out_width  = tex_out->width;
    out_height = tex_out->height;
    int stride = (tex_out->image_format == GL_RGBA) ? 4 : 3;

    float range = max_height - min_height;
    out_heights.resize((size_t)out_width * out_height);
    for (int y = 0; y < out_height; y++){
        for (int x = 0; x < out_width; x++){
            int index = (y * out_width + x) * stride;
            uint8_t byte = tex_out->img_data[index]; //R channel
            float t = byte / 255.0f;
            out_heights[y * out_width + x] = min_height + t * range;
        }
    }
    return true;
}

//Grid point (gx,gz) in world space: X/Z centered on the origin, Y from the heightmap.
static vec3 HeightmapGridPos(const std::vector<float>& heights, int width, int gx, int gz, float cell_size_x, float cell_size_z){
    float wx = (gx - (width  - 1) * 0.5f) * cell_size_x;
    float wz = (gz - (heights.size() / width - 1) * 0.5f) * cell_size_z;
    float wy = heights[gz * width + gx];
    return vec3(wx,wy,wz);
}

Mesh* CreateMeshFromHeightmap(const std::vector<float>& heights, int width, int height, float cell_size_x, float cell_size_z){
    if ((size_t)width * height != heights.size()){
        debug->Err("CreateMeshFromHeightmap: heights.size() (%zu) != width*height (%i)\n",heights.size(),width*height);
        return NULL;
    }
    if (width < 2 || height < 2){
        debug->Err("CreateMeshFromHeightmap: need at least a 2x2 grid, got %ix%i\n",width,height);
        return NULL;
    }

    int num_quads = (width - 1) * (height - 1);
    std::vector<vertex> verts;
    verts.reserve((size_t)num_quads * 6); //2 triangles, 3 verts each, per quad

    for (int z = 0; z < height - 1; z++){
        for (int x = 0; x < width - 1; x++){
            vec3 p00 = HeightmapGridPos(heights,width,x,  z,  cell_size_x,cell_size_z);
            vec3 p01 = HeightmapGridPos(heights,width,x,  z+1,cell_size_x,cell_size_z);
            vec3 p10 = HeightmapGridPos(heights,width,x+1,z,  cell_size_x,cell_size_z);
            vec3 p11 = HeightmapGridPos(heights,width,x+1,z+1,cell_size_x,cell_size_z);

            vec2 uv00 = vec2((float)x/(width-1),    (float)z/(height-1));
            vec2 uv01 = vec2((float)x/(width-1),    (float)(z+1)/(height-1));
            vec2 uv10 = vec2((float)(x+1)/(width-1),(float)z/(height-1));
            vec2 uv11 = vec2((float)(x+1)/(width-1),(float)(z+1)/(height-1));

            //Two triangles per quad, wound so (v1-v0) x (v2-v0) points up (+Y).
            vec3 tri_a[3] = {p00,p01,p11};
            vec2 uv_a[3]  = {uv00,uv01,uv11};
            vec3 tri_b[3] = {p00,p11,p10};
            vec2 uv_b[3]  = {uv00,uv11,uv10};

            for (int tri = 0; tri < 2; tri++){
                vec3* tri_pos = (tri == 0) ? tri_a : tri_b;
                vec2* tri_uv  = (tri == 0) ? uv_a  : uv_b;
                vec3 normal = (tri_pos[1]-tri_pos[0]).cross(tri_pos[2]-tri_pos[0]);
                normal.normalize();

                for (int i = 0; i < 3; i++){
                    vertex v = vertex();
                    v.pos    = tri_pos[i];
                    v.normal = normal;
                    v.tangent = vec3(1,0,0);
                    v.uv     = tri_uv[i];
                    v.matid  = 0;
                    verts.push_back(v);
                }
            }
        }
    }

    Mesh* mesh = new Mesh();
    mesh->SetMeshData(verts.data(),(int)verts.size());
    return mesh;
}
