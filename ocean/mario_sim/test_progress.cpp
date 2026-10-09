#include "game.h"
#include <cstdio>
#ifdef __CUDACC__
#include <cuda_runtime.h>
#endif
#define CHECK(test) do {if(!(test))return __LINE__;} while(0)
SMB_HD void position(SmbLogic* s,int x) {s->ram[0x6d]=x/256;s->ram[0x86]=x%256;}
SMB_HD int progress_controls() {
    SmbLogic s={};SmbEpisode e={};SmbGameProgress p={};SmbGameConfig c;
    c.clear_reward=c.time_bonus=0;
    s.ram[0x770]=1;s.ram[0x772]=3;s.ram[0xe]=8;s.ram[0xb5]=1;s.ram[0x75a]=2;s.ram[0xe8]=0x90;
    p.mode=1;p.routine=8;p.lives=2;position(&s,40);
    CHECK(smb_game_track_progress(&s,&p,&c)==0);
    for(int x=41;x<168;x++){position(&s,x);CHECK(smb_game_after_frame(&s,&e,&p,&c)==0);}
    position(&s,168);CHECK(smb_game_after_frame(&s,&e,&p,&c)==.025f&&p.checkpoints==1&&p.progress_pixels==128);
    // Backtracking and retraversal cannot collect the first checkpoint again.
    for(int x=80;x<=168;x++){position(&s,x);CHECK(smb_game_after_frame(&s,&e,&p,&c)==0);}
    for(int x=169;x<296;x++){position(&s,x);CHECK(smb_game_after_frame(&s,&e,&p,&c)==0);}
    position(&s,296);CHECK(smb_game_after_frame(&s,&e,&p,&c)==.025f&&p.checkpoints==2);
    // A life loss keeps the entire visited-area frontier.
    s.ram[0xe]=11;CHECK(smb_game_after_frame(&s,&e,&p,&c)==0&&!e.status);
    s.ram[0xe]=8;s.ram[0x75a]=1;
    for(int x=40;x<=296;x++){position(&s,x);CHECK(smb_game_after_frame(&s,&e,&p,&c)==0);}
    for(int x=297;x<424;x++){position(&s,x);CHECK(smb_game_after_frame(&s,&e,&p,&c)==0);}
    position(&s,424);CHECK(smb_game_after_frame(&s,&e,&p,&c)==.025f&&p.deaths==1&&p.checkpoints==3);
    // A pipe trip establishes a new area baseline. Returning much farther right
    // in the original area pays no teleport bonus, but new walking still counts.
    s.ram[0xe]=3;position(&s,1000);CHECK(smb_game_after_frame(&s,&e,&p,&c)==0);
    s.ram[0xe]=8;s.ram[0xe7]=0x20;position(&s,10);CHECK(smb_game_after_frame(&s,&e,&p,&c)==0);
    for(int x=11;x<138;x++){position(&s,x);CHECK(smb_game_after_frame(&s,&e,&p,&c)==0);}
    position(&s,138);CHECK(smb_game_after_frame(&s,&e,&p,&c)==.025f&&p.checkpoints==4);
    s.ram[0xe7]=0;position(&s,2600);CHECK(smb_game_after_frame(&s,&e,&p,&c)==0);
    for(int x=2601;x<2728;x++){position(&s,x);CHECK(smb_game_after_frame(&s,&e,&p,&c)==0);}
    position(&s,2728);CHECK(smb_game_after_frame(&s,&e,&p,&c)==.025f&&p.checkpoints==5&&p.progress_pixels==640);
    // Same-area discontinuities, coordinate-wrap jackpots and automatic flag
    // walking must not produce extra checkpoints.
    position(&s,65535);CHECK(smb_game_after_frame(&s,&e,&p,&c)==0);
    s.ram[0xe]=4;position(&s,3000);CHECK(smb_game_after_frame(&s,&e,&p,&c)==0);
    CHECK(p.checkpoints==5);
    // Separate stage identity even when its area data pointer is shared.
    s.ram[0xe]=8;s.ram[0x75c]=1;position(&s,40);CHECK(smb_game_after_frame(&s,&e,&p,&c)==0);
    for(int x=41;x<168;x++){position(&s,x);CHECK(smb_game_after_frame(&s,&e,&p,&c)==0);}
    position(&s,168);CHECK(smb_game_after_frame(&s,&e,&p,&c)==.025f&&p.checkpoints==6);
    // Configurable spacing, fractional progress across area changes, and zero
    // bonus remain independent from completion/life handling.
    p={};e={};c.checkpoint_distance=64;c.checkpoint_reward=.1f;
    position(&s,40);CHECK(smb_game_track_progress(&s,&p,&c)==0);
    for(int x=41;x<=72;x++){position(&s,x);CHECK(smb_game_track_progress(&s,&p,&c)==0);}
    s.ram[0xe7]=0x40;position(&s,20);CHECK(smb_game_track_progress(&s,&p,&c)==0);
    for(int x=21;x<52;x++){position(&s,x);CHECK(smb_game_track_progress(&s,&p,&c)==0);}
    position(&s,52);CHECK(smb_game_after_frame(&s,&e,&p,&c)==.1f&&p.progress_pixels==64);
    c.checkpoint_reward=0;
    for(int x=53;x<=116;x++){position(&s,x);CHECK(smb_game_after_frame(&s,&e,&p,&c)==0);}
    CHECK(p.checkpoints==2);
    // Bounded storage must fail closed rather than evicting a collectible area.
    p={};p.frontier_count=256;
    for(int i=0;i<256;i++)p.frontiers[i].key=i;
    for(int x=0;x<300;x++){position(&s,x);CHECK(smb_game_track_progress(&s,&p,&c)==0);}
    CHECK(!p.progress_pixels&&!p.checkpoints);
    return 0;
}
#ifdef __CUDACC__
__global__ void progress_kernel(int* result) {*result=progress_controls();}
#endif
int main() {
    int result=progress_controls();
    if(result){fprintf(stderr,"CPU checkpoint reward failed at line %d\n",result);return 1;}
#ifdef __CUDACC__
    int* device=nullptr;cudaError_t status=cudaMalloc(&device,sizeof(int));
    if(status==cudaSuccess){progress_kernel<<<1,1>>>(device);status=cudaGetLastError();}
    if(status==cudaSuccess)status=cudaMemcpy(&result,device,sizeof(int),cudaMemcpyDeviceToHost);
    cudaFree(device);
    if(status!=cudaSuccess||result){fprintf(stderr,"CUDA checkpoint reward failed at line %d: %s\n",result,cudaGetErrorString(status));return 1;}
#endif
    puts("PASS checkpoint boundaries, backtracking, deaths, pipe revisits, teleport/flag guards, stage identity, spacing/zero bonus and frontier capacity");return 0;
}
