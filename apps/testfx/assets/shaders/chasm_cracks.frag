#version 450 core
#include "shadertoy.glsl"
#include "engine.glsl"

/*
    THE CHASM FROM CRACKED MUD - a top-down map of the chasm made the way dried mud cracks
    (art_source/chasm/cracked_desert.jpg), to judge the SHAPE before anything goes into apps/chasm.
    World units and the game's layout: x east, y north, the rift coming in from the south edge and
    ending in a tip short of the north one.

    What the photo has and a noise-wobbled rift does not: the ground is PLATES, and every edge of the
    chasm is an edge of a plate. So the rim runs straight for a while and then turns a corner, and
    whatever stands in the chasm or slumps off its wall is a whole plate or a piece of one - never a
    blob. Three ways of using that, on `mode`:

      0  PULL-APART. The plates are cut once, closed; then every plate east of the spine slides east
         and every plate west of it slides west, by the opening. The two walls MATCH like a torn
         sheet - "as if the crack appeared in a short amount of time". Plates straddling the spine
         may stay where they were and become islands, a step down: blocks that lost their support.
      1  ERODED CHANNEL. Nothing moves; plates whose seed lies within the opening are gone. The
         stream in the photo: the walls do not match, the channel is ragged along plate edges.
      2  MUD ONLY. No chasm, the plate and crack network on its own, to tune against the photo.

    On any of them, borders between plates open into side cracks, wider and deeper the nearer the
    chasm - the game's ChasmLayout cracks, but as a network along plate edges, with T-junctions,
    instead of single lines. And the pieces of a plate near the rim break away: some slump two steps
    in one piece (a balcony), some break a few steps (the fringe), some fall to the floor.

    `view` 0 is shaded like the photo, 1 the game's kinds, 2 height. `plot_grid` samples on 1.8-unit
    squares, about a plot, to see what survives being cut into plots - in 3D too, where they stand as
    the game's columns.

    `view` 3 is the same ground in 3D, marched through the bench's own camera: the middle mouse
    orbits, shift and the middle mouse pans, the wheel zooms. 4 is that with the game's kinds. The
    bench starts the camera low over the cube it was built for; a view of the whole map is
        camera_set position [2.2,2.4,3.6] look_at [0,-0.15,0]

    Heights are the game's steps (Terrain.h): 0 the plateau, 2 a balcony, 4 an island, 12 the floor,
    6 units a step. HashInts and Noise are ChasmLayout.cpp's, bit for bit, and nothing here needs
    trigonometry, so a port to the generator is a transcription rather than a re-tuning.
*/

//--- knobs -------------------------------------------------------------------------------------------
uniform int   mode = 0;             //0 pull-apart, 1 eroded channel, 2 mud only
uniform int   view = 0;             //0 shaded, 1 the game's kinds, 2 height, 3 in 3D, 4 in 3D with the kinds
uniform int   seed = 12;
uniform float map_width = 540.0;    //world units; the depth follows the window, 16:9 like the game's map
uniform float zoom = 1.0;
uniform vec2  pan = vec2(0.0,0.0);  //world units

uniform float plate_size = 46.0;    //world units between plate seeds
uniform float plate_jitter = 0.85;  //0 a square grid of seeds, 1 anywhere in their cell
uniform float warp = 0.30;          //of a plate, how far its borders wander
uniform float wiggle = 0.6;         //world units, the fine waver along every border
uniform float sub_size = 14.0;      //the pieces a plate breaks into

uniform float rift_open = 34.0;     //half the opening, where it is widest
uniform float rift_wander = 70.0;   //how far the spine swings east and west
uniform float rift_tip = 0.80;      //of the depth, from the south edge
uniform float island_band = 0.9;    //plates across, about the spine, that may stay behind as islands
uniform float island_chance = 0.5;

uniform float crack_reach = 80.0;   //how far from the chasm side cracks are open
uniform float crack_width = 2.0;    //half-width of a side crack where it leaves the chasm
uniform float crack_far = 0.25;     //chance a border beyond crack_reach is still a shallow crack
uniform float crumble = 0.55;       //chance a piece at the rim breaks away
uniform float crumble_reach = 8.0;  //how far back from the rim a piece's seed may be: about touching it
uniform float slump_chance = 0.3;   //chance a whole plate on the rim slumps two steps: a balcony

uniform bool  animate = false;      //open the rift over ten seconds, again and again
uniform bool  plot_grid = false;
uniform bool  show_spine = false;
uniform float sun_elevation = 0.75; //rise over run
uniform int   shadow_taps = 6;
uniform float lip = 0.5;            //the pale crumbled edge round every plate

uniform float world_scale = 0.01;   //3D: bench units per world unit - the bench's orbit and zoom are sized for a cube
uniform float march_step = 6.0;     //3D: pixels of ground per step; more is faster and slips through thin cracks
uniform int   march_steps = 160;    //3D: the most steps a ray takes before it gives up in the steam
uniform float steam_density = 0.02; //3D: per world unit of a ray under the cloud deck

//--- the game's numbers ------------------------------------------------------------------------------
#define STEP            6.0     //TERRAIN_STEP
#define FLOOR_STEPS     12.0    //TERRAIN_FLOOR_STEPS
#define ISLAND_STEPS    4.0     //HEIGHT_ISLAND_STEPS
#define BALCONY_STEPS   2.0     //HEIGHT_BALCONY_STEPS
#define CRACK_DEEPEST   5.0     //HEIGHT_CRACK_DEEPEST
#define PLOT            1.8
#define DECK_H          -39.0   //TERRAIN_DECK_Y: under this, the clouds

//Which way a plate went when the ground opened.
#define G_WEST      0
#define G_ISLAND    1
#define G_EAST      2
#define G_GONE      3

//What a point is, for colouring and for the game's view.
#define K_PLATEAU   0
#define K_BALCONY   1
#define K_ISLAND    2
#define K_FRINGE    3
#define K_CRACK     4
#define K_FLOOR     5
#define K_HAIRLINE  6
#define K_RUBBLE    7

//--- hash and noise: ChasmLayout.cpp's -------------------------------------------------------------
uint HashInts(int x, int z, uint s){
    uint h = s * 0x9E3779B1u ^ uint(x) * 0x85EBCA77u ^ uint(z) * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

uint Salt(uint k){
    return uint(seed) * 0x9E3779B1u + k;
}

float Hash01(uint h){
    return float(h & 0xFFFFu) / 65535.0;
}

//Smooth value noise, -1..1, varying over about `wavelength`.
float Noise(vec2 p, float wavelength, uint s){
    vec2 f = p / wavelength;
    vec2 i = floor(f);
    vec2 t = f - i;
    t = t * t * (3.0 - 2.0 * t);
    int x = int(i.x);
    int z = int(i.y);
    float v00 = float(HashInts(x,z,s) & 0xFFFFu) / 32767.5 - 1.0;
    float v10 = float(HashInts(x + 1,z,s) & 0xFFFFu) / 32767.5 - 1.0;
    float v01 = float(HashInts(x,z + 1,s) & 0xFFFFu) / 32767.5 - 1.0;
    float v11 = float(HashInts(x + 1,z + 1,s) & 0xFFFFu) / 32767.5 - 1.0;
    float a = v00 + (v10 - v00) * t.x;
    float b = v01 + (v11 - v01) * t.x;
    return a + (b - a) * t.y;
}

//The same for the cell on either side of a border, whichever side asks.
uint EdgeHash(ivec2 a, ivec2 b, uint k){
    uint ka = HashInts(a.x,a.y,Salt(30u));
    uint kb = HashInts(b.x,b.y,Salt(30u));
    return HashInts(int(min(ka,kb)),int(max(ka,kb)),Salt(k));
}

//--- the rift ------------------------------------------------------------------------------------------
float MapDepth(){
    return map_width * iResolution.y / iResolution.x;
}

float SpineX(float y){
    float swing = 0.75 * Noise(vec2(0.0,y),170.0,Salt(11u)) + 0.25 * Noise(vec2(0.0,y),60.0,Salt(12u));
    return 0.5 * map_width + rift_wander * swing;
}

//How far open, 0..1: always 1 unless animating.
float Opening(){
    return animate ? smoothstep(0.05,0.75,fract(iTime / 10.0)) : 1.0;
}

//Half the opening at y, fully open: widest in the south, closing to nothing at the tip. North of the
//tip the plates still meet along the same line, which reads as the crack running on, shut.
float OpenFull(float y){
    float tip = rift_tip * MapDepth();
    float t = clamp((tip - y) / (0.5 * tip),0.0,1.0);
    return rift_open * sqrt(t) * (1.0 + 0.3 * Noise(vec2(y,0.0),90.0,Salt(13u)));
}

/*
    Which way a plate went, from where its seed was before anything opened. A plate is decided whole,
    by its seed - never point by point - which is what keeps every edge of the chasm an edge of a
    plate.
*/
int CellGroup(vec2 c, ivec2 id){
    if (mode == 2){
        return G_WEST;
    }
    float side = c.x - SpineX(c.y);
    float o = OpenFull(c.y);
    float h = Hash01(HashInts(id.x,id.y,Salt(21u)));
    if (mode == 0){
        bool f_island = o > 0.5 * plate_size && abs(side) < 0.5 * island_band * plate_size && h < island_chance;
        return f_island ? G_ISLAND : (side < 0.0 ? G_WEST : G_EAST);
    }
    //Eroded: each plate gives way at its own distance, so the channel's edge goes in and out by
    //whole plates rather than following the spine.
    float reach = o * Opening() * (0.55 + 0.9 * h);
    if (abs(side) < reach){
        float h2 = Hash01(HashInts(id.x,id.y,Salt(22u)));
        return (h2 < 0.35 * island_chance && abs(side) < 0.6 * reach) ? G_ISLAND : G_GONE;
    }
    return side < 0.0 ? G_WEST : G_EAST;
}

//How far a plate of group g slid east, at y.
float Shift(int g, float y){
    if (mode != 0){
        return 0.0;
    }
    float o = OpenFull(y) * Opening();
    return g == G_WEST ? -o : (g == G_EAST ? o : 0.0);
}

//--- plates ----------------------------------------------------------------------------------------------
//The space plates are cut in: borders that wander at the plate's scale, and waver at a plot's.
vec2 Warp(vec2 q){
    float a = warp * plate_size;
    vec2 w = a * vec2(Noise(q,1.1 * plate_size,Salt(1u)),Noise(q,1.1 * plate_size,Salt(2u)));
    w += 0.25 * a * vec2(Noise(q,0.3 * plate_size,Salt(3u)),Noise(q,0.3 * plate_size,Salt(4u)));
    w += wiggle * vec2(Noise(q,3.0,Salt(5u)),Noise(q,3.0,Salt(6u)));
    return q + w;
}

vec2 SeedOf(ivec2 id, float size, uint salt){
    vec2 o = vec2(Hash01(HashInts(id.x,id.y,salt)),Hash01(HashInts(id.x,id.y,salt + 1u))) - 0.5;
    return (vec2(id) + 0.5 + o * plate_jitter) * size;
}

struct Vor{
    vec2  c;        //the nearest seed
    ivec2 id;
    int   grp;      //its CellGroup, when asked for
    float b;        //distance to the nearest border
    ivec2 nid;      //the cell across that border
    float b_rim;    //distance to the nearest border with a cell of another group: the rim
};

/*
    Voronoi with the exact distance to the border (two passes, the second over 5x5 round the nearest
    seed). The border distance is what makes cracks of an even width; the nearest-seed distance
    would make them fat at one end and gone at the other.
*/
Vor Voronoi(vec2 w, float size, uint salt, bool f_groups){
    ivec2 base = ivec2(floor(w / size));
    float best = 1e20;
    Vor v;
    v.id = base;
    v.c = w;
    for (int j = -1; j <= 1; j++){
        for (int i = -1; i <= 1; i++){
            ivec2 id = base + ivec2(i,j);
            vec2 c = SeedOf(id,size,salt);
            float d = dot(c - w,c - w);
            if (d < best){
                best = d;
                v.id = id;
                v.c = c;
            }
        }
    }
    v.grp = f_groups ? CellGroup(v.c,v.id) : 0;
    v.b = 1e9;
    v.nid = v.id;
    v.b_rim = 1e9;
    //Only the crumbling reads b_rim, and only this close: classifying every neighbour is the
    //expensive part, so the far ones are not.
    float rim_needed = crumble_reach + 1.5 * sub_size;
    vec2 mr = v.c - w;
    for (int j = -2; j <= 2; j++){
        for (int i = -2; i <= 2; i++){
            ivec2 id = v.id + ivec2(i,j);
            if (i == 0 && j == 0){
                continue;
            }
            vec2 c = SeedOf(id,size,salt);
            vec2 r = c - w;
            vec2 dr = r - mr;
            float d = dot(0.5 * (mr + r),dr * inversesqrt(max(dot(dr,dr),1e-9)));
            if (d < v.b){
                v.b = d;
                v.nid = id;
            }
            if (f_groups && d < v.b_rim && d < rim_needed && CellGroup(c,id) != v.grp){
                v.b_rim = d;
            }
        }
    }
    return v;
}

/*
    Whether a plate stands on the rim, from its seed and its neighbours' - so the whole plate is
    decided at once and every point of it agrees. Neighbours by grid cell and distance rather than
    by shared border, which is near enough at this jitter.
*/
bool OnRim(ivec2 id, vec2 c, int grp){
    for (int j = -1; j <= 1; j++){
        for (int i = -1; i <= 1; i++){
            if (i == 0 && j == 0){
                continue;
            }
            ivec2 n = id + ivec2(i,j);
            vec2 cn = SeedOf(n,plate_size,Salt(0u));
            if (dot(cn - c,cn - c) < 2.25 * plate_size * plate_size && CellGroup(cn,n) != grp){
                return true;
            }
        }
    }
    return false;
}

//--- the ground ------------------------------------------------------------------------------------------
//Whether to draw the photo's hairlines: in the views coloured like it, and not on plots - a line
//thinner than a plot comes out as a scatter of dark plots that read as holes.
bool Hairlines(){
    return (view == 0 || view == 3) && !plot_grid;
}

struct Ground{
    float steps;    //down from the plateau, in steps
    int   kind;
    float tone;     //0..1, per plate or piece, for colour
    float edge;     //on a top: how far to its nearest drop
};

//The floor, and with detail the blocks that fell onto it.
Ground Floor(vec2 p, bool f_detail){
    Ground r = Ground(FLOOR_STEPS,K_FLOOR,0.5,1e9);
    if (f_detail){
        Vor s = Voronoi(Warp(p),0.8 * sub_size,Salt(60u),false);
        float h = Hash01(HashInts(s.id.x,s.id.y,Salt(61u)));
        if (h < 0.35 && s.b > 0.6){
            r.steps = FLOOR_STEPS - 1.0 - floor(3.0 * Hash01(HashInts(s.id.x,s.id.y,Salt(62u))));
            r.kind = K_RUBBLE;
            r.tone = h / 0.35;
        }
    }
    return r;
}

/*
    What stands at p. f_detail adds what only colours it - hairlines, rubble - and is off for the
    shadow taps, which only want the height.
*/
Ground Sample(vec2 p, bool f_detail){
    float side = p.x - SpineX(p.y);
    float o = OpenFull(p.y) * Opening();

    //Which plate p is on: one Voronoi per way a plate can have moved, and only where a plate that
    //moved that way could reach.
    Vor v;
    vec2 w;
    bool f_hit = false;
    if (mode == 0){
        float far = plate_size * (1.2 + 1.3 * warp) + wiggle + 2.0;
        if (side > o - far){
            w = Warp(p - vec2(o,0.0));
            v = Voronoi(w,plate_size,Salt(0u),true);
            f_hit = v.grp == G_EAST;
        }
        if (!f_hit && side < far - o){
            w = Warp(p + vec2(o,0.0));
            v = Voronoi(w,plate_size,Salt(0u),true);
            f_hit = v.grp == G_WEST;
        }
        if (!f_hit && abs(side) < 0.5 * island_band * plate_size + far){
            w = Warp(p);
            v = Voronoi(w,plate_size,Salt(0u),true);
            f_hit = v.grp == G_ISLAND;
        }
    }else{
        w = Warp(p);
        v = Voronoi(w,plate_size,Salt(0u),true);
        f_hit = v.grp != G_GONE;
    }
    if (!f_hit){
        return Floor(p,f_detail);
    }

    Ground r;
    float top = v.grp == G_ISLAND ? ISLAND_STEPS : 0.0;
    r.kind = v.grp == G_ISLAND ? K_ISLAND : K_PLATEAU;
    //A plate on the rim may slump two steps in one piece: a balcony a whole plate wide, with the
    //plateau's wall behind it and the drop in front - a slump block, as rift walls really have.
    if (v.grp != G_ISLAND && mode != 2 && abs(side) - o < 2.5 * plate_size &&
        OpenFull(v.c.y) > 0.4 * plate_size && Opening() > 0.5 &&
        Hash01(HashInts(v.id.x,v.id.y,Salt(23u))) < slump_chance && OnRim(v.id,v.c,v.grp)){
        top = BALCONY_STEPS;
        r.kind = K_BALCONY;
    }
    r.steps = top;
    r.tone = Hash01(HashInts(v.id.x,v.id.y,Salt(31u)));
    r.edge = v.b_rim;

    //1 at the rim, 0 from crack_reach on: what opens the side cracks.
    float rim_d = mode == 0 ? abs(side) - o : abs(side) - OpenFull(p.y) * Opening();
    float near = mode == 2 ? 0.5 : clamp(1.0 - max(rim_d,0.0) / crack_reach,0.0,1.0);

    //The pieces of this plate, which break away near the rim and show as hairlines elsewhere.
    bool f_crumble_zone = mode != 2 && v.b_rim < crumble_reach + 1.5 * sub_size;
    Vor s;
    if (f_crumble_zone || (f_detail && Hairlines())){
        s = Voronoi(w,sub_size,Salt(40u),false);
    }
    if (f_crumble_zone){
        /*
            Decided by the piece's seed, so a piece breaks whole: how far IT stands from the rim. Only
            pieces about touching the drop - one further back would leave a pit in the plate, which
            the game fills back in anyway - and only where the rift has opened: north of the tip the
            two sides still meet, and nothing has anywhere to fall.
        */
        Vor ps = Voronoi(s.c,plate_size,Salt(0u),true);
        if (ps.id == v.id && ps.b_rim < crumble_reach){
            float opened = clamp(OpenFull(s.c.y) * Opening() / (0.5 * sub_size),0.0,1.0);
            float chance = crumble * opened;
            if (Hash01(HashInts(s.id.x,s.id.y,Salt(41u))) < chance){
                float h2 = Hash01(HashInts(s.id.x,s.id.y,Salt(42u)));
                if (h2 < 0.2){
                    return Floor(p,f_detail);
                }
                if (h2 < 0.55 && r.kind == K_PLATEAU){
                    r.steps = top + BALCONY_STEPS;
                    r.kind = K_BALCONY;
                }else{
                    r.steps = top + 1.0 + floor(5.0 * Hash01(HashInts(s.id.x,s.id.y,Salt(43u))));
                    r.kind = K_FRINGE;
                }
                r.tone = h2;
                r.edge = s.b;
            }
        }
    }

    //A border with a plate that went the same way: a crack, deep and wide by the chasm, a shallow
    //one now and then away from it, a hairline otherwise.
    if (v.b < 4.0 * crack_width + 1.0 && CellGroup(SeedOf(v.nid,plate_size,Salt(0u)),v.nid) == v.grp){
        uint e = EdgeHash(v.id,v.nid,50u);
        bool f_open = Hash01(e) < mix(crack_far,1.0,near);
        float along = 0.75 + 0.25 * Noise(w,7.0,Salt(51u));
        float hw = f_open ? crack_width * (0.35 + 1.65 * near) * along * (0.7 + 0.6 * Hash01(e >> 16)) : 0.0;
        if (f_open && v.b < hw){
            float d = top + 1.0 + floor((CRACK_DEEPEST - 1.0) * near + 0.5);
            if (d > r.steps){
                r.steps = min(d,FLOOR_STEPS);
                r.kind = K_CRACK;
                r.tone = near;
            }
        }else if (f_detail && Hairlines() && v.b < 0.3 * along && (r.kind == K_PLATEAU || r.kind == K_ISLAND)){
            r.kind = K_HAIRLINE;
        }
        r.edge = min(r.edge,v.b - hw);
    }

    //Hairlines inside a plate, along its pieces' borders: the photo's finest cracks. Only colour.
    if (f_detail && Hairlines() && (r.kind == K_PLATEAU || r.kind == K_ISLAND)){
        if (Hash01(EdgeHash(s.id,s.nid,52u)) < 0.55 && s.b < 0.22){
            r.kind = K_HAIRLINE;
        }
    }
    return r;
}

//--- drawing -----------------------------------------------------------------------------------------------
vec3 KindColour(Ground g){
    if (g.kind == K_PLATEAU)  return vec3(0.42,0.60,0.28);
    if (g.kind == K_HAIRLINE) return vec3(0.36,0.52,0.24);
    if (g.kind == K_BALCONY)  return vec3(0.92,0.76,0.24);
    if (g.kind == K_ISLAND)   return vec3(0.30,0.70,0.86);
    if (g.kind == K_CRACK)    return mix(vec3(0.85,0.45,0.30),vec3(0.65,0.12,0.10),g.steps / FLOOR_STEPS * 2.0);
    if (g.kind == K_FRINGE)   return vec3(0.62,0.52,0.42) * (1.0 - 0.05 * g.steps);
    if (g.kind == K_RUBBLE)   return vec3(0.20,0.20,0.30);
    return vec3(0.12,0.13,0.24);
}

vec3 MudColour(Ground g, vec2 p, bool f_steam){
    vec3 mud = vec3(0.80,0.61,0.43);
    vec3 pale = vec3(0.92,0.87,0.80);
    vec3 dark = vec3(0.24,0.14,0.08);
    if (g.kind == K_HAIRLINE){
        return dark * 1.6;
    }
    if (g.kind == K_CRACK){
        return dark * (1.0 - 0.5 * g.tone);
    }
    vec3 col = mud * (0.84 + 0.3 * g.tone);
    //Salt-pale crust, as in the photo: thicker on some plates than others, so it changes at the
    //cracks rather than drifting across them.
    float salt = smoothstep(0.15,0.7,Noise(p,38.0,Salt(70u)) + 0.4 * Noise(p,9.0,Salt(71u)) + 0.8 * (g.tone - 0.5));
    col = mix(col,pale,0.4 * salt);
    //The crumbled lip round every top.
    col = mix(col,pale,lip * (1.0 - smoothstep(0.0,1.8,g.edge)));
    col *= 1.0 - 0.055 * g.steps;
    if (f_steam && (g.kind == K_FLOOR || g.kind == K_RUBBLE)){
        //Steam over the floor, seen from straight above, drifting with the sim's clock.
        float puff = 0.5 + 0.5 * Noise(p + iTime * vec2(1.5,4.0),26.0,Salt(72u));
        col = mix(col,vec3(0.80,0.82,0.85),0.35 + 0.35 * puff);
    }
    return col;
}

//--- sun -----------------------------------------------------------------------------------------------------
vec2 PlotOf(vec2 p){
    return plot_grid ? (floor(p / PLOT) + 0.5) * PLOT : p;
}

//The ground's height at p, in world units: 0 the plateau, -72 the floor.
float GroundH(vec2 p){
    return -Sample(PlotOf(p),false).steps * STEP;
}

//Toward the sun, in the map's x east, y north, h up: from the north-west, sun_elevation over the run.
vec3 SunDir(){
    return normalize(vec3(-0.6,0.8,sun_elevation));
}

/*
    How lit a point at height h over p is, by walking toward the sun over the height field. The walls
    are sheer, so from above the shadows they throw are what shows the depth. Taps grow
    geometrically: the near ones catch cracks, the far ones walls.
*/
float SunLight(vec2 p, float h){
    vec2 toward = vec2(-0.6,0.8);
    float lit = 1.0;
    float t = 1.5;
    for (int k = 0; k < shadow_taps; k++){
        //Nothing stands above the plateau, so once the line to the sun is over it nothing further
        //along can be in the way - which leaves the plateau itself, most of any view, with no taps.
        if (h + t * sun_elevation > 0.0 || lit <= 0.0){
            break;
        }
        float over = GroundH(p + toward * t) - (h + t * sun_elevation);
        lit = min(lit,clamp(1.0 - over / 3.0,0.0,1.0));
        t *= 1.75;
    }
    return lit;
}

//--- 3D --------------------------------------------------------------------------------------------------------
/*
    The march runs in map space - x east, y north, h up, world units, the plateau at h = 0. The
    bench's camera lives in its own units with north along -Z (the engine's forward), so it is
    scaled and turned into map space here, once, and nothing below knows about it.
*/
vec3 MapFromBench(vec3 b){
    return vec3(b.x / world_scale + 0.5 * map_width,0.5 * MapDepth() - b.z / world_scale,b.y / world_scale);
}

vec3 Sky(vec3 d){
    if (d.z < 0.0){
        return vec3(0.07,0.07,0.08);    //under the horizon and off the map: the map stands in nothing
    }
    return mix(vec3(0.82,0.80,0.76),vec3(0.46,0.60,0.76),sqrt(d.z));
}

//A wall face: strata a step thick, the plate's dried crust along its top, darker the deeper.
vec3 WallColour(vec3 P, vec3 n, float top){
    float band = -P.z / STEP;
    vec3 rock = vec3(0.56,0.41,0.29) * (0.85 + 0.3 * Hash01(HashInts(int(floor(band)),0,Salt(80u))));
    rock *= 0.82 + 0.18 * smoothstep(0.0,0.12,fract(band));
    float along = dot(P.xy,vec2(-n.y,n.x));
    rock *= 0.9 + 0.1 * Noise(vec2(along,P.z * 0.2),1.5,Salt(81u));
    rock = mix(rock,vec3(0.80,0.61,0.43),smoothstep(1.2,0.3,top - P.z));
    return rock * (1.0 - 0.03 * band);
}

/*
    The ground in 3D. There is no distance bound to march by - the walls are sheer, so ground can
    stand up anywhere - so a ray steps a few PIXELS' worth of ground at a time, which keeps the
    near ground fine and the far ground cheap, never further than the drop to the ground under it,
    and bisects the step it went under in. A top is then exact, since tops are flat; anything else
    is a wall, turned by the slope of the heights round it.
*/
vec3 Render3D(){
    vec3 o = MapFromBench(iCamPos);
    vec3 db = FxViewRay();
    vec3 d = normalize(vec3(db.x,-db.z,db.y));
    vec3 ds = vec3(abs(d.x) < 1e-6 ? 1e-6 : d.x,abs(d.y) < 1e-6 ? 1e-6 : d.y,abs(d.z) < 1e-6 ? 1e-6 : d.z);

    //The map as a block, from a hair over the plateau to under the floor.
    vec3 lo = vec3(0.0,0.0,-FLOOR_STEPS * STEP - 6.0);
    vec3 hi = vec3(map_width,MapDepth(),0.01);
    vec3 ta = (lo - o) / ds;
    vec3 tb = (hi - o) / ds;
    vec3 tn = min(ta,tb);
    vec3 tf = max(ta,tb);
    float t_in = max(max(tn.x,tn.y),max(tn.z,0.0));
    float t_out = min(min(tf.x,tf.y),tf.z);
    if (t_out <= t_in){
        return Sky(d);
    }

    float pixel = 2.0 * length(iCamUp) / iResolution.y;    //world units per pixel, a unit away
    float flat_len = max(length(d.xy),0.02);
    float t = t_in;
    vec3 P = o + d * t;
    //Coming in through the top, the plateau's height is all there is to know; only a ray coming in
    //through a side has to look at the ground to see whether it hit the block's own face.
    float g = tn.z == t_in ? 0.0 : GroundH(P.xy);
    bool f_hit = P.z <= g;
    bool f_wall = f_hit;            //under the ground where it enters: the block's own side
    vec3 n = -vec3(tn.x == t_in ? sign(ds.x) : 0.0,tn.y == t_in ? sign(ds.y) : 0.0,0.0);
    float top = g;
    float t_prev = t;
    float g_prev = g;
    for (int i = 0; i < march_steps && !f_hit; i++){
        float dt_flat = clamp(march_step * pixel * t,0.6,6.0) / flat_len;
        float dt_down = d.z < 0.0 ? (P.z - g) / -d.z + 0.02 : 1e9;
        t_prev = t;
        g_prev = g;
        t += min(dt_flat,dt_down);
        if (t > t_out){
            return Sky(d);
        }
        P = o + d * t;
        g = GroundH(P.xy);
        f_hit = P.z <= g;
    }
    if (f_hit && !f_wall && g == g_prev){
        //The same height either side of the step - nearly every ray, landing on a top: exact
        //already, and bisecting it would only cost six more looks at the ground.
        t = clamp((g - o.z) / ds.z,t_prev,t);
        P = o + d * t;
        top = g;
        n = vec3(0.0,0.0,1.0);
    }else if (f_hit && !f_wall){
        float a = t_prev;
        float b = t;
        for (int k = 0; k < 6; k++){
            float m = 0.5 * (a + b);
            vec3 Q = o + d * m;
            if (Q.z <= GroundH(Q.xy)){
                b = m;
            }else{
                a = m;
            }
        }
        vec3 B = o + d * b;
        float ga = GroundH((o + d * a).xy);
        top = GroundH(B.xy);
        if (top - ga > 0.5 && B.z < top - 0.05){
            //The ground stepped up under the ray between a and b: a wall, facing downhill.
            f_wall = true;
            P = B;
            float e = 0.6;
            vec2 grad = vec2(GroundH(B.xy - vec2(e,0.0)) - GroundH(B.xy + vec2(e,0.0)),
                             GroundH(B.xy - vec2(0.0,e)) - GroundH(B.xy + vec2(0.0,e)));
            n = vec3(length(grad) > 0.0 ? normalize(grad) : -d.xy / flat_len,0.0);
        }else{
            t = clamp((top - o.z) / ds.z,a,b);
            P = o + d * t;
            n = vec3(0.0,0.0,1.0);
        }
    }else if (!f_hit){
        //Out of steps, deep in the chasm: the steam below takes it.
        n = vec3(0.0,0.0,1.0);
    }
    if (f_wall && dot(n,n) < 0.5){
        n = vec3(-d.xy / flat_len,0.0);
    }

    vec3 sun = SunDir();
    float lit = SunLight(P.xy + n.xy * 0.3,P.z + 0.05);
    vec3 col;
    if (f_wall){
        col = WallColour(P,n,top);
    }else{
        Ground gd = Sample(PlotOf(P.xy),true);
        col = view == 4 ? KindColour(gd) : MudColour(gd,P.xy,false);
    }
    col *= 0.45 + 0.9 * lit * max(dot(n,sun),0.0);

    //Steam: how much of the ray ran under the cloud deck INSIDE the map, thicker in drifting puffs.
    //Outside it there is no chasm to hold any, and the block's own sides stay clear.
    float t_deck = max(t_in,o.z <= DECK_H ? 0.0 : (d.z < 0.0 ? (DECK_H - o.z) / d.z : 1e9));
    float under = max(0.0,t - t_deck);
    if (under > 0.0){
        vec3 Q = o + d * t_deck;
        float puff = 0.5 + 0.5 * Noise(Q.xy + iTime * vec2(1.5,4.0),26.0,Salt(72u));
        col = mix(col,vec3(0.84,0.85,0.88),1.0 - exp(-under * steam_density * (0.5 + puff)));
    }
    return mix(col,vec3(0.82,0.80,0.76),1.0 - exp(-t * 0.0005));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord){
    if (view >= 3){
        fragColor = vec4(min(Render3D(),vec3(1.0)),1.0);
        return;
    }
    float depth = MapDepth();
    vec2 uv = fragCoord / iResolution.xy;
    vec2 p = (uv - 0.5) * vec2(map_width,depth) / zoom + 0.5 * vec2(map_width,depth) + pan;
    float px = map_width / zoom / iResolution.x;
    if (p.x < 0.0 || p.y < 0.0 || p.x > map_width || p.y > depth){
        fragColor = vec4(0.07,0.07,0.08,1.0);
        return;
    }
    vec2 ps = PlotOf(p);
    Ground g = Sample(ps,true);
    float lit = SunLight(ps,-g.steps * STEP);

    vec3 col;
    if (view == 1){
        col = KindColour(g) * (0.7 + 0.3 * lit);
        if (plot_grid && zoom > 3.0){
            vec2 f = abs(fract(p / PLOT) - 0.5);
            col *= 0.85 + 0.15 * smoothstep(0.46,0.42,max(f.x,f.y));
        }
    }else if (view == 2){
        col = vec3(1.0 - g.steps / FLOOR_STEPS) * (0.6 + 0.4 * lit);
    }else{
        col = MudColour(g,p,true) * (0.5 + 0.5 * lit);
    }
    if (show_spine && abs(p.x - SpineX(p.y)) < 1.5 * px){
        col = vec3(1.0,0.15,0.1);
    }
    fragColor = vec4(pow(col,vec3(1.0)),1.0);
}

#include "shadertoy_main.glsl"
