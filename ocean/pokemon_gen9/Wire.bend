import Base
import ./StatTable.bend as StatTable
import ./MoveSlots.bend as MoveSlots
import ./SpeciesChange.bend as SpeciesChange
import ./TypeChange.bend as TypeChange
import ./TextJoin.bend as TextJoin
import ./TextMem.bend as TextMem
import ./TextStore.bend as TextStore
import ./ValueArray.bend as Values
import ./PokemonTypes.bend as Types
import ./TypeInit.bend as TypeInit
import ./Grounded.bend as Grounded
import ./EventListener.bend as EventListener
import ./TerrainQuery.bend as Terrain
import ./Math.bend as M
import ./Rng.bend as R
import ./Mem.bend as S
import ./Catalog.bend as C
import ./GenData.bend as D
import ./Lists.bend as L
import ./GenCounter.bend as Counter
import ./GenAbility.bend as Ability
import ./GenRng.bend as GenRng
import ./GenItem.bend as Item
import ./GenCull.bend as Cull
import ./GenMoves.bend as Moves
import ./GenSet.bend as Set
import ./GenTeam.bend as Team
import ./Init.bend as Init
import ./Stats.bend as Stats
import ./State.bend as State
import ./Queue.bend as Queue
import ./Damage.bend as Damage
import ./Events.bend as Events
import ./Pokemon.bend as Pokemon
import ./Boosts.bend as Boosts
import ./Registry.bend as Registry
import ./Sodium.bend as Sodium
import ./SodiumMem.bend as SodiumMem
import ./Number.bend as Number
import ./HP.bend as HP
import ./Faint.bend as Faint
import ./Suppress.bend as Suppress
import ./MoveView.bend as MoveView
import ./data/Layout.bend as Layout
import ./Request.bend as Request
import ./EventGate.bend as EventGate
import ./EventValue.bend as Value
import ./EventContext.bend as Context
import ./Callbacks.bend as Callbacks
import ./SingleEvent.bend as SingleEvent
import ./RunEvent.bend as RunEvent
import ./VectorEvent.bend as VectorEvent
import ./ListenerPriority.bend as ListenerPriority
import ./EffectStore.bend as Effects
import ./EffectInit.bend as EffectInit
import ./Collect.bend as Collect
import ./CollectedEvent.bend as CollectedEvent
import ./BoostTable.bend as BoostTable
import ./LiveStats.bend as LiveStats
import ./WeatherQuery.bend as Weather

def store_rng(+at: U32, pair: R.Seed & U32) -> S.Mem<Unit>:
  (R.S{a, b, c, d}, value) = pair
  do S.Mem<Unit>:
    S.put(at, a)
    S.put((at + 1 : U32), b)
    S.put((at + 2 : U32), c)
    S.put((at + 3 : U32), d)
    S.put((at + 4 : U32), value)

def rng_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    a : U32 <- S.get(8)
    b : U32 <- S.get(9)
    c : U32 <- S.get(10)
    d : U32 <- S.get(11)
    bound : U32 <- S.get(12)
    store_rng(16, R.random(R.S{a, b, c, d}, bound))

def math_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +a : U32 <- S.get(8)
    +b : U32 <- S.get(9)
    S.put(16, M.mul_hi(a, b))
    S.put(17, M.chain(a, b))
    S.put(18, M.modify(a, b))

def save_list(xs: +List<U32>, +at: U32) -> S.Mem<Unit>:
  match xs:
    case Nil{}: S.Mem.pure(Unit, Unit{})
    case Con{head, tail}:
      do S.Mem<Unit>:
        S.put(at, head)
        save_list(tail, (at + 1 : U32))

def save_catalog(p: D.Species) -> S.Mem<Unit>:
  D.P{id, base, first, second, +stats, gender, nfe, _, +items, +forms,
    _, level, templates, _, weaknesses} = p
  do S.Mem<Unit>:
    S.put(16, id)
    S.put(17, base)
    S.put(18, first)
    S.put(19, second)
    S.put(20, L.at(stats, 0n))
    S.put(21, L.at(stats, 1n))
    S.put(22, L.at(stats, 2n))
    S.put(23, L.at(stats, 3n))
    S.put(24, L.at(stats, 4n))
    S.put(25, L.at(stats, 5n))
    S.put(26, gender)
    S.put(27, level)
    S.put(28, L.length(templates))
    S.put(29, L.length(items))
    S.put(30, L.length(forms))
    S.put(31, Bool.to_u32(nfe))
    save_list(weaknesses, 64)

def catalog_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    id : U32 <- S.get(12)
    p : D.Species <- C.species(id)
    save_catalog(p)

def save_counter(c: D.Counter) -> S.Mem<Unit>:
  D.C{counts, +dmg, +bp} = c
  do S.Mem<Unit>:
    save_list(counts, 64)
    S.put(112, L.length(dmg))
    S.put(113, L.length(bp))
    save_list(dmg, 128)
    save_list(bp, 192)

def counter_builder(b: D.Builder, tera: U32, moves: +List<U32>) -> S.Mem<Unit>:
  D.B{info, pool, _, _} = b
  do S.Mem<Unit>:
    c : D.Counter <- Counter.query(D.B{D.with_tera(info, tera), pool, moves, D.Uncounted{}})
    save_counter(c)

def counter_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    sid : U32 <- S.get(12)
    tid : U32 <- S.get(13)
    tera : U32 <- S.get(14)
    nm : U32 <- S.get(32)
    moves : +List<U32> <- C.read_list(U32.to_nat(nm), 33)
    p : D.Species <- C.species(sid)
    b : D.Builder <- C.template(tid, p, False{}, Nil{})
    counter_builder(b, tera, moves)

def ability_builder(b: D.Builder, tera: U32, moves: +List<U32>) -> S.Mem<Unit>:
  D.B{info, pool, _, _} = b
  do S.Mem<Unit>:
    ability : U32 <- Ability.get(D.B{D.with_tera(info, tera), pool, moves, D.Uncounted{}})
    S.put(16, ability)

def ability_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    sid : U32 <- S.get(12)
    tid : U32 <- S.get(13)
    tera : U32 <- S.get(14)
    nm : U32 <- S.get(32)
    moves : +List<U32> <- C.read_list(U32.to_nat(nm), 33)
    team : +List<U32> <- C.read_list(13n, 48)
    p : D.Species <- C.species(sid)
    b : D.Builder <- C.template(tid, p, False{}, team)
    ability_builder(b, tera, moves)

def empty_sample_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    unused : U32 <- GenRng.sample(Nil{})
    S.put(16, unused)

def item_builder(b: D.Builder, tera: U32, moves: +List<U32>, ability: U32) -> S.Mem<Unit>:
  D.B{info, pool, _, _} = b
  do S.Mem<Unit>:
    item : U32 <- Item.get(D.B{D.with_tera(info, tera), pool, moves, D.Uncounted{}}, ability)
    S.put(16, item)

def item_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    sid : U32 <- S.get(12)
    tid : U32 <- S.get(13)
    tera : U32 <- S.get(14)
    lead : U32 <- S.get(15)
    ability : U32 <- S.get(30)
    nm : U32 <- S.get(32)
    moves : +List<U32> <- C.read_list(U32.to_nat(nm), 33)
    team : +List<U32> <- C.read_list(13n, 48)
    p : D.Species <- C.species(sid)
    b : D.Builder <- C.template(tid, p, U32.is_ne(lead, 0), team)
    item_builder(b, tera, moves, ability)

def cull_builder(b: D.Builder, tera: U32, +moves: +List<U32>) -> S.Mem<Unit>:
  D.B{info, pool, _, _} = b
  do S.Mem<Unit>:
    +next : D.Builder <- S.from_result(D.Builder,
      Cull.cull(D.B{D.with_tera(info, tera), L.filter_out(pool, moves), moves, D.Uncounted{}}))
    S.put(16, L.length(D.pool(next)))
    save_list(D.pool(next), 64)

def cull_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    sid : U32 <- S.get(12)
    tid : U32 <- S.get(13)
    tera : U32 <- S.get(14)
    nm : U32 <- S.get(32)
    moves : +List<U32> <- C.read_list(U32.to_nat(nm), 33)
    team : +List<U32> <- C.read_list(13n, 48)
    p : D.Species <- C.species(sid)
    b : D.Builder <- C.template(tid, p, False{}, team)
    cull_builder(b, tera, moves)

def moves_builder(b: D.Builder, tera: U32) -> S.Mem<Unit>:
  D.B{info, pool, _, _} = b
  do S.Mem<Unit>:
    +next : D.Builder <- Moves.get(D.B{D.with_tera(info, tera), pool, Nil{}, D.Uncounted{}})
    S.put(16, L.length(D.moves(next)))
    save_list(D.moves(next), 64)

def moves_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    sid : U32 <- S.get(12)
    tid : U32 <- S.get(13)
    tera : U32 <- S.get(14)
    lead : U32 <- S.get(15)
    team : +List<U32> <- C.read_list(13n, 48)
    p : D.Species <- C.species(sid)
    b : D.Builder <- C.template(tid, p, U32.is_ne(lead, 0), team)
    moves_builder(b, tera)

def save_set(s: D.GeneratedSet, +at: U32) -> S.Mem<Unit>:
  D.Set{mon, display, role, tera, ability, item, level, gender, shiny, +moves, evs, ivs} = s
  do S.Mem<Unit>:
    S.put(at, mon)
    S.put((at + 1 : U32), display)
    S.put((at + 2 : U32), role)
    S.put((at + 3 : U32), tera)
    S.put((at + 4 : U32), ability)
    S.put((at + 5 : U32), item)
    S.put((at + 6 : U32), level)
    S.put((at + 7 : U32), gender)
    S.put((at + 8 : U32), Bool.to_u32(shiny))
    S.put((at + 9 : U32), L.length(moves))
    save_list(moves, (at + 10 : U32))
    save_list(evs, (at + 16 : U32))
    save_list(ivs, (at + 22 : U32))

def set_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    sid : U32 <- S.get(12)
    lead : U32 <- S.get(15)
    team : +List<U32> <- C.read_list(13n, 48)
    p : D.Species <- C.species(sid)
    generated : D.GeneratedSet <- Set.get(p, team, U32.is_ne(lead, 0))
    save_set(generated, 64)

def save_sets(ss: +List<D.GeneratedSet>, +at: U32) -> S.Mem<Unit>:
  match ss:
    case Nil{}: S.Mem.pure(Unit, Unit{})
    case Con{s, rest}:
      do S.Mem<Unit>:
        save_set(s, at)
        save_sets(rest, (at + 32 : U32))

def team_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    ss : +List<D.GeneratedSet> <- Team.get()
    S.put(16, 6)
    save_sets(ss, 64)

def load_set(+at: U32) -> S.Mem<D.GeneratedSet>:
  do S.Mem<D.GeneratedSet>:
    mon : U32 <- S.get(at)
    display : U32 <- S.get((at + 1 : U32))
    role : U32 <- S.get((at + 2 : U32))
    tera : U32 <- S.get((at + 3 : U32))
    ability : U32 <- S.get((at + 4 : U32))
    item : U32 <- S.get((at + 5 : U32))
    level : U32 <- S.get((at + 6 : U32))
    gender : U32 <- S.get((at + 7 : U32))
    shiny : U32 <- S.get((at + 8 : U32))
    +nm : U32 <- S.get((at + 9 : U32))
    S.branch(D.GeneratedSet, U32.is_ge(nm, 1) && U32.is_le(nm, 4),
      (do S.Mem<D.GeneratedSet>:
        moves : +List<U32> <- C.read_list(U32.to_nat(nm), (at + 10 : U32))
        evs : +List<U32> <- C.read_list(6n, (at + 16 : U32))
        ivs : +List<U32> <- C.read_list(6n, (at + 22 : U32))
        return D.Set{mon, display, role, tera, ability, item, level, gender,
          U32.is_ne(shiny, 0), moves, evs, ivs}),
      S.raise(D.GeneratedSet, 3))

def init_pokemon_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    set : D.GeneratedSet <- load_set(64)
    State.clear()
    EffectInit.battle()
    Init.pokemon(set, 0, 0)
    EffectInit.side(0)
    TypeInit.pokemon(576)
    StatTable.attach(576)
    MoveSlots.attach(576)
    BoostTable.attach(576)

def stats_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +base : U32 <- S.get(8)
    +iv : U32 <- S.get(9)
    +ev : U32 <- S.get(10)
    +level : U32 <- S.get(11)
    kind : U32 <- S.get(12)
    stage : U32 <- S.get(13)
    +move : U32 <- S.get(14)
    +hp : U32 <- S.get(22)
    +amount : U32 <- S.get(23)
    maximum : U32 <- S.get(24)
    base_pp : U32 <- C.move(move, 5)
    no_boost : U32 <- C.move(move, 7)
    +stat : U32 = Stats.stat(base, iv, ev, level, kind)
    S.put(16, stat)
    S.put(17, M.hp(base, iv, ev, level))
    S.put(18, Stats.boosted(stat, stage))
    S.put(19, Stats.pp(move, base_pp, no_boost))
    S.put(20, Stats.remaining(hp, amount))
    S.put(21, Stats.healed(hp, maximum, amount))

def init_gen5_teams() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    other_seed : +List<U32> <- C.read_list(4n, 12)
    State.clear()
    EffectInit.battle()
    first : +List<D.GeneratedSet> <- Team.get()
    Init.team(first, 0, 0)
    EffectInit.side(0)
    Init.store(other_seed, 8)
    second : +List<D.GeneratedSet> <- Team.get()
    Init.team(second, 1, 0)
    EffectInit.side(1)
    Init.constructed()

def init_teams_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    mode : U32 <- S.get(4)
    S.branch(Unit, U32.is_eq(mode, 0), init_gen5_teams(), S.raise(Unit, 3))

def sodium_seed_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    a : U32 <- S.get(8)
    b : U32 <- S.get(9)
    c : U32 <- S.get(10)
    d : U32 <- S.get(11)
    e : U32 <- S.get(12)
    f : U32 <- S.get(13)
    g : U32 <- S.get(14)
    h : U32 <- S.get(15)
    SodiumMem.write(Sodium.K{a,b,c,d,e,f,g,h})
    S.put(4, 1)

def random_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    bound : U32 <- S.get(12)
    value : U32 <- GenRng.random(bound)
    S.put(16, value)

def sodium_raw_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    mode : U32 <- S.get(4)
    S.branch(Unit, U32.is_eq(mode, 1),
      (do S.Mem<Unit>:
        seed : Sodium.Seed <- SodiumMem.read()
        value : U32 <- SodiumMem.save(Sodium.next(seed))
        S.put(16, value)), S.raise(Unit, 3))

def sort_queue_valid(yes: Bool, +size: U32, mode: U32, +base: U32) -> S.Mem<Unit>:
  match yes:
    case False{}: S.raise(Unit, 3)
    case True{}:
      do S.Mem<Unit>:
        input : +List<U32> <- C.read_list(U32.to_nat((size * 8 : U32)), 8192)
        Queue.store(input,base)
        Queue.sort_mode_buffer(U32.to_nat(mode),base,size)
        output : +List<U32> <- C.read_list(U32.to_nat((size * 8 : U32)),base)
        save_list(output, 8192)
        S.put(16, size)

def sort_queue_op(+separate: Bool) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +size : U32 <- S.get(Bool.pick(U32,separate,32,12))
    +mode : U32 <- S.get(Bool.pick(U32,separate,33,13))
    sort_queue_valid(U32.is_le(size,64) && U32.is_le(mode,2),size,mode,Bool.pick(U32,separate,12288,2176))

def damage_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    level : U32 <- S.get(32)
    power : U32 <- S.get(33)
    attack : U32 <- S.get(34)
    +defense : U32 <- S.get(35)
    flags : U32 <- S.get(36)
    weather : U32 <- S.get(37)
    stab : U32 <- S.get(38)
    exponent : U32 <- S.get(39)
    final : U32 <- S.get(40)
    will : U32 <- S.get(41)
    ratio : U32 <- S.get(42)
    allowed : U32 <- S.get(43)
    S.branch(Unit, U32.is_ne(defense, 0),
      (do S.Mem<Unit>:
        +crit : Bool <- Damage.crit_result(will, ratio, U32.is_ne(allowed, 0))
        +first : U32 = Damage.base(level, power, attack, defense)
        +bits : U32 = flags
        before_roll : U32 = Damage.critical(
          M.modify(Damage.added(first, U32.is_ne((bits .&. 1 : U32), 0)), weather), crit)
        +rolled : U32 <- Damage.randomize(before_roll)
        after_stab : U32 = L.pick(U32.is_ne((bits .&. 2 : U32), 0), rolled, M.modify(rolled, stab))
        after_type : U32 = Damage.effectiveness(after_stab, exponent)
        after_burn : U32 = Damage.burned(after_type, U32.is_ne((bits .&. 4 : U32), 0))
        amount : U32 = Damage.finish(M.modify(after_burn, final), U32.is_ne((bits .&. 8 : U32), 0))
        S.put(16, amount)
        S.put(17, first)
        S.put(18, Bool.to_u32(crit))),
      S.raise(Unit, 3))

def confusion_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    level : U32 <- S.get(32)
    power : U32 <- S.get(33)
    attack : U32 <- S.get(34)
    +defense : U32 <- S.get(35)
    S.branch(Unit, U32.is_ne(defense, 0),
      (do S.Mem<Unit>:
        amount : U32 <- Damage.confusion(level, power, attack, defense)
        S.put(16, amount)), S.raise(Unit, 3))

def event_metadata_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +kind : U32 <- S.get(32)
    +ability : U32 <- S.get(33)
    scope : U32 <- S.get(34)
    explicit : U32 <- S.get(35)
    +flags : U32 <- S.get(36)
    creation : U32 <- S.get(37)
    cached : U32 <- S.get(38)
    unboosted : U32 <- S.get(39)
    rank : U32 <- S.get(40)
    S.put(16, Events.sub_order(Events.kind(U32.to_nat(kind)), ability, scope, explicit))
    S.put(17, Events.creation_order(flags, creation))
    S.put(18, Events.speed(Events.kind(U32.to_nat(kind)), ability, flags, cached, unboosted, rank))

def pokemon_pp_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +side : U32 <- S.get(32)
    +pos : U32 <- S.get(33)
    move : U32 <- S.get(34)
    amount : U32 <- S.get(35)
    S.branch(Unit, U32.is_lt(side, 2) && U32.is_lt(pos, 6),
      (do S.Mem<Unit>:
        paid : U32 <- Pokemon.deduct(State.mon(side, pos), move, amount)
        S.put(16, paid)), S.raise(Unit, 3))

def load_boosts(n: Nat, +at: U32, +seen: U32, +setting: Bool) -> S.Mem<+List<Boosts.Change>>:
  match n:
    case 0n: S.Mem.pure(+List<Boosts.Change>, Nil{})
    case 1n+p:
      do S.Mem<+List<Boosts.Change>>:
        +stat : U32 <- S.get(at)
        +delta : U32 <- S.get((at + 1 : U32))
        S.branch(+List<Boosts.Change>, U32.is_lt(stat, 7) &&
          U32.is_le(delta, Bool.pick(U32, setting, 12, 65535)),
          (do S.Mem<+List<Boosts.Change>>:
            +bit : U32 = (1 << U32.to_nat(stat) : U32)
            S.branch(+List<Boosts.Change>, U32.is_eq((seen .&. bit : U32), 0),
              (do S.Mem<+List<Boosts.Change>>:
                rest : +List<Boosts.Change> <- load_boosts(p, (at + 2 : U32), (seen .|. bit : U32), setting)
                return Boosts.D{stat, delta} <> rest), S.raise(+List<Boosts.Change>, 3))),
          S.raise(+List<Boosts.Change>, 3))

def boost_mode(mode: Nat, +at: U32, changes: +List<Boosts.Change>) -> S.Mem<Unit>:
  match mode:
    case 0n:
      do S.Mem<Unit>:
        value : U32 <- Boosts.apply(changes, at, 32768)
        S.put(16, value)
    case 1n:
      do S.Mem<Unit>:
        Boosts.clear(at)
        S.put(16, 32768)
    case 2n:
      do S.Mem<Unit>:
        Boosts.set(changes, at)
        S.put(16, 32768)
    case _: S.raise(Unit, 3)

def pokemon_boost_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +side : U32 <- S.get(32)
    +pos : U32 <- S.get(33)
    +mode : U32 <- S.get(34)
    +count : U32 <- S.get(35)
    S.branch(Unit, U32.is_lt(side, 2) && U32.is_lt(pos, 6) && U32.is_le(mode, 2) && U32.is_le(count, 7),
      (do S.Mem<Unit>:
        changes : +List<Boosts.Change> <- load_boosts(U32.to_nat(count), 64, 0, U32.is_eq(mode, 2))
        boost_mode(U32.to_nat(mode), State.mon(side, pos), changes)
        positive : U32 <- Boosts.positive(7n,State.mon(side,pos),0,0)
        S.put(17, positive)), S.raise(Unit, 3))

def pokemon_disable_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +side : U32 <- S.get(32)
    +pos : U32 <- S.get(33)
    move : U32 <- S.get(34)
    hidden : U32 <- S.get(35)
    effect_kind : U32 <- S.get(36)
    effect : U32 <- S.get(37)
    S.branch(Unit, U32.is_lt(side, 2) && U32.is_lt(pos, 6),
      Pokemon.disable(State.mon(side, pos), move, U32.is_ne(hidden, 0), effect_kind, effect), S.raise(Unit, 3))

def effect_kind(kind: Nat, id: U32) -> Registry.Effect:
  match kind:
    case 0n: Registry.MoveEffect{id}
    case 1n: Registry.AbilityEffect{id}
    case 2n: Registry.ItemEffect{id}
    case 5n: Registry.SpeciesEffect{id}
    case _: Registry.ConditionEffect{id}

def effect_family(+family: U32) -> Bool:
  U32.is_le(family,3) || U32.is_eq(family,5)

def save_handler(result: Registry.Lookup) -> S.Mem<Unit>:
  match result:
    case Registry.Missing{}: S.put(16, 0)
    case Registry.Found{Registry.H{callback, tag, order, priority, sub, value}}:
      do S.Mem<Unit>:
        S.put(16, 1)
        save_list([callback, tag, order, priority, sub, value], 64)

def registry_op(aliases: Bool) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +kind : U32 <- S.get(32)
    id : U32 <- S.get(33)
    +code : U32 <- S.get(34)
    pokemon : U32 <- S.get(35)
    +effect : Registry.Effect = effect_kind(U32.to_nat(U32.min(kind, 5)), id)
    S.branch(Unit, effect_family(kind) && Registry.valid(effect),
      (do S.Mem<Unit>:
        found : Registry.Lookup <- S.branch(Registry.Lookup, aliases,
          Registry.callback(effect, code, U32.is_ne(pokemon, 0)), Registry.get_raw(effect, code))
        save_handler(found)), S.raise(Unit, 3))

def hp_scalar(mode: Nat, +high: U32, +low: U32, +denominator: U32) -> Number.Scalar:
  match mode:
    case 0n: Number.Finite{False{}, Number.numerator(high, low), denominator}
    case 1n: Number.Finite{True{}, Number.numerator(high, low), denominator}
    case 2n: Number.NaN{}
    case 3n: Number.Infinity{False{}}
    case _: Number.Infinity{True{}}

def save_hp(result: HP.Outcome) -> S.Mem<Unit>:
  match result:
    case HP.Changed{negative, amount}:
      do S.Mem<Unit>:
        S.put(16, 0)
        S.put(17, Bool.to_u32(negative))
        S.put(18, amount)
    case HP.Refused{}:
      do S.Mem<Unit>:
        S.put(16, 1)
        S.put(17, 0)
        S.put(18, 0)

def hp_mode(mode: Nat, at: U32, number: Number.Scalar, source: U32, kind: U32, effect: U32) -> S.Mem<HP.Outcome>:
  match mode:
    case 0n: HP.damage(at, number, source, kind, effect)
    case 1n: HP.heal(at, number)
    case 2n: HP.sethp(at, number)
    case _:
      do S.Mem<HP.Outcome>:
        amount : U32 <- Faint.enqueue(at, source, kind, effect)
        return HP.Changed{False{}, amount}

def hp_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +side : U32 <- S.get(32)
    +pos : U32 <- S.get(33)
    +mode : U32 <- S.get(34)
    +tag : U32 <- S.get(35)
    +high : U32 <- S.get(36)
    low : U32 <- S.get(37)
    +denominator : U32 <- S.get(38)
    source : U32 <- S.get(39)
    kind : U32 <- S.get(40)
    effect : U32 <- S.get(41)
    S.branch(Unit, U32.is_lt(side, 2) && U32.is_lt(pos, 6) && U32.is_le(mode, 3) &&
      U32.is_le(tag, 4) && U32.is_le(high, 65535) && U32.is_ne(denominator, 0),
      (do S.Mem<Unit>:
        result : HP.Outcome <- hp_mode(U32.to_nat(mode), State.mon(side, pos),
          hp_scalar(U32.to_nat(tag), U32.min(high, 65535), low, U32.max(denominator, 1)), source, kind, effect)
        save_hp(result)), S.raise(Unit, 3))

def suppression_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +side : U32 <- S.get(32)
    +pos : U32 <- S.get(33)
    +actor : U32 <- S.get(34)
    +ignore : U32 <- S.get(35)
    S.branch(Unit, U32.is_lt(side, 2) && U32.is_lt(pos, 6) && U32.is_le(actor, 12) && U32.is_le(ignore, 1),
      (do S.Mem<Unit>:
        +index : U32 = (U32.max(actor, 1) - 1 : U32)
        +at : U32 = State.mon(side, pos)
        S.put(548, Bool.pick(U32, U32.is_eq(actor, 0), 0, State.mon((index / 6 : U32), (index % 6 : U32))))
        S.put(549, ignore)
        ability : Bool <- Suppress.ability(at)
        item : Bool <- Suppress.item(at, False{})
        fling : Bool <- Suppress.item(at, True{})
        weather : Bool <- Suppress.weather()
        breaker : Bool <- Suppress.breaker(at)
        save_list([Bool.to_u32(ability), Bool.to_u32(item), Bool.to_u32(fling),
          Bool.to_u32(weather), Bool.to_u32(breaker)], 16)), S.raise(Unit, 3))

def request_lock(mode: Nat, move: U32) -> MoveView.Lock:
  match mode:
    case 0n: MoveView.Unlocked{}
    case 1n: MoveView.Locked{move}
    case _: MoveView.Recharge{}

def save_request_move(row: MoveView.Choice, at: U32) -> S.Mem<Unit>:
  MoveView.M{move, slot, pp, maximum, target, disabled, present, synthetic} = row
  save_list([move, slot, pp, maximum, target, Bool.to_u32(disabled), present, synthetic], at)

def save_request_moves(rows: +List<MoveView.Choice>, +at: U32, +count: U32) -> S.Mem<Unit>:
  match rows:
    case Nil{}: S.put(16, count)
    case Con{row, rest}:
      do S.Mem<Unit>:
        save_request_move(row, at)
        save_request_moves(rest, (at + 8 : U32), (count + 1 : U32))

def moves_view_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +side : U32 <- S.get(32)
    +pos : U32 <- S.get(33)
    +locked : U32 <- S.get(34)
    +move : U32 <- S.get(35)
    +restrict : U32 <- S.get(36)
    S.branch(Unit, U32.is_lt(side, 2) && U32.is_lt(pos, 6) && U32.is_le(locked, 2) &&
      U32.is_le(restrict, 1) && (U32.is_ne(locked, 1) ||
        U32.is_gt(move, 0) && U32.is_lt(move, Layout.moves_count())),
      (do S.Mem<Unit>:
        rows : +List<MoveView.Choice> <- MoveView.get(State.mon(side, pos),
          request_lock(U32.to_nat(locked), move), U32.is_ne(restrict, 0))
        save_request_moves(rows, 64, 0)), S.raise(Unit, 3))

def valid_lock(+tag: U32, +move: U32) -> Bool:
  U32.is_le(tag, 2) && (U32.is_ne(tag, 1) || U32.is_gt(move, 0) && U32.is_lt(move, Layout.moves_count()))

def save_move_request(projection: Request.Projection) -> S.Mem<Unit>:
  Request.RD{moves, flags, tera} = projection
  do S.Mem<Unit>:
    save_request_moves(moves, 64, 0)
    S.put(17, flags)
    S.put(18, tera)

def move_request_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +side : U32 <- S.get(32)
    +pos : U32 <- S.get(33)
    +hard : U32 <- S.get(34)
    +hard_move : U32 <- S.get(35)
    +semi : U32 <- S.get(36)
    +semi_move : U32 <- S.get(37)
    +can_switch : U32 <- S.get(38)
    S.branch(Unit, U32.is_lt(side, 2) && U32.is_lt(pos, 6) && valid_lock(hard, hard_move) &&
      valid_lock(semi, semi_move) && U32.is_le(can_switch, 1),
      (do S.Mem<Unit>:
        projection : Request.Projection <- Request.get(State.mon(side, pos),
          request_lock(U32.to_nat(hard), hard_move), request_lock(U32.to_nat(semi), semi_move),
          U32.is_ne(can_switch, 0))
        save_move_request(projection)), S.raise(Unit, 3))

def event_gate_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +kind : U32 <- S.get(32)
    id : U32 <- S.get(33)
    event : U32 <- S.get(34)
    +side : U32 <- S.get(35)
    +pos : U32 <- S.get(36)
    +mode : U32 <- S.get(37)
    +pokemon : U32 <- S.get(38)
    depth : U32 <- S.get(39)
    lines : U32 <- S.get(40)
    +effect : Registry.Effect = effect_kind(U32.to_nat(U32.min(kind, 5)), id)
    S.branch(Unit, effect_family(kind) && Registry.valid(effect) && U32.is_lt(side, 2) && U32.is_lt(pos, 6) &&
      U32.is_le(mode, 1) && U32.is_le(pokemon, 1),
      (do S.Mem<Unit>:
        S.put(536, depth)
        S.put(537, lines)
        allowed : Bool <- EventGate.check(effect, event, State.mon(side, pos),
          U32.is_ne(pokemon, 0), U32.is_ne(mode, 0))
        S.put(16, Bool.to_u32(allowed))), S.raise(Unit, 3))

def event_value_kind(tag: Nat, +high: U32, +low: U32, +denominator: U32) -> Value.Value:
  match tag:
    case 0n: Value.VUndefined{}
    case 1n: Value.VNull{}
    case 2n: Value.VBool{False{}}
    case 3n: Value.VBool{True{}}
    case 4n: Value.VNumber{Number.Finite{False{}, Number.numerator(U32.min(high, 65535), low), U32.max(denominator, 1)}}
    case 5n: Value.VNumber{Number.Finite{True{}, Number.numerator(U32.min(high, 65535), low), U32.max(denominator, 1)}}
    case 6n: Value.VNumber{Number.NaN{}}
    case 7n: Value.VNumber{Number.Infinity{False{}}}
    case 8n: Value.VNumber{Number.Infinity{True{}}}
    case 9n: Value.VText{low}
    case _: Value.VObject{high,low}

def load_event_value(+at: U32) -> S.Mem<Value.Value>:
  do S.Mem<Value.Value>:
    +tag : U32 <- S.get(at)
    +high : U32 <- S.get((at + 1 : U32))
    low : U32 <- S.get((at + 2 : U32))
    +denominator : U32 <- S.get((at + 3 : U32))
    S.branch(Value.Value, U32.is_le(tag, 10) &&
      (U32.is_ne(tag, 4) && U32.is_ne(tag, 5) || U32.is_le(high, 65535) && U32.is_ne(denominator, 0)),
      S.Mem.pure(Value.Value, event_value_kind(U32.to_nat(U32.min(tag, 10)), high, low, denominator)),
      S.raise(Value.Value, 3))

def event_scalar_fields(number: Number.Scalar) -> +List<U32>:
  match number:
    case Number.Finite{negative, +numerator, denominator}:
      +base : Nat = (U32.to_nat(65536) * U32.to_nat(65536) : Nat)
      [Bool.pick(U32, negative, 5, 4), U32.from_nat((numerator / base : Nat)),
        U32.from_nat((numerator % base : Nat)), denominator]
    case Number.NaN{}: [6,0,0,0]
    case Number.Infinity{negative}: [Bool.pick(U32, negative, 8, 7),0,0,0]

def event_value_fields(value: Value.Value) -> +List<U32>:
  match value:
    case Value.VUndefined{}: [0,0,0,0]
    case Value.VNull{}: [1,0,0,0]
    case Value.VBool{yes}: [Bool.pick(U32, yes, 3, 2),0,0,0]
    case Value.VNumber{number}: event_scalar_fields(number)
    case Value.VText{id}: [9,0,id,0]
    case Value.VObject{family,id}: [10,family,id,0]

def save_event_value(+value: Value.Value) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    save_list(event_value_fields(value), 64)
    S.put(17, Bool.to_u32(Value.truthy(value)))

def save_relay(relay: Value.Relay) -> S.Mem<Unit>:
  Value.R{value, stopped} = relay
  do S.Mem<Unit>:
    S.put(16, Bool.to_u32(stopped))
    save_event_value(value)

def event_value_mode(mode: Nat, old: Value.Value, returned: Value.Value,
  fast: Bool, modifier: U32, tag: U32, payload: U32) -> S.Mem<Unit>:
  match mode:
    case 0n: save_event_value(Value.initial(old, True{}))
    case 1n: save_event_value(Value.initial(old, False{}))
    case 2n: save_event_value(Value.single_result(old, returned))
    case 3n: save_relay(Value.replace(old, returned, fast))
    case 4n: save_event_value(Value.final_value(old, modifier))
    case 5n:
      do S.Mem<Unit>:
        value : Value.Value <- S.from_result(Value.Value, Value.literal_kind(U32.to_nat(tag), payload))
        save_event_value(value)
    case _: S.raise(Unit, 3)

def event_value_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    mode : U32 <- S.get(32)
    +fast : U32 <- S.get(33)
    modifier : U32 <- S.get(34)
    tag : U32 <- S.get(35)
    payload : U32 <- S.get(36)
    S.branch(Unit, U32.is_le(fast, 1),
      (do S.Mem<Unit>:
        old : Value.Value <- load_event_value(64)
        returned : Value.Value <- load_event_value(68)
        S.put(16, 0)
        event_value_mode(U32.to_nat(mode), old, returned, U32.is_ne(fast, 0), modifier, tag, payload)), S.raise(Unit, 3))

def context_ref(kind: Nat, id: U32) -> Context.EffectRef:
  match kind:
    case 4n: Context.EmptyEffect{}
    case _: Context.BoundEffect{effect_kind(kind,id)}

def load_context_ref(+at: U32) -> S.Mem<Context.EffectRef>:
  do S.Mem<Context.EffectRef>:
    +kind : U32 <- S.get(at)
    id : U32 <- S.get((at + 1:U32))
    S.branch(Context.EffectRef,U32.is_le(kind,5),
      S.Mem.pure(Context.EffectRef,context_ref(U32.to_nat(U32.min(kind,5)),id)),
      S.raise(Context.EffectRef,3))

def load_scope_event(+at: U32) -> S.Mem<Context.EventData>:
  do S.Mem<Context.EventData>:
    id : U32 <- S.get(at)
    target : Value.Value <- load_event_value((at + 1:U32))
    source : Value.Value <- load_event_value((at + 5:U32))
    source_effect : Value.Value <- load_event_value((at + 9:U32))
    return Context.E{id,target,source,source_effect,Context.NoModifier{}}

def with_scope_modifier(event: Context.EventData, present: Bool, modifier: U32) -> Context.EventData:
  Context.E{id,target,source,source_effect,_} = event
  Context.E{id,target,source,source_effect,Bool.pick(Context.Modifier,present,
    Context.Q12{modifier},Context.NoModifier{})}

def load_context() -> S.Mem<Context.Current>:
  do S.Mem<Context.Current>:
    effect : Context.EffectRef <- load_context_ref(64)
    state : U32 <- S.get(66)
    event : Context.EventData <- load_scope_event(67)
    +present : U32 <- S.get(80)
    modifier : U32 <- S.get(81)
    depth : U32 <- S.get(82)
    S.branch(Context.Current,U32.is_le(present,1),
      S.Mem.pure(Context.Current,Context.C{effect,state,
        with_scope_modifier(event,U32.is_ne(present,0),modifier),depth}),S.raise(Context.Current,3))

def load_scope_command(mode: Nat, +at: U32) -> S.Mem<Context.Command>:
  match mode:
    case 0n:
      do S.Mem<Context.Command>:
        effect : Context.EffectRef <- load_context_ref((at + 1:U32))
        state : U32 <- S.get((at + 3:U32))
        event : Context.EventData <- load_scope_event((at + 4:U32))
        lines : U32 <- S.get((at + 17:U32))
        return Context.EnterSingle{effect,state,event,lines}
    case 1n:
      do S.Mem<Context.Command>:
        event : Context.EventData <- load_scope_event((at + 4:U32))
        return Context.EnterRun{event}
    case 2n:
      do S.Mem<Context.Command>:
        effect : Context.EffectRef <- load_context_ref((at + 1:U32))
        state : U32 <- S.get((at + 3:U32))
        return Context.EnterHandler{effect,state}
    case 3n: S.Mem.pure(Context.Command,Context.LeaveSingle{})
    case 4n: S.Mem.pure(Context.Command,Context.LeaveRun{})
    case 5n: S.Mem.pure(Context.Command,Context.LeaveHandler{})
    case 6n:
      do S.Mem<Context.Command>:
        modifier : U32 <- S.get((at + 1:U32))
        return Context.Chain{modifier}
    case 7n:
      do S.Mem<Context.Command>:
        number : U32 <- S.get((at + 1:U32))
        return Context.FinalModify{number}
    case _: S.raise(Context.Command,3)

def context_effect_fields(effect: Context.EffectRef) -> +List<U32>:
  match effect:
    case Context.EmptyEffect{}: [4,0]
    case Context.BoundEffect{raw}:
      match raw:
        case Registry.MoveEffect{id}: [0,id]
        case Registry.AbilityEffect{id}: [1,id]
        case Registry.ItemEffect{id}: [2,id]
        case Registry.ConditionEffect{id}: [3,id]
        case Registry.SpeciesEffect{id}: [5,id]
        case Registry.FormatEffect{}: [6,0]

def context_modifier_fields(modifier: Context.Modifier) -> +List<U32>:
  match modifier:
    case Context.NoModifier{}: [0,0]
    case Context.Q12{value}: [1,value]

def context_event_fields(event: Context.EventData) -> +List<U32>:
  Context.E{id,target,source,source_effect,modifier} = event
  L.append([id],L.append(event_value_fields(target),L.append(event_value_fields(source),
    L.append(event_value_fields(source_effect),context_modifier_fields(modifier)))))

def context_fields(current: Context.Current) -> +List<U32>:
  Context.C{effect,state,event,depth} = current
  L.append(context_effect_fields(effect),L.append([state],L.append(context_event_fields(event),[depth])))

def frame_count(stack: +List<Context.Frame>) -> U32:
  match stack:
    case Nil{}: 0
    case Con{_,tail}: (1 + frame_count(tail):U32)

def save_context(machine: Context.Machine, at: U32) -> S.Mem<Unit>:
  Context.VM{current,stack,last} = machine
  save_list(L.append(context_fields(current),[frame_count(stack),last,0,0,0]),at)

def run_context(n: Nat, +command_at: U32, +output_at: U32, +count: U32, machine: Context.Machine) -> S.Mem<Unit>:
  match n:
    case 0n: S.Mem.pure(Unit,Unit{})
    case 1n+p:
      do S.Mem<Unit>:
        mode : U32 <- S.get(command_at)
        command : Context.Command <- load_scope_command(U32.to_nat(mode),command_at)
        +next : Context.Machine <- S.from_result(Context.Machine,Context.step(machine,command))
        save_context(next,output_at)
        S.put(16,(count + 1:U32))
        run_context(p,(command_at + 24:U32),(output_at + 24:U32),(count + 1:U32),next)

def context_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +count : U32 <- S.get(32)
    S.branch(Unit,U32.is_le(count,64),
      (do S.Mem<Unit>:
        current : Context.Current <- load_context()
        +machine : Context.Machine = Context.VM{current,Nil{},0}
        save_context(machine,10216)
        S.put(16,0)
        run_context(U32.to_nat(count),8192,10240,0,machine)),S.raise(Unit,3))

def save_callback(returned: Callbacks.Returned) -> S.Mem<Unit>:
  Callbacks.ReturnedData{current,+value} = returned
  do S.Mem<Unit>:
    save_list(event_value_fields(value),96)
    S.put(17,Bool.to_u32(Value.truthy(value)))
    save_context(Context.VM{current,Nil{},0},10216)

def callback_lookup(effect: Registry.Effect, lookup: Registry.Lookup, current: Context.Current, args: Callbacks.Args)
  -> S.Mem<Unit>:
  match lookup:
    case Registry.Missing{}: S.raise(Unit,4)
    case Registry.Found{handler}:
      do S.Mem<Unit>:
        returned : Callbacks.Returned <- Callbacks.call(effect,handler,current,args)
        save_callback(returned)

def callback_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +family : U32 <- S.get(32)
    id : U32 <- S.get(33)
    callback : U32 <- S.get(34)
    +side : U32 <- S.get(35)
    +position : U32 <- S.get(36)
    +effect : Registry.Effect = effect_kind(U32.to_nat(U32.min(family,5)),id)
    S.branch(Unit,effect_family(family) && Registry.valid(effect) &&
      U32.is_lt(side,2) && U32.is_lt(position,6),
      (do S.Mem<Unit>:
        current : Context.Current <- load_context()
        relay : Value.Value <- load_event_value(88)
        lookup : Registry.Lookup <- Registry.get(effect,callback)
        callback_lookup(effect,lookup,current,Callbacks.ArgsData{relay,State.mon(side,position)})),S.raise(Unit,3))

def single_selection(lookup: Registry.Lookup) -> SingleEvent.Selection:
  match lookup:
    case Registry.Missing{}: SingleEvent.DefaultCallback{}
    case Registry.Found{handler}: SingleEvent.SuppliedCallback{handler}

def single_event_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +family : U32 <- S.get(32)
    id : U32 <- S.get(33)
    +callback : U32 <- S.get(34)
    state : U32 <- S.get(35)
    +override : U32 <- S.get(36)
    lines : U32 <- S.get(37)
    +effect : Registry.Effect = effect_kind(U32.to_nat(U32.min(family,5)),id)
    S.branch(Unit,effect_family(family) && Registry.valid(effect) &&
      U32.is_gt(callback,0) && U32.is_lt(callback,Layout.callbacks_count()) && U32.is_lt(override,Layout.callbacks_count()),
      (do S.Mem<Unit>:
        parent : Context.Current <- load_context()
        +event : Context.EventData <- load_scope_event(104)
        +target : U32 = SingleEvent.pokemon_at(SingleEvent.target_value(event))
        S.branch(Unit,U32.is_eq(SingleEvent.event_id(event),callback) &&
          (U32.is_eq(target,0) || U32.is_ge(target,576) && U32.is_le(target,1984) &&
            U32.is_eq(((target - U32.min(target,576) : U32) % 128 : U32),0)),
          (do S.Mem<Unit>:
            relay : Value.Value <- load_event_value(88)
            selection : Registry.Lookup <- S.branch(Registry.Lookup,U32.is_eq(override,0),
              S.Mem.pure(Registry.Lookup,Registry.Missing{}),Registry.get(effect,override))
            S.put(537,lines)
            returned : Callbacks.Returned <- SingleEvent.call(effect,single_selection(selection),state,event,parent,relay)
            save_callback(returned)),S.raise(Unit,3))),S.raise(Unit,3))

def listener_from_lookup(effect: Registry.Effect, state: U32, holder: U32, pokemon: Bool,
  key: Queue.Entry, redirect: U32, target_index: U32, holder_value: Value.Value,
  lookup: Registry.Lookup) -> Result<&2,&2,U32,EventListener.ListenerRow>:
  match lookup:
    case Registry.Missing{}: Fail{3}
    case Registry.Found{handler}:
      Done{EventListener.ListenerData{effect,handler,state,holder,pokemon,key,redirect,target_index,holder_value}}

def load_listener(+at: U32) -> S.Mem<EventListener.ListenerRow>:
  do S.Mem<EventListener.ListenerRow>:
    +family : U32 <- S.get(at)
    id : U32 <- S.get((at + 1 : U32))
    +callback : U32 <- S.get((at + 2 : U32))
    state : U32 <- S.get((at + 3 : U32))
    +holder : U32 <- S.get((at + 4 : U32))
    +pokemon : U32 <- S.get((at + 5 : U32))
    order : U32 <- S.get((at + 6 : U32))
    +priority : U32 <- S.get((at + 7 : U32))
    +speed : U32 <- S.get((at + 8 : U32))
    +sub : U32 <- S.get((at + 9 : U32))
    creation : U32 <- S.get((at + 10 : U32))
    redirect : U32 <- S.get((at + 11 : U32))
    target_index : U32 <- S.get((at + 12 : U32))
    +holder_family : U32 <- S.get((at + 13 : U32))
    +holder_id : U32 <- S.get((at + 14 : U32))
    +effect : Registry.Effect = effect_kind(U32.to_nat(U32.min(family,5)),id)
    S.branch(EventListener.ListenerRow,effect_family(family) && Registry.valid(effect) && U32.is_le(pokemon,1) &&
      U32.is_gt(callback,0) && U32.is_lt(callback,Layout.callbacks_count()) &&
      U32.is_le(priority,65535) && U32.is_le(speed,65535) && U32.is_le(sub,65535) &&
      (U32.is_eq(holder_family,0) && U32.is_eq(holder_id,0) || U32.is_eq(pokemon,0) &&
        (U32.is_eq(holder_family,2) && U32.is_lt(holder_id,2) ||
         (U32.is_eq(holder_family,3) || U32.is_eq(holder_family,4)) && U32.is_eq(holder_id,0))) &&
      (U32.is_eq(pokemon,0) && U32.is_eq(holder,0) || U32.is_eq(pokemon,1) &&
        U32.is_ge(holder,576) && U32.is_le(holder,1984) &&
        U32.is_eq(((holder - U32.min(holder,576) : U32) % 128 : U32),0)),
      (do S.Mem<EventListener.ListenerRow>:
        lookup : Registry.Lookup <- Registry.callback(effect,callback,U32.is_ne(pokemon,0))
        S.from_result(EventListener.ListenerRow,listener_from_lookup(effect,state,holder,U32.is_ne(pokemon,0),
          Queue.E{0,order,priority,speed,sub,creation},redirect,target_index,
          Bool.pick(Value.Value,U32.is_ne(pokemon,0),Value.VObject{1,holder},
            Value.VObject{Bool.pick(U32,U32.is_eq(holder_family,0),4,holder_family),holder_id}),lookup))),
      S.raise(EventListener.ListenerRow,3))

def load_listeners(n: Nat, +at: U32) -> S.Mem<+List<EventListener.ListenerRow>>:
  match n:
    case 0n: S.Mem.pure(+List<EventListener.ListenerRow>,Nil{})
    case 1n+p:
      do S.Mem<+List<EventListener.ListenerRow>>:
        listener : EventListener.ListenerRow <- load_listener(at)
        rest : +List<EventListener.ListenerRow> <- load_listeners(p,(at + 16 : U32))
        return Con{listener,rest}

def save_run(result: RunEvent.Executed) -> S.Mem<Unit>:
  RunEvent.ExecutedData{returned,+visited} = result
  do S.Mem<Unit>:
    save_callback(returned)
    S.put(16,L.length(visited))
    save_list(visited,13824)

def run_event_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +count : U32 <- S.get(32)
    +fast_exit : U32 <- S.get(33)
    +count_mode : U32 <- S.get(4)
    S.branch(Unit,U32.is_le(count,64) && U32.is_le(fast_exit,1) && U32.is_le(count_mode,1),
      (do S.Mem<Unit>:
        parent : Context.Current <- load_context()
        +event : Context.EventData <- load_scope_event(104)
        +target : U32 = SingleEvent.pokemon_at(SingleEvent.target_value(event))
        S.branch(Unit,U32.is_gt(SingleEvent.event_id(event),0) &&
          U32.is_lt(SingleEvent.event_id(event),Layout.callbacks_count()) &&
          (U32.is_eq(target,0) || U32.is_ge(target,576) && U32.is_le(target,1984) &&
            U32.is_eq(((target - U32.min(target,576) : U32) % 128 : U32),0)),
          (do S.Mem<Unit>:
            relay : Value.Value <- load_event_value(88)
            listeners : +List<EventListener.ListenerRow> <- load_listeners(U32.to_nat(count),8192)
            result : RunEvent.Executed <- RunEvent.call_scalar(listeners,event,parent,relay,U32.is_ne(fast_exit,0))
            save_run(result)),S.raise(Unit,3))),S.raise(Unit,3))

def load_vector_targets(n: Nat, +at: U32) -> S.Mem<+List<U32>>:
  match n:
    case 0n: S.Mem.pure(+List<U32>,Nil{})
    case 1n+p:
      do S.Mem<+List<U32>>:
        +target : U32 <- S.get(at)
        S.branch(+List<U32>,U32.is_ge(target,576) && U32.is_le(target,1984) &&
          U32.is_eq(((target - U32.min(target,576) : U32) % 128 : U32),0),
          (do S.Mem<+List<U32>>:
            tail : +List<U32> <- load_vector_targets(p,(at + 1 : U32))
            return Con{target,tail}),S.raise(+List<U32>,3))

def load_vector_values(n: Nat, +at: U32) -> S.Mem<+List<Value.Value>>:
  match n:
    case 0n: S.Mem.pure(+List<Value.Value>,Nil{})
    case 1n+p:
      do S.Mem<+List<Value.Value>>:
        value : Value.Value <- load_event_value(at)
        tail : +List<Value.Value> <- load_vector_values(p,(at + 4 : U32))
        return Con{value,tail}

def save_vector_values(values: +List<Value.Value>, +at: U32) -> S.Mem<Unit>:
  match values:
    case Nil{}: S.Mem.pure(Unit,Unit{})
    case Con{head,tail}:
      do S.Mem<Unit>:
        save_list(event_value_fields(head),at)
        save_vector_values(tail,(at + 4 : U32))

def vector_indices(listeners: +List<EventListener.ListenerRow>, +size: U32) -> Bool:
  match listeners:
    case Nil{}: True{}
    case Con{head,tail}: U32.is_lt(VectorEvent.listener_index(head),size) && vector_indices(tail,size)

def save_vector(result: VectorEvent.VectorExecuted) -> S.Mem<Unit>:
  VectorEvent.VectorResult{current,values,+visited} = result
  do S.Mem<Unit>:
    save_context(Context.VM{current,Nil{},0},10216)
    save_vector_values(values,96)
    S.put(16,L.length(visited))
    save_list(visited,13824)

def vector_event_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +count : U32 <- S.get(32)
    +fast_exit : U32 <- S.get(33)
    +size : U32 <- S.get(34)
    +has_relay : U32 <- S.get(35)
    mode : U32 <- S.get(4)
    S.branch(Unit,U32.is_le(count,64) && U32.is_le(fast_exit,1) && U32.is_le(mode,1) &&
      U32.is_le(size,12) && U32.is_le(has_relay,1),
      (do S.Mem<Unit>:
        parent : Context.Current <- load_context()
        +event : Context.EventData <- load_scope_event(104)
        S.branch(Unit,U32.is_gt(SingleEvent.event_id(event),0) &&
          U32.is_lt(SingleEvent.event_id(event),Layout.callbacks_count()),
          (do S.Mem<Unit>:
            targets : +List<U32> <- load_vector_targets(U32.to_nat(size),144)
            values : +List<Value.Value> <- load_vector_values(U32.to_nat(size),160)
            +listeners : +List<EventListener.ListenerRow> <- load_listeners(U32.to_nat(count),8192)
            S.branch(Unit,vector_indices(listeners,size),
              (do S.Mem<Unit>:
                result : VectorEvent.VectorExecuted <- VectorEvent.call(listeners,targets,values,event,parent,
                  U32.is_ne(has_relay,0),U32.is_ne(fast_exit,0))
                save_vector(result)
                S.put(18,size)),S.raise(Unit,3))),S.raise(Unit,3))),S.raise(Unit,3))

def save_priority_fields(listener: EventListener.ListenerRow) -> S.Mem<Unit>:
  EventListener.ListenerData{_,Registry.H{callback,tag,order,priority,sub,value},_,_,_,_,_,_,_} = listener
  save_list([callback,tag,order,priority,sub,value],72)

def priority_lookup(lookup: Registry.Lookup, effect: Registry.Effect, callback: U32, state: U32,
  scope: U32, creation: U32, holder: ListenerPriority.HolderFacts, index: U32) -> S.Mem<Unit>:
  match lookup:
    case Registry.Missing{}: S.put(16,0)
    case Registry.Found{handler}:
      do S.Mem<Unit>:
        +listener : EventListener.ListenerRow <- ListenerPriority.resolve(effect,handler,callback,state,scope,creation,holder,index)
        save_list(RunEvent.key_fields(listener,0),64)
        save_priority_fields(listener)
        S.put(16,1)

def priority_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +family : U32 <- S.get(32)
    id : U32 <- S.get(33)
    +callback : U32 <- S.get(34)
    state : U32 <- S.get(35)
    +scope : U32 <- S.get(36)
    creation : U32 <- S.get(37)
    +holder : U32 <- S.get(38)
    +pokemon : U32 <- S.get(39)
    +cached : U32 <- S.get(40)
    +unboosted : U32 <- S.get(41)
    +rank : U32 <- S.get(42)
    +ability_order : U32 <- S.get(43)
    index : U32 <- S.get(44)
    +effect : Registry.Effect = effect_kind(U32.to_nat(U32.min(family,5)),id)
    S.branch(Unit,effect_family(family) && Registry.valid(effect) && U32.is_gt(callback,0) &&
      U32.is_lt(callback,Layout.callbacks_count()) && U32.is_le(scope,4) && U32.is_le(pokemon,1) &&
      U32.is_le(cached,16383) && U32.is_le(unboosted,16383) && U32.is_le(rank,2) && U32.is_lt(ability_order,4294967295) &&
      (U32.is_eq(pokemon,0) && U32.is_eq(holder,0) || U32.is_eq(pokemon,1) &&
        U32.is_ge(holder,576) && U32.is_le(holder,1984) &&
        U32.is_eq(((holder - U32.min(holder,576) : U32) % 128 : U32),0)),
      (do S.Mem<Unit>:
        lookup : Registry.Lookup <- Registry.callback(effect,callback,U32.is_ne(pokemon,0))
        priority_lookup(lookup,effect,callback,state,scope,creation,
          ListenerPriority.HolderData{holder,U32.is_ne(pokemon,0),cached,unboosted,rank,
            (ability_order + 1 : U32),Bool.pick(Value.Value,U32.is_ne(pokemon,0),
              Value.VObject{1,holder},Value.VObject{4,0})},index)),S.raise(Unit,3))

def load_effect_fields(n: Nat, +at: U32, fields: +List<Effects.Field>) -> S.Mem<+List<Effects.Field>>:
  match n:
    case 0n: S.Mem.pure(+List<Effects.Field>,fields)
    case 1n+p:
      do S.Mem<+List<Effects.Field>>:
        key : U32 <- S.get(at)
        value : Value.Value <- load_event_value((at + 1 : U32))
        load_effect_fields(p,(at + 5 : U32),Effects.field_set(fields,key,value))

def effect_field_count(fields: +List<Effects.Field>) -> U32:
  match fields:
    case Nil{}: 0
    case Con{_,tail}: (1 + effect_field_count(tail) : U32)

def save_effect_fields(fields: +List<Effects.Field>, +at: U32) -> S.Mem<Unit>:
  match fields:
    case Nil{}: S.Mem.pure(Unit,Unit{})
    case Con{Effects.FieldData{key,value},tail}:
      do S.Mem<Unit>:
        S.put(at,key)
        save_list(event_value_fields(value),(at + 1 : U32))
        save_effect_fields(tail,(at + 5 : U32))

def save_effect_meta(heap: Effects.Heap) -> S.Mem<Unit>:
  Effects.HeapData{next,counter,_,_} = heap
  do S.Mem<Unit>:
    S.put(17,next)
    S.put(18,counter)

def save_effect_object(+reference: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +heap : Effects.Heap <- S.heap()
    save_effect_meta(heap)
    S.put(16,reference)
    S.branch(Unit,U32.is_eq(reference,0),S.put(19,0),
      (do S.Mem<Unit>:
        +fields : +List<Effects.Field> <- S.from_result(+List<Effects.Field>,Effects.get_fields(heap,reference))
        +size : U32 = effect_field_count(fields)
        S.branch(Unit,U32.is_le(size,128),
          (do S.Mem<Unit>:
            S.put(19,size)
            save_effect_fields(fields,8192)),S.raise(Unit,3))))

def save_effect_allocation(pair: Effects.Allocated) -> S.Mem<Unit>:
  Effects.Allocation{heap,+reference} = pair
  do S.Mem<Unit>:
    S.set_heap(heap)
    save_list(event_value_fields(Value.VObject{7,reference}),64)
    save_effect_object(reference)

def load_effect_order(explicit: U32) -> S.Mem<Effects.InitialOrder>:
  S.branch(Effects.InitialOrder,U32.is_eq(explicit,0),S.Mem.pure(Effects.InitialOrder,Effects.AutomaticOrder{}),
    (do S.Mem<Effects.InitialOrder>:
      value : Value.Value <- load_event_value(38)
      return Effects.ExplicitOrder{value}))

def create_effect(clone: Bool, reference: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +size : U32 <- S.get(34)
    +pokemon : U32 <- S.get(35)
    +active : U32 <- S.get(36)
    +explicit : U32 <- S.get(37)
    S.branch(Unit,U32.is_le(size,128) && U32.is_le(pokemon,1) && U32.is_le(active,1) && U32.is_le(explicit,1),
      (do S.Mem<Unit>:
        +heap : Effects.Heap <- S.heap()
        +fields : +List<Effects.Field> <- load_effect_fields(U32.to_nat(size),8192,Nil{})
        +order : Effects.InitialOrder <- load_effect_order(explicit)
        pair : Effects.Allocated <- S.from_result(Effects.Allocated,
          Bool.pick(Result<&2,&2,U32,Effects.Allocated>,clone,
            Effects.clone(heap,reference,fields,order,U32.is_ne(pokemon,0),U32.is_ne(active,0)),
            Effects.allocate(heap,fields,order,U32.is_ne(pokemon,0),U32.is_ne(active,0))))
        save_effect_allocation(pair)),S.raise(Unit,3))

def mutate_effect(mode: Nat, +heap: Effects.Heap, +reference: U32, key: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    S.put(16,reference)
    save_list(event_value_fields(Value.VUndefined{}),64)
    next : Effects.Heap <- S.from_result(Effects.Heap,
      Bool.pick(Result<&2,&2,U32,Effects.Heap>,Nat.is_eq(mode,4n),Effects.delete(heap,reference,key),Effects.clear(heap,reference)))
    S.set_heap(next)
    save_effect_object(reference)

def import_effect(+reference: U32, +size: U32) -> S.Mem<Unit>:
  S.branch(Unit,U32.is_le(size,128),
    (do S.Mem<Unit>:
      heap : Effects.Heap <- S.heap()
      fields : +List<Effects.Field> <- load_effect_fields(U32.to_nat(size),8192,Nil{})
      next : Effects.Heap <- S.from_result(Effects.Heap,Effects.import_object(heap,reference,fields))
      S.set_heap(next)
      save_list(event_value_fields(Value.VObject{7,reference}),64)
      save_effect_object(reference)),S.raise(Unit,3))

def effect_store_mode(mode: Nat, +reference: U32, key: U32) -> S.Mem<Unit>:
  match mode:
    case 0n:
      do S.Mem<Unit>:
        S.set_heap(Effects.empty(reference))
        save_list(event_value_fields(Value.VUndefined{}),64)
        save_effect_object(0)
    case 1n: create_effect(False{},reference)
    case 2n:
      do S.Mem<Unit>:
        heap : Effects.Heap <- S.heap()
        value : Value.Value <- S.from_result(Value.Value,Effects.read(heap,reference,key))
        save_list(event_value_fields(value),64)
        save_effect_object(reference)
    case 3n:
      do S.Mem<Unit>:
        heap : Effects.Heap <- S.heap()
        value : Value.Value <- load_event_value(64)
        next : Effects.Heap <- S.from_result(Effects.Heap,Effects.write(heap,reference,key,value))
        S.set_heap(next)
        save_list(event_value_fields(Value.VUndefined{}),64)
        save_effect_object(reference)
    case 4n:
      do S.Mem<Unit>:
        heap : Effects.Heap <- S.heap()
        mutate_effect(4n,heap,reference,key)
    case 5n:
      do S.Mem<Unit>:
        heap : Effects.Heap <- S.heap()
        mutate_effect(5n,heap,reference,key)
    case 6n: create_effect(True{},reference)
    case 7n:
      do S.Mem<Unit>:
        save_list(event_value_fields(Value.VUndefined{}),64)
        save_effect_object(reference)
    case 8n: import_effect(reference,key)
    case _: S.raise(Unit,3)

def effect_store_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    mode : U32 <- S.get(32)
    reference : U32 <- S.get(33)
    key : U32 <- S.get(34)
    effect_store_mode(U32.to_nat(mode),reference,key)

def identity_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +family : U32 <- S.get(32)
    id : U32 <- S.get(33)
    +effect : Registry.Effect = effect_kind(U32.to_nat(U32.min(family,5)),id)
    S.branch(Unit,effect_family(family) && Registry.valid(effect),
      (do S.Mem<Unit>:
        text : U32 <- Registry.identity(effect,0)
        name : U32 <- Registry.identity(effect,1)
        S.put(16,text)
        S.put(17,name)),S.raise(Unit,3))

def end_fields(end: Collect.EndAction) -> +List<U32>:
  match end:
    case Collect.NoEnd{}: [0,0,0,0]
    case Collect.ClearStatus{at}: [1,0,at,0]
    case Collect.RemoveVolatile{at,id}: [2,0,at,id]
    case Collect.ClearAbility{at}: [3,0,at,0]
    case Collect.ClearItem{at}: [4,0,at,0]
    case Collect.RemoveSlot{side,at,id}: [5,side,at,id]
    case Collect.RemoveSide{side,id}: [6,side,0,id]
    case Collect.RemovePseudo{id}: [7,0,0,id]
    case Collect.ClearWeather{}: [8,0,0,0]
    case Collect.ClearTerrain{}: [9,0,0,0]

def object_fields(value: Value.Value) -> +List<U32>:
  match value:
    case Value.VObject{family,id}: [family,id]
    case _: [0,0]

def collect_fields(+entry: Collect.Collected, index: U32) -> +List<U32>:
  Collect.CollectedData{+listener,present,end,target,scope,requested} = entry
  EventListener.ListenerData{effect,Registry.H{callback,tag,_,_,_,value},state,_,_,_,_,_,holder} = listener
  L.append(context_effect_fields(Context.BoundEffect{effect}),L.append([Bool.to_u32(present),requested,callback,tag,value,state],
    L.append(object_fields(holder),L.append(end_fields(end),L.append(object_fields(target),
      L.append([scope,0,0,0,0,0,0,0],RunEvent.key_fields(listener,index)))))))

def save_collected(xs: +List<Collect.Collected>, +index: U32) -> S.Mem<Unit>:
  match xs:
    case Nil{}: S.Mem.pure(Unit,Unit{})
    case Con{head,tail}:
      do S.Mem<Unit>:
        save_list(collect_fields(head,index),(8192 + index * 32 : U32))
        save_collected(tail,(index + 1 : U32))

def save_collection(+xs: +List<Collect.Collected>) -> S.Mem<Unit>:
  +count : U32 = Collect.size(xs)
  S.branch(Unit,U32.is_le(count,256),
    (do S.Mem<Unit>:
      S.put(16,count)
      save_collected(xs,0)),S.raise(Unit,3))

def valid_pokemon(+at: U32) -> Bool:
  U32.is_ge(at,576) && U32.is_le(at,1984) && U32.is_eq(((at - U32.min(at,576) : U32) % 128 : U32),0)

def optional_valid_pokemon(+at: U32) -> Bool:
  U32.is_eq(at,0) || valid_pokemon(at)

def collect_mode(mode: Nat, family: U32, target: U32, callback: U32, duration: Bool,
  custom: U32, source: U32, size: U32) -> S.Mem<+List<Collect.Collected>>:
  match mode:
    case 0n: Collect.pokemon(target,callback,duration)
    case 1n: Collect.side(target,callback,duration,custom)
    case 2n: Collect.field(callback,duration,custom)
    case 3n: Collect.battle(callback,duration,custom)
    case 4n: Collect.scalar(Value.VObject{family,target},callback,source)
    case 5n:
      do S.Mem<+List<Collect.Collected>>:
        targets : +List<U32> <- load_vector_targets(U32.to_nat(size),160)
        Collect.vector(targets,callback,source,0)
    case _: S.raise(+List<Collect.Collected>,3)

def collection_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +mode : U32 <- S.get(32)
    +family : U32 <- S.get(33)
    +target : U32 <- S.get(34)
    +callback : U32 <- S.get(35)
    +duration : U32 <- S.get(36)
    +custom : U32 <- S.get(37)
    +source : U32 <- S.get(38)
    +size : U32 <- S.get(39)
    order_count : U32 <- S.get(530)
    +target_ok : Bool = U32.is_eq(family,1) && valid_pokemon(target) ||
      U32.is_eq(family,2) && U32.is_lt(target,2) || U32.is_eq(family,4) && U32.is_eq(target,0)
    +mode_ok : Bool = U32.is_eq(mode,0) && U32.is_eq(family,1) && valid_pokemon(target) && U32.is_eq(custom,0) ||
      U32.is_eq(mode,1) && U32.is_eq(family,2) && U32.is_lt(target,2) ||
      U32.is_eq(mode,2) && U32.is_eq(family,3) && U32.is_eq(target,0) ||
      U32.is_eq(mode,3) && U32.is_eq(family,4) && U32.is_eq(target,0) ||
      U32.is_eq(mode,4) && target_ok && U32.is_eq(duration,0) && U32.is_eq(custom,0) ||
      U32.is_eq(mode,5) && U32.is_eq(duration,0) && U32.is_eq(custom,0)
    S.branch(Unit,mode_ok && U32.is_gt(callback,0) && U32.is_lt(callback,Layout.callbacks_count()) &&
      U32.is_le(duration,1) && optional_valid_pokemon(custom) && optional_valid_pokemon(source) &&
      U32.is_le(size,12) && U32.is_le(order_count,2),
      (do S.Mem<Unit>:
        xs : +List<Collect.Collected> <- collect_mode(U32.to_nat(mode),family,target,callback,
          U32.is_ne(duration,0),custom,source,size)
        save_collection(xs)),S.raise(Unit,3))

def valid_event_target(value: Value.Value) -> Bool:
  match value:
    case Value.VObject{1,at}: valid_pokemon(at)
    case Value.VObject{2,id}: U32.is_lt(id,2)
    case Value.VObject{4,id}: U32.is_eq(id,0)
    case _: Bool.not(Value.truthy(value))

def valid_event_source(value: Value.Value) -> Bool:
  match value:
    case Value.VObject{1,at}: valid_pokemon(at)
    case _: True{}

def event_source(event: Context.EventData) -> Value.Value:
  Context.E{_,_,value,_,_} = event
  value

def valid_on_effect_ref(ref: Context.EffectRef) -> Bool:
  match ref:
    case Context.EmptyEffect{}: True{}
    case Context.BoundEffect{effect}: Registry.valid(effect)

def collected_event_op(+vector: Bool) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +fast : U32 <- S.get(32)
    +on_effect : U32 <- S.get(33)
    +size : U32 <- S.get(36)
    +has_relay : U32 <- S.get(37)
    rng_mode : U32 <- S.get(4)
    order_count : U32 <- S.get(530)
    S.branch(Unit,U32.is_le(fast,1) && U32.is_le(on_effect,1) && U32.is_le(size,12) &&
      U32.is_le(has_relay,1) && U32.is_le(rng_mode,1) && U32.is_le(order_count,2),
      (do S.Mem<Unit>:
        +event : Context.EventData <- load_scope_event(104)
        +parent : Context.Current <- load_context()
        +ref : Context.EffectRef <- load_context_ref(34)
        S.branch(Unit,U32.is_gt(SingleEvent.event_id(event),0) &&
          U32.is_lt(SingleEvent.event_id(event),Layout.callbacks_count()) && valid_on_effect_ref(ref) &&
          valid_event_source(event_source(event)) && (vector || valid_event_target(SingleEvent.target_value(event))),
          S.branch(Unit,vector,
            (do S.Mem<Unit>:
              targets : +List<U32> <- load_vector_targets(U32.to_nat(size),144)
              values : +List<Value.Value> <- load_vector_values(U32.to_nat(size),160)
              result : VectorEvent.VectorExecuted <- CollectedEvent.vector(targets,values,event,parent,
                U32.is_ne(has_relay,0),U32.is_ne(fast,0),U32.is_ne(on_effect,0),ref)
              save_vector(result)
              S.put(18,size)),
            (do S.Mem<Unit>:
              relay : Value.Value <- load_event_value(88)
              result : RunEvent.Executed <- CollectedEvent.scalar(event,parent,relay,
                U32.is_ne(fast,0),U32.is_ne(on_effect,0),ref)
              save_run(result))),S.raise(Unit,3))),S.raise(Unit,3))

def boost_table_mode(mode: Nat, +at: U32, stat: U32, value: Value.Value) -> S.Mem<Unit>:
  match mode:
    case 0n:
      do S.Mem<Unit>:
        reference : U32 <- BoostTable.clone(at)
        S.put(16,reference)
    case 1n:
      do S.Mem<Unit>:
        reference : U32 <- BoostTable.sparse(stat,value)
        S.put(16,reference)
    case 2n:
      do S.Mem<Unit>:
        stage : U32 <- S.from_result(U32,BoostTable.stage(value))
        S.put(16,stage)
    case _: S.raise(Unit,3)

def boost_table_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    mode : U32 <- S.get(32)
    +at : U32 <- S.get(33)
    +stat : U32 <- S.get(34)
    value : Value.Value <- load_event_value(88)
    S.branch(Unit,valid_pokemon(at) && U32.is_lt(stat,7),
      boost_table_mode(U32.to_nat(mode),at,stat,value),S.raise(Unit,3))

def live_stat_mode(mode: Nat, +at: U32, stat: U32, unboosted: Bool, unmodified: Bool,
  modifier: U32, user: U32, boost: Value.Value, parent: Context.Current) -> S.Mem<LiveStats.Computed>:
  match mode:
    case 0n: LiveStats.get(at,stat,unboosted,unmodified,parent)
    case 1n: LiveStats.calculate(at,stat,boost,modifier,user,parent)
    case 2n: LiveStats.action(at,parent)
    case 3n: LiveStats.update(at,parent)
    case 4n: LiveStats.best(at,unboosted,unmodified,parent)
    case 5n: LiveStats.weight(at,parent)
    case _: S.raise(LiveStats.Computed,3)

def save_live_stat(result: LiveStats.Computed) -> S.Mem<Unit>:
  LiveStats.ComputedData{current,number} = result
  do S.Mem<Unit>:
    S.put(16,number)
    save_context(Context.VM{current,Nil{},0},10216)

def live_stat_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    mode : U32 <- S.get(32)
    +at : U32 <- S.get(33)
    +stat : U32 <- S.get(34)
    +unboosted : U32 <- S.get(35)
    +unmodified : U32 <- S.get(36)
    modifier : U32 <- S.get(37)
    +user : U32 <- S.get(38)
    S.branch(Unit,valid_pokemon(at) && U32.is_lt(stat,5) && U32.is_le(unboosted,1) &&
      U32.is_le(unmodified,1) && optional_valid_pokemon(user),
      (do S.Mem<Unit>:
        parent : Context.Current <- load_context()
        boost : Value.Value <- load_event_value(88)
        result : LiveStats.Computed <- live_stat_mode(U32.to_nat(mode),at,stat,U32.is_ne(unboosted,0),
          U32.is_ne(unmodified,0),modifier,user,boost,parent)
        save_live_stat(result)),S.raise(Unit,3))

def weather_mode(mode: Nat, at: U32, current: Context.Current, ids: +List<U32>) -> S.Mem<U32>:
  match mode:
    case 0n: Weather.field()
    case 1n: Weather.pokemon(at,current)
    case 2n:
      do S.Mem<U32>:
        yes : Bool <- Weather.matches(ids)
        return Bool.to_u32(yes)
    case _: S.raise(U32,3)

def valid_weather_ids(ids: +List<U32>) -> Bool:
  match ids:
    case Nil{}: True{}
    case Con{head,tail}: U32.is_lt(head,Layout.conditions_count()) && valid_weather_ids(tail)

def weather_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    mode : U32 <- S.get(32)
    +at : U32 <- S.get(33)
    +count : U32 <- S.get(34)
    S.branch(Unit,valid_pokemon(at) && U32.is_le(count,8),
      (do S.Mem<Unit>:
        current : Context.Current <- load_context()
        +ids : +List<U32> <- C.read_list(U32.to_nat(count),160)
        S.branch(Unit,valid_weather_ids(ids),
          (do S.Mem<Unit>:
            value : U32 <- weather_mode(U32.to_nat(mode),at,current,ids)
            S.put(16,value)),S.raise(Unit,3))),S.raise(Unit,3))

def terrain_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +mode : U32 <- S.get(32)
    +count : U32 <- S.get(33)
    +requested : Value.Value <- load_event_value(88)
    +current : Context.Current <- load_context()
    S.branch(Unit,U32.is_le(mode,1) && U32.is_le(count,8) && valid_event_target(requested) &&
      valid_event_target(Terrain.parent_target(current)),
      (do S.Mem<Unit>:
        +ids : +List<U32> <- C.read_list(U32.to_nat(count),160)
        S.branch(Unit,valid_weather_ids(ids),
          (do S.Mem<Unit>:
            value : U32 <- S.branch(U32,U32.is_eq(mode,0),Terrain.effective(current,requested),
              (do S.Mem<U32>:
                yes : Bool <- Terrain.matches(ids,current,requested)
                return Bool.to_u32(yes)))
            S.put(16,value)),S.raise(Unit,3))),S.raise(Unit,3))

def array_mode(mode: Nat, +ref: U32, +argument: U32) -> S.Mem<Unit>:
  match mode:
    case 0n:
      S.branch(Unit,U32.is_le(argument,64),
        (do S.Mem<Unit>:
          values : +List<Value.Value> <- load_vector_values(U32.to_nat(argument),160)
          +allocated : U32 <- Values.make(values)
          save_list(event_value_fields(Value.VObject{8,allocated}),64)
          save_effect_object(allocated)),S.raise(Unit,3))
    case 1n:
      do S.Mem<Unit>:
        size : U32 <- Values.length(ref)
        save_list(event_value_fields(Effects.number(size)),64)
        save_effect_object(ref)
    case 2n:
      do S.Mem<Unit>:
        value : Value.Value <- Values.get(ref,argument)
        save_list(event_value_fields(value),64)
        save_effect_object(ref)
    case 3n:
      do S.Mem<Unit>:
        value : Value.Value <- load_event_value(88)
        size : U32 <- Values.push(ref,value)
        save_list(event_value_fields(Effects.number(size)),64)
        save_effect_object(ref)
    case 4n:
      do S.Mem<Unit>:
        units : +List<U32> <- TextMem.get(argument)
        +allocated : U32 <- Values.filter_text(ref,argument)
        save_list(event_value_fields(Value.VObject{8,allocated}),64)
        save_effect_object(allocated)
    case 5n:
      do S.Mem<Unit>:
        value : Value.Value <- load_event_value(88)
        +allocated : U32 <- Values.concat_one(ref,value)
        save_list(event_value_fields(Value.VObject{8,allocated}),64)
        save_effect_object(allocated)
    case 6n:
      do S.Mem<Unit>:
        units : +List<U32> <- TextMem.get(argument)
        yes : Bool <- Values.includes_text(ref,argument)
        save_list(event_value_fields(Value.VBool{yes}),64)
        save_effect_object(ref)
    case 7n:
      do S.Mem<Unit>:
        +allocated : U32 <- Values.clone(ref)
        save_list(event_value_fields(Value.VObject{8,allocated}),64)
        save_effect_object(allocated)
    case 8n:
      do S.Mem<Unit>:
        value : Value.Value <- load_event_value(88)
        result : Value.Value <- Values.set_index(ref,argument,value)
        save_list(event_value_fields(result),64)
        save_effect_object(ref)
    case _: S.raise(Unit,3)

def array_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    mode : U32 <- S.get(32)
    ref : U32 <- S.get(33)
    argument : U32 <- S.get(34)
    array_mode(U32.to_nat(mode),ref,argument)

def validate_type_texts(ids: +List<U32>) -> S.Mem<Unit>:
  match ids:
    case Nil{}: S.Mem.pure(Unit,Unit{})
    case Con{id,tail}:
      do S.Mem<Unit>:
        units : +List<U32> <- TextMem.get(id)
        validate_type_texts(tail)

def types_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +mode : U32 <- S.get(32)
    +at : U32 <- S.get(33)
    +exclude : U32 <- S.get(34)
    +pretera : U32 <- S.get(35)
    +count : U32 <- S.get(36)
    S.branch(Unit,U32.is_le(mode,1) && valid_pokemon(at) && U32.is_le(exclude,1) &&
      U32.is_le(pretera,1) && U32.is_le(count,64),
      (do S.Mem<Unit>:
        +current : Context.Current <- load_context()
        S.branch(Unit,U32.is_eq(mode,0),
          (do S.Mem<Unit>:
            +ref : U32 <- Types.get(at,current,U32.is_ne(exclude,0),U32.is_ne(pretera,0))
            save_list(event_value_fields(Value.VObject{8,ref}),64)
            save_effect_object(ref)),
          (do S.Mem<Unit>:
            +ids : +List<U32> <- C.read_list(U32.to_nat(count),160)
            validate_type_texts(ids)
            yes : Bool <- Types.has(at,current,ids)
            save_list(event_value_fields(Value.VBool{yes}),64)))
        save_context(Context.VM{current,Nil{},0},10216)),S.raise(Unit,3))

def grounded_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +at : U32 <- S.get(32)
    +negate : U32 <- S.get(33)
    S.branch(Unit,valid_pokemon(at) && U32.is_le(negate,1),
      (do S.Mem<Unit>:
        +current : Context.Current <- load_context()
        value : Value.Value <- Grounded.get(at,current,U32.is_ne(negate,0))
        save_list(event_value_fields(value),64)
        save_context(Context.VM{current,Nil{},0},10216)),S.raise(Unit,3))

def save_text(+id: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +units : +List<U32> <- TextMem.get(id)
    S.branch(Unit,U32.is_le(L.length(units),4096),
      (do S.Mem<Unit>:
        +heap : Effects.Heap <- S.heap()
        save_effect_meta(heap)
        S.put(16,id)
        S.put(19,L.length(units))
        S.put(20,TextStore.dynamic_count(Effects.text_heap(heap)))
        S.put(21,TextStore.hash(units))
        save_list(event_value_fields(Value.VText{id}),64)
        save_list(units,8192)),S.raise(Unit,3))

def text_mode(mode: Nat, +argument: U32) -> S.Mem<Unit>:
  match mode:
    case 0n: save_text(argument)
    case 1n:
      S.branch(Unit,U32.is_le(argument,1024),
        (do S.Mem<Unit>:
          units : +List<U32> <- C.read_list(U32.to_nat(argument),160)
          id : U32 <- TextMem.intern(units)
          save_text(id)),S.raise(Unit,3))
    case _: S.raise(Unit,3)

def text_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    mode : U32 <- S.get(32)
    argument : U32 <- S.get(33)
    text_mode(U32.to_nat(mode),argument)

def type_change_mode(mode: Nat, at: U32, enforce: Bool, argument: U32) -> S.Mem<Bool>:
  match mode:
    case 0n: TypeChange.set(at,TypeChange.StringType{argument},enforce)
    case 1n: TypeChange.set(at,TypeChange.ArrayTypes{argument},enforce)
    case 2n: TypeChange.add(at,argument)
    case _: S.raise(Bool,3)

def type_change_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    mode : U32 <- S.get(32)
    +at : U32 <- S.get(33)
    +enforce : U32 <- S.get(34)
    argument : U32 <- S.get(35)
    S.branch(Unit,valid_pokemon(at) && U32.is_le(enforce,1),
      (do S.Mem<Unit>:
        result : Bool <- type_change_mode(U32.to_nat(mode),at,U32.is_ne(enforce,0),argument)
        save_list(event_value_fields(Value.VBool{result}),64)),S.raise(Unit,3))

def stat_table_mode(mode: Nat, +at: U32, index: U32) -> S.Mem<Unit>:
  match mode:
    case 0n: StatTable.attach(at)
    case 1n:
      do S.Mem<Unit>:
        value : U32 <- StatTable.current(at,index)
        S.put(16,value)
    case _: S.raise(Unit,3)

def stat_table_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    mode : U32 <- S.get(32)
    +at : U32 <- S.get(33)
    +index : U32 <- S.get(34)
    S.branch(Unit,valid_pokemon(at) && U32.is_lt(index,5),
      stat_table_mode(U32.to_nat(mode),at,index),S.raise(Unit,3))

def species_change_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +at : U32 <- S.get(32)
    +display : U32 <- S.get(33)
    +is_transform : U32 <- S.get(34)
    +use_default : U32 <- S.get(35)
    S.branch(Unit,valid_pokemon(at) && U32.is_gt(display,0) &&
      U32.is_lt(display,Layout.species_count()) && U32.is_le(is_transform,1) && U32.is_le(use_default,1),
      (do S.Mem<Unit>:
        +parent : Context.Current <- load_context()
        explicit_source : Value.Value <- load_event_value(88)
        result : Callbacks.Returned <- SpeciesChange.set(at,display,
          Bool.pick(Value.Value,U32.is_ne(use_default,0),SpeciesChange.default_source(parent),explicit_source),
          U32.is_ne(is_transform,0),parent)
        save_callback(result)),S.raise(Unit,3))

def move_slots_mode(mode: Nat, +at: U32, argument: U32, hp_type: U32) -> S.Mem<Unit>:
  match mode:
    case 0n: MoveSlots.attach(at)
    case 1n:
      do S.Mem<Unit>:
        reference : U32 <- MoveSlots.peek(at,argument)
        S.put(16,reference)
    case 2n:
      do S.Mem<Unit>:
        reference : U32 <- MoveSlots.lookup(at,argument)
        S.put(16,reference)
    case 3n:
      do S.Mem<Unit>:
        reference : U32 <- MoveSlots.restore(at)
        S.put(16,reference)
    case 4n:
      do S.Mem<Unit>:
        reference : U32 <- MoveSlots.install(at,argument)
        S.put(16,reference)
    case 5n:
      do S.Mem<Unit>:
        reference : U32 <- MoveSlots.transform(at,argument,hp_type)
        S.put(16,reference)
    case _: S.raise(Unit,3)

def move_slots_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +mode : U32 <- S.get(32)
    +at : U32 <- S.get(33)
    +argument : U32 <- S.get(34)
    hp_type : U32 <- S.get(35)
    S.branch(Unit,valid_pokemon(at) && (U32.is_ne(mode,2) ||
      U32.is_gt(argument,0) && U32.is_lt(argument,Layout.moves_count())) &&
      (U32.is_ne(mode,5) || valid_pokemon(argument)),
      move_slots_mode(U32.to_nat(mode),at,argument,hp_type),S.raise(Unit,3))

def pokemon_boost_table_mode(mode: Nat, +at: U32, argument: U32) -> S.Mem<Unit>:
  match mode:
    case 0n: BoostTable.attach(at)
    case 1n:
      do S.Mem<Unit>:
        stage : U32 <- BoostTable.current(at,argument)
        S.put(16,stage)
    case 2n:
      do S.Mem<Unit>:
        reference : U32 <- BoostTable.fresh(at)
        S.put(16,reference)
    case 3n:
      do S.Mem<Unit>:
        reference : U32 <- BoostTable.share(at,argument)
        S.put(16,reference)
    case 4n:
      do S.Mem<Unit>:
        reference : U32 <- BoostTable.copy_from(at,argument)
        S.put(16,reference)
    case _: S.raise(Unit,3)

def pokemon_boost_table_op() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +mode : U32 <- S.get(32)
    +at : U32 <- S.get(33)
    +argument : U32 <- S.get(34)
    S.branch(Unit,valid_pokemon(at) &&
      (U32.is_ne(mode,1) || U32.is_lt(argument,7)) &&
      (U32.is_lt(mode,3) || U32.is_gt(mode,4) || valid_pokemon(argument)),
      pokemon_boost_table_mode(U32.to_nat(mode),at,argument),S.raise(Unit,3))

def dispatch(op: Nat) -> S.Mem<Unit>:
  match op:
    case 1n: rng_op()
    case 2n: math_op()
    case 3n: catalog_op()
    case 4n: counter_op()
    case 5n: ability_op()
    case 6n: empty_sample_op()
    case 7n: item_op()
    case 8n: cull_op()
    case 9n: moves_op()
    case 10n: set_op()
    case 11n: team_op()
    case 12n: init_pokemon_op()
    case 13n: stats_op()
    case 14n: init_teams_op()
    case 15n: sort_queue_op(False{})
    case 16n: damage_op()
    case 17n: confusion_op()
    case 18n: event_metadata_op()
    case 19n: pokemon_pp_op()
    case 20n: pokemon_boost_op()
    case 21n: pokemon_disable_op()
    case 22n: registry_op(False{})
    case 23n: registry_op(True{})
    case 24n: sodium_seed_op()
    case 25n: random_op()
    case 26n: sodium_raw_op()
    case 27n: hp_op()
    case 28n: suppression_op()
    case 29n: moves_view_op()
    case 30n: move_request_op()
    case 31n: event_gate_op()
    case 32n: event_value_op()
    case 33n: context_op()
    case 34n: callback_op()
    case 35n: single_event_op()
    case 36n: sort_queue_op(True{})
    case 37n: run_event_op()
    case 38n: vector_event_op()
    case 39n: priority_op()
    case 40n: effect_store_op()
    case 41n: identity_op()
    case 42n: collection_op()
    case 43n: collected_event_op(False{})
    case 44n: collected_event_op(True{})
    case 45n: boost_table_op()
    case 46n: live_stat_op()
    case 47n: weather_op()
    case 48n: terrain_op()
    case 49n: array_op()
    case 50n: types_op()
    case 51n: TypeInit.party(12n,0)
    case 52n: grounded_op()
    case 53n: text_op()
    case 54n: type_change_op()
    case 55n: stat_table_op()
    case 56n: species_change_op()
    case 57n: move_slots_op()
    case 58n: pokemon_boost_table_op()
    case _: S.put(1, 1)

def operation() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    op : U32 <- S.get(0)
    S.put(1, 0)
    dispatch(U32.to_nat(op))

def finish(pair: S.World & Result<&2, &2, U32, Unit>) -> S.World:
  (a, result) = pair
  S.MemoryWorld{words,effects} = a
  match result:
    case Done{_}: S.MemoryWorld{words,effects}
    case Fail{code}: S.MemoryWorld{words[1] <- code,effects}

def execute(a: S.World) -> S.World:
  finish(S.run(Unit, operation(), a))
