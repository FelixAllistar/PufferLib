#pragma once
// Bounded diagnostic search. This is neither a perfect TAS nor a trained policy.
#include "fpg_core.h"
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <cmath>

struct FpgSearchNode { FpgBody body; int parent, action; };
static inline uint64_t fpg_search_key(const FpgBody& b) {
    // Preserve all byte-sized hidden physics state, not just visible pixels.
    uint64_t h=1469598103934665603ull;
    const unsigned char* p=(const unsigned char*)&b;
    for(size_t i=0;i<sizeof(b);i++) {h^=p[i];h*=1099511628211ull;}
    return h;
}
static inline std::vector<int> fpg_search(const FpgBody& start,const FpgWorld& world,int width,int depth) {
    std::vector<FpgSearchNode> nodes={{start,-1,-1}};
    std::vector<int> frontier={0};
    std::unordered_set<uint64_t> visited;visited.insert(fpg_search_key(start));
    for(int t=0;t<depth;t++) {
        std::vector<FpgSearchNode> next;
        std::unordered_set<uint64_t> layer_seen;
        for(int parent:frontier) for(int a=0;a<12;a++) {
            FpgBody b=nodes[parent].body;fpg_physics(&b,&world,fpg_buttons(a));
            if(b.routine==5 && b.grab_y>=162 && b.flag_y==start.flag_y) {
                std::vector<int> path={a};
                for(int at=parent;nodes[at].parent>=0;at=nodes[at].parent) path.push_back(nodes[at].action);
                std::reverse(path.begin(),path.end());return path;
            }
            if(b.routine!=8||b.y>=208||b.x<start.x-32||b.x>world.pole_col*16+10) continue;
            uint64_t key=fpg_search_key(b);
            if(!visited.count(key)&&layer_seen.insert(key).second) next.push_back({b,parent,a});
        }
        // Progress plus a mild bias toward arriving low. Keep a broad beam of
        // exact subpixel states so a near miss does not erase timing diversity.
        auto quality=[&](const FpgSearchNode& n) {
            float dx=(float)(world.pole_col*16+6-n.body.x);
            return dx + (dx<80 ? fabsf((float)n.body.y-164)*0.3f : 0.0f);
        };
        std::sort(next.begin(),next.end(),[&](const auto&a,const auto&b){return quality(a)<quality(b);});
        // Retain different jump arcs and braking states. A pure progress beam
        // fills with subpixel variants stuck against the same stair face.
        std::unordered_map<int,int> occupancy;
        frontier.clear();
        for(auto& n:next) {
            int cell=((n.body.y+128)/4)*4096+((n.body.vx+64)/4)*64+((n.body.vy+16)/2)*4+n.body.motion;
            if(occupancy[cell]++>=12) continue;
            visited.insert(fpg_search_key(n.body));frontier.push_back((int)nodes.size());nodes.push_back(n);
            if((int)frontier.size()>=width)break;
        }
        if(frontier.empty()) break;
    }
    return {};
}
