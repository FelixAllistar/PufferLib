#ifndef WEBNAV_BOARD_PUBLIC_CONTROLLER_H
#define WEBNAV_BOARD_PUBLIC_CONTROLLER_H
#include "../common/family_api.h"
#include <string.h>

/* Memoized public-board search. A perfect reply sets the primary score;
   uniformly random replies break ties in favor of paths with more wins.
   The model's hidden RNG and row fields are never read. */
typedef struct {int worst;double win_chance;} BoardPublicScore;
static BoardPublicScore board_public_cache[2][19683];
static unsigned char board_public_seen[2][19683];

static int board_public_win(unsigned mask){
    static const unsigned lines[]={7,56,448,73,146,292,273,84};
    for(unsigned i=0;i<8;i++)if((mask&lines[i])==lines[i])return 1;
    return 0;
}
static unsigned board_public_key(unsigned x,unsigned o){
    unsigned key=0;
    for(unsigned i=0;i<9;i++)key=key*3+((x&(1u<<i))?1u:(o&(1u<<i))?2u:0u);
    return key;
}
static BoardPublicScore board_public_score(unsigned x,unsigned o,int xturn){
    if(board_public_win(x))return (BoardPublicScore){1,1.0};
    if(board_public_win(o))return (BoardPublicScore){-1,0.0};
    if((x|o)==511u)return (BoardPublicScore){0,0.0};
    unsigned key=board_public_key(x,o),side=xturn?1u:0u;
    if(board_public_seen[side][key])return board_public_cache[side][key];
    BoardPublicScore result={xturn?-2:2,0.0};unsigned moves=0;
    for(unsigned i=0;i<9;i++){
        unsigned bit=1u<<i;if((x|o)&bit)continue;
        BoardPublicScore child=xturn?
            board_public_score(x|bit,o,0):board_public_score(x,o|bit,1);
        if(xturn){
            if(child.worst>result.worst||
               (child.worst==result.worst&&child.win_chance>result.win_chance))
                result=child;
        }else{
            if(child.worst<result.worst)result.worst=child.worst;
            result.win_chance+=child.win_chance;moves++;
        }
    }
    if(!xturn)result.win_chance/=moves;
    board_public_seen[side][key]=1;board_public_cache[side][key]=result;
    return result;
}
static int board_public_action(const WFView *v,WFAction *a){
    if(!v||!a)return -1;
    unsigned x=0,o=0,seen=0;
    for(unsigned i=0;i<v->count;i++){
        const WFNode *n=v->nodes+i;
        if(n->role!=WF_CELL||n->ref<1||n->ref>9)continue;
        unsigned bit=1u<<(n->ref-1);if(seen&bit)return -1;seen|=bit;
        const char *value=wf_text_get(v,n->value);if(!value)return -1;
        if(!strcmp(value,"X"))x|=bit;
        else if(!strcmp(value,"O"))o|=bit;
        else if(strcmp(value,""))return -1;
    }
    if(seen!=511u||(x&o)||board_public_win(x)||board_public_win(o))return -1;
    BoardPublicScore best={-2,-1.0};int chosen=-1;
    static const unsigned preference[]={4,0,2,6,8,1,3,5,7};
    for(unsigned k=0;k<9;k++){
        unsigned i=preference[k],bit=1u<<i;if((x|o)&bit)continue;
        BoardPublicScore candidate=board_public_score(x|bit,o,0);
        if(candidate.worst>best.worst||
           (candidate.worst==best.worst&&candidate.win_chance>best.win_chance+1e-12)){
            best=candidate;chosen=(int)i;
        }
    }
    if(chosen<0)return -1;
    a->kind=WF_CLICK;a->target=(unsigned)chosen+1;
    a->text=NULL;a->text_length=0;return 0;
}
#endif
