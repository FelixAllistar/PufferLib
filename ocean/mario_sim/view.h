#pragma once
#include "logic.h"
#ifndef SMB_HEADLESS
static void smb_render_state(const SmbLogic* s,const uint8_t* world) {
    if(!IsWindowReady()){InitWindow(768,576,"Mario native runtime: geometry view");SetTargetFPS(60);}
    // NES world coordinates use square pixels. Leave room outside the viewport
    // for the HUD instead of stretching the 256x240 playfield horizontally.
    const int scale=2,origin_x=128,origin_y=48;
    const auto* m=s->ram;int left=m[0x71a]*256+m[0x71c];
    int last=m[0x725]*16+m[0x726]-((m[0x71f]&3)==0);
    auto rectangle=[&](int x,int y,int width,int height,Color color) {
        DrawRectangle(origin_x+x*scale,origin_y+y*scale,width*scale,height*scale,color);
    };
    BeginDrawing();ClearBackground((Color){24,30,44,255});
    BeginScissorMode(origin_x,origin_y,256*scale,240*scale);
    rectangle(0,0,256,240,(Color){31,43,62,255});
    for(int col=last-31;col<=last;col++)for(int row=2;row<=14;row++) {
        int tile=m[0x500+((col&16)?0xd0:0)+(row-2)*16+(col&15)];
        if(!tile)continue;int x=col*16-left,y=row*16;
        if(tile==0x24||tile==0x25) {
            rectangle(x+7,y,2,16,WHITE);
            if(tile==0x24)rectangle(x+5,y,6,6,YELLOW);
        } else {
            rectangle(x,y,16,16,(Color){148,111,76,255});
            DrawRectangleLines(origin_x+x*scale,origin_y+y*scale,16*scale,16*scale,(Color){55,47,38,255});
        }
    }
    // Actor origins and sizes differ. Use each object's native bounding-box
    // controller and the immutable ROM table rather than a generic 16px box.
    const uint8_t* box_table=world+(0xe1fd-0x8000);
    for(int k=0;k<6;k++)if(m[15+k]&&m[15+k]<128) {
        int control=m[0x49a+k];if(control>=12)continue;
        const uint8_t* box=box_table+4*control;
        int x=m[0x6e + k]*256+m[0x87+k]-left,y=((int)m[0xb6+k]-1)*256+m[0xcf+k];
        rectangle(x+box[0],y+box[1],box[2]-box[0],box[3]-box[1],ORANGE);
    }
    int x=m[0x6d]*256+m[0x86]-left,y=((int)m[0xb5]-1)*256+m[0xce];
    // Player_Y_Position anchors a 32px object even when Mario is small.
    // His small graphics occupy its bottom 16px; crouching big Mario is 24px.
    int height=m[0x754]?16:m[0x714]?24:32;
    rectangle(x,y+32-height,16,height,RED);
    EndScissorMode();
    DrawText(TextFormat("World %d-%d | frame %d | x=%d | routine=%d",m[0x75f]+1,m[0x75c]+1,
        s->timing.video_frame,m[0x6d]*256+m[0x86],m[0xe]),12,10,18,WHITE);
    const char* states[]={"grounded","jumping","falling","climbing"};
    DrawText(TextFormat("Mario: %s | red: player | orange: actor collision boxes",m[0x1d]<4?states[m[0x1d]]:"transition"),12,32,14,WHITE);
    DrawText("Geometry view | square pixels | policy input: game RAM",12,546,16,WHITE);
    EndDrawing();puf_web_vsync();
}
#endif
