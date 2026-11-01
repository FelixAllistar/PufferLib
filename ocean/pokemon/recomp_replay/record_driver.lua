-- Recording driver for the puffer_replay mod (2026-10-31): plays one replay and screenshots it frame by frame.
--   REPLAY_JSON=replay.json SHOT_DIR=/tmp/frames [REPLAY_MAP=AGATHAS_ROOM REPLAY_X=5 REPLAY_Y=5 REPLAY_STEP=2]
--   PUFFER_REPLAY=1 POKEPORT_DRIVER=.../record_driver.lua love .
-- Writes SHOT_DIR/f000001.png... (one every REPLAY_STEP game frames: 2 = 30 fps), then quits; ffmpeg turns them into
-- a video (record.sh).
-- REPLAY_AUDIO_DIR: also capture the game's sound in game time (the capture runs slower than real time): every music
-- buffer queued (music.raw + events.txt), every one-shot sound played (sfx_<id>.wav + events.txt, with pitch and
-- volume) and the game frame of the first video frame. The music synth asks the device how many buffers it has
-- played to decide when to render more; here that answer follows game frames, so the music stays in step with the
-- video. mix_audio.py lays it all on the video's timeline.
local function le16(v) return string.char(v % 256, math.floor(v / 256) % 256) end
local function le32(v) return le16(v % 65536) .. le16(math.floor(v / 65536)) end
local function writeWav(path, sd)
  local raw, ch, rate, bits = sd:getString(), sd:getChannelCount(), sd:getSampleRate(), sd:getBitDepth()
  local f = assert(io.open(path, "wb"))
  f:write("RIFF", le32(36 + #raw), "WAVE", "fmt ", le32(16), le16(1), le16(ch), le32(rate),
    le32(rate * ch * bits / 8), le16(ch * bits / 8), le16(bits), "data", le32(#raw), raw)
  f:close()
end
local function captureAudio(dir, frameNow)
  os.execute('mkdir -p "' .. dir .. '"')
  local stream = assert(io.open(dir .. "/music.raw", "wb"))
  local events = assert(io.open(dir .. "/events.txt", "w"))
  events:setvbuf("line")
  local data = setmetatable({}, { __mode = "k" })   -- one-shot source -> SoundData
  local queued = setmetatable({}, { __mode = "k" }) -- streaming source -> virtual playback state
  local ids, nextId, offset = setmetatable({}, { __mode = "k" }), 0, 0
  local newSource, newQueueable = love.audio.newSource, love.audio.newQueueableSource
  love.audio.newSource = function(arg, kind, ...)
    local s = newSource(arg, kind, ...)
    if kind == "static" or kind == nil then
      local ok, sd = pcall(function()
        if type(arg) == "userdata" and arg.typeOf and arg:typeOf("SoundData") then return arg end
        return love.sound.newSoundData(arg)
      end)
      if ok then data[s] = sd end
    end
    return s
  end
  love.audio.newQueueableSource = function(rate, bits, channels, count, ...)
    local s = newQueueable(rate, bits, channels, count, ...)
    nextId = nextId + 1
    queued[s] = { id = nextId, count = count or 8, rate = rate, queued = 0, size = 1 }
    return s
  end
  local probe = newQueueable(22050, 16, 1, 2)
  local index = getmetatable(probe).__index
  local queue, free, play, stop, clone = index.queue, index.getFreeBufferCount, index.play, index.stop, index.clone
  index.queue = function(s, sd, ...)
    local q = queued[s]
    if q then
      q.start = q.start or frameNow()
      local raw = sd:getString()
      stream:write(raw)
      events:write(("%d queue %d %d %d %d %d\n"):format(frameNow(), q.id, offset, #raw, sd:getSampleRate(),
        sd:getChannelCount()))
      offset = offset + #raw
      q.queued, q.size = q.queued + sd:getSampleCount(), math.max(1, sd:getSampleCount())
    end
    return queue(s, sd, ...)
  end
  index.getFreeBufferCount = function(s, ...)
    local q = queued[s]
    if not q or not q.start then return free(s, ...) end
    local outstanding = q.queued - (frameNow() - q.start) * q.rate / 60
    return math.max(0, math.min(q.count, q.count - math.ceil(outstanding / q.size)))
  end
  index.clone = function(s, ...)
    local c = clone(s, ...)
    data[c] = data[s]
    return c
  end
  index.play = function(s, ...)
    if data[s] then
      if not ids[data[s]] then
        nextId = nextId + 1
        ids[data[s]] = nextId
        writeWav(("%s/sfx_%d.wav"):format(dir, nextId), data[s])
      end
      events:write(("%d play %d %.4f %.4f\n"):format(frameNow(), ids[data[s]], s:getPitch(), s:getVolume()))
      ids[s] = ids[data[s]]
    end
    return play(s, ...)
  end
  index.stop = function(s, ...)
    local q = queued[s]
    if q then events:write(("%d stopstream %d\n"):format(frameNow(), q.id))
    elseif ids[s] then events:write(("%d stop %d\n"):format(frameNow(), ids[s])) end
    return stop(s, ...)
  end
  return events
end

return function(game)
  local U = dofile("tests/drivers/util.lua")
  local audioEvents
  if os.getenv("REPLAY_AUDIO_DIR") then audioEvents = captureAudio(os.getenv("REPLAY_AUDIO_DIR"), U.frame) end
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
  if audioEvents then audioEvents:write(("%d video_start\n"):format(U.frame())) end
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
