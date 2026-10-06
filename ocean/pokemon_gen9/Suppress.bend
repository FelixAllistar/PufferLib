import Base
import ./Mem.bend as S
import ./Catalog.bend as C
import ./data/abilities.bend as A
import ./data/items.bend as I
import ./data/conditions.bend as D
import ./EffectInit.bend as Roots
import ./EffectMem.bend as Effects
import ./EffectStore.bend as Store
import ./EventValue.bend as V

# Pokemon row 23: active/transformed/Gastro Acid/Embargo/Commanding/ending,
# followed by side.active membership (64). The source keeps membership separate
# from isActive and excludes fainted holders from getAllActive.
# No public projection may infer these private flags before a revelation.
type Facts is Data:
  F{ability: U32, item: U32, flags: U32, fainted: Bool}

def bit(flags: U32, mask: U32) -> Bool:
  U32.is_ne((flags .&. mask : U32), 0)

def held(facts: Facts) -> U32:
  F{_, item, _, _} = facts
  item

def active(facts: Facts) -> Bool:
  F{_, _, flags, _} = facts
  bit(flags, 1)

# Persistent abilityState is authoritative when supplied. Bit 32 is retained
# only for older diagnostics with no object root; it is never written back.
def ending_flags(flags: U32, value: V.Value) -> U32:
  ((flags .&. 4294967263 : U32) .|. Bool.pick(U32,V.truthy(value),32,0) : U32)

def live_flags(+at: U32, +flags: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    +reference : U32 <- S.get(Roots.ability_state(at))
    S.branch(U32,U32.is_eq(reference,0),S.Mem.pure(U32,flags),
      (do S.Mem<U32>:
        value : V.Value <- Effects.read(reference,Store.ending_key())
        return ending_flags(flags,value)))

def read(+at: U32) -> S.Mem<Facts>:
  do S.Mem<Facts>:
    ability : U32 <- S.get((at + 5 : U32))
    item : U32 <- S.get((at + 6 : U32))
    flags : U32 <- S.get((at + 23 : U32))
    fainted : U32 <- S.get((at + 81 : U32))
    resolved : U32 <- live_flags(at,flags)
    return F{ability, item, resolved, U32.is_ne(fainted, 0)}

def gas_active(facts: Facts) -> Bool:
  F{ability, _, +flags, fainted} = facts
  U32.is_eq(ability, A.abilities_neutralizinggas()) && bit(flags, 64) && Bool.not(fainted) &&
    Bool.not(bit(flags, 4) || bit(flags, 2) || bit(flags, 32))

def gas_scan(n: Nat, +at: U32) -> S.Mem<Bool>:
  match n:
    case 0n: S.Mem.pure(Bool, False{})
    case 1n+p:
      do S.Mem<Bool>:
        facts : Facts <- read(at)
        S.branch(Bool, gas_active(facts), S.Mem.pure(Bool, True{}), gas_scan(p, (at + 128 : U32)))

# Ability Shield has ignoreKlutz in the pinned Dex. Its hasItem test therefore
# depends only on active/Embargo/Magic Room, breaking the source's ability/item
# call cycle without changing any ability's suppression order.
def shield(facts: Facts, magic_room: Bool) -> Bool:
  F{_, item, +flags, _} = facts
  U32.is_eq(item, I.items_abilityshield()) && bit(flags, 1) &&
    Bool.not(bit(flags, 8) || magic_room)

def ignoring_ability(+facts: Facts, magic_room: Bool, gas: Bool,
  cantsuppress: Bool, notransform: Bool) -> Bool:
  F{ability, _, +flags, _} = facts
  Bool.not(bit(flags, 1)) || notransform && bit(flags, 2) ||
    Bool.not(cantsuppress) && (bit(flags, 4) ||
      Bool.not(shield(facts, magic_room)) && U32.is_ne(ability, A.abilities_neutralizinggas()) &&
      Bool.not(bit(flags, 16)) && gas)

def ignoring_item(facts: Facts, magic_room: Bool, fling: Bool,
  primal: Bool, ignore_klutz: Bool, ability_ignored: Bool) -> Bool:
  F{ability, _, +flags, _} = facts
  Bool.not(primal) && (Bool.not(bit(flags, 1)) || bit(flags, 8) || magic_room ||
    (fling || Bool.not(ignore_klutz)) && U32.is_eq(ability, A.abilities_klutz()) && Bool.not(ability_ignored))

def ability_with_gas(+facts: Facts, magic: Bool, gas: Bool) -> S.Mem<Bool>:
  F{+id, _, _, _} = facts
  do S.Mem<Bool>:
    cant : U32 <- C.ability(id, 3)
    transform : U32 <- C.ability(id, 4)
    return ignoring_ability(facts, magic, gas, U32.is_ne(cant, 0), U32.is_ne(transform, 0))

def magic_room() -> S.Mem<Bool>:
  do S.Mem<Bool>:
    +map : U32 <- S.get(Roots.pseudo_weather_map())
    S.branch(Bool,U32.is_eq(map,0),
      (do S.Mem<Bool>:
        resolved : U32 <- S.get(544)
        return U32.is_ne(resolved,0)),
      (do S.Mem<Bool>:
        value : V.Value <- Effects.read(map,Roots.condition_key(D.conditions_magicroom()))
        return V.truthy(value)))

def ability(+at: U32) -> S.Mem<Bool>:
  do S.Mem<Bool>:
    facts : Facts <- read(at)
    gas : Bool <- gas_scan(12n, 576)
    magic : Bool <- magic_room()
    ability_with_gas(facts, magic, gas)

def item(+at: U32, fling: Bool) -> S.Mem<Bool>:
  do S.Mem<Bool>:
    +facts : Facts <- read(at)
    +magic : Bool <- magic_room()
    gas : Bool <- gas_scan(12n, 576)
    ignored : Bool <- ability_with_gas(facts, magic, gas)
    +id : U32 = held(facts)
    primal : U32 <- C.item(id, 2)
    klutz : U32 <- C.item(id, 3)
    return ignoring_item(facts, magic, fling,
      U32.is_ne(primal, 0), U32.is_ne(klutz, 0), ignored)

def weather_candidate(+facts: Facts) -> Bool:
  F{+ability, _, +flags, fainted} = facts
  bit(flags, 64) && Bool.not(fainted || bit(flags, 32)) &&
    (U32.is_eq(ability, A.abilities_airlock()) || U32.is_eq(ability, A.abilities_cloudnine()))

def weather_scan(n: Nat, +at: U32, +magic: Bool, +gas: Bool) -> S.Mem<Bool>:
  match n:
    case 0n: S.Mem.pure(Bool, False{})
    case 1n+p:
      do S.Mem<Bool>:
        +facts : Facts <- read(at)
        ignored : Bool <- ability_with_gas(facts, magic, gas)
        S.branch(Bool, weather_candidate(facts) && Bool.not(ignored),
          S.Mem.pure(Bool, True{}), weather_scan(p, (at + 128 : U32), magic, gas))

def weather() -> S.Mem<Bool>:
  do S.Mem<Bool>:
    gas : Bool <- gas_scan(12n, 576)
    magic : Bool <- magic_room()
    weather_scan(12n, 576, magic, gas)

# Battle.activePokemon address 548, activeMove.ignoreAbility 549. Gen 9 exempts
# the active attacker itself and any target with an effective Ability Shield.
def breaker(+at: U32) -> S.Mem<Bool>:
  do S.Mem<Bool>:
    +actor : U32 <- S.get(548)
    ignore : U32 <- S.get(549)
    S.branch(Bool, U32.is_ne(actor, 0) && U32.is_ne(actor, at) && U32.is_ne(ignore, 0),
      (do S.Mem<Bool>:
        facts : Facts <- read(actor)
        target : Facts <- read(at)
        magic : Bool <- magic_room()
        return active(facts) && Bool.not(shield(target, magic))),
      S.Mem.pure(Bool, False{}))
