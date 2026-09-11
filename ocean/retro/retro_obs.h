#pragma once
// Shared SMB1 observation + reward builder. Single source of truth for every
// backend (QuickNES ROM path, native-C fast path) and the future libretro
// deployment adapter: everything except the 144-pixel window comes straight
// from NES RAM (canonical addresses, smbdis.asm / Data Crystal), so the same
// code runs on RETRO_MEMORY_SYSTEM_RAM.
//
// Layout: OBS 256 = 64 ego/physics + 48 entities + 12*12 pixel patch.
// The pixel patch is backend-rendered RGB luma. Sharing the RAM builder does
// not imply the experimental native port's rendering or timing matches ROM.

#include <stdint.h>

#define RETRO_EGO_SIZE 64
#define RETRO_ENT_PER 8
#define RETRO_NUM_ENEMIES 5
#define RETRO_ENT_EXTRA 8
#define RETRO_ENT_SIZE (RETRO_NUM_ENEMIES*RETRO_ENT_PER + RETRO_ENT_EXTRA)
#define RETRO_WINDOW_W 12
#define RETRO_WINDOW_H 12
#define RETRO_TILES (RETRO_WINDOW_W * RETRO_WINDOW_H)
#define OBS_SIZE (RETRO_EGO_SIZE + RETRO_ENT_SIZE + RETRO_TILES)
#define RETRO_WINDOW_RADIUS_W (RETRO_WINDOW_W/2)
#define RETRO_WINDOW_RADIUS_H (RETRO_WINDOW_H/2)

static inline int robs_s8(int v){ return v>=0x80 ? v-0x100 : v; }
static inline float robs_c01(float v){ if(v<0) v=0; if(v>1) v=1; return v; }
static inline float robs_c11(float v){ if(v<-1) v=-1; if(v>1) v=1; return v; }
static inline float retro_luma(uint8_t r, uint8_t g, uint8_t b){
    return (0.299f*(float)r + 0.587f*(float)g + 0.114f*(float)b)/255.0f;
}

// Canonical RAM readers (smbdis.asm / Data Crystal). Both backends and the
// libretro adapter share these, so an address means the same thing everywhere.
static inline int robs_time(const uint8_t *m){ return (m[0x07F8]%10)*100 + (m[0x07F9]%10)*10 + (m[0x07FA]%10); }
static inline int robs_world(const uint8_t *m){ return m[0x075F]+1; }
static inline int robs_stage(const uint8_t *m){ return m[0x075C]+1; }
static inline int robs_area(const uint8_t *m){ return m[0x0760]+1; }
static inline int robs_score(const uint8_t *m){ int v=0; for(int i=0;i<6;i++) v=v*10 + (m[0x07DE + i]%10); return v; }
static inline int robs_coins(const uint8_t *m){ return (m[0x07ED]%10)*10 + (m[0x07EE]%10); }
static inline int robs_life(const uint8_t *m){ return m[0x075A]; }
static inline int robs_x(const uint8_t *m){ return m[0x6D]*256 + m[0x86]; }
static inline int robs_pstate(const uint8_t *m){ return m[0x000E]; }
static inline int robs_dying(const uint8_t *m){ int s=m[0x000E]; return s==0x0B || m[0x00B5]>1; }
static inline int robs_dead(const uint8_t *m){ return m[0x000E]==0x06; }
static inline int robs_gameover(const uint8_t *m){ return m[0x075A]==0xFF; }
static inline int robs_worldover(const uint8_t *m){ return m[0x0770]==2; }
static inline int robs_flagget(const uint8_t *m){
    if(m[0x0770]==2) return 1;
    for(int i=0;i<6;i++) if(m[0x000f+i] && m[0x0016+i]==0x30 && m[0x001d]==3) return 1;
    return 0;
}

// Scalars the caller tracks per env (reward shaping + derived obs fields).
typedef struct RetroScalars {
    int x_pos, x_pos_max, coins, score, tick;
    int world, stage, area, time, has_flag, is_dead;
    // kills: cumulative enemies-killed counter (see retro_kill_scan).
    // idle: 1 for this step when gameplay made no forward progress.
    int kills, idle;
} RetroScalars;

// Sparse-event reward weights, all configurable via [env] in the env ini
// (retro_reward reads them through this struct; defaults in puf_init match
// the pre-configuration hardcoded values). score is per point (kills award
// 100-8000 pts, so they already pay through score_scale too).
typedef struct RetroWeights {
    float score, coin, kill, death, flag, area, idle;
} RetroWeights;

// Cumulative enemy-kill counter. A kill = an occupied enemy slot frees
// (Enemy_ID -> 0) with the enemy's last known position near the player
// (<= 64px). The proximity filter excludes off-screen despawns (enemies
// vanish at the far screen edge), which would otherwise count as kills.
// Tracks prev IDs/positions across steps; call once per step, after the
// frameskip loop, before retro_reward. Also refreshes the snapshot.
static inline int retro_kill_scan(const uint8_t *m, unsigned char *pid,
        short *pex, int player_x){
    int kills = 0;
    for(int i=0;i<RETRO_NUM_ENEMIES;i++){
        int id = m[0x0016 + i];
        int ex = m[0x006E + i]*256 + m[0x0087 + i];
        if(pid[i] != 0 && id == 0){
            int dx = pex[i] - player_x;
            if(dx >= -64 && dx <= 64) kills++;
        }
        pid[i] = (unsigned char)id;
        pex[i] = (short)ex;
    }
    return kills;
}

// Potential-based progress shaping (Ng-Harada-Russell '99): F = g*P(x')-P(x)
// with P(x) = clamp(x/XMAX) in [0,1]. Telescopes over the episode, so the
// optimal policy is unchanged; retreat is penalized symmetrically (unlike
// max-tracking). gamma MUST equal the learner's train.gamma (policy
// invariance also requires complete transition differences and zero terminal
// potential, as implemented by the ROM wrapper. The legacy reward below does
// not satisfy these boundary conditions.
#define RETRO_POT_XMAX 3400.0f
static inline float retro_potential(int x){
    float v = (float)x / RETRO_POT_XMAX;
    if(v < 0) v = 0; if(v > 1) v = 1;
    return v;
}

// Fills o[0 .. RETRO_EGO_SIZE+RETRO_ENT_SIZE). m = 2KB NES RAM. Pure RAM.
static inline void retro_ego_ent(float *o, const uint8_t *m, const RetroScalars *s){
    for(int i=0;i<RETRO_EGO_SIZE+RETRO_ENT_SIZE;i++) o[i]=0;
    int px = m[0x6D]*256 + m[0x86];
    int py = m[0x00B5]*256 + m[0x00CE];
    int leftx = (m[0x86] - m[0x071C]) & 0xFF;
    (void)leftx;
    o[0]=robs_c01(px/4096.0f);
    o[1]=robs_c01(m[0x6D]/8.0f);
    o[2]=robs_c01(m[0x86]/255.0f);
    o[3]=robs_c01(m[0x0400]/255.0f);
    o[4]=robs_c01(m[0x0705]/255.0f);
    o[5]=robs_c01(m[0x03AD]/255.0f);
    o[6]=robs_c01(((m[0x86]-m[0x071C])&0xFF)/255.0f);
    o[7]=robs_c11(robs_s8(m[0x57])/40.0f);
    o[8]=robs_c01(m[0x0700]/40.0f);
    o[9]=robs_c01(m[0x00B5]/4.0f);
    o[10]=robs_c01(m[0xCE]/255.0f);
    o[11]=robs_c01(m[0x03B8]/255.0f);
    o[12]=robs_c01(m[0x0433]/255.0f);
    o[13]=robs_c11(robs_s8(m[0x9F])/8.0f);
    o[14]=robs_c01(m[0x001D]/4.0f);
    o[15]=robs_c01(m[0x000E]/12.0f);
    o[16]=m[0x33]==1 ? 1.0f : 0.0f;
    o[17]=robs_c01(m[0x45]/2.0f);
    o[18]=robs_c01(m[0x0754]);
    o[19]=robs_c01(m[0x0756]/2.0f);
    o[20]=m[0x0714]!=0 ? 1.0f : 0.0f;
    o[21]=m[0x0704]!=0 ? 1.0f : 0.0f;
    o[22]=robs_c01(m[0x0782]/255.0f);
    o[23]=m[0x079E]!=0 ? 1.0f : 0.0f;
    o[24]=m[0x079F]!=0 ? 1.0f : 0.0f;
    o[25]=m[0x001D]==0 ? 1.0f : 0.0f;
    o[26]=(m[0x001D]==1 || m[0x001D]==2) ? 1.0f : 0.0f;
    o[27]=m[0x001D]==3 ? 1.0f : 0.0f;
    o[28]=robs_c01(m[0x073F]/255.0f);
    o[29]=robs_c01(m[0x071A]/8.0f);
    o[30]=robs_c01(m[0x071C]/255.0f);
    o[31]=robs_c01(m[0x071D]/255.0f);
    o[32]=robs_c11(robs_s8(m[0x0775])/16.0f);
    o[33]=m[0x0723]!=0 ? 1.0f : 0.0f;
    o[34]=robs_c01(m[0x0750]/255.0f);
    o[35]=robs_c01(m[0x06D6]/255.0f);
    o[36]=robs_c01(m[0x072C]/255.0f);
    o[37]=robs_c01(m[0x0739]/255.0f);
    o[38]=robs_c01(s->world/8.0f);
    o[39]=robs_c01(s->stage/4.0f);
    o[40]=robs_c01(s->area/8.0f);
    o[41]=robs_c01(m[0x0770]/4.0f);
    o[42]=robs_c01(s->time/400.0f);
    o[43]=robs_c01(m[0x0009]/255.0f);
    o[44]=robs_c01(m[0x07A7]/255.0f);
    o[45]=robs_c01(m[0x077F]/20.0f);
    o[46]=robs_c01(m[0x0785]/255.0f);
    o[47]=robs_c01(s->coins/99.0f);
    o[48]=robs_c01(s->score/999990.0f);
    o[49]=robs_c01(s->tick/5000.0f);
    o[50]=s->has_flag?1.0f:0.0f;
    o[51]=s->is_dead?1.0f:0.0f;
    o[52]=robs_c01(m[0x075A]/5.0f);
    o[53]=m[0x0023]!=0 ? 1.0f : 0.0f;
    o[54]=robs_c01(m[0x0039]/4.0f);
    o[55]=m[0x0024]!=0 ? 1.0f : 0.0f;
    o[56]=m[0x0025]!=0 ? 1.0f : 0.0f;
    o[57]=robs_c01(m[0x0709]/255.0f);
    o[58]=robs_c01(m[0x070A]/255.0f);
    o[59]=robs_c01(m[0x0456]/48.0f);
    o[60]=m[0x0701]!=0 ? 1.0f : 0.0f;
    o[61]=robs_c01(m[0x0768]/255.0f);
    o[62]=robs_c01(m[0x0747]/4.0f);
    o[63]=robs_c01(s->x_pos_max/3200.0f);
    for(int i=0;i<RETRO_NUM_ENEMIES;i++){
        int b = RETRO_EGO_SIZE + i*RETRO_ENT_PER;
        int act = m[0x000F + i]!=0 ? 1 : 0;
        o[b+0]=act?1.0f:0.0f;
        o[b+1]=robs_c01(m[0x0016 + i]/64.0f);
        o[b+2]=robs_c01(m[0x001E + i]/255.0f);
        if(act){
            int ex = m[0x006E + i]*256 + m[0x0087 + i];
            int ey = m[0x00B6 + i]*256 + m[0x00CF + i];
            o[b+3]=robs_c11((ex-px)/256.0f);
            o[b+4]=robs_c11((ey-py)/256.0f);
        }
        o[b+5]=m[0x0046 + i]==1 ? 1.0f : 0.0f;
        o[b+6]=robs_c11(robs_s8(m[0x0058 + i])/16.0f);
        o[b+7]=robs_c11(robs_s8(m[0x00A0 + i])/8.0f);
    }
    {
        int b = RETRO_EGO_SIZE + RETRO_NUM_ENEMIES*RETRO_ENT_PER;
        int pact = m[0x0023]!=0 ? 1 : 0;
        if(pact){
            // Powerups occupy enemy slot 5: both page and offset must use
            // that slot, not a page byte from enemy slot 1.
            int ex = m[0x0073]*256 + m[0x008C];
            int ey = m[0x00BB]*256 + m[0x00D4];
            o[b+0]=robs_c11((ex-px)/256.0f);
            o[b+1]=robs_c11((ey-py)/256.0f);
        }
        o[b+2]=robs_c01(m[0x0039]/4.0f);
        o[b+3]=pact?1.0f:0.0f;
        for(int i=0;i<2;i++){
            int fact = m[0x0024 + i]!=0 ? 1 : 0;
            if(fact){
                int ex = m[0x0074 + i]*256 + m[0x008D + i];
                int ey = m[0x00BC + i]*256 + m[0x00D5 + i];
                o[b+4+i*2+0]=robs_c11((ex-px)/256.0f);
                o[b+4+i*2+1]=robs_c11((ey-py)/256.0f);
            }
        }
    }
}

// Shared step reward. pr = values before the action's frames, cu = after.
// Progress is potential-based (telescoping, see retro_potential); gamma is
// the learner discount (must match train.gamma). x_max is logging-only.
// Sparse-event weights come from rw (config [env], defaults = the original
// hardcoded values). w_idle>0 additionally penalizes steps that make no
// forward progress during normal gameplay (anti sit-still; default 0).
static inline float retro_reward(const RetroScalars *pr, const RetroScalars *cu,
        int *x_max_io, int dying, int dead, int flag_edge, float gamma,
        const RetroWeights *rw){
    float reward = 0;
    if(cu->world == pr->world && cu->stage == pr->stage && cu->area == pr->area){
        reward += gamma * retro_potential(cu->x_pos) - retro_potential(pr->x_pos);
    }
    if(cu->x_pos > *x_max_io){
        *x_max_io = cu->x_pos;
    }
    int dscore = cu->score - pr->score;
    if(dscore>0) reward += dscore * rw->score;
    int dcoins = cu->coins - pr->coins;
    if(dcoins!=0){ if(dcoins<-50) dcoins+=100; if(dcoins>0) reward += dcoins * rw->coin; }
    if(cu->kills > pr->kills){ reward += (cu->kills - pr->kills) * rw->kill; }
    if(dying || dead){ reward -= rw->death; }
    if(flag_edge){ reward += rw->flag; }
    // Warp/area advance bonus: X resets on area change so progress alone
    // misses wrong warps; reaching a later world/stage/area must pay.
    if(cu->world > pr->world || cu->stage > pr->stage || cu->area > pr->area){ reward += rw->area; }
    if(cu->idle){ reward -= rw->idle; }
    return reward;
}
