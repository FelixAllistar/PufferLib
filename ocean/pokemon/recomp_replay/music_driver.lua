-- Battle music for the replay videos (2026-11-01): renders Gen 1 battle themes through the game's own chip synth
-- (ChipAudio._renderMusicForTest, as tests/drivers/audio_capture_test.lua does) to WAV files in MUSIC_DIR.
--   MUSIC_DIR=out MUSIC_SECONDS=360 POKEPORT_DRIVER=.../music_driver.lua love .
local function le16(v) return string.char(v % 256, math.floor(v / 256) % 256) end
local function le32(v) return le16(v % 65536) .. le16(math.floor(v / 65536)) end
local function writeWav(path, sd, channels)
  local raw = sd:getString()
  local rate, bits = sd:getSampleRate(), sd:getBitDepth()
  local f = assert(io.open(path, "wb"))
  f:write("RIFF", le32(36 + #raw), "WAVE", "fmt ", le32(16), le16(1), le16(channels), le32(rate),
    le32(rate * channels * bits / 8), le16(channels * bits / 8), le16(bits), "data", le32(#raw), raw)
  f:close()
end
return function(game)
  local ChipAudio = require("src.core.ChipAudio")
  local out = os.getenv("MUSIC_DIR") or "/tmp/music"
  local seconds = tonumber(os.getenv("MUSIC_SECONDS") or "360")
  os.execute('mkdir -p "' .. out .. '"')
  local songs = game.data.audio.songs
  local names = {}
  for name in pairs(songs) do
    if name:find("Battle") or name:find("Victory") then names[#names + 1] = name end
  end
  table.sort(names)
  for _, name in ipairs(names) do
    writeWav(out .. "/" .. name .. ".wav", ChipAudio._renderMusicForTest(game.data, songs[name], seconds), 2)
    print("[music] " .. name)
  end
  love.event.quit()
end
