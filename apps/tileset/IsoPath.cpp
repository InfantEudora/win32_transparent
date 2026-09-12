#include "IsoPath.h"
#include "Debug.h"
#include <unordered_map>
#include <unordered_set>

static Debugger* debug = new Debugger("IsoPath", DEBUG_INFO);

IsoPath::IsoPath(){ }
IsoPath::~IsoPath(){ }

void IsoPath::Clear(){ cells.clear(); }
bool IsoPath::Empty() const{ return cells.empty(); }

IsoCell* IsoPath::PopNext(){
    if(cells.empty()) return NULL;
    IsoCell* c = cells.front();
    cells.erase(cells.begin());
    return c;
}

// Simple BFS pathfinder that only traverses cells that have roads.
bool IsoPath::BuildPath(IsoTerrain* terrain, IsoCell* start, IsoCell* end, IsoPath& out){
    out.Clear();
    if(!terrain || !start || !end) return false;
    if(start == end){
        out.cells.push_back(start);
        return true;
    }

    // Both start and end should have roads for road-only path.
    if(!start->road_object || !end->road_object) return false;

    std::deque<IsoCell*> queue;
    std::unordered_map<IsoCell*, IsoCell*> parent;
    std::unordered_set<IsoCell*> visited;

    queue.push_back(start);
    visited.insert(start);

    bool found = false;
    while(!queue.empty()){
        IsoCell* cur = queue.front(); queue.pop_front();
        if(cur == end){ found = true; break; }

        for(int d=0; d<4; d++){
            IsoCell* n = cur->GetNeighbour(d);
            if(!n) continue;
            if(visited.count(n)) continue;
            // Only traverse cells that have a road
            if(!n->road_object) continue;
            visited.insert(n);
            parent[n] = cur;
            queue.push_back(n);
        }
    }

    if(!found) return false;

    // Backtrack path
    IsoCell* cur = end;
    std::vector<IsoCell*> rev;
    while(cur){
        rev.push_back(cur);
        auto it = parent.find(cur);
        if(it == parent.end()) break;
        cur = it->second;
    }
    // reverse
    for(auto it = rev.rbegin(); it != rev.rend(); ++it){
        out.cells.push_back(*it);
    }
    return true;
}

// BFS search to find the closest cell that has a road. We traverse all cells.
IsoCell* IsoPath::FindClosestRoadCell(IsoTerrain* terrain, IsoCell* start){
    if(!terrain || !start) return NULL;
    if(start->road_object) return start;

    std::deque<IsoCell*> queue;
    std::unordered_set<IsoCell*> visited;

    queue.push_back(start);
    visited.insert(start);

    while(!queue.empty()){
        IsoCell* cur = queue.front(); queue.pop_front();
        if(cur->road_object) return cur;
        for(int d=0; d<4; d++){
            IsoCell* n = cur->GetNeighbour(d);
            if(!n) continue;
            if(visited.count(n)) continue;
            visited.insert(n);
            queue.push_back(n);
        }
    }
    return NULL;
}
