#include "ApplicationChasm.h"
#include "MeshBuild.h"
#include "Palette.h"

#include <math.h>

/*
    THE WORKERS, DRAWN (docs/economy_plan.md, P2 + P3). Each of the economy's workers is a walker's
    figure (ApplicationChasmRoads.cpp) in his job's tunic - so a woodcutter, a farmer and a water
    carrier are told apart at the game's zoom - with what he carries on him while he carries it: a log
    on the shoulder, a sack on the back, a pail in the hand. A woodcutter at work bobs as he swings.

    View only, like the walkers: one object per worker, reused, placed every frame from the published
    EconomyState. The state moves on ticks, so a worker steps at the tick rate.
*/

//ApplicationChasmRoads.cpp: the walker's figure, origin at the feet, facing +z.
void BuildWalkerFigure(std::vector<vertex>& out, int tunic_column);

#define WORKER_JOBS         4       //WORKER_JOB_*, and the last for nobody's (WORKER_JOB_NONE)
#define WORKER_CHOP_RATE    1.6f    //a working woodcutter's swings a second
#define WORKER_CHOP_BOB     0.07f   //and how far each lifts him

namespace {

//A tunic per job, from the palette's temperate row: the forest's dark green, the wheat, the water.
const int job_tunics[WORKER_JOBS] = {PAL_PINE_DARK,PAL_FIELD,PAL_WATER,PAL_WALL};    //no job: plain linen

//An axis-aligned box from lo to hi, flat-shaded.
void Box(std::vector<vertex>& out, const vec3& lo, const vec3& hi, int column){
    vec3 c[8];
    for (int i = 0; i < 8; i++){
        c[i] = vec3((i & 1) ? hi.x : lo.x,(i & 2) ? hi.y : lo.y,(i & 4) ? hi.z : lo.z);
    }
    MeshQuad(out,c[0],c[2],c[6],c[4],vec3(-1,0,0),column);
    MeshQuad(out,c[1],c[3],c[7],c[5],vec3(1,0,0),column);
    MeshQuad(out,c[0],c[1],c[5],c[4],vec3(0,-1,0),column);
    MeshQuad(out,c[2],c[3],c[7],c[6],vec3(0,1,0),column);
    MeshQuad(out,c[0],c[1],c[3],c[2],vec3(0,0,-1),column);
    MeshQuad(out,c[4],c[5],c[7],c[6],vec3(0,0,1),column);
}

//What a job carries, added to the figure: wood as a log along the right shoulder, food as a sack on
//the back, water as a pail in the right hand with the water showing at its top.
void AddLoad(std::vector<vertex>& out, int job){
    switch (job){
        case WORKER_JOB_WOODCUTTER:
            Box(out,vec3(0.09f,0.50f,-0.34f),vec3(0.21f,0.61f,0.30f),PAL_BARK);
            break;
        case WORKER_JOB_FARMER:
            Box(out,vec3(-0.13f,0.24f,-0.32f),vec3(0.13f,0.54f,-0.12f),PAL_STONE_LIGHT);
            break;
        default:{
            Box(out,vec3(0.17f,0.10f,-0.07f),vec3(0.31f,0.28f,0.07f),PAL_TIMBER);
            const float y = 0.282f;
            MeshQuad(out,vec3(0.19f,y,-0.05f),vec3(0.29f,y,-0.05f),vec3(0.29f,y,0.05f),vec3(0.19f,y,0.05f),
                     vec3(0,1,0),PAL_WATER);
            break;
        }
    }
}

}

//RENDER THREAD, from BuildScene: a mesh per job, empty-handed and carrying.
void ApplicationChasm::BuildWorkerScene(){
    std::vector<vertex> verts;
    for (int job = 0; job < WORKER_JOBS; job++){
        for (int carrying = 0; carrying < 2; carrying++){
            BuildWalkerFigure(verts,job_tunics[job]);
            if (carrying){
                AddLoad(verts,job);
            }
            Mesh* mesh = new Mesh();
            mesh->SetMeshData(verts.data(),(int)verts.size());
            /*
                Held for good. Object::SetMesh releases the mesh an object had, and the last release
                deletes it - so without this, the moment every worker carrying nothing had picked
                something up, the empty-handed figure was freed, and the next one to put his load
                down was drawn from a deleted mesh (GL errors, then a crash).
            */
            mesh->Retain();
            worker_meshes[job * 2 + carrying] = mesh;
        }
    }
}

/*
    RENDER THREAD. One object per worker, made when there are more workers than objects and hidden
    when there are fewer. An object's mesh follows its worker's job and load; a change of mesh joins
    the scene's walk like a new object does, under its lock, and is rare (a load picked up or put
    down), so the frames between pay nothing for it.
*/
void ApplicationChasm::UploadWorkers(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    size_t n = (w && e && e->world == w) ? e->workers.size() : 0;
    if (worker_objects.size() < n){
        main_scene->AtTickBoundary([&](){
            while (worker_objects.size() < n){
                Object* o = new Object();
                o->name = "Worker " + std::to_string(worker_objects.size());
                o->SetMesh(worker_meshes[0]);
                o->SetMaterialSlot(0,palette_material);
                o->SetPickability(false);
                o->SetVisualOnly(true);
                o->SetVisibility(false);
                main_scene->AddObject(o);
                worker_objects.push_back(o);
                worker_object_mesh.push_back(0);
            }
        });
    }
    std::vector<int> want(n,0);
    bool f_swap = false;
    for (size_t i = 0; i < n; i++){
        const EconomyWorker& k = e->workers[i];
        int job = (k.job < 0) ? WORKER_JOBS - 1 : std::min(WORKER_JOBS - 1,k.job);
        want[i] = job * 2 + (k.carry > 0 ? 1 : 0);
        f_swap = f_swap || want[i] != worker_object_mesh[i];
    }
    if (f_swap){
        main_scene->AtTickBoundary([&](){
            for (size_t i = 0; i < n; i++){
                if (want[i] != worker_object_mesh[i]){
                    worker_objects[i]->SetMesh(worker_meshes[want[i]]);
                    worker_object_mesh[i] = want[i];
                }
            }
        });
    }
    float seconds = (float)SimSeconds();
    for (size_t i = 0; i < worker_objects.size(); i++){
        Object* o = worker_objects[i];
        if (i >= n){
            o->SetVisibility(false);
            continue;
        }
        const EconomyWorker& k = e->workers[i];
        //At home, he is indoors (P4): not drawn.
        if (EconomyIndoors(k)){
            o->SetVisibility(false);
            continue;
        }
        //On the ground of the plot he stands on.
        GridPick pick = w->picker->Pick(k.pos);
        float y = pick.f_hit ? w->terrain->GroundHeight(k.pos,w->terrain->Height(pick.plot)) : 0.0f;
        if ((k.job == WORKER_JOB_WOODCUTTER && k.state == WORKER_WORKING) || k.state == WORKER_BUILDING){
            //Each worker on his own beat, so two at one tree (or one site) do not swing as one.
            float phase = seconds * WORKER_CHOP_RATE + (float)i * 0.37f;
            y += WORKER_CHOP_BOB * fabsf(sinf(phase * 3.1415927f));
        }
        vec2 facing = EconomyWorkerFacing(k);
        if (facing.dot(facing) < 1e-8f){
            facing = vec2(0.0f,1.0f);
        }
        o->SetPosition(vec3(k.pos.x,y,k.pos.y),false);
        o->SetRotation(quat(vec3(0.0f,1.0f,0.0f),atan2f(facing.x,facing.y)),false);
        o->SetVisibility(true);
    }
}
