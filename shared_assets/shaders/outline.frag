#version 430 core
//The engine-wide texture unit map. Mirrored in C++ by core/TextureUnits.h.
#include "texture_units.glsl"

/*
    Renderer::OutlinePass, step 2: the outline itself, over the resolved frame.

    A pixel takes the colour of the NEAREST mask pixel within the width that belongs to a different
    outlined object than its own - outside every outlined object that is the line round it; inside
    one, it is the line where two outlined objects meet, drawn on one side only (the lower index's)
    so it is the same width as the outside line. Coverage falls off over the last pixel of the
    width, which is what keeps the line from stair-stepping without any multisampling.

    Where the mask pixel it came from is hidden behind the scene, the line is drawn at
    hidden_alpha: the faint silhouette of a selected thing behind a wall.
*/

layout (location = 0) out vec4 color;

//RGBA32I, see outline_mask.frag: object index + 1 (0 none), hidden, packed colour.
layout (binding = TEXUNIT_LOWRES_COMPOSITE) uniform isampler2D outline_mask;

uniform float width = 2.0;          //in pixels of this target
uniform float hidden_alpha = 0.35;

void main(){
    ivec2 size = textureSize(outline_mask,0);
    ivec2 p = ivec2(gl_FragCoord.xy);
    int own = texelFetch(outline_mask,p,0).x;
    float reach = width + 0.5;
    int r = int(ceil(reach));
    float best = reach;
    ivec4 hit = ivec4(0);
    for (int y = -r; y <= r; y++){
        for (int x = -r; x <= r; x++){
            float d = length(vec2(x,y));
            if (d >= best || (x == 0 && y == 0)){
                continue;
            }
            ivec2 q = p + ivec2(x,y);
            if (q.x < 0 || q.y < 0 || q.x >= size.x || q.y >= size.y){
                continue;
            }
            ivec4 m = texelFetch(outline_mask,q,0);
            if (m.x == 0 || m.x == own || (own != 0 && own > m.x)){
                continue;
            }
            best = d;
            hit = m;
        }
    }
    if (hit.x == 0){
        discard;
    }
    vec4 c = unpackUnorm4x8(uint(hit.z));
    c.a *= clamp(reach - best,0.0,1.0) * ((hit.y != 0) ? hidden_alpha : 1.0);
    color = c;
}
