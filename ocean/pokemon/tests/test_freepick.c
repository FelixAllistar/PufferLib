#define _POSIX_C_SOURCE 200809L
#include "../pokemon.h"
#include "../semantic_contract.h"
#include <sys/wait.h>
static void reject_lead(const char* team,const char* lead) {
    pid_t pid=fork();assert(pid>=0);
    if(!pid) {PKGame g={0};pk_parse_fixed_team(&g,0,team);pk_parse_lead(&g,0,lead);_exit(0);}
    int status;assert(waitpid(pid,&status,0)==pid);assert(WIFEXITED(status) && WEXITSTATUS(status)==1);
}
static void moves(void) {
    assert(PK_MOVE_FEATURES==PK_MF && PK_SPECIES_FEATURES==PK_SF && sizeof(PKMon)==5);
    for(int m=1;m<=165;m++) {
        uint16_t engine[6];pk_move_engine_info(m,engine);
        assert(fabs(pk_sem_move[m][1]*250-engine[0])<1e-4);
        assert(fabs(pk_sem_move[m][2]*256-engine[1])<1e-4);
        assert(engine[2]==pk_move_types[m] && engine[4]==pk_move_pp[m]);
        assert(pk_sem_move[m][69+engine[2]]==1);
        assert(pk_sem_move[m][84+engine[3]]==1);
        assert(pk_sem_move[m][5]+pk_sem_move[m][6]+pk_sem_move[m][7]==1);
    }
    assert(pk_sem_move[156][10]==1 && pk_sem_move[156][14]==1); // Rest
    assert(pk_sem_move[153][13]==1 && pk_sem_move[153][22]==1); // Explosion
    assert(pk_sem_move[163][23]==1); // Slash's high-critical effect
    assert(pk_sem_move[63][20]==1 && pk_sem_move[63][21]==1); // Hyper Beam's Gen1 recharge rule
    assert(pk_sem_chart[7][12]==0 && pk_sem_chart[6][3]==2); // Gen1 Ghost/Psychic and Bug/Poison
    const int bans[]={12,19,32,90,91,104,107,165};
    for(int s=1;s<=149;s++) {
        int legal=0;
        for(int m=1;m<=164;m++)legal+=pk_move_learnable(s,m);
        assert(legal>0);
        for(size_t i=0;i<sizeof(bans)/sizeof(*bans);i++)assert(!pk_move_learnable(s,bans[i]));
    }
    for(int i=0;i<PK_FORBIDDEN_COUNT;i++) {
        const PKForbiddenSet* bad=&pk_forbidden_sets[i];assert(bad->count==2);
        for(int order=0;order<2;order++) {
            PKMon mon={0};mon.species=bad->species;mon.moves[0]=bad->moves[order];
            assert(pk_mon_legal(&mon));assert(!pk_move_allowed(&mon,bad->moves[1-order]));
            mon.moves[1]=bad->moves[1-order];assert(!pk_mon_legal(&mon));
            PKGame g={0};g.rng=1;g.max_updates=8;pk_game_reset(&g);
            g.teams[0][0]=mon;
            for(int k=1;k<6;k++)if(g.teams[0][k].species==mon.species)g.teams[0][k].species=150;
            PKBattle before=g.battle;
            assert(pk_start_free(&g.battle,1,&g.teams[0][0])==4);
            assert(!memcmp(&before,&g.battle,sizeof(before)));
        }
    }
    PKMon ditto={132,{144,0,0,0}};assert(pk_mon_legal(&ditto));
    assert(!pk_move_allowed(&ditto,144));ditto.moves[2]=144;assert(!pk_mon_legal(&ditto));
}
static void compositions(void) {
    uint64_t rng=971;
    for(int count=1;count<=6;count++)for(int mode=0;mode<2;mode++)for(int trial=0;trial<30;trial++) {
        const int ids[]={124,121,112,113,143,128};char team[100]="species:";
        for(int i=0;i<count;i++) {char id[16];snprintf(id,sizeof(id),"%s%d",i?",":"",ids[i]);strcat(team,id);}
        PKGame g={0};g.rng=trial+1;g.max_updates=512;g.draft=mode;
        pk_parse_fixed_team(&g,0,team);pk_parse_lead(&g,0,"Jynx");pk_game_reset(&g);
        int decisions=0,free_choices=0;
        while(g.phase!=PK_PHASE_BATTLE) {
            int a=pk_random_action(g.masks[0],&rng);
            if(g.phase==PK_PHASE_SPECIES && g.picks>0)for(int s=1;s<=149;s++)
                if(g.masks[0][s-1] && !pk_required_species(&g,0,s)) {a=s-1;break;}
            if(g.phase==PK_PHASE_MOVES) {
                int legal=0;for(int m=0;m<164;m++)legal+=g.masks[0][m];
                free_choices+=legal>1;
            }
            assert(!pk_game_step(&g,a,pk_random_action(g.masks[1],&rng)));decisions++;
        }
        assert(decisions==(mode?30:0));assert(g.teams[0][0].species==124);
        if(mode)assert(free_choices>0);
        for(int i=0;i<6;i++)assert(pk_mon_legal(&g.teams[0][i]));
        for(int i=0;i<count;i++) {
            int found=0;for(int j=0;j<6;j++)found+=g.teams[0][j].species==ids[i];assert(found==1);
        }
        assert(!g.invalid_actions);
    }
    reject_lead("species:1,2,3,4,5,6","Jynx");reject_lead("None","Mewtwo");
}
int main(void) {
    moves();compositions();
    puts("Free-pick: mechanics match engine, bans, source conflicts, native rejection, 1..4 moves, species/lead constraints and 30-step private drafting PASS");
}
