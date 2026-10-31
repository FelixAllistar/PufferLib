-- Recording driver for the puffer_replay mod (2026-10-31): plays one replay and screenshots it frame by frame.
--   REPLAY_JSON=replay.json SHOT_DIR=/tmp/frames [REPLAY_MAP=AGATHAS_ROOM REPLAY_X=5 REPLAY_Y=5 REPLAY_STEP=2]
--   PUFFER_REPLAY=1 POKEPORT_DRIVER=.../record_driver.lua love .
-- Writes SHOT_DIR/f000001.png... (one every REPLAY_STEP game frames: 2 = 30 fps), then quits; ffmpeg turns them into
-- a video (record.sh).
return function(game)
  local U = dofile("tests/drivers/util.lua")
  local Json = require("src.link.Json")
  local Runtime = require("src.mods.Runtime")
  local file = assert(io.open(assert(os.getenv("REPLAY_JSON"), "REPLAY_JSON"), "r"))
  local replay = Json.decode(file:read("a"), 1e9)
  file:close()
  local dir = os.getenv("SHOT_DIR") or "/tmp/replay_frames"
  local step = tonumber(os.getenv("REPLAY_STEP") or "2")
  -- default: the Indigo Plateau spot where the voxel mod's staged arena keeps both Pokemon in frame
  U.teleport(game, os.getenv("REPLAY_MAP") or "INDIGO_PLATEAU", tonumber(os.getenv("REPLAY_X") or "6"),
    tonumber(os.getenv("REPLAY_Y") or "12"), os.getenv("REPLAY_FACING") or "right")
  -- REPLAY_WARMUP game frames unrecorded first: the voxel mod builds the room's meshes in the background
  U.wait(tonumber(os.getenv("REPLAY_WARMUP") or "1800"))
  local n, limit = 0, tonumber(os.getenv("REPLAY_MAX_FRAMES") or "40000")
  -- REPLAY_PREROLL frames of the room before the battle (meshes build while it plays)
  for _ = 1, tonumber(os.getenv("REPLAY_PREROLL") or "90") do
    n = n + 1
    U.shot(game, string.format("%s/f%06d.png", dir, n))
    if step > 1 then U.wait(step - 1) end
  end
  Runtime.emit("puffer_replay.play", { game = game, replay = replay })
  while not game.pufferReplayDone and n < limit do
    n = n + 1
    U.shot(game, string.format("%s/f%06d.png", dir, n))
    if step > 1 then U.wait(step - 1) end
  end
  for _ = 1, 45 do -- a moment on the overworld after the battle
    n = n + 1
    U.shot(game, string.format("%s/f%06d.png", dir, n))
    if step > 1 then U.wait(step - 1) end
  end
  U.log("replay frames:", n)
  love.event.quit()
end
