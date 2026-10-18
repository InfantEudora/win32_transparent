#include "ApplicationChasm.h"

#ifdef USE_MCP
//Only this from the server: see the USE_MCP block in engine.mk.
#include "MCPServer.h"
#endif

#include "Debug.h"

#include <math.h>
#include <algorithm>

static Debugger* debug = new Debugger("ChasmBirds",DEBUG_ALL);

/*
    BIRDS IN THE VIEW (Birds.h, docs/birds_plan.md): the flocks drawn, and what they are told of the world.

    VIEW, NOT STATE. Everything here runs on the render thread from PreRender, reads the published snapshots - the
    world, the zones, the economy - and writes nothing any rule reads. So the birds are never saved, hashed or
    recorded, and a replay may show different ones.

    THEIR CLOCK is the game's: real time times the play speed, nothing while paused - so a pause holds them in the air
    like the mist, and 3x flies them three times as fast. Not the tick count itself, which moves in 20 ms steps: a
    bird crossing the view at 5 units a second would visibly stutter on it.

    WHAT THEY ARE TOLD:
      - where they can perch: standing trees, from cells of trees made once per world, each tree's perch points from
        its mesh - the upward faces at the top of its crown, the very tip first - per LOOK, so a bare winter oak's
        are its own;
      - how high the ground is (THE ground, Terrain::GroundHeight);
      - what scares them: someone chopping at a tree (the test SignalWork makes for the axe's sound), anyone out of
        doors who moved since the last tick, and a tree just felled (EVENT_TREE_FELLED).

    DRAWN as one Object per bird, all on one mesh, so the renderer draws every bird in one instanced call a pass,
    each with its own wings: the shape keys' factors are per object (instance_data), worked on the GPU. Not an
    instance set, whose instances share the owner's factors - the whole flock would beat in step.

    HEARD through the cue table, like any sound of the game: the flocks' calls are handed over in bird_calls, which the
    physics thread takes once a pass (ApplicationChasmSound.cpp, SignalBirds) - what each sounds like is the table's.
*/

#define BIRD_OBJECTS_MAX        256     //a winter's flocks are twice their size (BIRD_GATHER_WINTER)
#define BIRD_DRAW_SCALE         1.0f    //modelled at the size drawn (birds_plan.md: about half a villager)
#define BIRD_TREE_CELL          8.0f    //world units a side of a cell of trees
#define BIRD_PERCHES_PER_LOOK   6
#define BIRD_ROOST_SPREAD       3.0f    //trees this near the roost's share its perches
#define BIRD_ROOST_PERCHES      18
#define BIRD_STEP_S             (1.0f / 30.0f)
#define BIRD_STEPS_MAX          8
#define BIRD_FRAME_MAX_S        0.25f   //a longer frame - a load, a stall - is not flown through
#define BIRD_RECENT_S           1.0f    //how long a one-off scare is held
#define BIRD_LURE_MEMORY_S      90.0f   //how long a felling site draws the colony's flocks after the axe was last there
#define BIRD_LURE_MERGE         6.0f    //felling sites this close are one
#define BIRD_LURES_MAX          16
#define BIRD_CALLS_HELD         64      //calls waiting for the sound to take them

namespace {

uint32_t Hash(uint32_t x){
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/*
    Where a bird can sit on a mesh, in its own space: the centres of the faces turned up in the top two fifths of it,
    the highest first, no two closer than a share of its height - and the very highest vertex before them all, the tip
    of a pine. The props are flat-shaded triangle lists, three vertices a face, wound outward (glTF's rule).
*/
std::vector<vec3> PerchPoints(const Mesh* m, float& top){
    std::vector<vec3> out;
    top = 0.0f;
    if (!m){
        return out;
    }
    const std::vector<vertex>& v = m->GetVertices();
    if (v.size() < 3){
        return out;
    }
    float lo = v[0].pos.y;
    float hi = v[0].pos.y;
    vec3 tip = v[0].pos;
    for (const vertex& x : v){
        lo = std::min(lo,x.pos.y);
        if (x.pos.y > hi){
            hi = x.pos.y;
            tip = x.pos;
        }
    }
    top = hi;
    float h = hi - lo;
    if (h <= 0.0f){
        return out;
    }
    std::vector<std::pair<float,vec3>> faces;
    for (size_t i = 0; i + 2 < v.size(); i += 3){
        vec3 a = v[i].pos;
        vec3 b = v[i + 1].pos;
        vec3 c = v[i + 2].pos;
        vec3 n = (b - a).cross(c - a);
        float len = n.length();
        if (len <= 1e-8f || n.y / len < 0.35f){
            continue;
        }
        vec3 mid = (a + b + c) / 3.0f;
        if (mid.y < lo + 0.6f * h){
            continue;
        }
        faces.push_back(std::make_pair(mid.y,mid));
    }
    std::sort(faces.begin(),faces.end(),[](const std::pair<float,vec3>& p, const std::pair<float,vec3>& q){
        return p.first > q.first;
    });
    const float apart = std::max(0.2f,0.08f * h);
    out.push_back(tip);
    for (const auto& f : faces){
        if ((int)out.size() >= BIRD_PERCHES_PER_LOOK){
            break;
        }
        bool f_clear = true;
        for (const vec3& p : out){
            f_clear = f_clear && (p - f.second).length() >= apart;
        }
        if (f_clear){
            out.push_back(f.second);
        }
    }
    return out;
}

//A bird's object where its pose says, wings and all, and shown.
void PoseBird(Object* o, const BirdPose& p){
    o->SetPosition(p.pos,false);
    o->SetRotation(p.turn,false);
    o->SetShapekey(BIRD_KEY_UP,p.up);
    o->SetShapekey(BIRD_KEY_DOWN,p.down);
    o->SetShapekey(BIRD_KEY_FOLDED,p.folded);
    o->SetVisibility(true);
}

//A number from a marker's JSON, or `or_else` - with JSON_NOEXCEPTION a wrong type would abort the app.
float JsonNumber(const json& j, const char* key, float or_else){
    json v = j.value(key,json());
    return v.is_number() ? v.get<float>() : or_else;
}

}

//RENDER THREAD, from LoadProps: the bird's mesh. Without it there are no birds, and the log says so.
void ApplicationChasm::BuildBirdScene(){
    bird_mesh = assetmanager->GetMeshFromAsset(CHASM_BIRD_ASSET);
    if (!bird_mesh){
        debug->Warn("'%s' not in chasm_props.glb - no birds will fly\n",CHASM_BIRD_ASSET);
        return;
    }
    //Held for good, as the worker meshes are: the last Object::SetMesh release would delete it.
    bird_mesh->Retain();
    if (bird_mesh->num_morph_targets < BIRD_KEY_FOLDED + 1){
        debug->Warn("'%s' has %i shape keys, not the %i its wings need - it will fly stiff\n",CHASM_BIRD_ASSET,
                    bird_mesh->num_morph_targets,BIRD_KEY_FOLDED + 1);
    }
    debug->Info("Birds: '%s', %u vertices, %i shape keys\n",CHASM_BIRD_ASSET,bird_mesh->num_vertices,
                bird_mesh->num_morph_targets);
}

/*
    A new world: its trees into cells, every forest set's perch points (once - the meshes never change), the tallest
    tree, and the flocks' homes - trees in the forests' dense cores, from a hash of the seed, BIRD_HOME_SPACING apart.
*/
void ApplicationChasm::BuildBirdTrees(const ChasmWorld& w){
    const ForestData& f = *w.forest;
    if (bird_perch_local.empty()){
        int sets = PROP_KIND_COUNT + (int)season_look_mesh.size();
        bird_perch_local.resize(sets);
        bird_set_top.assign(sets,0.0f);
        for (int s = 0; s < sets; s++){
            const Mesh* m = NULL;
            if (s < PROP_KIND_COUNT){
                m = (ForestIsTree(s) && f_props_loaded[s]) ? assetmanager->GetMeshFromAsset(prop_asset_names[s]) : NULL;
            }else{
                int look = s - PROP_KIND_COUNT;
                m = ForestIsTree(season_look_kind[look]) ? season_look_mesh[look] : NULL;
            }
            bird_perch_local[s] = PerchPoints(m,bird_set_top[s]);
        }
    }
    float x0 = 1e30f, z0 = 1e30f, x1 = -1e30f, z1 = -1e30f;
    for (const PropInstance& p : f.props){
        x0 = std::min(x0,p.pos.x);
        z0 = std::min(z0,p.pos.z);
        x1 = std::max(x1,p.pos.x);
        z1 = std::max(z1,p.pos.z);
    }
    bird_cells.clear();
    bird_tree_top = 0.0f;
    if (x1 < x0){
        bird_cell_cols = bird_cell_rows = 0;
        return;
    }
    bird_cell_x0 = x0;
    bird_cell_z0 = z0;
    bird_cell_cols = (int)((x1 - x0) / BIRD_TREE_CELL) + 1;
    bird_cell_rows = (int)((z1 - z0) / BIRD_TREE_CELL) + 1;
    bird_cells.resize((size_t)bird_cell_cols * bird_cell_rows);
    int trees = 0;
    for (size_t i = 0; i < f.props.size(); i++){
        const PropInstance& p = f.props[i];
        if (!ForestIsTree(p.kind) || bird_perch_local[p.kind].empty()){
            continue;
        }
        int cx = (int)((p.pos.x - x0) / BIRD_TREE_CELL);
        int cz = (int)((p.pos.z - z0) / BIRD_TREE_CELL);
        bird_cells[(size_t)cz * bird_cell_cols + cx].push_back((int)i);
        bird_tree_top = std::max(bird_tree_top,bird_set_top[p.kind] * p.scale);
        trees++;
    }
    birds.Reset(w.grid->settings.seed);
    f_birds_homed = false;
    birds_homing_wait = 0.0f;
    debug->Info("Birds: %i trees in %ix%i cells, tallest %.1f\n",trees,bird_cell_cols,bird_cell_rows,bird_tree_top);
}

/*
    The flocks' homes, once the world's economy is out (or it has been long enough that there is none to wait for):
    BIRD_COLONY_FLOCKS in the woods round the colony's middle - the mean of where its people are - then the rest in the
    forests' dense cores, all in the seed's order, each far enough from those taken.
*/
void ApplicationChasm::HomeBirds(const ChasmWorld& w, const EconomyState* e){
    const ForestData& f = *w.forest;
    const uint32_t seed = w.grid->settings.seed;
    auto by_seed = [seed](std::vector<int>& v){
        std::sort(v.begin(),v.end(),[seed](int a, int b){
            uint32_t ha = Hash(seed ^ Hash((uint32_t)a));
            uint32_t hb = Hash(seed ^ Hash((uint32_t)b));
            return (ha != hb) ? ha < hb : a < b;
        });
    };
    bool f_colony = e && !e->workers.empty();
    vec2 colony;
    if (f_colony){
        for (const EconomyWorker& k : e->workers){
            colony += k.pos;
        }
        colony = colony / (float)e->workers.size();
    }
    std::vector<int> near_colony;
    std::vector<int> wander_near;
    std::vector<int> cores;
    std::vector<int> all;
    for (const std::vector<int>& cell : bird_cells){
        for (int i : cell){
            const PropInstance& p = f.props[i];
            all.push_back(i);
            if (f_colony){
                float d = (vec2(p.pos.x,p.pos.z) - colony).length();
                if (d >= BIRD_COLONY_MIN && d <= BIRD_COLONY_MAX){
                    near_colony.push_back(i);
                }
                if (d <= BIRD_WANDER_ROAM){
                    wander_near.push_back(i);
                }
            }
            if (p.plot >= 0 && (size_t)p.plot < f.core.size() && f.core[p.plot]){
                cores.push_back(i);
            }
        }
    }
    by_seed(near_colony);
    by_seed(wander_near);
    by_seed(cores);
    by_seed(all);
    auto take = [&](std::vector<vec2>& taken, const std::vector<int>& from, int up_to, float spacing){
        for (int i : from){
            if ((int)taken.size() >= up_to){
                return;
            }
            vec2 at(f.props[i].pos.x,f.props[i].pos.z);
            bool f_far = true;
            for (const vec2& t : taken){
                f_far = f_far && (t - at).length() >= spacing;
            }
            if (f_far){
                taken.push_back(at);
            }
        }
    };
    std::vector<vec2> taken;
    take(taken,near_colony,BIRD_COLONY_FLOCKS,BIRD_COLONY_SPACING);
    int by_colony = (int)taken.size();
    take(taken,cores,BIRD_FLOCKS,BIRD_HOME_SPACING);
    //The wanderers: half round the colony, where the player looks, the rest anywhere there are trees.
    std::vector<vec2> wanderers;
    take(wanderers,wander_near,BIRD_WANDER_COLONY,BIRD_WANDER_AWAY * 0.5f);
    take(wanderers,all,BIRD_WANDERERS,BIRD_WANDER_AWAY);
    //The flocks themselves are made by UploadBirds, which has the roosts.
    auto draw_of = [seed](const vec2& at){
        return Hash(seed ^ Hash((uint32_t)(at.x * 7.0f) ^ (uint32_t)(at.y * 13.0f)));
    };
    std::lock_guard<std::mutex> lock(birds_mutex);
    for (size_t i = 0; i < taken.size(); i++){
        BirdRequest r;
        r.op = BIRD_REQUEST_SPAWN;
        r.at = taken[i];
        r.role = ((int)i < by_colony) ? BIRD_ROLE_COLONY : BIRD_ROLE_FOREST;
        r.count = BIRD_FLOCK_MIN + (int)(draw_of(r.at) % (uint32_t)(BIRD_FLOCK_MAX - BIRD_FLOCK_MIN + 1));
        bird_requests.push_back(r);
    }
    for (const vec2& at : wanderers){
        BirdRequest r;
        r.op = BIRD_REQUEST_SPAWN;
        r.at = at;
        r.role = BIRD_ROLE_WANDER;
        uint32_t h = draw_of(at);
        r.count = ((float)(h & 0xFFFF) / 65536.0f < BIRD_WANDER_ALONE) ? 1 : 2 + (int)((h >> 16) & 1);
        bird_requests.push_back(r);
    }
    f_birds_homed = true;
    birds_colony = f_colony ? colony : vec2();
    debug->Info("Birds: %i flock homes, %i by the colony%s; %i wanderers\n",(int)taken.size(),by_colony,
                f_colony ? "" : " (none: no colony)",(int)wanderers.size());
}

//Still standing and drawn: not felled (the economy's prop state) and not hidden under a house or a field.
bool ApplicationChasm::BirdTreeStanding(const ChasmWorld& w, const ZoneState* z, const EconomyState* e, int tree){
    const ForestData& f = *w.forest;
    if (tree < 0 || (size_t)tree >= f.props.size()){
        return false;
    }
    if (e && (size_t)tree < e->prop_state.size() && e->prop_state[tree] != PROP_STATE_STANDING){
        return false;
    }
    return !PropHidden(f.props[tree],z);
}

//A tree's perches in the world: its look's points, turned and scaled as the tree is drawn (RebuildPropChunk).
void ApplicationChasm::BirdTreePerches(const ChasmWorld& w, int tree, std::vector<BirdPerch>& out){
    const PropInstance& p = w.forest->props[tree];
    int set = p.kind;
    if (ForestSeasonal(p.kind) != FOREST_SEASONAL_NONE && (size_t)tree < forest_prop_stage.size()){
        set = season_look_set[p.kind][forest_prop_stage[tree]];
    }
    if (set < 0 || (size_t)set >= bird_perch_local.size()){
        set = p.kind;
    }
    quat turn(vec3(0.0f,1.0f,0.0f),p.yaw);
    for (const vec3& l : bird_perch_local[set]){
        if ((int)out.size() >= BIRD_ROOST_PERCHES){
            return;
        }
        out.push_back(BirdPerch{p.pos + turn * (l * p.scale),tree});
    }
}

//The season's gathering (Birds.h, SEASONS): by the date shown, or as the tool holds it.
float ApplicationChasm::BirdGather(){
    float held = bird_gather_held.load();
    if (held > 0.0f){
        return held;
    }
    int season = CalendarDateOf(calendar_shown.load()).season;
    return (season == SEASON_WINTER) ? BIRD_GATHER_WINTER : (season == SEASON_AUTUMN) ? BIRD_GATHER_AUTUMN : 1.0f;
}

//What the birds ask of the world this frame: the ground, the roosts, which trees still stand, the season.
BirdWorld ApplicationChasm::MakeBirdWorld(const ChasmWorld& w, const ZoneState* z, const EconomyState* e){
    BirdWorld bw;
    bw.tree_top = bird_tree_top;
    bw.gather = BirdGather();
    const ChasmWorld* wp = &w;
    bw.ground = [wp](const vec2& p){
        GridPick pick = wp->picker->Pick(p);
        return pick.f_hit ? wp->terrain->GroundHeight(p,wp->terrain->Height(pick.plot)) : 0.0f;
    };
    auto standing = [this,wp,z,e](int tree){
        return BirdTreeStanding(*wp,z,e,tree);
    };
    bw.standing = standing;
    auto perches_of = [this,wp](int tree, std::vector<BirdPerch>& out){
        BirdTreePerches(*wp,tree,out);
    };
    bw.roost = [this,wp,standing,perches_of](const vec2& centre, float reach, const std::vector<BirdScare>& avoid,
                                             uint32_t draw, BirdRoost& out){
        if (bird_cells.empty()){
            return false;
        }
        const ForestData& f = *wp->forest;
        auto cells_round = [&](const vec2& at, float r, const std::function<void(int)>& each){
            int cx0 = std::max(0,(int)((at.x - r - bird_cell_x0) / BIRD_TREE_CELL));
            int cz0 = std::max(0,(int)((at.y - r - bird_cell_z0) / BIRD_TREE_CELL));
            int cx1 = std::min(bird_cell_cols - 1,(int)((at.x + r - bird_cell_x0) / BIRD_TREE_CELL));
            int cz1 = std::min(bird_cell_rows - 1,(int)((at.y + r - bird_cell_z0) / BIRD_TREE_CELL));
            for (int cz = cz0; cz <= cz1; cz++){
                for (int cx = cx0; cx <= cx1; cx++){
                    for (int t : bird_cells[(size_t)cz * bird_cell_cols + cx]){
                        each(t);
                    }
                }
            }
        };
        std::vector<int> found;
        cells_round(centre,reach,[&](int t){
            vec2 at(f.props[t].pos.x,f.props[t].pos.z);
            if ((at - centre).length() > reach){
                return;
            }
            for (const BirdScare& s : avoid){
                if ((at - s.at).length() <= s.reach){
                    return;
                }
            }
            if (standing(t)){
                found.push_back(t);
            }
        });
        if (found.empty()){
            return false;
        }
        int tree = found[draw % (uint32_t)found.size()];
        const PropInstance& p = f.props[tree];
        out.tree = tree;
        out.at = p.pos + vec3(0.0f,bird_set_top[p.kind] * p.scale,0.0f);
        out.perches.clear();
        perches_of(tree,out.perches);
        //...and its neighbours', so a flock sits over two or three trees.
        vec2 at(p.pos.x,p.pos.z);
        cells_round(at,BIRD_ROOST_SPREAD,[&](int t){
            if (t != tree && (vec2(f.props[t].pos.x,f.props[t].pos.z) - at).length() <= BIRD_ROOST_SPREAD && standing(t)){
                perches_of(t,out.perches);
            }
        });
        return true;
    };
    return bw;
}

/*
    What scares the birds this frame. The axe and the walkers come from the economy as published; a tree felled is an
    event, held BIRD_RECENT_S so it is not missed between two steps.
*/
void ApplicationChasm::BirdScares(const ChasmWorld& w, const EconomyState* e, float dt, std::vector<BirdScare>& out){
    for (BirdRecent& r : bird_recent){
        r.left -= dt;
    }
    bird_recent.erase(std::remove_if(bird_recent.begin(),bird_recent.end(),[](const BirdRecent& r){
        return r.left <= 0.0f;
    }),bird_recent.end());
    //Felling sites forgotten after a while: the woodcutters have moved on.
    for (auto& l : bird_lure_ages){
        l.second += dt;
    }
    bird_lure_ages.erase(std::remove_if(bird_lure_ages.begin(),bird_lure_ages.end(),[](const std::pair<vec2,float>& l){
        return l.second > BIRD_LURE_MEMORY_S;
    }),bird_lure_ages.end());
    if (e && e->world.get() == &w){
        const ForestData& f = *w.forest;
        for (const EconomyWorker& k : e->workers){
            //At a tree with the axe: SignalWork's test, which times the chop's sound.
            bool f_chop = (k.state == WORKER_CLEARING)
                       || (k.state == WORKER_WORKING && k.job == WORKER_JOB_WOODCUTTER && k.prop >= 0
                           && (size_t)k.prop < e->prop_state.size() && e->prop_state[k.prop] == PROP_STATE_STANDING);
            if (f_chop && k.prop >= 0 && (size_t)k.prop < f.props.size()){
                vec2 tree(f.props[k.prop].pos.x,f.props[k.prop].pos.z);
                out.push_back(BirdScare{tree,BIRD_REACH_CHOP});
                //A felling site, remembered: one near it already is made fresh rather than added.
                auto it = std::find_if(bird_lure_ages.begin(),bird_lure_ages.end(),[&](const std::pair<vec2,float>& l){
                    return (l.first - tree).length() < BIRD_LURE_MERGE;
                });
                if (it != bird_lure_ages.end()){
                    it->second = 0.0f;
                }else if (bird_lure_ages.size() < BIRD_LURES_MAX){
                    bird_lure_ages.push_back(std::make_pair(tree,0.0f));
                }
            }
        }
        //Who moved since the last tick seen. Worked out once a tick; the scares stand until the next.
        if (e->tick != bird_walkers_tick){
            bird_walkers_tick = e->tick;
            bird_moving.clear();
            std::map<uint32_t,vec2> now;
            for (const EconomyWorker& k : e->workers){
                if (EconomyIndoors(k)){
                    continue;
                }
                vec2 at(k.pos.x + k.off.x,k.pos.y + k.off.y);
                auto it = bird_walkers.find(k.id);
                if (it != bird_walkers.end() && (it->second - at).length() > 0.001f){
                    bird_moving.push_back(BirdScare{at,BIRD_REACH_WALK});
                }
                now[k.id] = at;
            }
            bird_walkers.swap(now);
        }
        out.insert(out.end(),bird_moving.begin(),bird_moving.end());
        //Trees felled since the last look.
        if (e->events_made < bird_events_seen){
            bird_events_seen = e->events_made;
        }
        size_t fresh = std::min((size_t)(e->events_made - bird_events_seen),e->events.size());
        for (size_t i = e->events.size() - fresh; i < e->events.size(); i++){
            const EconomyEvent& ev = e->events[i];
            //Its prop is `amount`: the tree's own place, whatever the event's pos says.
            if (ev.kind == EVENT_TREE_FELLED && ev.amount >= 0 && (size_t)ev.amount < f.props.size()){
                const vec3& p = f.props[ev.amount].pos;
                bird_recent.push_back(BirdRecent{BirdScare{vec2(p.x,p.z),BIRD_REACH_FELLED},BIRD_RECENT_S});
            }
        }
        bird_events_seen = e->events_made;
    }
    for (const BirdRecent& r : bird_recent){
        out.push_back(r.scare);
    }
    bird_lures.clear();
    for (const auto& l : bird_lure_ages){
        bird_lures.push_back(l.first);
    }
}

//RENDER THREAD, from PreRender: the tool's requests, a step of the flocks by the game's clock, and every bird posed.
void ApplicationChasm::UploadBirds(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    if (!bird_mesh || !w || !w->forest){
        return;
    }
    std::shared_ptr<const ZoneState> z = GetZones();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    const EconomyState* ep = (e && e->world == w) ? e.get() : NULL;
    if (w != birds_world){
        birds_world = w;
        bird_walkers.clear();
        bird_moving.clear();
        bird_recent.clear();
        bird_events_seen = ep ? ep->events_made : 0;
        BuildBirdTrees(*w);
    }

    //The game's clock: real time by the play speed, none in a pause.
    auto now = std::chrono::steady_clock::now();
    float dt = f_birds_clock ? std::chrono::duration<float>(now - birds_last).count() : 0.0f;
    birds_last = now;
    f_birds_clock = true;
    //Homed once the economy for this world is out, or after a while without one.
    birds_homing_wait += dt;
    if (!f_birds_homed && (ep || birds_homing_wait > 2.0f)){
        HomeBirds(*w,ep);
    }
    const bool f_live = world_scene && main_scene == world_scene && !world_scene->IsPhysicsPaused();
    dt = f_live ? std::min(dt,BIRD_FRAME_MAX_S) * physics_time_factor : 0.0f;

    BirdWorld bw = MakeBirdWorld(*w,z.get(),ep);
    std::vector<BirdRequest> requests;
    int bench_key;
    vec3 bench_at;
    {
        std::lock_guard<std::mutex> lock(birds_mutex);
        requests.swap(bird_requests);
        bench_key = bird_bench_key;
        bench_at = bird_bench_at;
    }
    for (const BirdRequest& r : requests){
        if (r.op == BIRD_REQUEST_SPAWN){
            if (!birds.AddFlock(bw,r.at,r.count,r.role)){
                debug->Info("Birds: no roost near %.1f, %.1f for a flock\n",r.at.x,r.at.y);
            }
        }else if (r.op == BIRD_REQUEST_FLUSH){
            birds.Flush(bw,BirdScare{r.at,r.reach});
        }else if (r.op == BIRD_REQUEST_CLEAR){
            birds.Reset(w->grid->settings.seed);
        }else if (r.op == BIRD_REQUEST_RESET){
            birds_world.reset();    //made again next frame, homes and all
        }
    }

    std::vector<BirdScare> scares;
    auto step_t0 = std::chrono::steady_clock::now();
    if (dt > 0.0f){
        BirdScares(*w,ep,dt,scares);
        bw.lures = bird_lures;
        int steps = std::min(BIRD_STEPS_MAX,std::max(1,(int)ceilf(dt / BIRD_STEP_S)));
        for (int i = 0; i < steps; i++){
            birds.Step(bw,dt / (float)steps,scares);
        }
    }
    //What the step cost this frame, scares and all, eased for the tool to read.
    float step_us = std::chrono::duration<float,std::micro>(std::chrono::steady_clock::now() - step_t0).count();
    birds_step_us += (step_us - birds_step_us) * 0.05f;
    //The calls, for the sound to take on the physics thread (SignalBirds) - none from birds not drawn.
    std::vector<BirdCall> calls;
    birds.TakeCalls(calls);
    if (!calls.empty() && f_view_birds){
        std::lock_guard<std::mutex> lock(birds_mutex);
        //Not taken for a while - the physics thread held up - they are let go rather than piled up and heard late.
        if (bird_calls.size() + calls.size() <= BIRD_CALLS_HELD){
            bird_calls.insert(bird_calls.end(),calls.begin(),calls.end());
        }
    }

    std::vector<BirdPose> poses;
    if (f_view_birds){
        birds.Poses(poses);
    }
    size_t want = std::min(poses.size() + 1,(size_t)BIRD_OBJECTS_MAX);
    if (bird_objects.size() < want){
        std::vector<Object*> made;
        while (bird_objects.size() + made.size() < want){
            Object* o = new Object();
            o->name = "Bird " + std::to_string(bird_objects.size() + made.size());
            o->SetMesh(bird_mesh);
            o->SetMaterialSlot(0,palette_material);
            o->SetPickability(false);
            o->SetVisualOnly(true);
            o->SetVisibility(false);
            o->SetScale(vec3(BIRD_DRAW_SCALE,BIRD_DRAW_SCALE,BIRD_DRAW_SCALE));
            made.push_back(o);
        }
        world_scene->AtTickBoundary([&](){
            for (Object* o : made){
                world_scene->AddObject(o);
                bird_objects.push_back(o);
            }
        });
    }
    //The bench: the last object, on its own clock so it moves in a pause too.
    size_t shown = std::min(poses.size(),bird_objects.size() - 1);
    for (size_t i = 0; i < bird_objects.size(); i++){
        Object* o = bird_objects[i];
        if (i < shown){
            //A bird in a tree rides it as the wind bends it (weather_plan.md) - the bend at its feet, this frame's.
            BirdPose pose = poses[i];
            if (pose.tree >= 0 && pose.ride > 0.0f){
                pose.pos = pose.pos + SwayOffset(pose.tree,pose.pos) * pose.ride;
            }
            PoseBird(o,pose);
        }else if (i == bird_objects.size() - 1 && bench_key >= 0){
            static const auto t0 = std::chrono::steady_clock::now();
            float t = std::chrono::duration<float>(now - t0).count();
            float s = sinf(t * 6.2831853f * BIRD_FLAP_HZ);
            float up = (bench_key == BIRD_KEY_UP) ? 1.0f : (bench_key == 4 ? std::max(0.0f,s) : 0.0f);
            float down = (bench_key == BIRD_KEY_DOWN) ? 1.0f : (bench_key == 4 ? std::max(0.0f,-s) : 0.0f);
            o->SetPosition(bench_at,false);
            o->SetRotation(quat(vec3(0.0f,1.0f,0.0f),t * 0.4f),false);
            o->SetShapekey(BIRD_KEY_UP,up);
            o->SetShapekey(BIRD_KEY_DOWN,down);
            o->SetShapekey(BIRD_KEY_FOLDED,bench_key == BIRD_KEY_FOLDED ? 1.0f : 0.0f);
            o->SetVisibility(true);
        }else{
            o->SetVisibility(false);
        }
    }

    //What the tool reads, now and then.
    if (++bird_status_frames >= 10){
        bird_status_frames = 0;
        static const char* states[] = {"settled","travel","circle","scatter"};
        static const char* roles[] = {"forest","colony","wander"};
        json flocks = json::array();
        for (const BirdFlock& f : birds.Flocks()){
            int perched = 0;
            int leaving = 0;
            for (const Bird& b : f.birds){
                perched += (b.state == BIRD_PERCHED) ? 1 : 0;
                leaving += b.f_leaving ? 1 : 0;
            }
            //How near the nearest scare came to it this frame, past its reach: what tuning a reach is about.
            float nearest = -1.0f;
            for (const BirdScare& s : scares){
                float d = (vec2(f.anchor.x,f.anchor.z) - s.at).length() - s.reach;
                nearest = (nearest < 0.0f || d < nearest) ? std::max(0.0f,d) : nearest;
            }
            flocks.push_back(json{{"id",f.id},{"state",states[f.state & 3]},{"birds",(int)f.birds.size()},
                                  {"size",f.size},{"leaving",leaving},
                                  {"perched",perched},{"anchor",{f.anchor.x,f.anchor.y,f.anchor.z}},
                                  {"roost_tree",f.roost.tree},{"perches",(int)f.roost.perches.size()},
                                  {"home",{f.home.x,f.home.y}},{"timer",f.timer},{"wary_s",f.wary_left},
                                  {"flushes",f.flushes},{"scare_gap",nearest},
                                  {"role",roles[f.role % 3]},{"voice",f.voice == BIRD_VOICE_MAGPIE ? "magpie" : "crow"},
                                  {"calls",f.calls},{"next_call_s",f.call}});
        }
        json status{{"flocks",flocks},{"birds",birds.Count()},{"objects",(int)bird_objects.size()},
                    {"shape_keys",bird_mesh->num_morph_targets},{"tree_top",bird_tree_top},
                    {"scares",(int)scares.size()},{"live",f_live},{"speed",physics_time_factor},
                    {"gather",bw.gather},{"gather_held",bird_gather_held.load() > 0.0f},
                    {"shown",f_view_birds.load()},{"colony",{birds_colony.x,birds_colony.y}},
                    {"lures",(int)bird_lures.size()},{"step_us",birds_step_us}};
        std::lock_guard<std::mutex> lock(birds_mutex);
        birds_status = status;
    }
}

/*
    THE TITLE'S FLOCK (birds_plan.md step 5): one flock over the diorama, the same Birds.{h,cpp} - stepped from
    PreRender as the world's are, on the title's own clock (real time: the title never pauses), and SILENT, since the
    title has its music and the world's buses are held under it.

    Its trees are the diorama's PERCH markers, which a capture writes (BirdDioramaMarkers) - a diorama has no forest to
    ask. Without them, as in a diorama captured before 2026-10-18, the flock lives in the air (BirdWorld::sky_reach):
    it circles and passes over the town and never comes down. The diorama's walkers put it up when they pass under it,
    as anyone walking does in the world.
*/
#define BIRD_TITLE_FLOCK        9
#define BIRD_TITLE_SKY          0.7f        //of the diorama's radius: how far from its middle a flock with no trees flies
#define BIRD_TITLE_SEED         0x7171E5u   //its own: the title is the same every time

//RENDER THREAD, at a capture (DioramaMarkers): each standing tree's perches within the radius, its number and base.
void ApplicationChasm::BirdDioramaMarkers(const vec3& centre, float radius, std::vector<SceneCaptureMarker>& out){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    if (!w || !w->forest || bird_perch_local.empty()){
        return;
    }
    std::shared_ptr<const ZoneState> z = GetZones();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    const ZoneState* zp = (z && z->world == w) ? z.get() : NULL;
    const EconomyState* ep = (e && e->world == w) ? e.get() : NULL;
    const ForestData& f = *w->forest;
    int trees = 0;
    for (size_t i = 0; i < f.props.size(); i++){
        const PropInstance& p = f.props[i];
        float dx = p.pos.x - centre.x;
        float dz = p.pos.z - centre.z;
        if (!ForestIsTree(p.kind) || dx * dx + dz * dz > radius * radius || !BirdTreeStanding(*w,zp,ep,(int)i)){
            continue;
        }
        std::vector<BirdPerch> perches;
        BirdTreePerches(*w,(int)i,perches);
        if (perches.empty()){
            continue;
        }
        std::string d = json{{"tree",trees++},{"x",p.pos.x},{"z",p.pos.z},{"ground",p.pos.y}}.dump();
        for (const BirdPerch& b : perches){
            out.push_back(SceneCaptureMarker{"perch",b.at,d});
        }
    }
}

/*
    RENDER THREAD, from DioramaLife: the diorama's trees from its perch markers, and the flock - made in the air, so
    the title shows it flying from the first frame. Its ground is the highest a tree or a person stands on: one level
    for the whole diorama, which a flock flying over can never dip into.
*/
void ApplicationChasm::BuildTitleBirds(const std::vector<SceneCaptureMarker>& markers){
    title_trees.clear();
    title_bird_objects.clear();     //the last diorama's went with it (ServiceDiorama)
    title_birds.Reset(BIRD_TITLE_SEED);
    f_title_birds_clock = false;
    if (!bird_mesh){
        return;
    }
    bool f_ground = false;
    float ground = diorama.centre.y;
    float top = 0.0f;
    std::map<int,size_t> index;
    for (const SceneCaptureMarker& m : markers){
        if (m.kind == "person"){
            ground = f_ground ? std::max(ground,m.at.y) : m.at.y;
            f_ground = true;
            continue;
        }
        if (m.kind != "perch"){
            continue;
        }
        json d = json::parse(m.data,nullptr,false);
        if (d.is_discarded() || !d.is_object()){
            continue;
        }
        int tree = (int)JsonNumber(d,"tree",-1.0f);
        float base = JsonNumber(d,"ground",m.at.y);
        auto it = index.find(tree);
        if (it == index.end()){
            it = index.insert(std::make_pair(tree,title_trees.size())).first;
            TitleTree t;
            t.at = vec2(JsonNumber(d,"x",m.at.x),JsonNumber(d,"z",m.at.z));
            title_trees.push_back(t);
        }
        TitleTree& t = title_trees[it->second];
        t.perches.push_back(m.at);
        t.top = std::max(t.top,m.at.y);
        ground = f_ground ? std::max(ground,base) : base;
        f_ground = true;
        top = std::max(top,m.at.y - base);
    }
    title_bird_ground = ground;
    title_bird_top = (top > 0.0f) ? top : bird_tree_top;
    BirdWorld bw = TitleBirdWorld();
    title_birds.AddFlockInAir(bw,vec2(diorama.centre.x,diorama.centre.z),BIRD_TITLE_FLOCK,BIRD_ROLE_WANDER);
    for (int i = 0; i < title_birds.Count(); i++){
        Object* o = new Object();
        o->name = "Title bird " + std::to_string(i);
        o->SetMesh(bird_mesh);
        o->SetMaterialSlot(0,palette_material);
        o->SetPickability(false);
        o->SetVisualOnly(true);
        o->SetVisibility(false);
        o->SetScale(vec3(BIRD_DRAW_SCALE,BIRD_DRAW_SCALE,BIRD_DRAW_SCALE));
        title_scene->AddObject(o);
        diorama_objects.push_back(o);
        title_bird_objects.push_back(o);
    }
    debug->Info("Title birds: %i over the diorama, %zu trees to perch in%s\n",title_birds.Count(),title_trees.size(),
                title_trees.empty() ? " (none captured: they stay in the air)" : "");
}

//The diorama as the title's flock asks it: a level ground, its trees as roosts, and the sky to fly on to.
BirdWorld ApplicationChasm::TitleBirdWorld(){
    BirdWorld bw;
    bw.tree_top = title_bird_top;
    bw.sky_reach = BIRD_TITLE_SKY * ((diorama.radius > 0.0f) ? diorama.radius : 30.0f);
    const float ground = title_bird_ground;
    bw.ground = [ground](const vec2&){
        return ground;
    };
    bw.roost = [this](const vec2& centre, float reach, const std::vector<BirdScare>& avoid, uint32_t draw, BirdRoost& out){
        std::vector<int> found;
        for (size_t t = 0; t < title_trees.size(); t++){
            const vec2& at = title_trees[t].at;
            bool f_clear = (at - centre).length() <= reach;
            for (const BirdScare& s : avoid){
                f_clear = f_clear && (at - s.at).length() > s.reach;
            }
            if (f_clear){
                found.push_back((int)t);
            }
        }
        if (found.empty()){
            return false;
        }
        int tree = found[draw % (uint32_t)found.size()];
        const TitleTree& t = title_trees[tree];
        out.tree = tree;
        out.at = vec3(t.at.x,t.top,t.at.y);
        out.perches.clear();
        //Its perches first, then its neighbours', so a flock sits over two or three trees as in the world.
        for (size_t n = 0; n < title_trees.size() + 1; n++){
            int i = (n == 0) ? tree : (int)n - 1;
            if (n > 0 && (i == tree || (title_trees[i].at - t.at).length() > BIRD_ROOST_SPREAD)){
                continue;
            }
            for (const vec3& p : title_trees[i].perches){
                if ((int)out.perches.size() < BIRD_ROOST_PERCHES){
                    out.perches.push_back(BirdPerch{p,i});
                }
            }
        }
        return true;
    };
    return bw;
}

//RENDER THREAD, from PreRender while the title shows: the flock stepped by real time, its calls let go, and posed.
void ApplicationChasm::UploadTitleBirds(){
    if (title_bird_objects.empty()){
        return;
    }
    auto now = std::chrono::steady_clock::now();
    float dt = f_title_birds_clock ? std::min(std::chrono::duration<float>(now - title_birds_last).count(),BIRD_FRAME_MAX_S)
                                   : 0.0f;
    title_birds_last = now;
    f_title_birds_clock = true;
    BirdWorld bw = TitleBirdWorld();
    std::vector<BirdScare> scares;
    for (const DioramaPerson& p : diorama_people){
        if (!p.path.empty() && p.ws.figure){
            vec3 at = p.ws.figure->GetPosition();
            scares.push_back(BirdScare{vec2(at.x,at.z),BIRD_REACH_WALK});
        }
    }
    if (dt > 0.0f){
        int steps = std::min(BIRD_STEPS_MAX,std::max(1,(int)ceilf(dt / BIRD_STEP_S)));
        for (int i = 0; i < steps; i++){
            title_birds.Step(bw,dt / (float)steps,scares);
        }
    }
    std::vector<BirdCall> calls;
    title_birds.TakeCalls(calls);
    std::vector<BirdPose> poses;
    if (f_view_birds){
        title_birds.Poses(poses);
    }
    for (size_t i = 0; i < title_bird_objects.size(); i++){
        if (i < poses.size()){
            PoseBird(title_bird_objects[i],poses[i]);
        }else{
            title_bird_objects[i]->SetVisibility(false);
        }
    }
    if (++title_bird_status_frames >= 10 && !title_birds.Flocks().empty()){
        title_bird_status_frames = 0;
        static const char* states[] = {"settled","travel","circle","scatter"};
        const BirdFlock& f = title_birds.Flocks()[0];
        int perched = 0;
        for (const Bird& b : f.birds){
            perched += (b.state == BIRD_PERCHED) ? 1 : 0;
        }
        json status{{"birds",(int)f.birds.size()},{"state",states[f.state & 3]},{"perched",perched},
                    {"anchor",{f.anchor.x,f.anchor.y,f.anchor.z}},{"roost_tree",f.roost.tree},
                    {"trees",(int)title_trees.size()},{"flushes",f.flushes},{"walkers",(int)scares.size()},
                    {"ground",title_bird_ground},{"tree_top",title_bird_top}};
        std::lock_guard<std::mutex> lock(birds_mutex);
        title_birds_status = status;
    }
}

#ifdef USE_MCP
void ApplicationChasm::RegisterBirdTools(){
    MCPServer::Get()->RegisterTool("chasm_birds",
        "The birds (birds_plan.md, ApplicationChasmBirds.cpp) - VIEW, never state: flocks homed in the forests, perched "
        "in the trees, moving on now and then, scared up by anyone walking near, an axe at a tree or a tree felled. "
        "op status (default): each flock's state, birds, its own size and how many are leaving, anchor, roost, voice "
        "(crow, or magpie for a wanderer), calls made and seconds to its next; the scares this frame; the season's "
        "`gather` (flocks grow to their size times it, in the air: 1 spring and summer, more in autumn and winter); and "
        "`title`, the title's flock over its diorama. op gather: holds `value` as the gather (0 or less: the date's "
        "again), for seeing a winter's flocks without waiting for one. op spawn: a flock of "
        "`count` near x,z (default the camera target), living by `role`. op flush: scares every flock within `reach` (default the axe's) "
        "of x,z, as if someone were there. op clear: no flocks. op reset: the map's flocks made again at their homes. "
        "op bench: one bird at x,z (default the camera target, `height` over the ground, default 1.5), turning slowly, "
        "held in `pose` glide|up|down|folded or `flap`ping on the real clock, so it moves in a pause; pose off removes "
        "it. op show / hide: draw the flocks or not. A request is carried out on the render thread's next frame; the "
        "status is at most a sixth of a second old.",
        json{
            {"type","object"},
            {"properties",{
                {"op",{{"type","string"},{"enum",json::array({"status","spawn","flush","clear","reset","bench","show","hide",
                                                                "gather"})}}},
                {"value",{{"type","number"},{"description","gather: the gather held, 0 or less the date's"}}},
                {"x",{{"type","number"}}},
                {"z",{{"type","number"}}},
                {"count",{{"type","integer"},{"description","spawn: birds in the flock (default 7)"}}},
                {"role",{{"type","string"},{"enum",json::array({"forest","colony","wander"})},
                         {"description","spawn: how it lives (default forest); wander is mostly in the air"}}},
                {"reach",{{"type","number"},{"description","flush: how far it reaches"}}},
                {"pose",{{"type","string"},{"enum",json::array({"glide","up","down","folded","flap","off"})}}},
                {"height",{{"type","number"},{"description","bench: over the ground"}}},
                {"include_screenshot",{{"type","boolean"}}},
                {"include_ui",{{"type","boolean"}}}
            }}
        },
        [this](const json& args) -> json {
            if (!bird_mesh){
                return json{{"error","no bird in chasm_props.glb"}};
            }
            std::string op = args.value("op","status");
            //camera_target belongs to the physics thread (ApplicationChasm.h, THREADS).
            vec3 target;
            main_scene->AtTickBoundary([&](){ target = camera_target; });
            vec2 at(args.value("x",target.x),args.value("z",target.z));
            json result;
            if (op == "status"){
                std::lock_guard<std::mutex> lock(birds_mutex);
                result = birds_status;
                result["title"] = title_birds_status;
            }else if (op == "gather"){
                float value = args.value("value",0.0f);
                bird_gather_held = (value > 0.0f) ? value : -1.0f;
                result = json{{"op","gather"},{"held",value > 0.0f ? json(value) : json("the date's")}};
            }else if (op == "spawn" || op == "flush" || op == "clear" || op == "reset"){
                BirdRequest r;
                r.op = (op == "spawn") ? BIRD_REQUEST_SPAWN : (op == "flush") ? BIRD_REQUEST_FLUSH
                     : (op == "clear") ? BIRD_REQUEST_CLEAR : BIRD_REQUEST_RESET;
                r.at = at;
                r.count = std::max(1,std::min(BIRD_FLOCK_MAX * 3,args.value("count",7)));
                std::string role = args.value("role","forest");
                r.role = (role == "wander") ? BIRD_ROLE_WANDER : (role == "colony") ? BIRD_ROLE_COLONY : BIRD_ROLE_FOREST;
                r.reach = args.value("reach",BIRD_REACH_CHOP);
                std::lock_guard<std::mutex> lock(birds_mutex);
                bird_requests.push_back(r);
                result = json{{"op",op},{"at",{at.x,at.y}},{"note","carried out on the render thread's next frame"}};
            }else if (op == "bench"){
                std::string pose = args.value("pose","flap");
                int key = (pose == "up") ? BIRD_KEY_UP : (pose == "down") ? BIRD_KEY_DOWN : (pose == "folded") ? BIRD_KEY_FOLDED
                        : (pose == "glide") ? 3 : (pose == "flap") ? 4 : -1;
                std::shared_ptr<const ChasmWorld> w = GetWorld();
                float y = target.y;
                if (w){
                    GridPick pick = w->picker->Pick(at);
                    if (pick.f_hit){
                        y = w->terrain->GroundHeight(at,w->terrain->Height(pick.plot));
                    }
                }
                vec3 where(at.x,y + args.value("height",1.5f),at.y);
                std::lock_guard<std::mutex> lock(birds_mutex);
                bird_bench_key = key;
                bird_bench_at = where;
                result = json{{"op","bench"},{"pose",pose},{"at",{where.x,where.y,where.z}}};
            }else if (op == "show" || op == "hide"){
                f_view_birds = (op == "show");
                result = json{{"op",op}};
            }else{
                return json{{"error","unknown op " + op}};
            }
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false),args.value("include_ui",true));
        });
}
#endif
