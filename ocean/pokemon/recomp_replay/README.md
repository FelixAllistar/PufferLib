# Battle replays in Gen1Recomp (2026-10-31)

Our model's battles (trainer tournament, Weedle vs the game) played back through the Gen1Recomp
(github.com/bryanthaboi/pokemon-gen1-recomp-project) battle screen, staged in 3D by the Battle Art Voxel Fork mod
(github.com/absol89/DramaticShapeVoxelMod). Nothing is re-simulated: every turn shows exactly what our engine logged.

1. Record battles as replay pages: env.trainer_replay_dir / trainer_replay_games / trainer_names (pokemon_base.h). The
   bridge (bridge.zig) writes each battle in Pokemon Showdown protocol.
2. `python3 convert.py PAGE.html... mod/replays`: per-turn events + parties as JSON, and replays/index.json.
3. mod/ is the Gen1Recomp mod (copy to the game's mods folder, e.g. %APPDATA%\pokemon-love2d\mods\puffer_replay):
   START > REPLAY plays the next replay; a link-battle spectator whose turns are queued from the log.
4. Recording (record_driver.lua, the game's POKEPORT_DRIVER hook), offscreen, frame by frame:
   SDL_VIDEODRIVER=offscreen PUFFER_REPLAY=1 REPLAY_JSON=replay.json SHOT_DIR=frames REPLAY_MAP=INDIGO_PLATEAU \
     REPLAY_X=10 REPLAY_Y=10 POKEPORT_DRIVER=record_driver.lua love GEN1RECOMP_DIR
   ffmpeg -framerate 30 -i frames/f%06d.png -c:v libx264 -pix_fmt yuv420p out.mp4
   The voxel mod stages battles only where it finds open ground: the Elite Four rooms fall back to a flat backdrop,
   the Indigo Plateau exterior and outdoor maps work. REPLAY_WARMUP (default 1800 frames) lets its meshes build first.
