#include <math.h>
#include <string.h>
#include <algorithm>

#include "Stage.h"

/*
    The floors, their edges and the feet of their walls - see StageEdge in Stage.h and
    vine_plan.md section 15. Stage's own methods, in a file of their own because they are a
    geometry pass over the blocks rather than rules, and like Stage.cpp nothing here includes an
    engine header, so `make rules` tests it with the rest.

    THE WHOLE PASS, in three steps:
      1. Each live block's top, less whatever stands on it or runs through it. A one-way platform
         covers nothing - she stands under one, and walks up through it.
      2. The pieces at one height that touch are joined into spans: a floor built of several boxes,
         like the main ground and the bay against it, is one floor with no edge where they meet.
      3. Each span's two ends, probed just past them: something rising there is a CORNER, the foot
         of a wall; nothing is an EDGE, measured for how far it drops and how much bare face is
         under the lip.

    A level has a few hundred blocks at most and this runs when they change, not every tick, so it
    is written to be plainly right rather than fast: every step is a scan over all the blocks.
*/

static const float EDGE_EPS = 0.001f;

//The block counts as a WALL - something she cannot walk into or through at this height.
static bool IsWall(const StageBlock& b){
    return b.f_alive && b.kind != BLOCK_PLATFORM;
}

/*
    The solid run of column x that contains height y: the union of every block across x, walked up
    and down from y while the boxes touch. Returns false if nothing at x holds y. `walls_only`
    leaves platforms out.
*/
static bool ColumnAt(const std::vector<StageBlock>& blocks, float x, float y, bool walls_only,
                     float& out_bottom, float& out_top){
    bool f_found = false;
    float lo = 0.0f, hi = 0.0f;
    for (const StageBlock& b : blocks){
        if (!b.f_alive || (walls_only && !IsWall(b)) || x < b.Left() || x > b.Right()){
            continue;
        }
        if (y >= b.Bottom() && y <= b.Top()){
            lo = f_found ? std::min(lo,b.Bottom()) : b.Bottom();
            hi = f_found ? std::max(hi,b.Top()) : b.Top();
            f_found = true;
        }
    }
    if (!f_found){
        return false;
    }
    //Grow through boxes that touch the run, until none does - a stack of three is one face.
    for (bool f_grew = true; f_grew;){
        f_grew = false;
        for (const StageBlock& b : blocks){
            if (!b.f_alive || (walls_only && !IsWall(b)) || x < b.Left() || x > b.Right()){
                continue;
            }
            if (b.Top() >= lo - EDGE_EPS && b.Bottom() < lo - EDGE_EPS){ lo = b.Bottom(); f_grew = true; }
            if (b.Bottom() <= hi + EDGE_EPS && b.Top() > hi + EDGE_EPS){ hi = b.Top(); f_grew = true; }
        }
    }
    out_bottom = lo;
    out_top = hi;
    return true;
}

//The highest floor top at column x that is below y, or false for none.
static bool FloorBelow(const std::vector<StageBlock>& blocks, float x, float y, float& out_top){
    bool f_found = false;
    for (const StageBlock& b : blocks){
        if (!b.f_alive || x < b.Left() || x > b.Right() || b.Top() > y - EDGE_EPS){
            continue;
        }
        if (!f_found || b.Top() > out_top){
            out_top = b.Top();
            f_found = true;
        }
    }
    return f_found;
}

void Stage::RebuildEdges(){
    spans.clear();
    edges.clear();
    corners.clear();

    //--- 1. The open part of every top -----------------------------------------------------------
    struct Piece{ float a, b, y; int block; long key; };
    std::vector<Piece> pieces;
    for (size_t i = 0; i < blocks.size(); i++){
        const StageBlock& b = blocks[i];
        if (!b.f_alive){
            continue;
        }
        float top = b.Top();
        std::vector<std::pair<float,float>> open(1,std::make_pair(b.Left(),b.Right()));
        for (size_t j = 0; j < blocks.size() && !open.empty(); j++){
            const StageBlock& o = blocks[j];
            if (j == i || !IsWall(o) || o.Bottom() > top + EDGE_EPS || o.Top() <= top + EDGE_EPS){
                continue;       //not standing on this top, or not running up through it
            }
            std::vector<std::pair<float,float>> left;
            for (const std::pair<float,float>& r : open){
                if (o.Right() <= r.first || o.Left() >= r.second){
                    left.push_back(r);
                    continue;
                }
                if (o.Left() > r.first){ left.push_back(std::make_pair(r.first,o.Left())); }
                if (o.Right() < r.second){ left.push_back(std::make_pair(o.Right(),r.second)); }
            }
            open.swap(left);
        }
        for (const std::pair<float,float>& r : open){
            if (r.second - r.first > EDGE_EPS){
                //Keyed by the height to a join's width, so two tops a hair apart sort together.
                Piece p = { r.first,r.second,top,(int)i,lroundf(top / STAGE_EDGE_JOIN) };
                pieces.push_back(p);
            }
        }
    }

    //--- 2. Joined into floors -------------------------------------------------------------------
    std::sort(pieces.begin(),pieces.end(),[](const Piece& p, const Piece& q){
        if (p.key != q.key){ return p.key < q.key; }
        if (p.a != q.a){ return p.a < q.a; }
        return p.block < q.block;
    });
    for (size_t i = 0; i < pieces.size(); i++){
        const Piece& p = pieces[i];
        if (!spans.empty()){
            StageSpan& last = spans.back();
            const Piece& prev = pieces[i - 1];
            if (prev.key == p.key && p.a <= last.x1 + STAGE_EDGE_JOIN){
                if (p.b > last.x1){
                    last.x1 = p.b;
                    last.block_right = p.block;
                }
                continue;
            }
        }
        StageSpan s;
        s.x0 = p.a;
        s.x1 = p.b;
        s.y = p.y;
        s.block_left = p.block;
        s.block_right = p.block;
        spans.push_back(s);
    }

    //--- 3. What is past each end ----------------------------------------------------------------
    for (size_t k = 0; k < spans.size(); k++){
        const StageSpan& s = spans[k];
        for (int end = 0; end < 2; end++){
            int side = end ? 1 : -1;
            float x = end ? s.x1 : s.x0;
            int block = end ? s.block_right : s.block_left;
            float outside = x + side * STAGE_EDGE_JOIN;
            float inside = x - side * STAGE_EDGE_JOIN;

            //A wall rising from the floor: its run in the column just past the end.
            float wb = 0.0f, wt = 0.0f;
            if (ColumnAt(blocks,outside,s.y + EDGE_EPS * 2.0f,true,wb,wt) && wt > s.y + EDGE_EPS){
                StageCorner c;
                c.x = x;
                c.y = s.y;
                c.side = side;
                c.span = (int)k;
                c.rise = wt - s.y;
                //The block that rises: the one across the column there with the floor's height in it.
                for (size_t j = 0; j < blocks.size(); j++){
                    const StageBlock& o = blocks[j];
                    if (IsWall(o) && outside >= o.Left() && outside <= o.Right() &&
                        o.Bottom() <= s.y + EDGE_EPS * 2.0f && o.Top() > s.y + EDGE_EPS){
                        c.block = (int)j;
                        break;
                    }
                }
                corners.push_back(c);
                continue;
            }

            StageEdge e;
            e.x = x;
            e.y = s.y;
            e.side = side;
            e.block = block;
            e.span = (int)k;
            float floor_top = 0.0f;
            bool f_floor = FloorBelow(blocks,outside,s.y,floor_top);
            e.drop = f_floor ? (s.y - floor_top) : VITALS_NO_FLOOR;
            /*
                The bare face: the solid run under the lip, in the column just inside it, down to
                the bottom of that run or to the floor past the edge, whichever is higher - a lower
                floor butting against the face hides the rest of it.
            */
            float fb = s.y, ft = s.y;
            if (ColumnAt(blocks,inside,s.y - EDGE_EPS * 2.0f,false,fb,ft)){
                float bottom = f_floor ? std::max(fb,floor_top) : fb;
                e.wall = std::max(0.0f,s.y - bottom);
            }
            if (block >= 0 && block < (int)blocks.size()){
                e.z_front = blocks[block].Front();
                e.z_back = blocks[block].Back();
                e.f_grabbable = (blocks[block].kind == BLOCK_LEDGE);
            }
            edges.push_back(e);
        }
    }
    edges_signature = BlocksSignature();
    edges_blocks = blocks.size();
    edges_alive = CountAliveBlocks();
    edges_generation++;
}

int Stage::CountAliveBlocks() const{
    int n = 0;
    for (const StageBlock& b : blocks){
        n += b.f_alive ? 1 : 0;
    }
    return n;
}

/*
    Everything about the blocks the edges depend on, folded into one number: their count and, for
    each, its box, its kind and whether it is there. FNV-1a over the fields - never the struct's
    bytes, which include padding. A few hundred blocks a tick is nothing.
*/
uint64_t Stage::BlocksSignature() const{
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](const void* p, size_t n){
        const unsigned char* c = (const unsigned char*)p;
        for (size_t i = 0; i < n; i++){
            h ^= c[i];
            h *= 1099511628211ULL;
        }
    };
    size_t count = blocks.size();
    mix(&count,sizeof(count));
    for (const StageBlock& b : blocks){
        mix(&b.x,sizeof(b.x));
        mix(&b.y,sizeof(b.y));
        mix(&b.hw,sizeof(b.hw));
        mix(&b.hh,sizeof(b.hh));
        mix(&b.kind,sizeof(b.kind));
        unsigned char alive = b.f_alive ? 1 : 0;
        mix(&alive,1);
    }
    return h;
}

bool Stage::RefreshEdges(){
    if (BlocksSignature() == edges_signature && edges_generation > 0){
        return false;
    }
    RebuildEdges();
    return true;
}

int Stage::NearestEdge(float x, float floor_y, float max_d, int side) const{
    int best = -1;
    float best_d = 0.0f;
    for (size_t i = 0; i < edges.size(); i++){
        const StageEdge& e = edges[i];
        if (fabsf(e.y - floor_y) > STAGE_EDGE_JOIN || (side != 0 && e.side != side)){
            continue;
        }
        float d = fabsf(e.x - x);
        if (d <= max_d && (best < 0 || d < best_d)){
            best = (int)i;
            best_d = d;
        }
    }
    return best;
}

int Stage::SpanAt(float x, float y, float tol) const{
    for (size_t i = 0; i < spans.size(); i++){
        const StageSpan& s = spans[i];
        if (x >= s.x0 && x <= s.x1 && fabsf(y - s.y) <= tol){
            return (int)i;
        }
    }
    return -1;
}
