#pragma once
#include <stdint.h>
// ABI 3: byte observations remain compact; categorical IDs are never ordinal
// neural features. Tables contain pinned simulator mechanics, not usage ranks.
#define PK_AUG(n) (((n)+8)&~7)
#define PK_EMBED 32
#define PK_MON_COUNT 20
#define PK_MF 152
#define PK_SF 176
#define PK_MOVE_IN (166+PK_MF)
#define PK_SPECIES_IN (150+PK_SF)
#define PK_DYN 96
#define PK_MON_IN (PK_SPECIES_IN+PK_EMBED+PK_DYN)
#define PK_GLOBAL 160
#define PK_FUSION (PK_MON_COUNT*PK_EMBED+PK_GLOBAL)
#define PK_QUERIES 5
#define PK_QUERY_OUT (PK_QUERIES*PK_EMBED+PK_DYN+4)
static const float pk_sem_move[166][PK_MF]={
#include "move_features.inc"
};
static const float pk_sem_species[150][PK_SF]={
#include "species_features.inc"
};
static const float pk_sem_chart[15][15]={
#include "type_chart.inc"
};
#ifdef __CUDACC__
#define PK_FN template<typename T> __host__ __device__ static inline
#define PK_INPUT T
#else
#define PK_FN static inline
#define PK_INPUT float
#endif
PK_FN int pk_read(const PK_INPUT* o,int i) { return (int)(float)o[i]; }
PK_FN int pk_entity_species(const PK_INPUT* o,int e) {
    int s=pk_read(o,e<12?16+32*e:e<18?464+e-12:400+32*(e-18));
    return s>=0 && s<150?s:0;
}
PK_FN int pk_entity_move(const PK_INPUT* o,int e,int m) {
    if(e>=12 && e<18) return 0;
    if(e>=18 && m) return 0;
    int v=pk_read(o,e<12?16+32*e+8+m:8+e-18);
    return v>0 && v<166?v:0;
}
PK_FN float pk_dynamic(const PK_INPUT* o,int e,int f) {
    int s=pk_entity_species(o,e);
    if(!s) return 0;
    if(e>=18) {
        if(f==63) return 1;
        int slot=pk_read(o,4+e-18)-1;
        return slot>=0 && slot<6?pk_dynamic(o,(e-18)*6+slot,f):0;
    }
    if(f==0) return 1;
    if(f==1) return e<6;
    if(f==2) return e>=6 && e<12;
    if(f==3) return e>=12;
    if(f==4) return e==0;
    if(f==5) return e<6 && pk_read(o,0)==1 && pk_read(o,12)==e;
    if(e>=12) return 0;
    int b=16+32*e,side=e/6, active=pk_read(o,4+side)==e%6+1 && pk_read(o,0)==2;
    if(f==6) return active;
    if(f==7) return pk_read(o,b+1)/255.0f;
    if(f==8) return pk_read(o,b+3)!=0;
    if(f==9) return pk_read(o,b+4)/100.0f;
    if(f>=10 && f<17) return pk_read(o,b+2)==f-10;
    if(f>=17 && f<21) return pk_read(o,b+12+f-17)/61.0f;
    if(f>=21 && f<26) return pk_read(o,b+16+f-21)/255.0f;
    int a=400+32*side;
    if(f>=26 && f<32) return active?(pk_read(o,a+3+f-26)-6)/6.0f:0;
    if(f>=32 && f<47) return active?(float)pk_read(o,a+9+f-32):0;
    if(f>=47 && f<51) return active?pk_read(o,a+24+f-47)/255.0f:0;
    if(f>=51 && f<55) return pk_entity_move(o,e,f-51)!=0;
    if(f==55) return e<6; // Exact PP and base stats are private; foe zero means unknown.
    if(f>=56 && f<62) return e%6==f-56;
    if(f==62) return pk_read(o,471)==s && pk_read(o,0)!=2;
    if(f>=64 && f<96) return pk_read(o,b+5+(f-64)/16)==(f-64)%16;
    return 0;
}
PK_FN float pk_global_feature(const PK_INPUT* o,int f) {
    int phase=pk_read(o,0);
    if(f<3) return phase==f;
    if(f==3) return pk_read(o,1)/6.0f;
    if(f==4) return (pk_read(o,2)+256*pk_read(o,3))/512.0f;
    if(f>=5 && f<19) return pk_read(o,4+(f-5)/7)==(f-5)%7;
    if(f>=19 && f<31) return pk_read(o,6+(f-19)/6)==(f-19)%6;
    if(f>=31 && f<45) return pk_read(o,10+(f-31)/7)==(f-31)%7;
    if(f>=45 && f<52) return pk_read(o,12)==f-45 && phase==1;
    if(f==52) return pk_read(o,13)/4.0f;
    if(f==53) return pk_read(o,14)/24.0f;
    if(f==54) return pk_read(o,15)!=0;
    if(f==55) return pk_read(o,470)/6.0f;
    if(f==56) return pk_read(o,476)!=0;
    // Public active type identities (including transformed types).
    if(f>=57 && f<121) {
        int k=(f-57)/16,c=(f-57)%16;
        int type=pk_read(o,400+(k/2)*32+1+k%2);
        type=type>=0 && type<15?type:15;
        return phase==2 && type==c;
    }
    return 0;
}

typedef struct { int n,query[5],candidate[5],scalar; float coefficient[5]; } PKActionParts;
PK_FN PKActionParts pk_action_parts(const PK_INPUT* o,int action) {
    PKActionParts p={0};p.scalar=-1;
    int phase=pk_read(o,0);
    if(phase==0 && action<149) {p.n=1;p.query[0]=0;p.candidate[0]=action+1;p.coefficient[0]=1;}
    else if(phase==1) {
        if(action<164) {p.n=1;p.query[0]=1;p.candidate[0]=152+action+1;p.coefficient[0]=1;}
        else if(action==164)p.scalar=0;
    } else if(phase==2) {
        if(action<4 || action==11) {
            int slot=pk_read(o,4)-1;
            int move=action==11?165:slot>=0 && slot<6?pk_entity_move(o,slot,action):0;
            if(move) {p.n=1;p.query[0]=2;p.candidate[0]=152+move;p.coefficient[0]=1;}
            if(action==11)p.scalar=2;
        } else if(action<10) {
            int e=action-4,s=pk_entity_species(o,e),count=0;
            if(s) {p.n=1;p.query[0]=3;p.candidate[0]=s;p.coefficient[0]=1;}
            for(int m=0;m<4;m++) count+=pk_entity_move(o,e,m)!=0;
            for(int m=0;m<4;m++) {
                int move=pk_entity_move(o,e,m);
                if(move) {int k=p.n++;p.query[k]=4;p.candidate[k]=152+move;p.coefficient[k]=1.0f/count;}
            }
        } else if(action==10)p.scalar=1;
    }
    return p;
}
PK_FN float pk_action_dynamic(const PK_INPUT* o,int action,int f,const float* moves,const float* chart) {
    if(pk_read(o,0)!=2)return 0;
    if(action>=4 && action<10)return pk_dynamic(o,action-4,f);
    if(action>=4 && action!=11)return 0;
    int slot=pk_read(o,4)-1;
    if(slot<0 || slot>=6)return 0;
    int move=action==11?165:pk_entity_move(o,slot,action);
    if(!move)return 0;
    if(f<4)return action==f;
    if(f==4)return action==11?1:pk_read(o,16+32*slot+12+action)/61.0f;
    if(f==5)return action==11;
    const float* m=moves+move*PK_MF;
    int t0=pk_read(o,401),t1=pk_read(o,402),foe0=pk_read(o,433),foe1=pk_read(o,434);
    if(f==6)return (t0<15 && m[69+t0]>0) || (t1<15 && m[69+t1]>0);
    if(f>=8 && f<23)return m[69+f-8]*(float)(foe0==f-8 || foe1==f-8);
    if(f>=23 && f<=27 && foe0<15 && foe1<15) {
        float effectiveness=1;
        for(int type=0;type<15;type++)if(m[69+type]) {
            effectiveness=chart[type*15+foe0];
            if(foe1!=foe0)effectiveness*=chart[type*15+foe1];
        }
        // Nominal type matchup; fixed damage and special exceptions remain
        // separately identified in the candidate's mechanics embedding.
        if(f==23)return effectiveness/4;
        if(f==24)return effectiveness==0;
        if(f==25)return effectiveness>1;
        if(f==26)return effectiveness>0 && effectiveness<1;
        return m[1]*effectiveness;
    }
    return 0;
}
#undef PK_FN
#undef PK_INPUT
