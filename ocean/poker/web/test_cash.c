#include <stdio.h>
#include "bridge.c"
static int card_id(char r,char s){
    static const char ranks[]="23456789TJQKA",suits[]="cdhs";
    return 4*(strchr(ranks,r)-ranks)+(strchr(suits,s)-suits);
}
static void position_cards(const char* hero,const char* board,int n){
    web_init(9);game.actor[0]=0;game.street[0]=n-2;
    game.hole[0][0][0]=card_id(hero[0],hero[1]);game.hole[0][1][0]=card_id(hero[2],hero[3]);
    for(int i=0;i<n;i++)game.board[i][0]=card_id(board[2*i],board[2*i+1]);
}
int main(void){
    // Cash stacks above the old uint16 boundary, including public event decoding.
    for(int p=0;p<POKER_PLAYERS;p++)assert(web_stack(p,100000));
    assert(web_open(100,4));assert(game.initial[0][0]==100000);
    int actor=game.actor[0];assert(web_act(70000));
    assert(game.bet[actor][0]==70000&&game.high[0]==70000);
    float obs[POKER_OBS];poker_observe(&game,0,game.actor[0],obs);
    int base=16+7*POKER_PLAYERS;assert(obs[base+3]>3.49f&&obs[base+3]<3.51f);
    uint32_t chips=game.pot[0];for(int p=0;p<POKER_PLAYERS;p++)chips+=game.stack[p][0];assert(chips==100000*POKER_PLAYERS);
    position_cards("2s5c","5h9cKhAd9h",5);
    int32_t* s=web_state();assert((s[21]>>24)==2);assert(((s[21]>>16)&15)==9);assert(((s[21]>>12)&15)==5);
    assert(__builtin_popcount(s[22])==5);assert(!(s[22]&1)&&(s[22]&2));
    position_cards("AhKh","QhJhTh",3);
    int32_t before[18];memcpy(before,web_learning(),sizeof(before));assert(before[1]==4096&&before[3]==10000&&before[4]==1081&&before[13]==1081);
    // Analytics must be identical under arbitrary private-hole/future changes.
    for(int p=1;p<POKER_PLAYERS;p++){game.hole[p][0][0]=0;game.hole[p][1][0]=1;}
    game.board[3][0]=2;game.board[4][0]=3;assert(!memcmp(before,web_learning(),sizeof(before)));
    position_cards("2c3d","AhKhQhJhTh",5);s=web_state();assert(s[23]&&s[22]==124);
    int32_t* a=web_learning();assert(a[1]==0&&a[2]==4096&&a[4]==1&&a[13]==1);
    position_cards("2c3d","AhKhQhJh",4);a=web_learning();assert(a[4]==46);
    int sum=0;for(int i=0;i<9;i++)sum+=a[5+i];assert(sum==46);
    position_cards("AhKh","QhJh2c3d",4);a=web_learning();
    assert(a[4]==46&&a[10]+a[13]==9); // Nine remaining hearts, including one royal flush.
    puts("PASS: deep cash stacks/events, exact hand/highlight, runouts and analysis privacy");
}
