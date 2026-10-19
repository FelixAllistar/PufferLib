// Visible-information analysis. No opponent holes or unrevealed board read.
static int32_t learning[18];
int32_t* web_learning(void) {
    memset(learning,0,sizeof(learning));
    if(!(game.live[0]&1u))return learning;
    int bn=game.street[0]?game.street[0]+2:0;
    uint8_t cards[7]={game.hole[0][0][0],game.hole[0][1][0]},pool[52];
    uint64_t known=(UINT64_C(1)<<cards[0])|(UINT64_C(1)<<cards[1]);
    uint64_t rng=UINT64_C(0x92b16d8a27e45931);
    for(int i=0;i<bn;i++){cards[i+2]=game.board[i][0];known|=UINT64_C(1)<<cards[i+2];}
    rng^=known; rng^=(uint64_t)game.live[0]<<48;
    int available=0;for(int c=0;c<52;c++)if(!(known&(UINT64_C(1)<<c)))pool[available++]=c;
    int opponents=__builtin_popcount(game.live[0])-1;
    double equity=0;
    for(int sample=0;sample<4096;sample++) {
        uint8_t deck[52];memcpy(deck,pool,available);
        int draws=5-bn+2*opponents;
        for(int i=0;i<draws;i++) {
            unsigned left=available-i,x,limit=UINT32_MAX-UINT32_MAX%left;
            do{x=poker_random(&rng);}while(x>=limit);
            int j=i+x%left;uint8_t swap=deck[i];deck[i]=deck[j];deck[j]=swap;
        }
        int cursor=0;
        for(int i=bn;i<5;i++)cards[i+2]=deck[cursor++];
        cards[0]=game.hole[0][0][0];cards[1]=game.hole[0][1][0];
        uint32_t hero=poker_rank(cards,7);int lost=0,tied=0;
        if(!bn)learning[5+(hero>>24)]++;
        for(int p=0;p<opponents;p++) {
            cards[0]=deck[cursor++];cards[1]=deck[cursor++];uint32_t rank=poker_rank(cards,7);
            lost|=rank>hero;tied+=rank==hero;
        }
        learning[1]+=!lost&&!tied;learning[2]+=!lost&&tied>0;
        if(!lost)equity+=1.0/(tied+1);
    }
    cards[0]=game.hole[0][0][0];cards[1]=game.hole[0][1][0];
    if(bn==3) {
        for(int a=0;a<available;a++)for(int b=a+1;b<available;b++) {
            cards[5]=pool[a];cards[6]=pool[b];learning[5+(poker_rank(cards,7)>>24)]++;learning[4]++;
        }
    } else if(bn==4) {
        for(int a=0;a<available;a++){cards[6]=pool[a];learning[5+(poker_rank(cards,7)>>24)]++;learning[4]++;}
    } else if(bn==5) {learning[5+(poker_rank(cards,7)>>24)]=1;learning[4]=1;}
    else learning[4]=4096;
    learning[0]=4096;learning[3]=(int32_t)(10000*equity/4096+.5);
    uint32_t call=game.high[0]-game.bet[0][0];if(call>game.stack[0][0])call=game.stack[0][0];
    uint32_t cap=game.total[0][0]+call,eligible=0;
    for(int p=0;p<POKER_PLAYERS;p++) {
        uint32_t contribution=game.total[p][0]+(p==0?call:0);
        eligible+=contribution<cap?contribution:cap;
    }
    learning[14]=call?(int32_t)(10000.0*call/eligible+.5):0;
    learning[15]=opponents;learning[16]=bn>0;learning[17]=bn;
    return learning;
}
