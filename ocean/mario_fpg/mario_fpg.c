#include "mario_fpg.h"
int main(int argc,char**argv) {
    if(argc>2){fprintf(stderr,"usage: viewer [CONFIG]\n");return 2;}
    Ini ini={0};if(argc==2)puf_ini_load_file(&ini,argv[1]);else puf_ini_load_env(&ini,"mario_fpg",0,NULL);
    Env env={0};puf_init(&env,puf_ini_section(&ini,"env",0));
    if(env.cfg.fixed_tier<0)env.cfg.fixed_tier=3;
    puf_reset(&env);FpgState initial=*env.state;fpg_render_state(env.state);
    while(!WindowShouldClose()) {
        if(IsKeyPressed(KEY_N)){puf_reset(&env);initial=*env.state;}
        if(IsKeyPressed(KEY_R))*env.state=initial;
        for(int k=0;k<4;k++)if(IsKeyPressed(KEY_ONE+k)){env.cfg.fixed_tier=k;puf_reset(&env);initial=*env.state;}
        int direction=IsKeyDown(KEY_RIGHT)?1:IsKeyDown(KEY_LEFT)?2:0;
        int action=direction+(IsKeyDown(KEY_X)||IsKeyDown(KEY_SPACE)?3:0)+(IsKeyDown(KEY_Z)||IsKeyDown(KEY_LEFT_SHIFT)?6:0);
        fpg_step_task(env.state,&env.cfg,action);fpg_render_state(env.state);
    }
    puf_close(&env);puf_ini_free(&ini);CloseWindow();return 0;
}
