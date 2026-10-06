#include "Exploration.h"

#include <algorithm>
#include <cmath>
#include <sstream>

void Exploration::Reset(const vec2& lo, const vec2& hi){
    origin = lo;
    width = std::max(1,(int)std::ceil((hi.x - lo.x) / EXPLORATION_CELL) + 1);
    height = std::max(1,(int)std::ceil((hi.y - lo.y) / EXPLORATION_CELL) + 1);
    cells.assign((size_t)width * height,0);
    version++;
}

void Exploration::RevealAll(){
    std::fill(cells.begin(),cells.end(),(uint8_t)1);
    version++;
}

bool Exploration::Reveal(const vec2& centre, float radius){
    if (cells.empty()){
        return false;
    }
    //Cell (x, z) covers origin + (x, z) * CELL; a cell is in the disc if its centre is.
    int x0 = std::max(0,(int)std::floor((centre.x - radius - origin.x) / EXPLORATION_CELL));
    int x1 = std::min(width - 1,(int)std::ceil((centre.x + radius - origin.x) / EXPLORATION_CELL));
    int z0 = std::max(0,(int)std::floor((centre.y - radius - origin.y) / EXPLORATION_CELL));
    int z1 = std::min(height - 1,(int)std::ceil((centre.y + radius - origin.y) / EXPLORATION_CELL));
    float r2 = radius * radius;
    bool f_changed = false;
    for (int z = z0; z <= z1; z++){
        float dz = origin.y + ((float)z + 0.5f) * EXPLORATION_CELL - centre.y;
        for (int x = x0; x <= x1; x++){
            float dx = origin.x + ((float)x + 0.5f) * EXPLORATION_CELL - centre.x;
            if (dx * dx + dz * dz > r2){
                continue;
            }
            uint8_t& c = cells[(size_t)z * width + x];
            if (!c){
                c = 1;
                f_changed = true;
            }
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
    int x = (int)std::floor((p.x - origin.x) / EXPLORATION_CELL);
    int z = (int)std::floor((p.y - origin.y) / EXPLORATION_CELL);
    if (x < 0 || z < 0 || x >= width || z >= height){
        return false;
    }
    return cells[(size_t)z * width + x] != 0;
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
