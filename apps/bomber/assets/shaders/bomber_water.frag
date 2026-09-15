#version 430 core

/*
    The water tiles: an animated Worley (Voronoi) caustic net, matching the look the artist
    modelled into tile_water in bomber_assets.glb.

    --- WHAT IT IS ------------------------------------------------------------------------------
    An ordinary surface shader, not a volume. `tile_water`'s shared mesh is tagged
    MESH_MODE_SHADER and pointed at this program (ApplicationBomber::BuildWater), so every water
    tile on the board draws with it and nothing else does. It keeps the custom-material pass's
    default state - depth tested, depth written, back faces culled - because a water tile is solid
    floor and wants to behave like the rest of the board.

    --- THE PATTERN, AND WHY IT IS THIS ONE -----------------------------------------------------
    The reference is a net of bright thin bridges between darker rounded cells, with brighter
    blobs where three cells meet. That is not a texture to be drawn, it is what F2 MINUS F1 of a
    Worley noise looks like: F1 is the distance to the nearest feature point and F2 to the second
    nearest, so the difference falls to ZERO exactly on the boundary between two cells and is
    widest at a cell's centre. Light the small values and you get the net for free, with the
    vertex blobs falling out of it - three cells meeting means three distances are close together
    over a wider area, so the bright region naturally swells there. No special case.

    The difference is NORMALISED by the sum of the two distances, which is what rounds the cells
    off into the reference's blobs rather than leaving them flat-sided - see `edge` in main().

    IT IS SAMPLED IN WORLD XZ, NOT IN THE TILE'S UV, and that is the one decision here worth
    defending. Sampling per tile would put an identical pattern on all forty-odd water tiles and
    end every one of them at a seam - which reads as tiling, and tiling is the thing that says
    "texture" rather than "water". In world space the net simply continues across a run of tiles
    and a lone tile is a window onto the same field. It costs nothing: vposition is already there.

    --- THE CLOCK IS IN TICKS -------------------------------------------------------------------
    `water_time` is the simulation tick count, published by ApplicationBomber::SyncView, for the
    same reason the blast's clock is (see shaders/bomber_explosion.frag): under sim_pause the
    water FREEZES, and sim_step advances it by an exact number of frames. So a screenshot of the
    board is reproducible rather than being whatever the wall clock said. It also means the water
    and the fire are on one clock, which is what you want the moment you film a blast next to a
    pond.

    The feature points do not slide, they ORBIT inside their own cells - see `cell_point`. Sliding
    them would move the whole net in one direction like a scrolling texture; orbiting makes the
    net breathe, cells swelling and pinching against their neighbours, which is what the surface
    of shallow water actually does. It is also what keeps the 3x3 search below exact.

    --- THE COLOUR IS TWO NAMED CONSTANTS, AND WHY IT IS NOT THE MATERIAL'S -----------------------
    The obvious thing is to take the blue from tile_water's own material so the hue stays an art
    decision. IT CANNOT BE DONE HERE, and the reason is worth writing down because the next person
    will try it: the board's art is ONE 4096x4096 ATLAS shared by every tile type, so the water's
    material has no colour of its own to read - `m.color` is white and the blue lives in a corner
    of a texture that also holds the grass, the bricks and the stone. Reading `m.color` gets white
    (this shader rendered grey on its first run for exactly that reason); sampling the atlas at
    `vuv` gets the artist's blue AND the artist's own baked Voronoi, which then fights the animated
    one. Averaging the texture down its mip chain gets the average of the whole board.

    So the two blues are constants below, matched to the reference render, and they are the one
    place to change the hue. The four knobs are all about the PATTERN.

    Unlit on purpose - the reference is flat and self-lit, and lighting a caustic net with the
    scene's sun mostly greys it. The cost is that water in a shadowed corner still glows; if that
    ever matters the fix is a sun term here, not a fifth knob.
*/

layout (location = 0) out vec4 color;

//From default.vert. Only these two are used; the rest of its varyings are still there.
layout (location = 0) in vec3 vposition;        //Fragment position in WORLD space
layout (location = 1) in vec3 vnormal;          //World-space normal

/*
    The water, at its two extremes. Read off the reference render, and the ONE PLACE to change the
    hue - see the note at the top for why this is not taken from the material.

    SHALLOW is both the lit part of a cell and what the bridges fade up from; DEEP is where a
    cell's middle goes, and it is a deeper BLUE rather than a darkened version of SHALLOW. That
    difference is the whole reason there are two constants instead of one and a multiplier:
    scaling one colour towards black desaturates as it darkens, so deep water came out grey-blue
    and the tile read as wet slate. Real water gets bluer as it deepens, because the red goes
    first.
*/
#define WATER_SHALLOW vec3(0.42,0.75,0.91)
#define WATER_DEEP    vec3(0.06,0.32,0.58)

//--- the four knobs ---------------------------------------------------------------------------
//Worley cells per WORLD unit, and a cell of the board is 1.0 world unit - so this reads directly
//as "cells across a tile". Below about 1.5 the net is too coarse to read as water; above about 8
//the cells are smaller than the pixels the tile covers and it turns to noise.
uniform float water_scale = 4.0;
//How fast the net breathes, in radians per 100 ticks. The app runs at 60 TPS, so 1.0 is a little
//over a third of a turn per second - a slow swell. It is per 100 ticks rather than per tick
//purely so the useful range of the slider is not all in its first pixel.
uniform float water_speed = 1.0;
//How far a cell's middle sinks from WATER_SHALLOW towards WATER_DEEP. 0 is a flat tile with
//bright lines drawn on it, 1 puts the deep blue everywhere the net is not. The depth of the
//water, in the only sense a flat tile has one.
uniform float water_depth = 0.55;
//Width of the bright bridges. Dimensionless, because `edge` is a RATIO of two distances rather
//than a length - so it means the same thing at any water_scale, which is what stops the two knobs
//fighting. It sets thickness AND, because the white core is driven off the same falloff, how
//bright the net reads.
uniform float caustic_width = 0.18;

//Simulation ticks since the app started. See the clock note at the top.
uniform float water_time = 0.0;

/*
    Where cell `cell`'s feature point is, right now.

    The hash is the usual sin-fract one. It is not a good random number generator and does not
    need to be: it only has to be STABLE per cell and uncorrelated with its neighbours, and a
    visible pattern in it would show up as a visible pattern in the net, which is the only test
    that matters here.

    AMPLITUDE 0.38, AND IT CANNOT GO ABOVE 0.5. The search below only looks at the 3x3 block of
    cells around the sample, which finds the true nearest point only while every point stays
    inside its own cell. Push the orbit past half a cell and points start escaping into their
    neighbours, F1 stops being the nearest distance, and the net tears - intermittently, and worst
    where it is moving fastest, which is exactly the sort of thing that gets blamed on the driver.
*/
vec2 cell_point(vec2 cell){
    vec2 h = fract(sin(vec2(dot(cell,vec2(127.1,311.7)),
                            dot(cell,vec2(269.5,183.3)))) * 43758.5453);
    //Each cell gets its own phase from the hash, so neighbours are never in step.
    return cell + 0.5 + 0.38 * sin(water_time * water_speed * 0.01 + h * 6.28318530718);
}

/*
    Worley: the distances to the nearest and second nearest feature points, in cell units.

    Compared on SQUARED distance and square-rooted twice at the end rather than nine times.
    Ordering by squared distance is the same ordering as by distance, so this is exact and not an
    approximation - but the subtraction downstream is on the real distances, which it has to be:
    (F2^2 - F1^2) is not the border-finding function, it grows with how far the whole pair is
    from the sample, and the net would thin out with distance from nothing in particular.
*/
vec2 worley(vec2 p){
    vec2 base = floor(p);
    float f1 = 1.0e9;
    float f2 = 1.0e9;
    for (int y = -1;y <= 1;y++){
        for (int x = -1;x <= 1;x++){
            vec2 d = cell_point(base + vec2(x,y)) - p;
            float sq = dot(d,d);
            if (sq < f1){
                f2 = f1;
                f1 = sq;
            }else if (sq < f2){
                f2 = sq;
            }
        }
    }
    return vec2(sqrt(f1),sqrt(f2));
}

void main(){
    vec2 f = worley(vposition.xz * water_scale);
    /*
        Zero on the boundary between two cells, widest at a cell's centre. The whole effect.

        DIVIDED BY THE SUM, and that is what rounds the cells off. The raw F2-F1 is zero along the
        perpendicular bisector of two feature points, which is a straight line - so the net comes
        out as a flat-sided mosaic, and the reference's cells are plainly bulging rather than
        polygonal. Normalising turns it into a RATIO of the two distances, whose contours bow away
        from the nearer point, and the cells round off into the blobs the reference has. It also
        makes `edge` dimensionless, so caustic_width stops depending on the cell size at all.
    */
    float edge = (f.y - f.x) / max(f.y + f.x,0.0001);
    /*
        AN EXPONENTIAL FALLOFF, NOT A SMOOTHSTEP, and the difference is the whole difference
        between this reading as water and reading as a tiled floor.

        A smoothstep reaches zero at caustic_width and stays there, so every cell has a flat
        interior with a hard-edged bright line painted round it - which is a mosaic. Light refracted
        through a moving surface has no such edge: it falls off from the caustic and keeps falling,
        so the middle of a cell is still slightly brighter on the side nearest a bridge. exp never
        reaches zero, which puts a gentle gradient across the whole cell for free and costs one
        instruction less than the smoothstep it replaced.

        caustic_width is then the e-folding distance rather than a hard edge - still in cell units,
        so it still means the same thing at any water_scale.
    */
    float caustic = exp(-edge / max(caustic_width,0.001));

    /*
        Only the top. The pattern is a function of world XZ, so on the slab's vertical sides it is
        constant down the face and reads as vertical streaks rather than as anything. Fading it
        out by the normal leaves the sides the flat deep colour, which is what the gap between two
        tiles should look like anyway.
    */
    caustic *= smoothstep(0.25,0.75,vnormal.y);

    //How far a cell's middle sinks towards the deep blue. water_depth 0 leaves a flat tile with
    //bright lines drawn on it, which is a useful thing to be able to see the net against.
    vec3 deep = mix(WATER_SHALLOW,WATER_DEEP,clamp(water_depth,0.0,1.0));
    vec3 c = mix(deep,WATER_SHALLOW,caustic);
    //The core of each bridge blows out towards white. Fourth power rather than a second
    //smoothstep: it keeps the white confined to the middle of a line while the line itself stays
    //as wide as caustic_width asked for, so one knob really does control one thing.
    c = mix(c,vec3(1.0),caustic * caustic * caustic * caustic);

    color = vec4(c,1.0);
}
