#include "bridge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

static void advance(uint32_t *w,unsigned command,unsigned target){
    for(unsigned lane=0;lane<32;lane++){w[lane*256+7]=command;w[lane*256+8]=target;}
    webnav_batch(w);
}
static void string_at(const uint32_t *r,unsigned at,unsigned limit,char *out){
    unsigned i=0;for(;i<limit&&r[at+i];i++){assert(r[at+i]<128);out[i]=(char)r[at+i];}
    assert(i<limit);out[i]=0;
}
int main(void){
    uint32_t w[8192],again[8192];unsigned coverage[2][7]={0},empty=0,full=0;
    for(unsigned kind=1;kind<=3;kind+=2)for(unsigned batch=0;batch<128;batch++){
        memset(w,0xa5,sizeof w);memset(again,0x5a,sizeof again);
        for(unsigned lane=0;lane<32;lane++){
            const unsigned base=lane*256,seed=batch*32+lane;
            w[base]=again[base]=kind;w[base+7]=again[base+7]=3;w[base+13]=again[base+13]=seed;
        }
        webnav_batch(w);webnav_batch(again);assert(!memcmp(w,again,sizeof w));
        for(unsigned lane=0;lane<32;lane++){
            const uint32_t *r=w+lane*256;unsigned n=r[1]-1,goals=0;char want[128]="Select ",actual[128],name[8];
            assert(n>=2&&n<=6&&r[15]==1&&r[2]==0&&r[3]==0&&r[9]==0&&r[10]==0);
            coverage[kind==3][n]++;
            for(unsigned i=0;i<n;i++){
                assert(r[16+3*i]==(kind==3?5u:2u)&&r[17+3*i]==0&&r[18+3*i]<=1);
                string_at(r,64+8*i,8,name);assert(strlen(name)>=2&&strlen(name)<=7);
                for(unsigned j=0;name[j];j++)assert(isalnum((unsigned char)name[j]));
                if(r[18+3*i]){if(goals)strcat(want,", ");strcat(want,name);goals++;}
            }
            assert(r[16+3*n]==1&&r[17+3*n]==0&&r[18+3*n]==0);
            string_at(r,64+8*n,8,name);assert(!strcmp(name,"Submit"));
            if(kind==3)assert(goals==1);else{empty+=goals==0;full+=goals==n;}
            if(!goals)strcat(want,"nothing");strcat(want," and click Submit.");
            string_at(r,128,128,actual);assert(!strcmp(want,actual));
        }
        /* Solve from the public instruction. Random duplicate labels can make
           upstream radio semantics ambiguous; these sampled seeds are checked
           against private outcome only after the public action is selected. */
        for(unsigned turn=0;turn<8;turn++){
            for(unsigned lane=0;lane<32;lane++){
                uint32_t *r=w+lane*256;if(r[2]){r[7]=0;continue;}
                char instruction[128],label[8];string_at(r,128,128,instruction);
                unsigned n=r[1]-1,target=n;
                for(unsigned i=0;i<n;i++){
                    string_at(r,64+8*i,8,label);
                    /* Exact delimited names, not substring matching. */
                    int desired=0;const char *cursor=instruction+7,*end=strstr(cursor," and click Submit.");
                    while(cursor<end){const char *sep=strstr(cursor,", ");if(!sep||sep>end)sep=end;
                        if((size_t)(sep-cursor)==strlen(label)&&!strncmp(cursor,label,(size_t)(sep-cursor)))desired=1;
                        cursor=sep+2;
                    }
                    if(desired&&!r[17+3*i]){target=i;break;}
                }
                r[7]=1;r[8]=target;r[9]+=137;
            }
            webnav_batch(w);
        }
        for(unsigned lane=0;lane<32;lane++){const uint32_t *r=w+lane*256;if(!(r[2]==1&&r[4]==r[5])){char q[128];string_at(r,128,128,q);fprintf(stderr,"Failed kind=%u seed=%u done=%u score=%u/%u query=%s\n",kind,batch*32+lane,r[2],r[4],r[5],q);for(unsigned j=0;j<r[1];j++){char label[8];string_at(r,64+8*j,8,label);fprintf(stderr,"%s selected=%u target=%u\n",label,r[17+3*j],r[18+3*j]);}assert(0);}}
    }
    for(unsigned family=0;family<2;family++)for(unsigned n=2;n<=6;n++)assert(coverage[family][n]);
    assert(empty&&full);
    /* Exhaust every initial radio bitmask (including malformed multi-selected
       input), target and click for 2..6 controls. One click repairs exclusivity. */
    unsigned cases=0;
    for(unsigned n=2;n<=6;n++)for(unsigned bits=0;bits<(1u<<n);bits++)for(unsigned goal=0;goal<n;goal++)for(unsigned click=0;click<n;click++){
        memset(w,0,sizeof w);
        for(unsigned lane=0;lane<32;lane++){
            uint32_t *r=w+lane*256;r[0]=3;r[1]=n+1;
            for(unsigned i=0;i<n;i++){r[16+3*i]=5;r[17+3*i]=(bits>>i)&1;r[18+3*i]=i==goal;}
            r[16+3*n]=1;
        }
        advance(w,1,click);advance(w,1,click);
        for(unsigned i=0;i<n;i++)assert(w[17+3*i]==(i==click)&&w[18+3*i]==(i==goal));
        advance(w,1,n);assert(w[2]==1&&w[4]==(click==goal)&&w[5]==1);cases++;
    }
    printf("PASS: 8192 Bend-generated forms, deterministic reset independent of previous row, label/instruction contracts, public-label solving; %u exhaustive radio transitions\n",cases);
}
