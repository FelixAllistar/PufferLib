// Exact checks of the shared curriculum teacher arithmetic against values computed independently in Python
// (reports/retro/teacher_20261107/expected.py). No emulator, ROM or environment is involved.
#include "../curriculum_teacher.h"
#include <cstdio>
#include <cstring>
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static bool near(double a,double b,double tol=1e-12){return std::fabs(a-b)<=tol;}
int main(){try{
    TeacherParams p;teacher_validate(p);
    // 1. Per-outcome EMAs: five wins then five losses.
    TeacherGroup g;for(int i=0;i<5;i++)teacher_record(g,1,1,p);
    check(near(g.fast,0.22621906250000001)&&near(g.slow,0.024751246878125001),"raw EMA after five wins");
    double f,s;teacher_rates(g,p,&f,&s);check(near(f,1)&&near(s,1),"bias correction of a constant stream");
    check(teacher_progress(g,p)==0||near(teacher_progress(g,p),0),"constant stream has progress");
    for(int i=0;i<5;i++)teacher_record(g,1,0,p);
    check(near(g.fast,0.17504399826162112)&&near(g.slow,0.024138622656103108),"raw EMA after five losses");
    teacher_rates(g,p,&f,&s);
    check(near(f,0.43623252519027572)&&near(s,0.493734650676527),"corrected rates after a change");
    check(near(teacher_progress(g,p),0.057502125486251277),"progress after a change");
    check(g.attempts==10&&g.wins==5,"attempt counters");
    // Progress is zero below the minimum attempts.
    {TeacherGroup h;for(int i=0;i<4;i++)teacher_record(h,1,i%2,p);check(teacher_progress(h,p)==0,"progress before min attempts");}
    // 2. Batched outcomes equal sequential ones when identical; the correction stays exact across mixed batches.
    {TeacherGroup a,b;teacher_record(a,7,7,p);for(int i=0;i<7;i++)teacher_record(b,1,1,p);
     check(near(a.fast,0.30166270390625027)&&near(a.fast,b.fast,1e-12)&&near(a.slow,b.slow,1e-12),"batched EMA");
     TeacherGroup c;teacher_record(c,3,0,p);teacher_record(c,2,2,p);teacher_record(c,1,1,p);
     double cf,cs;teacher_rates(c,p,&cf,&cs);check(cf>0&&cf<1&&cs>0&&cs<1&&c.attempts==6&&c.wins==3,"mixed batches");
     // Five losses then five wins: the mirror image of case 1.
     TeacherGroup m;teacher_record(m,5,0,p);teacher_record(m,5,5,p);teacher_rates(m,p,&cf,&cs);
     check(near(cf,0.56376747480972378)&&near(cs,0.50626534932347245)&&near(teacher_progress(m,p),0.057502125486251332),"mirror case");}
    // 3. Death mass halves every half-life and settles lazily.
    {TeacherGroup d;d.deaths=1;d.death_stamp=0;
     check(near(teacher_decayed(d,(uint64_t)p.death_half_life,p),0.5)&&near(teacher_decayed(d,(uint64_t)(2*p.death_half_life),p),0.25),"half-life");
     check(teacher_decayed(d,0,p)==1,"no decay without time");
     teacher_settle(d,(uint64_t)p.death_half_life,p);check(near(d.deaths,0.5)&&d.death_stamp==(uint64_t)p.death_half_life,"settle");
     teacher_add_mass(d,14,(uint64_t)p.death_half_life,p);check(near(d.deaths,2.5),"integer kernel weights");}
    // 4. Upstream kernel: a failure at section 10 credits sections 4..10 of the same area with (7-d)/7.
    {std::vector<TeacherGroup> gs;
     for(int sec=2;sec<=12;sec++){TeacherGroup q;q.area=7;q.section=sec;q.eligible=1;gs.push_back(q);}
     {TeacherGroup other;other.area=8;other.section=10;other.eligible=1;gs.push_back(other);}
     check(teacher_credit_failure(gs,7,10,1000,p)==7,"kernel width");
     for(auto& q:gs){int d=10-q.section;double want=q.area==7&&d>=0&&d<=6?(7.0-d)/7.0:0;
         check(near(q.deaths,want)&&(want==0||q.death_stamp==1000),"kernel weight");}
     check(near(teacher_kernel(0,p),1)&&near(teacher_kernel(6,p),1.0/7)&&teacher_kernel(7,p)==0&&teacher_kernel(-1,p)==0,"kernel edges");}
    // 5. Hopeless and mastered groups shrink their death signal by 4p(1-p); a fresh group keeps it whole.
    {TeacherGroup h;h.deaths=3;h.attempts=30;h.wins=0;check(near(teacher_death_signal(h,0,p),3*0.12109375),"hopeless factor");
     h.wins=30;check(near(teacher_death_signal(h,0,p),3*0.12109375),"mastered factor");
     TeacherGroup fresh;fresh.deaths=3;check(near(teacher_death_signal(fresh,0,p),3),"fresh factor");}
    // 6. Ranks: ties share the better rank; a constant signal contributes uniformly.
    {std::vector<double> v={5,1,5,0},r;teacher_rank_priorities(v,1,&r);
     check(r[0]==1&&r[2]==1&&near(r[1],1.0/3)&&near(r[3],0.25),"tie ranking");
     teacher_rank_priorities(v,2,&r);check(near(r[1],1.0/9)&&near(r[3],1.0/16),"rank power");
     std::vector<double> flat={0,0,0};teacher_rank_priorities(flat,1,&r);check(r[0]==1&&r[1]==1&&r[2]==1,"flat ranking");}
    // 7. Hand-computed mixture: D=[2,1,0], L=[0,.3,.1], S=[100,50,50] -> P=[.40463..,.36024..,.23513..].
    {std::vector<TeacherGroup> gs(4);uint64_t now=100;
     for(int k=0;k<3;k++){gs[k].eligible=1;gs[k].deaths=k==0?2:k==1?1:0;gs[k].death_stamp=now;gs[k].last_selected=k==0?0:50;}
     // Progress via attempts >= 5 with chosen raw EMAs: corrected fast = raw/(1-.95^5), slow = raw/(1-.995^5).
     gs[1].attempts=5;gs[1].fast=0.3*(1-std::pow(0.95,5));gs[1].slow=0;        // L = 0.3
     gs[2].attempts=5;gs[2].fast=0.1*(1-std::pow(0.95,5));gs[2].slow=0;        // L = 0.1
     gs[0].attempts=5;                                                          // L = 0
     // Keep the death factor at 1 for all three: wins = attempts/2 exactly gives p = (w+1)/(a+2) = 0.5.
     for(int k=0;k<3;k++){gs[k].wins=2;gs[k].attempts=5;}  // p = 3/7, factor 4*(3/7)*(4/7) = 48/49 for all: same scale
     gs[3].eligible=0;gs[3].deaths=99;
     check(teacher_priorities(gs,now,p)==3,"eligible count");
     check(gs[3].prob==0,"ineligible group has probability");
     check(near(gs[0].prob,0.40463378176382664,1e-12)&&near(gs[1].prob,0.36023916292974584,1e-12)&&near(gs[2].prob,0.2351270553064275,1e-12),"mixture");
     check(near(gs[0].prob+gs[1].prob+gs[2].prob,1,1e-12),"probabilities sum to one");
     std::vector<float> cdf;teacher_cdf(gs,&cdf);check(cdf.size()==4&&cdf[3]==1.0f&&cdf[2]==cdf[3],"cdf shape");
     check(teacher_sample(cdf.data(),4,0.0f)==0&&teacher_sample(cdf.data(),4,cdf[0]-1e-4f)==0&&teacher_sample(cdf.data(),4,cdf[0])==1
         &&teacher_sample(cdf.data(),4,cdf[1])==2&&teacher_sample(cdf.data(),4,0.999999f)==2,"cdf sampling boundaries");
     for(int i=0;i<100000;i++)check(teacher_sample(cdf.data(),4,(float)(i/100000.0))!=3,"zero-probability group sampled");
     // Empirical frequencies of a fixed LCG agree with P within one percent.
     uint32_t rng=12345;int counts[4]={};
     for(int i=0;i<400000;i++){rng=rng*1664525u+1013904223u;counts[teacher_sample(cdf.data(),4,(float)((rng>>8)*(1.0/16777216.0)))]++;}
     for(int k=0;k<3;k++)check(std::fabs(counts[k]/400000.0-gs[k].prob)<0.01,"empirical sampling");
     check(teacher_entropy(gs)>1.0&&teacher_entropy(gs)<std::log(3.0)+1e-9,"entropy range");}
    // 8. Floor only: with zero weights the distribution is uniform over eligible groups.
    {TeacherParams q=p;q.weight_deaths=1e-300;q.weight_lp=0;q.weight_stale=0;q.floor=1;std::vector<TeacherGroup> gs(5);
     for(auto& x:gs)x.eligible=1;teacher_priorities(gs,10,q);for(auto& x:gs)check(near(x.prob,0.2),"uniform floor");}
    // 9. Validation rejects impossible parameters.
    {int rejected=0;TeacherParams bad[6]={p,p,p,p,p,p};bad[0].lp_fast=0;bad[1].lp_slow=0.5;bad[2].death_half_life=0;
     bad[3].weight_deaths=bad[3].weight_lp=bad[3].weight_stale=0;bad[4].floor=1.5;bad[5].rank_power=0;
     for(auto& b:bad){try{teacher_validate(b);}catch(const std::exception&){rejected++;}}check(rejected==6,"validation");
     try{TeacherGroup x;teacher_record(x,0,0,p);}catch(const std::exception&){rejected++;}
     try{TeacherGroup x;teacher_record(x,2,3,p);}catch(const std::exception&){rejected++;}check(rejected==8,"outcome validation");}
    // 10. Gate signal (2026-11-11): three identical fresh groups, weights .4/.2/.1/.3, gate [.9, 0, .5] -> ranks 1, 3, 2,
    // pr = [1.0, 0.8, 0.85], total 2.65. Without a gate vector the mixture is uniform.
    {TeacherParams q=p;q.weight_deaths=0.4;q.weight_lp=0.2;q.weight_stale=0.1;q.weight_gate=0.3;q.rank_power=1;q.floor=0.1;
     teacher_validate(q);std::vector<TeacherGroup> gs(3);for(auto& x:gs)x.eligible=1;
     std::vector<double> gate={0.9,0.0,0.5};
     check(teacher_priorities(gs,100,q,&gate)==3,"gate eligible count");
     check(near(gs[0].prob,0.37295597484276727)&&near(gs[1].prob,0.30503144654088050)&&near(gs[2].prob,0.32201257861635220),"gate mixture");
     teacher_priorities(gs,100,q,nullptr);for(auto& x:gs)check(near(x.prob,1.0/3),"no gate vector is not uniform");
     std::vector<double> short_gate={0.9};bool threw=false;
     try{teacher_priorities(gs,100,q,&short_gate);}catch(const std::exception&){threw=true;}check(threw,"gate size not checked");
     TeacherParams r=q;r.weight_gate=-1;threw=false;try{teacher_validate(r);}catch(const std::exception&){threw=true;}check(threw,"negative gate weight");
     TeacherGroup a,b;a.attempts=98;a.wins=0;b.attempts=98;b.wins=49;
     check(near(teacher_gate(a,&b,q),0.495)&&near(teacher_gate(a,nullptr,q),0.99),"teacher_gate");
     TeacherGroup c;c.attempts=3;check(teacher_gate(c,nullptr,q)==0&&teacher_gate(a,&c,q)==0,"teacher_gate below min attempts");}
    puts("PASS curriculum teacher: EMAs, bias correction, batched outcomes, half-life, kernel, hopeless factor, ranks, mixture, sampling, validation, gate");
    return 0;
}catch(const std::exception& e){fprintf(stderr,"teacher test: %s\n",e.what());return 1;}}
