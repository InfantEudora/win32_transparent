#include "ApplicationChasm.h"
#include "Areas.h"
#include "Forest.h"
#include "GridPick.h"
#include "Terrain.h"
#include "MeshBuild.h"
#include "Mist.h"
#include "Palette.h"
#ifdef USE_MCP
#include "MCPServer.h"
#endif

#include <cmath>

/*
    EXPLORATION AND THE CLOUDS (docs/play_mode_plan.md, steps 2-4).

    The simulation's half: what has been seen (Exploration.h) grows by what every person out of doors
    can see, every EXPLORATION_PERSON_TICKS - a line of sight over the ground, not a disc, and nothing
    round what is merely placed. A play zone command under unexplored ground is refused.

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

//PHYSICS THREAD. A new map, or a load: nothing explored, and nothing yet looked round from.
void ApplicationChasm::ResetExploration(){
    std::shared_ptr<const ChasmWorld> w = zones.GetWorld();
    exploration = Exploration();
    if (w){
        exploration.Reset(w->grid->bounds_min,w->grid->bounds_max);
    }
    looked_from.assign(exploration.cells.size(),NAN);
    f_seams_stale = true;       //a load fills the exploration in after this: the seams are looked at on the next tick
    f_areas_stale = true;       //...and the areas
    PublishExploration();
    //...and the discoveries worked out again, whatever matches - a save of the same map keeps the same world - once
    //there is something to work them out from: by a load, or on a new colony's first tick.
    discoveries_world = nullptr;
}

/*
    PHYSICS THREAD. A SEAM is seen once any plot along its foot is explored - from the rim, the air over a balcony is
    what is looked at, so a balcony's seams come into sight from the edge above. Told as a notice, which is never saved
    or hashed, so this list is not either.
*/
void ApplicationChasm::LookForSeams(const ChasmWorld& w, uint32_t person){
    bool f_tell = !f_seams_stale;
    if (f_seams_stale || seam_seen.size() != (w.deposits ? w.deposits->deposits.size() : 0)){
        seam_seen.assign(w.deposits ? w.deposits->deposits.size() : 0,0);
        f_seams_stale = false;
        f_tell = false;
    }
    for (size_t i = 0; i < seam_seen.size(); i++){
        const Deposit& d = w.deposits->deposits[i];
        if (seam_seen[i] || d.kind != DEPOSIT_SEAM){
            continue;
        }
        for (int p : d.plots){
            if (exploration.Explored(w.grid->fine.pos[p])){
                seam_seen[i] = 1;
                break;
            }
        }
        if (seam_seen[i] && f_tell){
            economy.SeamSeen(person);
        }
    }
}

/*
    PHYSICS THREAD. An AREA (Areas.h, areas_plan.md) is seen once any of its cells is explored - the seams' rule - and
    told as EVENT_DISCOVERED, with where it was seen and whether it is the first of its kind: one already seen, even
    silently at the landing, makes the next one not the first. Only the areas not seen yet are looked through, each
    until one of its cells is explored.
*/
void ApplicationChasm::LookForAreas(const ChasmWorld& w, uint32_t person, bool f_tell){
    const AreaData* data = w.areas.get();
    size_t n = data ? data->areas.size() : 0;
    if (f_areas_stale || area_seen.size() != n){
        area_seen.assign(n,0);
        f_areas_stale = false;
        f_tell = false;
    }
    for (size_t i = 0; i < n; i++){
        if (area_seen[i]){
            continue;
        }
        const Area& a = data->areas[i];
        int found = -1;
        for (int c : a.cells){
            if (c < (int)exploration.cells.size() && exploration.cells[(size_t)c]){
                found = c;
                break;
            }
        }
        if (found < 0){
            continue;
        }
        bool f_first = true;
        for (size_t j = 0; j < n; j++){
            f_first = f_first && !(area_seen[j] && data->areas[j].kind == a.kind);
        }
        area_seen[i] = 1;
        if (f_tell){
            economy.Discovered(person,(int)i,exploration.CellCentre(found),f_first);
        }
    }
}

/*
    The eye's ground is the person's own plot's, at his cell's middle - not the cell's, which at a rim
    may be the chasm's floor below him. At the middle rather than where he stands, so it is the same for
    everyone in the cell on that plot, and a look from there need only be taken once (looked_from).
*/
float ApplicationChasm::SightEyeGround(const ChasmWorld& w, const Exploration& ex, int cell, const vec2& at){
    GridPick pick = w.picker->Pick(at);
    if (pick.plot < 0){
        return w.sight->ground[(size_t)cell];
    }
    return w.terrain->GroundHeight(ex.CellCentre(cell),w.terrain->Height(pick.plot));
}

//PHYSICS THREAD, every tick: every few ticks, each person out of doors looks round from where he stands.
void ApplicationChasm::TickExploration(){
    std::shared_ptr<const ChasmWorld> w = zones.GetWorld();
    if (!w || !w->sight){
        return;
    }
    if (exploration.Empty()){
        ResetExploration();
    }
    if (f_seams_stale){
        LookForSeams(*w,0);
    }
    if (f_areas_stale){
        LookForAreas(*w,0,false);
    }
    if (calendar_tick % EXPLORATION_PERSON_TICKS != 0){
        UpdateDiscoveries(*w);
        return;
    }
    //The LANDING: a new colony's first look round, from nothing explored, is home - what it brings into sight is
    //marked seen and told as nothing.
    const bool f_landing = exploration.CountExplored() == 0;
    bool f_changed = false;
    for (const EconomyWorker& k : economy.State().workers){
        int cell = exploration.CellAt(k.pos);
        if (k.f_indoors || cell < 0){
            continue;
        }
        float eye_ground = SightEyeGround(*w,exploration,cell,k.pos);
        if (looked_from[(size_t)cell] == eye_ground){
            continue;
        }
        looked_from[(size_t)cell] = eye_ground;
        if (exploration.See(*w->sight,cell,eye_ground)){
            f_changed = true;
            LookForSeams(*w,k.id);
            LookForAreas(*w,k.id,!f_landing);
        }
    }
    if (f_changed){
        PublishExploration();
    }
    LookForFirsts(*w);
    UpdateDiscoveries(*w);
}

//--- Discoveries (demo_plan.md step 6) ---------------------------------------------------------------

const char* ChasmDiscoveryName(int discovery){
    switch (discovery){
        case DISCOVERY_RIVER:       return "river";
        case DISCOVERY_CHASM:       return "chasm";
        case DISCOVERY_BOULDER:     return "boulder";
        case DISCOVERY_FLOATSTONE:  return "floatstone";
        case DISCOVERY_BUILDERS:    return "builders_hut";
        case DISCOVERY_WINCH:       return "winch";
        case DISCOVERY_BALCONY:     return "balcony";
        default:                    return "unknown";
    }
}

/*
    WHAT UNLOCKS WHAT (the user's, 2026-10-16): a river found brings the water collector and the bridge; the chasm,
    the winch; a boulder the stonecutter can break, his hut; floatstone seen, the mine; and a builders' hut standing,
    the wall, the range and the tower. Everything else is there from the landing.
*/
int ChasmToolNeeds(int tool){
    switch (tool){
        case CHASM_TOOL_WATER:
        case CHASM_TOOL_BRIDGE:         return DISCOVERY_RIVER;
        case CHASM_TOOL_WINCH:          return DISCOVERY_CHASM;
        case CHASM_TOOL_STONECUTTER:    return DISCOVERY_BOULDER;
        case CHASM_TOOL_MINE:           return DISCOVERY_FLOATSTONE;
        case CHASM_TOOL_WALL:
        case CHASM_TOOL_RANGE:
        case CHASM_TOOL_TOWER:          return DISCOVERY_BUILDERS;
        default:                        return -1;
    }
}

std::string ChasmDiscoveryTools(int discovery){
    static const std::pair<int,const char*> names[] = {
        {CHASM_TOOL_WATER,"WATER"},{CHASM_TOOL_BRIDGE,"BRIDGE"},{CHASM_TOOL_WINCH,"WINCH"},{CHASM_TOOL_STONECUTTER,"STONECUTTER"},
        {CHASM_TOOL_MINE,"MINE"},{CHASM_TOOL_WALL,"WALL"},{CHASM_TOOL_RANGE,"RANGE"},{CHASM_TOOL_TOWER,"TOWER"}};
    std::string out;
    for (const auto& n : names){
        if (ChasmToolNeeds(n.first) == discovery){
            out += (out.empty() ? "" : ", ") + std::string(n.second);
        }
    }
    return out;
}

uint32_t ApplicationChasm::ToolsLocked() const{
    if (!PlayMode()){
        return 0;
    }
    uint32_t found = discovered_mask.load();
    uint32_t locked = 0;
    for (int t = 0; t < CHASM_TOOL_COUNT && t < 32; t++){
        int need = ChasmToolNeeds(t);
        if (need >= 0 && !(found & (1u << need))){
            locked |= 1u << t;
        }
    }
    return locked;
}

std::shared_ptr<const ChasmDiscoveries> ApplicationChasm::GetDiscoveries(){
    std::lock_guard<std::mutex> lock(grid_mutex);
    return discoveries_snapshot;
}

/*
    PHYSICS THREAD. The discoveries again, when the journal has grown or more has been seen since they were last worked
    out: first from the journal, with the tick and who - the areas, the seams, the firsts below and the buildings
    finished - then whatever is in sight without an event: the landing's look round, which is home and tells nothing.
    Those are dated to the history's start.
*/
void ApplicationChasm::UpdateDiscoveries(const ChasmWorld& w){
    const EconomyState& e = economy.State();
    static const std::vector<EconomyEvent> none;
    const std::vector<EconomyEvent>& journal = (e.world.get() == &w) ? e.history.journal : none;
    int seen = 0;
    for (size_t i = 0; !f_areas_stale && i < area_seen.size(); i++){
        seen += area_seen[i];
    }
    for (size_t i = 0; !f_seams_stale && i < seam_seen.size(); i++){
        seen += seam_seen[i];
    }
    const ZoneState& z = zones.State();
    if (discoveries_world == &w && discoveries_journal == journal.size() && discoveries_seen == seen
        && discoveries_zones == z.version){
        return;
    }
    bool f_entered = discoveries_world != &w;
    discoveries_world = &w;
    discoveries_journal = journal.size();
    discoveries_seen = seen;
    discoveries_zones = z.version;
    ChasmDiscoveries d;
    auto made = [&](int which, int64_t tick, uint32_t person){
        if (d.tick[which] < 0){
            d.tick[which] = tick;
            d.person[which] = person;
            d.mask |= 1u << which;
        }
    };
    const AreaData* areas = w.areas.get();
    auto area_kind = [&](int i){
        return (areas && i >= 0 && i < (int)areas->areas.size()) ? areas->areas[(size_t)i].kind : -1;
    };
    for (const EconomyEvent& ev : journal){
        int64_t tick = (int64_t)ev.tick;
        if (ev.kind == EVENT_DISCOVERED){
            int kind = area_kind(ev.amount);
            if (kind == AREA_RIVER || kind == AREA_WATERFALL){
                made(DISCOVERY_RIVER,tick,ev.person);
            }else if (kind == AREA_CHASM){
                made(DISCOVERY_CHASM,tick,ev.person);
            }
        }else if (ev.kind == EVENT_SEAM_SEEN){
            made(DISCOVERY_FLOATSTONE,tick,ev.person);
        }else if (ev.kind == EVENT_BOULDER_FOUND){
            made(DISCOVERY_BOULDER,tick,ev.person);
        }else if (ev.kind == EVENT_BALCONY_REACHED){
            made(DISCOVERY_BALCONY,tick,ev.person);
        }else if (ev.kind == EVENT_BUILDING_FINISHED && ev.amount == ZONE_KIND_BUILDERS){
            made(DISCOVERY_BUILDERS,tick,0);
        }else if (ev.kind == EVENT_BUILDING_FINISHED && ev.amount == ZONE_KIND_WINCH){
            made(DISCOVERY_WINCH,tick,0);
        }else if ((ev.kind == EVENT_GOAL_MET || ev.kind == EVENT_GOAL_FAILED) && ev.amount >= 0 && ev.amount < CHASM_GOALS){
            //THE DEMO'S GOALS and its end, told once each (ApplicationChasmGoals.cpp): read here with the rest.
            d.goal_told[ev.amount] = (ev.kind == EVENT_GOAL_MET) ? CHASM_GOAL_MET : CHASM_GOAL_FAILED;
        }else if (ev.kind == EVENT_DEMO_COMPLETE){
            d.f_complete_told = true;
        }else if (ev.kind == EVENT_COLONY_LOST){
            d.f_lost_told = true;
        }
    }
    int64_t landing = (int64_t)e.history.from;
    for (size_t i = 0; !f_areas_stale && i < area_seen.size(); i++){
        int kind = area_seen[i] ? area_kind((int)i) : -1;
        if (kind == AREA_RIVER || kind == AREA_WATERFALL){
            made(DISCOVERY_RIVER,landing,0);
        }else if (kind == AREA_CHASM){
            made(DISCOVERY_CHASM,landing,0);
        }
    }
    for (size_t i = 0; !f_seams_stale && i < seam_seen.size(); i++){
        if (seam_seen[i]){
            made(DISCOVERY_FLOATSTONE,landing,0);
        }
    }
    /*
        And whatever a building STANDING WHOLE shows was found - a collector or a bridge, a river; a winch, the chasm; a
        stonecutter's hut, a boulder; a mine, floatstone; a builders' hut, a range or a tower, the builders - finished by
        the builders or not: one put down in debug mode is never a site, and a save from before the history has none of
        its buildings in the journal. A colony that has one can build another.
    */
    auto shows = [](int kind){
        switch (kind){
            case ZONE_KIND_WATER:
            case ZONE_KIND_BRIDGE:          return DISCOVERY_RIVER;
            case ZONE_KIND_WINCH:           return DISCOVERY_CHASM;
            case ZONE_KIND_STONECUTTER:     return DISCOVERY_BOULDER;
            case ZONE_KIND_MINE:            return DISCOVERY_FLOATSTONE;
            case ZONE_KIND_BUILDERS:
            case ZONE_KIND_RANGE:
            case ZONE_KIND_TOWER:           return DISCOVERY_BUILDERS;
            default:                        return -1;
        }
    };
    for (size_t id = 1; z.world.get() == &w && id < z.buildings.size(); id++){
        int kind = z.buildings[id].kind;
        if (z.buildings[id].size <= 0 || (shows(kind) < 0 && kind != ZONE_KIND_WINCH)){
            continue;
        }
        ZoneBuildingInfo info = ZoneBuildingFigures(w,z,(uint32_t)id);
        if (info.site_storeys > 0 || info.site_cells > 0){
            continue;
        }
        made(shows(kind),landing,0);
        if (kind == ZONE_KIND_WINCH){
            made(DISCOVERY_WINCH,landing,0);
        }
    }
    discoveries = d;
    discovered_mask = d.mask;
    //After the mask: the bar takes what a colony has on entering it as known, not NEW.
    if (f_entered){
        discoveries_epoch++;
    }
    std::shared_ptr<const ChasmDiscoveries> copy = std::make_shared<ChasmDiscoveries>(d);
    std::lock_guard<std::mutex> lock(grid_mutex);
    discoveries_snapshot = copy;
}

/*
    PHYSICS THREAD, on the looks' ticks. The two firsts nothing else marks, each looked for until it is in the journal:
      - THE FIRST ON A BALCONY: anyone whose ground is a balcony's - only a winch takes anyone down to one;
      - THE FIRST BOULDER the stonecutter could break: one standing on the colony's own land (its settlements'
        territory, which is where it walks and builds), on ground explored - found by whoever is nearest it.
    Both read the state the tick has made, so a replay finds them on the same tick.
*/
void ApplicationChasm::LookForFirsts(const ChasmWorld& w){
    const EconomyState& e = economy.State();
    if (e.world.get() != &w){
        return;
    }
    if (!(discoveries.mask & (1u << DISCOVERY_BALCONY)) && w.terrain){
        for (const EconomyWorker& k : e.workers){
            GridPick pick = w.picker->Pick(k.pos);
            if (pick.plot >= 0 && pick.plot < (int)w.terrain->level.size() && w.terrain->level[(size_t)pick.plot] == TERRAIN_BALCONY){
                economy.BalconyReached(k.id,pick.plot);
                break;
            }
        }
    }
    if (!(discoveries.mask & (1u << DISCOVERY_BOULDER)) && w.forest){
        if (boulders_world != &w){
            boulders_world = &w;
            boulder_props.clear();
            for (size_t i = 0; i < w.forest->props.size(); i++){
                if (w.forest->props[i].kind == PROP_BOULDER){
                    boulder_props.push_back((int)i);
                }
            }
        }
        for (int i : boulder_props){
            const PropInstance& p = w.forest->props[(size_t)i];
            vec2 at(p.pos.x,p.pos.z);
            bool f_standing = (size_t)i < e.prop_state.size() && e.prop_state[(size_t)i] == PROP_STATE_STANDING;
            bool f_ours = p.plot >= 0 && p.plot < (int)e.settlement.size() && e.settlement[(size_t)p.plot] != 0;
            if (!f_standing || !f_ours || !exploration.Explored(at)){
                continue;
            }
            uint32_t who = 0;
            float nearest = INFINITY;
            for (const EconomyWorker& k : e.workers){
                float d = (k.pos - at).length();
                if (d < nearest){
                    nearest = d;
                    who = k.id;
                }
            }
            economy.BoulderFound(who,p.plot);
            break;
        }
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
    bool f_field = ZoneOpOnCell(op);
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
                spot.low = spot.ground;
                spot.low_at = spot.p;
                for (const vec2& a : around){
                    spot.ground = std::max(spot.ground,t.GroundHeight(spot.p + a * (CLOUD_RADIUS_MAX * 0.7f),plateau));
                    //Out to the next lattice point: the drop a stack has to fill before its neighbour.
                    vec2 q = spot.p + a * CLOUD_SPACING;
                    float low = std::max(plateau,t.GroundHeight(q,plateau));
                    if (low < spot.low){
                        spot.low = low;
                        spot.low_at = q;
                    }
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
            vec4 mv[2];
            MistMotion(puff,mv);
            sets[set].push_back(MistRest(puff));
            motion[set].push_back(mv[0]);
            motion[set].push_back(mv[1]);
            /*
                A cliff: the next lattice point down sits many units lower, and the rock face between
                would show through. Puffs stacked down the drop, toward the low side, close it.
            */
            float drop = ground - spot.low;
            int stack = ex->Explored(spot.low_at) ? 0 : (int)(drop / (CLOUD_RADIUS_MIN * 1.1f));
            for (int k = 1; k <= stack; k++){
                float f = (float)k / (float)(stack + 1);
                MistPuff more = puff;
                vec2 q = p + (spot.low_at - p) * f;
                more.pos = vec3(q.x,y - drop * f,q.y);
                more.phase = std::fmod(puff.phase + 0.37f * (float)k,1.0f);
                sets[set].push_back(MistRest(more));
                MistMotion(more,mv);
                motion[set].push_back(mv[0]);
                motion[set].push_back(mv[1]);
            }
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

//--- The areas, for agents ------------------------------------------------------------------------

#ifdef USE_MCP
void ApplicationChasm::RegisterAreaTools(){
    MCPServer::Get()->RegisterTool("chasm_areas",
        "THE AREAS (docs/areas_plan.md; Areas.h): the parts of the map there are to discover - each river, each waterfall, "
        "each biome as one, the starting land's open ground (plateau) and its forest - generated with the map, in the "
        "order the discovered event's amount names them. Each: index, kind, which (a river's or fall's own index), cells "
        "(the exploration's), x and z (its middle), and seen - any of its ground explored, worked out from the "
        "exploration as the rules do. `seen` counts the seen by kind. Being found is the event `discovered` "
        "(chasm_economy's events, chasm_stats' journal). The debug view's `areas` layer draws them (chasm_view).",
        json{{"type","object"},{"properties",json::object()}},
        [this](const json& args) -> json {
            (void)args;
            std::shared_ptr<const ChasmWorld> w = GetWorld();
            std::shared_ptr<const Exploration> ex = GetExploration();
            if (!w || !w->areas){
                return json{{"error","no world"}};
            }
            json list = json::array();
            json seen_by = json::object();
            for (size_t i = 0; i < w->areas->areas.size(); i++){
                const Area& a = w->areas->areas[i];
                bool f_seen = false;
                for (int c : a.cells){
                    if (ex && c < (int)ex->cells.size() && ex->cells[(size_t)c]){
                        f_seen = true;
                        break;
                    }
                }
                list.push_back(json{{"index",(int)i},{"kind",AreaKindName(a.kind)},{"which",a.which},
                                    {"cells",(int)a.cells.size()},{"x",std::round(a.middle.x * 10.0f) / 10.0f},
                                    {"z",std::round(a.middle.y * 10.0f) / 10.0f},{"seen",f_seen}});
                std::string k = AreaKindName(a.kind);
                seen_by[k] = (seen_by.contains(k) ? seen_by[k].get<int>() : 0) + (f_seen ? 1 : 0);
            }
            return json{{"areas",list},{"seen",seen_by},{"build_ms",std::round(w->areas->build_ms * 10.0f) / 10.0f}};
        });
}
#endif
