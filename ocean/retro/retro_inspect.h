#pragma once
// Standalone watcher only. Never linked into the running CUDA trainer.
#include <cmath>
#include <unistd.h>

static const char* const retro_ego_labels[64]={
    "world X", "X page", "X byte", "X subpixel", "X move force", "relative X",
    "screen X est.", "X speed", "X abs speed", "Y page", "Y byte", "relative Y RAM",
    "Y subpixel", "Y speed", "move state", "player routine", "facing flag", "moving dir",
    "size", "power state", "swimming", "crouching", "RAM $0782", "invincibility",
    "injury timer", "grounded", "jump/fall", "climbing", "scroll amount", "screen page",
    "screen X", "screen right", "scroll speed", "scroll lock", "area pointer", "RAM $06D6",
    "RAM $072C", "RAM $0739", "world", "stage", "area", "game mode", "time",
    "frame counter", "RNG byte", "interval timer", "RAM $0785", "coins", "score",
    "episode tick", "flag", "dying", "lives byte", "powerup active", "powerup type",
    "fireball 0 active", "fireball 1 active", "jump force", "jump force down", "Y force",
    "running flag", "RAM $0768", "master timer", "max X"
};

static Color retro_inspect_pixel(const Env& e,int x,int y) {
    x=std::max(0,std::min(255,x)); y=std::max(0,std::min(239,y));
    const unsigned char* p=e.reset_image?e.start->pixels:e.display->pixels;
    const short* pal=e.reset_image?e.start->palette:e.display->palette;
    const auto& c=Nes_Emu::nes_colors[pal[p[y*256+x]]&(Nes_Emu::color_table_size-1)];
    return Color{c.red,c.green,c.blue,255};
}
// Independent reconstruction for a diagnostic, not the source of the displayed
// image. The panel reads obs[112..15471] directly, exactly as inference does.
static float retro_inspect_error(const Env& e,const float* obs) {
    float error=0;
    const int scale=2,left=0,top=0;
    for(int ty=0;ty<RETRO_WINDOW_H;ty++) for(int tx=0;tx<RETRO_WINDOW_W;tx++) {
        float sum=0;
        for(int y=0;y<scale;y++) for(int x=0;x<scale;x++) {
            Color c=retro_inspect_pixel(e,left+tx*scale+x,top+ty*scale+y);
            sum+=retro_luma(c.r,c.g,c.b);
        }
        error=std::max(error,std::fabs(sum/(scale*scale)-obs[112+ty*RETRO_WINDOW_W+tx]));
    }
    return error;
}
static void retro_inspect_choose(RetroPolicy* net,float* obs,float* action,bool deterministic) {
    if(!net) return;
    retro_policy_act(net,obs,action,deterministic);
}
static void retro_inspect_clear_rnn(RetroPolicy* net) {
    if(!net) return;
    memset(net->mingru->state,0,net->mingru->num_layers*net->mingru->batch_size*net->mingru->hidden_size*sizeof(float));
}
static bool retro_inspect_screenshot(const char* path) {
    // TakeScreenshot strips directory components in this raylib build.
    // Export the framebuffer explicitly so PNG and CSV really share a path.
    Image frame=LoadImageFromScreen();
    bool ok=ExportImage(frame,path); UnloadImage(frame); return ok;
}
static void retro_inspect_dump(const Env& e,const float* obs,int action,long decision) {
    char prefix[256],path[280];
    snprintf(prefix,sizeof(prefix),"/tmp/retro-observation-%d-%ld",getpid(),decision);
    snprintf(path,sizeof(path),"%s.csv",prefix);
    FILE* f=fopen(path,"w");
    if(!f) { perror("observation dump"); return; }
    fprintf(f,"# contract=%s inputs=%d image_width=%d image_height=%d image_offset=112\n"
        "# decision=%ld world=%d stage=%d tick=%d action=%d mask=%u\nindex,value\n",
        RETRO_OBSERVATION_CONTRACT,OBS_SIZE,RETRO_WINDOW_W,RETRO_WINDOW_H,
        decision,e.world,e.stage,e.tick,action,retro_action_mask(action));
    for(int i=0;i<OBS_SIZE;i++) fprintf(f,"%d,%.9g\n",i,obs[i]);
    fclose(f);
    snprintf(path,sizeof(path),"%s.png",prefix);
    if(!retro_inspect_screenshot(path)) { fprintf(stderr,"Failed to save %s\n",path); return; }
    fprintf(stderr,"Observation saved: %s.png and %s.csv\n",prefix,prefix);
}

static int retro_inspect(Env& e,RetroPolicy* net,float* obs,float* action,
        float* reward,float* terminal,bool deterministic,bool headless,const char* snapshot=nullptr) {
    // Persistent full frame for comparison; policy construction is unchanged.
    if(!e.display) e.display=new RetroDisplay{};
    long decision=0; bool paused=true; int fps=60; Texture2D texture={};
    float last_reward=0; bool last_terminal=false;
    if(!headless) {
        SetTraceLogLevel(LOG_ERROR);
        if(snapshot) SetConfigFlags(FLAG_WINDOW_HIDDEN);
        InitWindow(1280,900,"Retro / FULL SCREEN 128x120 / actual policy input");
        SetTargetFPS(fps);
    }
    fprintf(stderr,"Inspector: %s, %d inputs; CNN 8/16/32 + RAM32 -> MinGRU; no crop mode\n",RETRO_OBSERVATION_CONTRACT,OBS_SIZE);
    retro_inspect_choose(net,obs,action,deterministic);
    while(headless?decision<600:!WindowShouldClose()) {
        bool advance=headless,reset=false,dump=false;
        if(!headless) {
            if(IsKeyPressed(KEY_P)||IsKeyPressed(KEY_SPACE)) paused=!paused;
            if(IsKeyPressed(KEY_N)) { paused=true; advance=true; }
            if(IsKeyPressed(KEY_R)) reset=true;
            if(IsKeyPressed(KEY_ONE)) fps=15;
            if(IsKeyPressed(KEY_TWO)) fps=30;
            if(IsKeyPressed(KEY_THREE)) fps=60;
            SetTargetFPS(fps); advance|=!paused;
            dump=IsKeyPressed(KEY_F12);
            if(!net) {
                // Space belongs to pause here; X/Z are manual A/B.
                unsigned char mask=(IsKeyDown(KEY_X)?1:0)|(IsKeyDown(KEY_Z)?2:0)
                    |(IsKeyDown(KEY_UP)?16:0)|(IsKeyDown(KEY_DOWN)?32:0)
                    |(IsKeyDown(KEY_LEFT)?64:0)|(IsKeyDown(KEY_RIGHT)?128:0);
                *action=retro_mask_action(mask);
            }
        } else if(!net) {
            *action=retro_mask_action(RETRO_BTN_RIGHT|RETRO_BTN_B|((decision%48<28)?RETRO_BTN_A:0));
        }
        if(reset) {
            puf_reset(&e); retro_inspect_clear_rnn(net); last_reward=0; last_terminal=false;
            retro_inspect_choose(net,obs,action,deterministic);
        } else if(advance) {
            puf_step(&e); decision++; last_reward=*reward; last_terminal=*terminal!=0;
            if(last_terminal) retro_inspect_clear_rnn(net);
            retro_inspect_choose(net,obs,action,deterministic);
        }
        for(int i=0;i<OBS_SIZE;i++) if(!std::isfinite(obs[i]))
            throw std::runtime_error("inspector: non-finite policy input");
        float error=retro_inspect_error(e,obs);
        if(headless) {
            if(error!=0) throw std::runtime_error("inspector: source pixels and policy grid disagree");
            continue;
        }
        unsigned char rgba[256*240*4];
        for(int y=0;y<240;y++) for(int x=0;x<256;x++) {
            Color c=retro_inspect_pixel(e,x,y); int i=(y*256+x)*4;
            rgba[i]=c.r; rgba[i+1]=c.g; rgba[i+2]=c.b; rgba[i+3]=255;
        }
        if(!texture.id) { Image im={rgba,256,240,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8}; texture=LoadTextureFromImage(im); }
        UpdateTexture(texture,rgba);
        BeginDrawing(); ClearBackground(Color{18,22,30,255});
        DrawText(TextFormat("FULL SCREEN 128x120  |  %s  |  decision %ld  |  level %d-%d  |  tick %d  |  %d Hz",
            paused?"PAUSED":"RUNNING",decision,e.world,e.stage,e.tick,fps),16,10,18,RAYWHITE);
        DrawText("P/Space pause   N advance one decision   R reset   1/2/3 = 15/30/60 Hz   F12 save PNG + inputs   Esc close",16,37,16,LIGHTGRAY);
        DrawText("Entire NES framebuffer: 256 x 240 RGB",16,64,18,RAYWHITE);
        DrawTextureEx(texture,Vector2{16,88},0,2,WHITE);
        DrawText("ACTUAL POLICY INPUT: 128 x 120 luma",550,64,18,RAYWHITE);
        const int cell=4;
        for(int y=0;y<RETRO_WINDOW_H;y++) for(int x=0;x<RETRO_WINDOW_W;x++) {
            float v=obs[112+y*RETRO_WINDOW_W+x]; unsigned char c=(unsigned char)std::lround(robs_c01(v)*255);
            DrawRectangle(550+x*cell,88+y*cell,cell,cell,Color{c,c,c,255});
        }
        DrawText("CNN + RAM encoder",1080,90,15,RAYWHITE);
        DrawText("2x2 pixel means",1080,118,15,LIGHTGRAY);
        DrawText("No crop / camera",1080,144,15,LIGHTGRAY);
        DrawText("Conv: 8 / 16 / 32",1080,170,15,LIGHTGRAY);
        DrawText("15,360 image inputs",1080,210,15,LIGHTGRAY);
        DrawText("112 RAM inputs",1080,236,15,LIGHTGRAY);
        DrawText("Pixel/input max error",1080,276,14,LIGHTGRAY);
        DrawText(TextFormat("%.9g",error),1080,300,18,error==0?GREEN:RED);
        Vector2 mouse=GetMousePosition(); int gx=(int)std::floor((mouse.x-550)/cell),gy=(int)std::floor((mouse.y-88)/cell);
        if(gx>=0&&gx<RETRO_WINDOW_W&&gy>=0&&gy<RETRO_WINDOW_H)
            DrawText(TextFormat("pixel (%d,%d): obs[%d] = %.9g",gx,gy,112+gy*RETRO_WINDOW_W+gx,obs[112+gy*RETRO_WINDOW_W+gx]),550,574,15,YELLOW);
        else DrawText("Hover the input image for its exact pixel value",550,574,15,LIGHTGRAY);
        DrawText(TextFormat(net?"NEXT action %d":"MANUAL action %d",(int)*action),1080,344,16,RAYWHITE);
        DrawText(net?"from shown input":"arrows + X/Z",1080,368,14,LIGHTGRAY);
        const char* buttons[]={"A","B","Up","Down","Left","Right"};
        for(int i=0;i<6;i++) {
            bool held=((int)*action&(1<<i))!=0;
            int x=1080+(i%2)*86,y=398+(i/2)*30;
            DrawRectangle(x,y,80,25,held?Color{40,150,90,255}:Color{45,50,60,255});
            DrawText(buttons[i],x+6,y+5,14,RAYWHITE);
        }
        DrawText(TextFormat("Prev reward: %+.5f",last_reward),1080,510,14,LIGHTGRAY);
        if(last_terminal) DrawText("Terminal -> reset",1080,536,14,YELLOW);
        DrawText("RAM state: normalized inputs [0..63] (not camera controls)",16,593,16,RAYWHITE);
        for(int i=0;i<64;i++) {
            int col=i/22,row=i%22;
            DrawText(TextFormat("%02d %-15s %+.5f",i,retro_ego_labels[i],obs[i]),16+col*258,618+row*12,11,LIGHTGRAY);
        }
        DrawText("Entities: normalized inputs [64..111]",808,593,16,RAYWHITE);
        DrawText("id  act  type  state    dx     dy   dir    vx     vy",808,620,12,LIGHTGRAY);
        for(int j=0;j<5;j++) {
            int b=64+j*8;
            DrawText(TextFormat("%d    %.0f   %.2f   %.2f  %+.3f  %+.3f   %.0f   %+.3f  %+.3f",
                j,obs[b],obs[b+1],obs[b+2],obs[b+3],obs[b+4],obs[b+5],obs[b+6],obs[b+7]),808,644+j*23,12,obs[b]?RAYWHITE:GRAY);
        }
        DrawText(TextFormat("Powerup: dx %+.3f dy %+.3f type %.3f active %.0f",obs[104],obs[105],obs[106],obs[107]),808,776,12,LIGHTGRAY);
        DrawText(TextFormat("Fireball 0: dx %+.3f dy %+.3f",obs[108],obs[109]),808,808,14,LIGHTGRAY);
        DrawText(TextFormat("Fireball 1: dx %+.3f dy %+.3f",obs[110],obs[111]),808,834,14,LIGHTGRAY);
        DrawText(net?"A checkpoint replay, not a live training lane. Pausing does not advance the emulator or recurrent policy.":
            "Manual observation preview; no policy loaded. Arrows move, X=A/jump, Z=B/run. P/Space pause, N single-step.",16,880,14,GRAY);
        EndDrawing();
        if(dump) retro_inspect_dump(e,obs,(int)*action,decision);
        if(snapshot) {
            if(!retro_inspect_screenshot(snapshot)) throw std::runtime_error("failed to save inspector screenshot");
            break;
        }
    }
    if(headless) printf("PASS: inspector checked 600 decisions, finite inputs, exact source/grid agreement through %g resets\n",e.log.n);
    else { UnloadTexture(texture); CloseWindow(); }
    return 0;
}
