// Retro standalone: human play + watch like puffer_survivors
// ./retro               -> human play (arrows + Z/X)
// ./retro play          -> human play
// ./retro watch [latest|PATH.bin] [--deterministic]

#include "retro.h"
#include "puffercpu.h"
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

static const char* RETRO_ENV_NAME = "retro";

static int retro_has_suffix(const char* s, const char* suffix){
    size_t n=strlen(s), m=strlen(suffix);
    return n>=m && strcmp(s+n-m,suffix)==0;
}
static void retro_find_latest(const char* dir, char* out, size_t out_size, time_t* best){
    DIR* dp=opendir(dir);
    if(!dp) return;
    struct dirent* ent;
    while((ent=readdir(dp))){
        if(strcmp(ent->d_name,".")==0 || strcmp(ent->d_name,"..")==0) continue;
        char path[4096];
        snprintf(path,sizeof(path),"%s/%s",dir,ent->d_name);
        struct stat st;
        if(stat(path,&st)!=0) continue;
        if(S_ISDIR(st.st_mode)){
            retro_find_latest(path,out,out_size,best);
        } else if(S_ISREG(st.st_mode) && retro_has_suffix(path,".bin") && st.st_ctime >= *best){
            *best=st.st_ctime;
            snprintf(out,out_size,"%s",path);
        }
    }
    closedir(dp);
}
static int retro_resolve_model(const char* arg, char* out, size_t out_size){
    if(!arg || !*arg || strcmp(arg,"latest")==0){
        const char* root="checkpoints/retro";
        out[0]=0; time_t best=0;
        retro_find_latest(root,out,out_size,&best);
        if(!out[0]){ fprintf(stderr,"no .bin checkpoints found in %s\n",root); return -1; }
        return 0;
    }
    snprintf(out,out_size,"%s",arg);
    return 0;
}
static void retro_print_usage(const char* a0){
    fprintf(stderr,"usage:\n  %s                 human play\n  %s play            human play\n  %s watch [latest|PATH.bin] [--deterministic]\n\nRun from repo root. Watch reads policy from config/retro.ini\n",a0,a0,a0);
}
static Weights* g_weights=nullptr;
static void retro_policy_arch(int* hidden,int* layers){
    Ini ini={0};
    puf_ini_load_env(&ini, RETRO_ENV_NAME, 0, NULL);
    *hidden=puf_ini_get_int(&ini,"policy","hidden_size");
    *layers=puf_ini_get_int(&ini,"policy","num_layers");
    puf_ini_free(&ini);
}
static PufferNet* retro_load_policy(const char* path){
    Weights* w=load_weights(path);
    if(!w){ fprintf(stderr,"failed to load %s\n",path); return nullptr; }
    int act_sizes[] = ACT_SIZES;
    int num_actions = (int)(sizeof(act_sizes)/sizeof(act_sizes[0]));
    int hidden=128, layers=4;
    retro_policy_arch(&hidden,&layers);
    PufferNet* net=make_puffernet(w,1,OBS_SIZE,hidden,layers,act_sizes,num_actions);
    fprintf(stderr,"watch: %s (hidden=%d layers=%d)\n",path,hidden,layers);
    g_weights=w;
    return net;
}
static int retro_human_action(){
    int right = IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D);
    int left  = IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A);
    int up    = IsKeyDown(KEY_UP)    || IsKeyDown(KEY_W);
    int down  = IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S);
    int a_btn = IsKeyDown(KEY_X) || IsKeyDown(KEY_SPACE);
    int b_btn = IsKeyDown(KEY_C) || IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_Z);
    unsigned char mask=0;
    if(right) mask|=RETRO_BTN_RIGHT;
    if(left)  mask|=RETRO_BTN_LEFT;
    if(up)    mask|=RETRO_BTN_UP;
    if(down)  mask|=RETRO_BTN_DOWN;
    if(a_btn) mask|=RETRO_BTN_A;
    if(b_btn) mask|=RETRO_BTN_B;
    for(int i=0;i<RETRO_NUM_ACTIONS;i++) if(RETRO_ACTION_MASKS[i]==mask) return i;
    if((mask & RETRO_BTN_RIGHT) && (mask & RETRO_BTN_A) && (mask & RETRO_BTN_B)) return 4;
    if((mask & RETRO_BTN_RIGHT) && (mask & RETRO_BTN_A)) return 2;
    if((mask & RETRO_BTN_RIGHT) && (mask & RETRO_BTN_B)) return 3;
    if(mask & RETRO_BTN_RIGHT) return 1;
    if(mask & RETRO_BTN_LEFT) return 6;
    if(mask & RETRO_BTN_A) return 5;
    if(mask & RETRO_BTN_B) return 9;
    if(mask & RETRO_BTN_DOWN) return 8;
    return 0;
}
int main(int argc, char** argv){
    int watch_mode=0;
    int watch_deterministic=0;
    const char* model_arg=nullptr;
    if(argc>=2){
        if(strcmp(argv[1],"watch")==0){
            watch_mode=1; model_arg="latest";
            int set=0;
            for(int i=2;i<argc;i++){
                if(strcmp(argv[i],"--deterministic")==0) watch_deterministic=1;
                else if(!set && argv[i][0]!='-'){ model_arg=argv[i]; set=1; }
                else { fprintf(stderr,"unknown watch arg %s\n",argv[i]); retro_print_usage(argv[0]); return 1; }
            }
        } else if(strcmp(argv[1],"play")==0){
        } else if(strcmp(argv[1],"help")==0 || strcmp(argv[1],"-h")==0 || strcmp(argv[1],"--help")==0){
            retro_print_usage(argv[0]); return 0;
        } else {
            fprintf(stderr,"unknown mode %s\n",argv[1]); retro_print_usage(argv[0]); return 1;
        }
    }
    PufferNet* net=nullptr;
    if(watch_mode){
        char path[4096];
        if(retro_resolve_model(model_arg,path,sizeof(path))!=0) return 1;
        net=retro_load_policy(path);
        if(!net) return 1;
    }
    Ini ini={0};
    puf_ini_load_env(&ini, RETRO_ENV_NAME, 0, NULL);
    Dict* env_kwargs=puf_ini_section(&ini,"env",0);
    Env env={0};
    float obs[OBS_SIZE]={0};
    float act[NUM_ATNS]={0};
    float rew[1]={0};
    float term[1]={0};
    puf_init(&env, env_kwargs);
    // puf_init memsets env, so set agents after
    env.agents[0].observations=obs;
    env.agents[0].actions=act;
    env.agents[0].rewards=rew;
    env.agents[0].terminals=term;
    env.agents[0].action_mask=nullptr;
    env.agents[0].policy=0;
    puf_reset(&env);
    const char* _hd=getenv("DISPLAY"); const char* _hw=getenv("WAYLAND_DISPLAY");
    if((!_hd || !*_hd) && (!_hw || !*_hw)){
        fprintf(stderr,"no display - headless demo 100 steps\n");
        for(int i=0;i<100;i++){
            if(!watch_mode) act[0]=1;
            else {
                if(watch_deterministic){ linear(net->encoder, obs); mingru(net->mingru, net->encoder->output); linear(net->decoder, net->mingru->output); argmax_multidiscrete(net->multidiscrete, net->decoder->output, act); }
                else forward_puffernet(net, obs, act);
            }
            puf_step(&env);
            if(i%20==0) fprintf(stderr,"headless step %d x=%d score=%d world=%d-%d term=%.0f\n", i, env.x_pos, env.score, env.world, env.stage, term[0]);
            if(term[0]>0.5f) puf_reset(&env);
        }
        puf_close(&env);
        if(net) free_puffernet(net);
        if(g_weights) free(g_weights);
        puf_ini_free(&ini);
        return 0;
    }
    SetTraceLogLevel(LOG_ERROR);
    puf_render(&env);
    if(!IsWindowReady()){
        fprintf(stderr,"no window - headless demo 100 steps\n");
        for(int i=0;i<100;i++){
            if(!watch_mode) act[0]=1;
            else {
                if(watch_deterministic){ linear(net->encoder, obs); mingru(net->mingru, net->encoder->output); linear(net->decoder, net->mingru->output); argmax_multidiscrete(net->multidiscrete, net->decoder->output, act); }
                else forward_puffernet(net, obs, act);
            }
            puf_step(&env);
            if(i%20==0) fprintf(stderr,"headless step %d x=%d score=%d world=%d-%d term=%.0f\n", i, env.x_pos, env.score, env.world, env.stage, term[0]);
            if(term[0]>0.5f) puf_reset(&env);
        }
        puf_close(&env);
        if(net) free_puffernet(net);
        if(g_weights) free(g_weights);
        puf_ini_free(&ini);
        return 0;
    }
    while(!WindowShouldClose()){
        if(IsKeyPressed(KEY_R)){
            puf_reset(&env);
        }
        if(!watch_mode){
            int a = retro_human_action();
            act[0]=(float)a;
        } else {
            if(watch_deterministic){
                linear(net->encoder, obs);
                mingru(net->mingru, net->encoder->output);
                linear(net->decoder, net->mingru->output);
                argmax_multidiscrete(net->multidiscrete, net->decoder->output, act);
            } else {
                forward_puffernet(net, obs, act);
            }
        }
        puf_step(&env);
        if(env.agents[0].terminals[0]>0.5f){
            if(net && net->mingru && net->mingru->state){
                int cnt=net->mingru->num_layers*net->mingru->batch_size*net->mingru->hidden_size;
                memset(net->mingru->state,0,cnt*sizeof(float));
            }
        }
        puf_render(&env);
    }
    puf_close(&env);
    if(net) free_puffernet(net);
    if(g_weights) free(g_weights);
    puf_ini_free(&ini);
    return 0;
}
