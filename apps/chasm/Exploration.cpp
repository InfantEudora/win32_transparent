#include "Exploration.h"
#include "Grid.h"
#include "GridPick.h"
#include "Terrain.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>

//The raster's size over the map from lo to hi: the exploration's and its sight's must agree.
static void RasterSize(const vec2& lo, const vec2& hi, int& width, int& height){
    width = std::max(1,(int)std::ceil((hi.x - lo.x) / EXPLORATION_CELL) + 1);
    height = std::max(1,(int)std::ceil((hi.y - lo.y) / EXPLORATION_CELL) + 1);
}

void ExplorationSight::Build(const Grid& g, const GridPicker& picker, const Terrain& t){
    auto t0 = std::chrono::steady_clock::now();
    origin = g.bounds_min;
    RasterSize(g.bounds_min,g.bounds_max,width,height);
    float plateau = terrain_levels[TERRAIN_PLATEAU].height;
    ground.assign((size_t)width * height,plateau);
    lid.assign((size_t)width * height,plateau);
    lid_min = plateau;
    for (int z = 0; z < height; z++){
        for (int x = 0; x < width; x++){
            size_t i = (size_t)z * width + x;
            vec2 p = origin + vec2((float)x + 0.5f,(float)z + 0.5f) * EXPLORATION_CELL;
            //As the clouds' lattice takes it (ApplicationChasmExplore.cpp): never below the plateau's level.
            lid[i] = std::max(plateau,t.GroundHeight(p,plateau));
            //Off the grid, past its ragged edge, is taken as the lid: nobody stands there to look.
            GridPick k = picker.Pick(p);
            ground[i] = (k.plot >= 0) ? t.GroundHeight(p,t.Height(k.plot)) : lid[i];
            lid_min = std::min(lid_min,lid[i]);
        }
    }
    build_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
}

bool ExplorationSight::Clear(const vec2& from, float eye_ground, const vec2& to, float top) const{
    if (ground.empty()){
        return true;
    }
    auto cell_of = [&](const vec2& p){
        int x = (int)std::floor((p.x - origin.x) / EXPLORATION_CELL);
        int z = (int)std::floor((p.y - origin.y) / EXPLORATION_CELL);
        return (x < 0 || z < 0 || x >= width || z >= height) ? -1 : z * width + x;
    };
    int a = cell_of(from);
    int b = cell_of(to);
    float eye = eye_ground + SIGHT_EYE;
    //Every half cell along the way, so no cell it crosses is stepped over.
    int n = (int)std::ceil((to - from).length() / (0.5f * EXPLORATION_CELL));
    for (int i = 1; i < n; i++){
        float f = (float)i / (float)n;
        int c = cell_of(from + (to - from) * f);
        if (c < 0 || c == a || c == b){
            continue;
        }
        float h = std::max(ground[(size_t)c],std::min(eye_ground,lid[(size_t)c]));
        if (h > eye + (top - eye) * f){
            return false;
        }
    }
    return true;
}

void Exploration::Reset(const vec2& lo, const vec2& hi){
    origin = lo;
    RasterSize(lo,hi,width,height);
    cells.assign((size_t)width * height,0);
    version++;
}

void Exploration::RevealAll(){
    std::fill(cells.begin(),cells.end(),(uint8_t)1);
    version++;
}

int Exploration::CellAt(const vec2& p) const{
    int x = (int)std::floor((p.x - origin.x) / EXPLORATION_CELL);
    int z = (int)std::floor((p.y - origin.y) / EXPLORATION_CELL);
    if (x < 0 || z < 0 || x >= width || z >= height){
        return -1;
    }
    return z * width + x;
}

vec2 Exploration::CellCentre(int cell) const{
    return origin + vec2((float)(cell % width) + 0.5f,(float)(cell / width) + 0.5f) * EXPLORATION_CELL;
}

bool Exploration::See(const ExplorationSight& sight, int cell, float eye_ground){
    if (cells.empty() || cell < 0 || cell >= (int)cells.size() || sight.width != width || sight.height != height){
        return false;
    }
    int cx = cell % width;
    int cz = cell / width;
    float eye = eye_ground + SIGHT_EYE;
    //The furthest anything could be seen: over the lowest ground there can be, the lowest lid's.
    float lowest = std::min(eye_ground,sight.lid_min);
    float reach = std::min(SIGHT_RANGE_MAX,SIGHT_RANGE + SIGHT_RANGE_PER_HEIGHT * (eye_ground - lowest));
    int n = std::max(1,(int)std::ceil(reach / EXPLORATION_CELL));
    bool f_changed = false;
    if (!cells[(size_t)cell]){
        cells[(size_t)cell] = 1;
        f_changed = true;
    }
    /*
        A ray to every cell on the edge of the square n cells out, one cell a step along its longer
        axis, so every cell inside is on some ray. Along each the HORIZON is the steepest the ground
        has risen so far, as the eye sees it: a cell is seen when the point over its ground stands at
        or above the horizon, and then its own ground may raise the horizon for the cells behind it.
    */
    for (int r = 0; r < 8 * n; r++){
        int side = r / (2 * n);
        int k = r % (2 * n) - n;
        int tx = (side == 0) ? k : (side == 1) ? n : (side == 2) ? -k : -n;
        int tz = (side == 0) ? -n : (side == 1) ? k : (side == 2) ? n : -k;
        float horizon = -1e30f;
        for (int i = 1; i <= n; i++){
            int x = cx + (int)std::floor((float)(tx * i) / (float)n + 0.5f);
            int z = cz + (int)std::floor((float)(tz * i) / (float)n + 0.5f);
            if (x < 0 || z < 0 || x >= width || z >= height){
                break;
            }
            float dx = (float)(x - cx) * EXPLORATION_CELL;
            float dz = (float)(z - cz) * EXPLORATION_CELL;
            float d = std::sqrt(dx * dx + dz * dz);
            if (d > reach){
                break;
            }
            size_t j = (size_t)z * width + x;
            //Lower than both the eye's ground and the lid is air at the lower of the two (Exploration.h).
            float h = std::max(sight.ground[j],std::min(eye_ground,sight.lid[j]));
            float range = std::min(SIGHT_RANGE_MAX,SIGHT_RANGE + SIGHT_RANGE_PER_HEIGHT * std::max(0.0f,eye_ground - h));
            if (!cells[j] && d <= range && (h + SIGHT_TARGET - eye) / d >= horizon){
                cells[j] = 1;
                f_changed = true;
            }
            horizon = std::max(horizon,(h - eye) / d);
        }
    }
    if (f_changed){
        version++;
    }
    return f_changed;
}

bool Exploration::Explored(const vec2& p) const{
    if (cells.empty()){
        return true;
    }
    int cell = CellAt(p);
    return cell >= 0 && cells[(size_t)cell] != 0;
}

/*
    A puff of the clouds' lattice (ApplicationChasmExplore.cpp) is jittered past the raster on every side, and off it
    Explored says no for good: a hundred puffs on seed 12 stood on the map's edge with everything round them explored
    (a bug report, 2026-10-24). One there takes the nearest cell's, so it clears with the edge it hangs over.
*/
bool Exploration::ExploredNearest(const vec2& p) const{
    if (cells.empty()){
        return true;
    }
    int x = std::max(0,std::min(width - 1,(int)std::floor((p.x - origin.x) / EXPLORATION_CELL)));
    int z = std::max(0,std::min(height - 1,(int)std::floor((p.y - origin.y) / EXPLORATION_CELL)));
    return cells[(size_t)z * width + x] != 0;
}

int Exploration::CountExplored() const{
    return (int)std::count(cells.begin(),cells.end(),(uint8_t)1);
}

float Exploration::DistanceTo(const vec2& p, float reach, uint8_t value) const{
    if (cells.empty()){
        return reach;
    }
    int cx = (int)std::floor((p.x - origin.x) / EXPLORATION_CELL);
    int cz = (int)std::floor((p.y - origin.y) / EXPLORATION_CELL);
    int r = (int)std::ceil(reach / EXPLORATION_CELL);
    float best = reach * reach;
    for (int z = cz - r; z <= cz + r; z++){
        for (int x = cx - r; x <= cx + r; x++){
            //Off the map counts as neither: the clouds stop at the map's edge, they do not thin there.
            if (x < 0 || z < 0 || x >= width || z >= height || cells[(size_t)z * width + x] != value){
                continue;
            }
            float dx = origin.x + ((float)x + 0.5f) * EXPLORATION_CELL - p.x;
            float dz = origin.y + ((float)z + 0.5f) * EXPLORATION_CELL - p.y;
            best = std::min(best,dx * dx + dz * dz);
        }
    }
    return std::sqrt(best);
}

std::string Exploration::ToString() const{
    std::ostringstream out;
    out << width << "x" << height;
    uint8_t run_value = 0;
    size_t run = 0;
    for (uint8_t c : cells){
        if (c == run_value){
            run++;
            continue;
        }
        out << " " << run;
        run_value = c;
        run = 1;
    }
    out << " " << run;
    return out.str();
}

bool Exploration::FromString(const std::string& s){
    std::istringstream in(s);
    int w = 0, h = 0;
    char x = 0;
    if (!(in >> w >> x >> h) || x != 'x' || w != width || h != height){
        return false;
    }
    std::vector<uint8_t> next;
    next.reserve(cells.size());
    uint8_t value = 0;
    size_t run;
    while (in >> run){
        if (next.size() + run > cells.size()){
            return false;
        }
        next.insert(next.end(),run,value);
        value ^= 1;
    }
    if (next.size() != cells.size()){
        return false;
    }
    cells.swap(next);
    version++;
    return true;
}

uint64_t Exploration::Hash() const{
    uint64_t h = 1469598103934665603ull;
    for (uint8_t c : cells){
        h = (h ^ c) * 1099511628211ull;
    }
    return h;
}
