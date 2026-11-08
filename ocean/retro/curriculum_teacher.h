#pragma once
// Curriculum teacher (2026-11-07): a bandit over groups of reset states, shared by ocean/retro and
// ocean/mario_sim. Pure host-side arithmetic over plain structs (no emulator, env or CUDA types), so both
// environments produce the same numbers and tests/test_teacher.cpp checks them against hand-computed values.
//
// A group is a section of one loaded area (environment-defined key); every reset state inside it is a variant.
// Per group the environments feed in three kinds of counters, and the teacher turns them into three signals:
//   deaths    D_g = decayed mass of natural-play failures credited to the sections at and upstream of where they
//                   happened, times 4 p (1-p) with p = (wins+1)/(attempts+2): where practice starts are hopeless
//                   (p ~ 0) or mastered (p ~ 1) more practice does not remove the deaths.
//   progress  L_g = |fast - slow|, bias-corrected EMAs of practice outcomes (0 below lp_min_attempts). It is zero
//                   for impossible, mastered or coin-flip groups and large while a success rate moves either way.
//   staleness S_g = agent steps since the group was last selected (never selected counts from step 0).
// Each signal is ranked over the eligible groups (rank 1 = largest; ties share the better rank), turned into
// rank^-power, mixed with the three weights, normalised, and mixed with a uniform floor:
//   P_g = (1 - floor) * pr_g / sum(pr) + floor / n_eligible.
// Ranks rather than raw values make counts, probabilities and step counts comparable (Prioritized Level Replay
// does the same). The only tunables are the weights, the floor, the EMA rates and the death half-life.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <vector>

// Device-callable qualifier for the sampling step; mario_sim defines it as __host__ __device__ before including.
#ifndef TEACHER_HD
#define TEACHER_HD
#endif

struct TeacherParams {
    double lp_fast=0.05, lp_slow=0.005;  // EMA rates per recorded outcome
    int lp_min_attempts=5;               // progress signal is 0 until a group has this many recorded outcomes
    double death_half_life=20e6;         // agent steps for a credited failure to lose half its mass
    int upstream_groups=6;               // a failure at section s credits sections s-upstream_groups..s
    double weight_deaths=0.5, weight_lp=0.35, weight_stale=0.15;
    double rank_power=1;                 // signal priority = rank^-rank_power
    double floor=0.1;                    // uniform share of every sampling distribution
};
// Plain data: the environments serialise it as-is. `area` must identify the loaded area including its level or
// stage, so sections of different levels never share a kernel. `eligible` is 0 while a group has no reset states.
struct TeacherGroup {
    uint64_t key=0;
    uint32_t area=0;
    int32_t level=0, section=0, eligible=0, reserved=0;
    double fast=0, slow=0;              // raw EMAs from zero; teacher_rates() removes the start-up bias
    uint64_t attempts=0, wins=0;
    double deaths=0;                    // failure mass as of death_stamp (agent steps)
    uint64_t death_stamp=0, last_selected=0;
    double prob=0;                      // sampling probability from the last teacher_priorities() call
};

static inline void teacher_validate(const TeacherParams& p) {
    bool ok=std::isfinite(p.lp_fast)&&std::isfinite(p.lp_slow)&&p.lp_fast>0&&p.lp_fast<1&&p.lp_slow>0&&p.lp_slow<1
        &&p.lp_slow<=p.lp_fast&&p.lp_min_attempts>=1&&p.lp_min_attempts<=1000000
        &&std::isfinite(p.death_half_life)&&p.death_half_life>0&&p.upstream_groups>=0&&p.upstream_groups<=1024
        &&std::isfinite(p.weight_deaths)&&std::isfinite(p.weight_lp)&&std::isfinite(p.weight_stale)
        &&p.weight_deaths>=0&&p.weight_lp>=0&&p.weight_stale>=0&&p.weight_deaths+p.weight_lp+p.weight_stale>0
        &&std::isfinite(p.rank_power)&&p.rank_power>0&&p.rank_power<=16
        &&std::isfinite(p.floor)&&p.floor>=0&&p.floor<=1;
    if(!ok) throw std::runtime_error("curriculum teacher: invalid parameters");
}
// Record n outcomes with w wins as one update. n == 1 is the per-outcome case. n > 1 (counters read back from a
// device) applies the mean outcome with the n-step rate 1-(1-a)^n, which equals n sequential updates whenever the
// n outcomes are identical, and keeps the bias correction below exact in every case.
static inline void teacher_record(TeacherGroup& g,uint64_t n,uint64_t w,const TeacherParams& p) {
    if(n==0||w>n) throw std::runtime_error("curriculum teacher: invalid outcome count");
    double mean=(double)w/(double)n;
    double af=1-std::pow(1-p.lp_fast,(double)n), as=1-std::pow(1-p.lp_slow,(double)n);
    g.fast+=af*(mean-g.fast); g.slow+=as*(mean-g.slow);
    g.attempts+=n; g.wins+=w;
}
// Bias-corrected success rates: the raw EMA from zero divided by its total weight 1-(1-a)^attempts, so a constant
// outcome stream reports that constant from the first outcome on.
static inline void teacher_rates(const TeacherGroup& g,const TeacherParams& p,double* fast,double* slow) {
    if(!g.attempts){*fast=*slow=0;return;}
    *fast=g.fast/(1-std::pow(1-p.lp_fast,(double)g.attempts));
    *slow=g.slow/(1-std::pow(1-p.lp_slow,(double)g.attempts));
}
static inline double teacher_progress(const TeacherGroup& g,const TeacherParams& p) {
    if(g.attempts<(uint64_t)p.lp_min_attempts) return 0;
    double f,s;teacher_rates(g,p,&f,&s);return std::fabs(f-s);
}
// Failure mass decays with a half-life in agent steps; it is brought forward lazily.
static inline double teacher_decayed(const TeacherGroup& g,uint64_t now,const TeacherParams& p) {
    if(now<=g.death_stamp) return g.deaths;
    return g.deaths*std::pow(0.5,(double)(now-g.death_stamp)/p.death_half_life);
}
static inline void teacher_settle(TeacherGroup& g,uint64_t now,const TeacherParams& p) {
    g.deaths=teacher_decayed(g,now,p);if(now>g.death_stamp)g.death_stamp=now;
}
// Kernel weight of a failure d sections downstream of a group: (U+1-d)/(U+1) for 0 <= d <= U, else 0.
static inline double teacher_kernel(int d,const TeacherParams& p) {
    return d<0||d>p.upstream_groups?0:(double)(p.upstream_groups+1-d)/(double)(p.upstream_groups+1);
}
// Credit one natural-play failure at (area, section). Returns the number of groups credited.
static inline int teacher_credit_failure(std::vector<TeacherGroup>& groups,uint32_t area,int section,uint64_t now,
        const TeacherParams& p) {
    int credited=0;
    for(auto& g:groups) {
        double w=g.area==area?teacher_kernel(section-g.section,p):0;
        if(w<=0) continue;
        teacher_settle(g,now,p);g.deaths+=w;credited++;
    }
    return credited;
}
// Credit integer kernel weights accumulated elsewhere (a device adds U+1-d per failure): mass += sum/(U+1).
static inline void teacher_add_mass(TeacherGroup& g,uint64_t weight_sum,uint64_t now,const TeacherParams& p) {
    teacher_settle(g,now,p);g.deaths+=(double)weight_sum/(double)(p.upstream_groups+1);
}
static inline double teacher_death_signal(const TeacherGroup& g,uint64_t now,const TeacherParams& p) {
    double pl=((double)g.wins+1)/((double)g.attempts+2);
    return teacher_decayed(g,now,p)*4*pl*(1-pl);
}
// rank^-power with rank = 1 + number of values strictly greater (ties share the better rank).
static inline void teacher_rank_priorities(const std::vector<double>& values,double power,std::vector<double>* out) {
    std::vector<double> sorted=values;std::sort(sorted.begin(),sorted.end(),std::greater<double>());
    out->resize(values.size());
    for(size_t k=0;k<values.size();k++) {
        size_t greater=std::lower_bound(sorted.begin(),sorted.end(),values[k],std::greater<double>())-sorted.begin();
        (*out)[k]=std::pow((double)(greater+1),-power);
    }
}
// Fills g.prob for every group (0 when ineligible) and returns the number of eligible groups.
static inline int teacher_priorities(std::vector<TeacherGroup>& groups,uint64_t now,const TeacherParams& p) {
    std::vector<size_t> idx;
    for(size_t i=0;i<groups.size();i++){if(groups[i].eligible)idx.push_back(i);else groups[i].prob=0;}
    size_t n=idx.size();if(!n)return 0;
    std::vector<double> d(n),l(n),s(n),rd,rl,rs;
    for(size_t k=0;k<n;k++) {
        const auto& g=groups[idx[k]];
        d[k]=teacher_death_signal(g,now,p);l[k]=teacher_progress(g,p);
        s[k]=(double)(now-std::min(now,g.last_selected));
    }
    teacher_rank_priorities(d,p.rank_power,&rd);teacher_rank_priorities(l,p.rank_power,&rl);teacher_rank_priorities(s,p.rank_power,&rs);
    double total=0;std::vector<double> pr(n);
    for(size_t k=0;k<n;k++){pr[k]=p.weight_deaths*rd[k]+p.weight_lp*rl[k]+p.weight_stale*rs[k];total+=pr[k];}
    for(size_t k=0;k<n;k++)groups[idx[k]].prob=(1-p.floor)*pr[k]/total+p.floor/(double)n;
    return (int)n;
}
// Cumulative distribution in float for identical host and device sampling; the last entry is exactly 1.
static inline void teacher_cdf(const std::vector<TeacherGroup>& groups,std::vector<float>* cdf) {
    cdf->resize(groups.size());double run=0;
    for(size_t i=0;i<groups.size();i++){run+=groups[i].prob;(*cdf)[i]=(float)run;}
    if(!groups.empty())cdf->back()=1.0f;
}
// First index whose cumulative value exceeds u (u in [0,1)); a zero-probability group has a flat step and is
// never returned. Usable verbatim in device code.
TEACHER_HD static inline int teacher_sample(const float* cdf,int n,float u) {
    int lo=0,hi=n-1;
    while(lo<hi){int mid=(lo+hi)/2;if(cdf[mid]>u)hi=mid;else lo=mid+1;}
    return lo;
}
static inline double teacher_entropy(const std::vector<TeacherGroup>& groups) {
    double h=0;for(const auto& g:groups)if(g.prob>0)h-=g.prob*std::log(g.prob);return h;
}
