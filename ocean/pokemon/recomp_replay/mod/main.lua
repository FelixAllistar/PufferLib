-- Puffer Replays (2026-10-31): plays a battle logged by our PufferLib Gen 1 engine through Gen1Recomp's own battle
-- screen, exactly as it happened there. replays/*.json come from ../convert.py (the Showdown-protocol log of a game,
-- turned into per-turn events). Nothing is simulated here: the battle is a link-battle spectator (as
-- LinkBattle.newSpectator builds it) whose turns are queued from the log -- the move text and animation with the
-- target's hit blink, HP drains to the logged HP, effectiveness/crit/miss/status/stat texts, faints and send-outs.
-- START > REPLAY plays the next battle in replays/index.json.
local BattleState = require("src.battle.BattleState")
local Json = require("src.link.Json")
local Pokemon = require("src.pokemon.Pokemon")
local Sound = require("src.core.Sound")
local Strings = require("src.core.Strings")

local STAT = { atk = "ATTACK", def = "DEFENSE", spe = "SPEED", spa = "SPECIAL", spd = "SPECIAL",
               accuracy = "ACCURACY", evasion = "EVADE" }
local CANT = { slp = "%s is\nfast asleep!", frz = "%s is\nfrozen solid!", par = "%s's\nfully paralyzed!",
               flinch = "%s\nflinched!", recharge = "%s\nmust recharge!", partiallytrapped = "%s\ncan't move!" }
local STATUS = { PSN = "%s\nwas poisoned!", BRN = "%s\nwas burned!", FRZ = "%s\nwas frozen solid!",
                 SLP = "%s\nfell asleep!", PAR = "%s's\nparalyzed! It may\nnot attack!" }
local PREPARE = { FLY = "%s\nflew up high!", DIG = "%s\ndug a hole!", SOLARBEAM = "%s\ntook in sunlight!",
                  SKULL_BASH = "%s\nlowered its head!", SKY_ATTACK = "%s\nis glowing!",
                  RAZOR_WIND = "%s\nmade a whirlwind!" }
local START = { Confusion = "%s\nbecame confused!", Substitute = "It created a\nSUBSTITUTE!",
                LightScreen = "%s's\nprotected against\nspecial attacks!", Reflect = "%s\ngained armor!",
                FocusEnergy = "%s's\ngetting pumped!", LeechSeed = "%s\nwas seeded!", Mist = "%s's\nshrouded in mist!",
                Bide = "%s\nis storing energy!" }
local END = { Confusion = "%s's\nconfused no more!", Substitute = "%s's\nSUBSTITUTE broke!" }

local function norm(s) return (tostring(s):upper():gsub("[^A-Z0-9]", "")) end

return function(mod)
  local moveIds, speciesIds
  local function index(data)
    if moveIds then return end
    moveIds, speciesIds = {}, {}
    for id in pairs(data.moves) do moveIds[norm(id)] = id end
    for id in pairs(data.pokemon) do speciesIds[norm(id)] = id end
    moveIds.PSYCHIC = moveIds.PSYCHIC or "PSYCHIC_M" -- the move, not the type
  end

  local function makeMon(data, m)
    local species = speciesIds[norm(m.species)]
    assert(species, "puffer_replay: unknown species " .. tostring(m.species))
    local mon = Pokemon.new(data, species, m.level, function(a, b) return love.math.random(a, b) end)
    mon.stats.hp, mon.hp = m.maxhp, m.maxhp
    return mon
  end

  -- Per move event: does it hit (a direct damage to the other side follows before the next move), how many extra
  -- strikes, and do crit/effectiveness texts wait until after its first damage (the game prints them after the drain)
  local function annotate(turn)
    local out, pending, move = {}, {}, nil
    for _, ev in ipairs(turn) do
      if ev.t == "move" then
        move = ev
        move.strikes = 0
        table.insert(out, ev)
      elseif move and (ev.t == "crit" or ev.t == "super" or ev.t == "resist" or ev.t == "ohko") then
        table.insert(pending, ev)
      elseif move and ev.t == "damage" and not ev.from and ev.side ~= move.side then
        move.strikes = move.strikes + 1
        ev.strike = move.strikes
        ev.move = move
        table.insert(out, ev)
        for _, p in ipairs(pending) do table.insert(out, p) end
        pending = {}
      else
        if move and (ev.t == "miss" or ev.t == "immune" or ev.t == "fail") and move.strikes == 0 then
          move.noanim = true
        end
        for _, p in ipairs(pending) do table.insert(out, p) end
        pending = {}
        table.insert(out, ev)
      end
    end
    for _, p in ipairs(pending) do table.insert(out, p) end
    return out
  end

  local function start(game, replay)
    local data = game.data
    index(data)
    local parties = { {}, {} }
    for s = 1, 2 do
      for _, m in ipairs(replay.teams[s]) do table.insert(parties[s], makeMon(data, m)) end
    end
    local b = BattleState.newWild(game, parties[2][1].species, 5)
    b.kind = "link"     -- link-battle presentation: no exp, money, catching or blackout
    b.spectating = true -- nobody's input drives anything
    b.player = BattleState.makeBattler(data, parties[1][1], true, nil)
    b.enemy = BattleState.makeBattler(data, parties[2][1], false, nil)
    b.playerParty, b.enemyParty = parties[1], parties[2]
    b.opponentName = replay.p2
    b.introText = Strings("%s vs %s!", replay.p1, replay.p2)
    b.replayTurn = 1
    -- nobody presses A in a replay: every message (ours and the engine's own: send-outs, faints) advances by itself
    -- after a hold long enough to read
    local HOLD = 40
    b.say = function(s, text) table.insert(s.queue, { text = text, auto = true, autoDelay = HOLD }) end
    b.sayNext = function(s, text) return s:sayNextAuto(text, HOLD) end
    b.sayChoice = function(s, text) return s:say(text) end

    local function battler(side) return side == 1 and b.player or b.enemy end
    local function name(x) return x.isPlayer and x.name or Strings("Enemy %s", x.name) end
    local function say(fmt, x, ...) b:sayNext(Strings(fmt, name(x), ...)) end

    local function sendOut(side, slot)
      local mon = parties[side][slot]
      if side == 1 then
        b.player = BattleState.makeBattler(data, mon, true, nil)
        b:syncSides()
        b.sendingOut = true
        b:sayNext(b:sendOutText(b.player.name))
        b:animNext("POOF_ANIM", false)
        b:actNext(function()
          b.sendingOut = false
          b:startGrowIn(b.player)
          Sound.playCry(data, b.player.mon.species)
        end)
      else
        b.enemy = BattleState.makeBattler(data, mon, false, nil)
        b:syncSides()
        b.enemySendingOut = true
        b:sayNext(Strings("%s sent\nout %s!", replay.p2, b.enemy.name))
        b:actNext(function()
          b.enemySendingOut = false
          b:startGrowIn(b.enemy)
          Sound.playCry(data, b.enemy.mon.species)
        end)
      end
    end

    local handle = {}
    function handle.move(ev)
      local user = battler(ev.side)
      local id = moveIds[norm(ev.move)]
      local def = id and data.moves[id]
      b:sayNextAuto(b:romText("_ItemUseText001", "%s\nused %s!", name(user), def and def.name or ev.move))
      if id and not ev.noanim then
        b.nextInsert = (b.nextInsert or 0) + 1
        local row = { anim = id, attackerIsPlayer = user.isPlayer }
        if ev.strikes > 0 then
          row.hit = { animType = user.isPlayer and 4 or 1, blink = battler(3 - ev.side) }
        end
        table.insert(b.queue, b.nextInsert, row)
      end
    end
    function handle.damage(ev)
      local x = battler(ev.side)
      if ev.strike and ev.strike > 1 and not ev.move.noanim then
        local id = moveIds[norm(ev.move.move)]
        b.nextInsert = (b.nextInsert or 0) + 1
        table.insert(b.queue, b.nextInsert, { anim = id, attackerIsPlayer = not x.isPlayer,
          hit = { animType = x.isPlayer and 1 or 4, blink = x } })
      end
      x.mon.hp = ev.hp
      x.mon.status = ev.status or (ev.hp > 0 and x.mon.status or nil)
      b:drainNext(x, ev.hp)
      if ev.from == "psn" then say("%s's\nhurt by poison!", x)
      elseif ev.from == "brn" then say("%s's\nhurt by the burn!", x)
      elseif ev.from == "confusion" then b:sayNext("It hurt itself in\nits confusion!")
      elseif ev.from == "Leech Seed" then say("LEECH SEED saps\n%s!", x)
      elseif ev.from == "Recoil" then say("%s's\nhit with recoil!", x) end
    end
    function handle.heal(ev)
      local x = battler(ev.side)
      x.mon.hp = ev.hp
      b:drainNext(x, ev.hp)
      if not ev.silent and not ev.from then say("%s\nregained health!", x) end
    end
    function handle.status(ev)
      local x = battler(ev.side)
      x.mon.status = ev.status
      if not ev.silent and STATUS[ev.status] then
        say(ev.tox and "%s's\nbadly poisoned!" or STATUS[ev.status], x)
      end
    end
    function handle.cure(ev)
      local x = battler(ev.side)
      x.mon.status = nil
      if ev.silent then return end
      if ev.status == "SLP" then say("%s\nwoke up!", x) elseif ev.status == "FRZ" then say("%s\nwas defrosted!", x) end
    end
    function handle.boost(ev)
      local x = battler(ev.side)
      local how = ev.n >= 2 and "greatly rose!" or ev.n > 0 and "rose!" or ev.n <= -2 and "greatly fell!" or "fell!"
      say("%s's\n%s " .. how, x, STAT[ev.stat] or ev.stat:upper())
    end
    function handle.faint(ev) b:onFaint(battler(ev.side)) end
    function handle.switch(ev)
      local x = battler(ev.side)
      if x.mon.hp > 0 then -- a voluntary switch: withdraw first
        if ev.side == 1 then
          b:sayNextAuto(b:withdrawText(x.name))
          b:queueRetreatAnim()
        else
          b:sayNext(b:romText("_AIBattleWithdrawText", "%s with-\ndrew %s!", replay.p2, x.name))
        end
      end
      b:actNext(function() sendOut(ev.side, ev.slot) end)
    end
    function handle.cant(ev)
      local x = battler(ev.side)
      if ev.why == "Disable" then say("%s's\n%s is disabled!", x, ev.move or "move")
      elseif CANT[ev.why] then say(CANT[ev.why], x) end
    end
    function handle.miss(ev) say("%s's\nattack missed!", battler(ev.side)) end
    function handle.immune(ev) say("It doesn't affect\n%s!", battler(ev.side)) end
    function handle.fail() b:sayNext("But, it failed!") end
    function handle.crit() b:sayNext(b:romText("_CriticalHitText", "Critical hit!")) end
    function handle.super() b:sayNext(b:romText("_SuperEffectiveText", "It's super\neffective!")) end
    function handle.resist() b:sayNext(b:romText("_NotVeryEffectiveText", "It's not very\neffective...")) end
    function handle.ohko() b:sayNext("One-hit KO!") end
    function handle.start(ev)
      if not ev.silent and START[ev.what] then say(START[ev.what], battler(ev.side)) end
    end
    handle["end"] = function(ev)
      if not ev.silent and END[ev.what] then say(END[ev.what], battler(ev.side)) end
    end
    function handle.activate(ev)
      local x = battler(ev.side)
      if ev.what == "confusion" then say("%s\nis confused!", x)
      elseif ev.what == "Haze" then b:sayNext("All STATUS changes\nare eliminated!")
      elseif ev.what == "Substitute" then say("The SUBSTITUTE\ntook damage for\n%s!", x) end
    end
    function handle.prepare(ev)
      local id = moveIds[norm(ev.move)]
      if PREPARE[id] then say(PREPARE[id], battler(ev.side)) end
    end
    function handle.transform(ev) say("%s\ntransformed into\n%s!", battler(ev.side), battler(ev.target).name) end
    function handle.clearboosts() b:sayNext("All STATUS changes\nare eliminated!") end
    function handle.msg(ev) b:sayNext(ev.text) end

    local function finishReplay(s)
      s.result = replay.winner == 1 and "hostWin" or replay.winner == 2 and "guestWin" or "draw"
      local winner = replay.winner == 1 and replay.p1 or replay.winner == 2 and replay.p2 or nil
      s:say(winner and Strings("%s wins!", winner) or "The match ended\nin a draw!")
      s.afterQueue = "finish"
      s.phase = "messages"
    end

    -- one queue fn per event, so every event resolves its battler when it plays and its own rows land right after it
    local function queueTurn(s)
      local turn = replay.turns[s.replayTurn]
      if not turn then return finishReplay(s) end
      local first = s.replayTurn == 1
      s.replayTurn = s.replayTurn + 1
      s.phase = "messages"
      s.afterQueue = "replayNext"
      for _, ev in ipairs(annotate(turn)) do
        -- the leads are already out from the battle intro
        if not (first and ev.t == "switch") and handle[ev.t] then
          s:act(function() handle[ev.t](ev) end)
        end
      end
      s:act(function() s:clearTurnFlinches() end)
    end

    b.resolveTurn = function() end
    b.resolveSwitch = function() end
    b.tryRun = function() end
    b.openParty = function(s) s.phase = "waitBoth" end
    b.playerMonFainted = function() end -- the log sends the replacement
    b.enemyMonFainted = function() end
    local baseFinish = b.finish
    b.finish = function(s, ...)
      game.pufferReplayDone = true
      return baseFinish(s, ...)
    end
    local baseUpdate = b.update
    b.update = function(s, dt)
      if s.phase == "messages" and s.afterQueue == "replayNext" then
        s:tickFx()
        if not s:updateQueue() then s.afterQueue, s.phase = "waitBoth", "waitBoth" end
        return
      end
      if (s.phase == "menu" or s.phase == "waitBoth") and not s.result then
        s:tickFx()
        queueTurn(s)
        return
      end
      baseUpdate(s, dt)
    end
    -- through the overworld's own entry when it is up: the encounter wipe, and the seam presentation mods (the voxel
    -- mod's 3D battle staging) wrap
    local ow = game.overworld
    if ow and ow.pushBattle and game.stack:top() == ow then ow:pushBattle(b) else game.stack:push(b) end
  end

  local list, nextIndex = nil, 1
  local function nextReplay()
    if not list then
      local ok, decoded = pcall(Json.decode, mod:read("replays/index.json") or "[]")
      list = ok and decoded or {}
    end
    if #list == 0 then return nil end
    local entry = list[(nextIndex - 1) % #list + 1]
    nextIndex = nextIndex + 1
    local text = mod:read("replays/" .. entry.file)
    return text and Json.decode(text, 64 * 1024 * 1024)
  end

  -- the recording driver (../record_driver.lua) starts a replay through this event
  mod.events:on("puffer_replay.play", function(p)
    if p and p.game and p.replay then
      p.game.pufferReplayDone = false
      start(p.game, p.replay)
    end
  end)

  -- PUFFER_REPLAY_AUTOPLAY=1: play every replay in a row as soon as the game is up (recording runs)
  local autoplay = os.getenv and os.getenv("PUFFER_REPLAY_AUTOPLAY")
  if autoplay and autoplay ~= "" and autoplay ~= "0" then
    mod.events:on("game.ready", function(ctx)
      local game = ctx and ctx.game or mod.game
      local replay = nextReplay()
      if game and replay then start(game, replay) end
    end)
  end

  mod.hooks:wrap("ui.start_menu.items", function(next, game, items)
    local out = next(game, items)
    if type(out) ~= "table" then return out end
    return mod.ui.insertBefore(out, "SAVE", {
      label = "REPLAY",
      onSelect = function()
        local replay = nextReplay()
        if replay then start(game, replay) else mod.log:warn("no replays in replays/index.json") end
      end,
    })
  end)
end
