#include "ApplicationChasm.h"
#include "MeshBuild.h"
#include "Mist.h"
#include "Palette.h"

#include <cmath>

/*
    EXPLORATION AND THE CLOUDS (docs/play_mode_plan.md, steps 2-4).

    The simulation's half: what has been seen (Exploration.h) grows round everything built - each
    plot, field cell and road once, when the zones change - and round every person out of doors,
    every EXPLORATION_PERSON_TICKS. A play zone command under unexplored ground is refused.

    The view's half: CLOUDS over what is not explored - lumpy flat-shaded white puffs like the chasm's
    mist (Mist.h; they breathe on the GPU the same way, core's INSTANCE_MOTION_PUFF), on a jittered
    lattice lying on the land, one instance set per terrain chunk per shade. Toward the explored edge
    they shrink and sink, so the cloud thins away rather than stopping at a line. Drawn in play mode;
    in debug mode only with the View panel's "clouds".
*/

/*
    The blanket LIES ON THE GROUND (user, 2026-10-06): a puff's middle a little over the ground under
    it, its flat underside buried, so there is no gap to look under from any pitch the game's camera
    allows - and it follows the land up the mountain. Tall enough to swallow the trees standing in
    it. Small puffs packed tight, so it reads as a soft heap rather than a few big balls.
*/
#define CLOUD_SPACING       4.5f    //world units between puffs on the lattice
#define CLOUD_JITTER        1.6f    //how far a puff strays from its lattice point
#define CLOUD_RADIUS_MIN    3.2f
#define CLOUD_RADIUS_MAX    4.8f
#define CLOUD_LIFT          1.6f    //a puff's middle over the ground below it
#define CLOUD_LIFT_VARY     1.4f    //and up to this much higher, so the top is lumpy
#define CLOUD_EDGE          8.0f    //over this far from explored ground a puff is at full size
#define CLOUD_FLAT          0.9f    //a puff's height against its width

//--- The simulation ---------------------------------------------------------------------------------

//PHYSICS THREAD. A new map, or a load: nothing explored, and nothing yet cleared round.
void ApplicationChasm::ResetExploration(){
    std::shared_ptr<const ChasmWorld> w = zones.GetWorld();
    exploration = Exploration();
    if (w){
        exploration.Reset(w->grid->bounds_min,w->grid->bounds_max);
    }
    explored_plot.assign(w ? w->grid->fine.pos.size() : 0,0);
    explored_cell.assign(w ? w->grid->coarse.quads.size() : 0,0);
    explored_zones_version = 0xFFFFFFFFu;
    PublishExploration();
}

/*
    PHYSICS THREAD, every tick. Round what is built, once a plot (explored never goes back, so a plot
    cleared round needs no second look - which keeps a big town's every repaint cheap), and round the
    people out of doors every few ticks.
*/
void ApplicationChasm::TickExploration(){
    std::shared_ptr<const ChasmWorld> w = zones.GetWorld();
    if (!w){
        return;
    }
    if (exploration.Empty()){
        ResetExploration();
    }
    const Grid& g = *w->grid;
    const ZoneState& z = zones.State();
    bool f_changed = false;
    if (z.version != explored_zones_version){
        explored_zones_version = z.version;
        size_t n = std::min(explored_plot.size(),std::min(z.building.size(),z.ground.size()));
        for (size_t v = 0; v < n; v++){
            if (!explored_plot[v] && (z.building[v] || z.ground[v])){
                explored_plot[v] = 1;
                f_changed = exploration.Reveal(g.fine.pos[v],EXPLORATION_BUILT_RADIUS) || f_changed;
            }
        }
        size_t m = std::min(explored_cell.size(),z.field.size());
        for (size_t c = 0; c < m; c++){
            if (!explored_cell[c] && z.field[c]){
                explored_cell[c] = 1;
                f_changed = exploration.Reveal(g.FineQuadCentre((int)c * 4),EXPLORATION_BUILT_RADIUS) || f_changed;
            }
        }
    }
    if (calendar_tick % EXPLORATION_PERSON_TICKS == 0){
        for (const EconomyWorker& k : economy.State().workers){
            if (!k.f_indoors){
                f_changed = exploration.Reveal(k.pos,EXPLORATION_PERSON_RADIUS) || f_changed;
            }
        }
    }
    if (f_changed){
        PublishExploration();
    }
}

void ApplicationChasm::PublishExploration(){
    std::shared_ptr<const Exploration> copy = std::make_shared<Exploration>(exploration);
    std::lock_guard<std::mutex> lock(grid_mutex);
    exploration_snapshot = copy;
}

std::shared_ptr<const Exploration> ApplicationChasm::GetExploration(){
    std::lock_guard<std::mutex> lock(grid_mutex);
    return exploration_snapshot;
}

//PHYSICS THREAD, from the zone command: the plot's point, or the middle of a field's coarse cell.
bool ApplicationChasm::ZoneCommandExplored(const ChasmWorld& w, int op, int index){
    const Grid& g = *w.grid;
    bool f_field = (op == ZONE_OP_FIELD_PAINT || op == ZONE_OP_FIELD_ERASE || op == ZONE_OP_FIELD_CROP);
    vec2 p(0.0f,0.0f);
    if (f_field){
        if (index < 0 || index >= (int)g.coarse.quads.size()){
            return true;    //nothing there to refuse: the zones refuse it themselves
        }
        for (int k = 0; k < 4; k++){
            p += g.FineQuadCentre(index * 4 + k) * 0.25f;
        }
    }else{
        if (index < 0 || index >= (int)g.fine.pos.size()){
            return true;
        }
        p = g.fine.pos[index];
    }
    return exploration.Explored(p);
}

//--- The view ---------------------------------------------------------------------------------------

//RENDER THREAD, from BuildScene: two shades of puff, the foam's white and the frozen lip's blue-white.
void ApplicationChasm::BuildCloudScene(){
    std::vector<vertex> verts;
    const int columns[2] = {PAL_FOAM,PAL_LIP};
    const int rows[2] = {PAL_EFFECTS,PAL_FROZEN};
    for (int i = 0; i < 2; i++){
        BuildPuffMesh(31u + (uint32_t)i * 7u,columns[i],rows[i],verts);
        cloud_meshes[i] = new Mesh();
        cloud_meshes[i]->SetMeshData(verts.data(),(int)verts.size());
        cloud_meshes[i]->Retain();      //held for good - see object_setmesh_releases_old
    }
}

/*
    RENDER THREAD. Rebuilt whole when the exploration, the map or the showing changes - a few
    thousand puffs, each asking how far explored ground is; the exploration changes a few times a
    second at most, while people walk into new ground.
*/
void ApplicationChasm::UploadClouds(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const Exploration> ex = GetExploration();
    bool f_show = PlayMode() || f_view_clouds;
    if (!w || !w->mesh){
        return;
    }
    const TerrainMeshData& mesh = *w->mesh;
    size_t chunks = mesh.chunks.size();
    size_t need = chunks * 2;
    if (cloud_sets.size() < need){
        main_scene->AtTickBoundary([&](){
            while (cloud_sets.size() < need){
                Object* o = new Object();
                o->name = "Cloud set " + std::to_string(cloud_sets.size());
                o->SetMesh(cloud_meshes[cloud_sets.size() % 2]);
                o->SetMaterialSlot(0,puff_material);
                o->SetVisualOnly(true);
                o->SetPickability(false);
                //Their shade on each other is what makes a blanket read as heaped (as the mist's);
                //on the explored edge they shadow the ground too, as a cloud would.
                o->SetCastsShadow(true);
                o->SetInstances(std::vector<fmat4>());
                o->SetVisibility(false);
                main_scene->AddObject(o);
                cloud_sets.push_back(o);
            }
        });
    }
    if (ex == clouds_built_for && w == clouds_built_world && f_show == f_clouds_shown){
        return;
    }
    clouds_built_for = ex;
    clouds_built_world = w;
    f_clouds_shown = f_show;

    std::vector<std::vector<fmat4>> sets(need);
    std::vector<std::vector<vec4>> motion(need);
    const Grid& g = *w->grid;
    if (f_show && cloud_lattice_world != w){
        /*
            The lattice, once a map. A puff sits on the HIGHEST ground under its footprint, so a steep
            mountainside cannot rise between two puffs - and never below the plateau's level, so over
            the chasm the blanket is a lid at the rim rather than a sag showing the void through it.
        */
        cloud_lattice_world = w;
        cloud_lattice.clear();
        const Terrain& t = *w->terrain;
        float plateau = terrain_levels[TERRAIN_PLATEAU].height;
        int nx = (int)std::ceil((g.bounds_max.x - g.bounds_min.x) / CLOUD_SPACING);
        int nz = (int)std::ceil((g.bounds_max.y - g.bounds_min.y) / CLOUD_SPACING);
        const vec2 around[4] = {vec2(1.0f,0.0f),vec2(-1.0f,0.0f),vec2(0.0f,1.0f),vec2(0.0f,-1.0f)};
        for (int iz = 0; iz <= nz; iz++){
            for (int ix = 0; ix <= nx; ix++){
                CloudSpot spot;
                spot.hash = MeshHash((uint32_t)ix,(uint32_t)iz,0xC10Du);
                float jx = (float)(MeshHash(spot.hash,1) & 0xFFFFFF) / (float)0x1000000;
                float jz = (float)(MeshHash(spot.hash,2) & 0xFFFFFF) / (float)0x1000000;
                spot.p = g.bounds_min + vec2((float)ix,(float)iz) * CLOUD_SPACING +
                         vec2(jx - 0.5f,jz - 0.5f) * (2.0f * CLOUD_JITTER);
                spot.ground = std::max(plateau,t.GroundHeight(spot.p,plateau));
                for (const vec2& a : around){
                    spot.ground = std::max(spot.ground,t.GroundHeight(spot.p + a * (CLOUD_RADIUS_MAX * 0.7f),plateau));
                }
                cloud_lattice.push_back(spot);
            }
        }
    }
    if (f_show && ex && !ex->Empty()){
        for (const CloudSpot& spot : cloud_lattice){
            uint32_t h = spot.hash;
            auto unit = [h](uint32_t salt){
                return (float)(MeshHash(h,salt) & 0xFFFFFF) / (float)0x1000000;
            };
            const vec2& p = spot.p;
            if (ex->Explored(p)){
                continue;
            }
            /*
                Toward explored ground a puff shrinks a little and sinks into the ground: the bank
                slopes down to its edge rather than ending in a row of small balls.
            */
            float d = ex->DistanceToExplored(p,CLOUD_EDGE);
            float s = std::max(0.0f,std::min(1.0f,d / CLOUD_EDGE));
            s = s * s * (3.0f - 2.0f * s);
            float ground = spot.ground;
            MistPuff puff;
            puff.radius = (CLOUD_RADIUS_MIN + (CLOUD_RADIUS_MAX - CLOUD_RADIUS_MIN) * unit(3)) *
                          (0.7f + 0.3f * s);
            float y = ground + CLOUD_LIFT + CLOUD_LIFT_VARY * unit(4) * s - puff.radius * 0.6f * (1.0f - s);
            puff.pos = vec3(p.x,y,p.y);
            puff.flat = CLOUD_FLAT;
            puff.phase = unit(5);
            puff.period = 14.0f + 8.0f * unit(6);
            puff.bob = 0.25f;       //low: more and the underside lifts off the ground
            puff.breath = 0.05f;
            int shade = (unit(7) < 0.7f) ? 0 : 1;
            int cx = std::max(0,std::min(mesh.chunks_x - 1,(int)((p.x - g.bounds_min.x) / TERRAIN_CHUNK_SIZE)));
            int cz = std::max(0,std::min(mesh.chunks_z - 1,(int)((p.y - g.bounds_min.y) / TERRAIN_CHUNK_SIZE)));
            size_t set = ((size_t)cz * mesh.chunks_x + cx) * 2 + shade;
            sets[set].push_back(MistRest(puff));
            vec4 mv[2];
            MistMotion(puff,mv);
            motion[set].push_back(mv[0]);
            motion[set].push_back(mv[1]);
        }
    }
    for (size_t i = 0; i < cloud_sets.size(); i++){
        Object* o = cloud_sets[i];
        if (i >= need){
            o->SetInstances(std::vector<fmat4>());
            o->SetVisibility(false);
            continue;
        }
        bool f_any = !sets[i].empty();
        o->SetInstances(std::move(sets[i]));
        o->SetInstanceMotion(std::move(motion[i]),MIST_MOTION_PAD);
        o->SetVisibility(f_any);
    }
}
