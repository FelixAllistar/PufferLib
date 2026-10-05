#include "raylib.h"
#include <stdio.h>
int main(int argc,char** argv) {
    SetTraceLogLevel(LOG_WARNING);
    if(argc!=3) return 2; Wave wave=LoadWave(argv[1]); if(!IsWaveValid(wave)) return 1;
    printf("%s: %u frames, %u Hz, %u channels\n",argv[1],wave.frameCount,wave.sampleRate,wave.channels);
    WaveFormat(&wave,48000,16,1); bool ok=ExportWave(wave,argv[2]); UnloadWave(wave); return ok ? 0 : 1;
}
