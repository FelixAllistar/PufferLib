import Base
import ./Lists.bend as L
import ./data/abilities.bend as A

# Numeric metadata from Battle.resolvePriority, for Gen 9 singles. Callback
# collection/execution is separate; this module resolves listener ordering.
type EffectKind is Data:
  Condition{}
  Ability{}
  Item{}
  Format{}
  Weather{}
  Rule{}
  Ruleset{}
  Other{}

def kind(code: Nat) -> EffectKind:
  match code:
    case 0n: Condition{}
    case 1n: Ability{}
    case 2n: Item{}
    case 3n: Format{}
    case 4n: Weather{}
    case 5n: Rule{}
    case 6n: Ruleset{}
    case _: Other{}

# Scope: 0 Pokémon, 1 side condition, 2 slot condition, 3 field, 4 other.
def default_sub(effect: EffectKind, +ability: U32, +scope: U32) -> U32:
  match effect:
    case Condition{}:
      L.pick(U32.is_eq(scope, 1), 4, L.pick(U32.is_eq(scope, 2), 3,
        L.pick(U32.is_eq(scope, 3), 5, 2)))
    case Ability{}:
      L.pick(U32.is_eq(ability, A.abilities_poisontouch()) || U32.is_eq(ability, A.abilities_perishbody()), 6,
        L.pick(U32.is_eq(ability, A.abilities_stall()), 9, 7))
    case Item{}: 8
    case Format{}: 5
    case Weather{}: 5
    case Rule{}: 5
    case Ruleset{}: 5
    case Other{}: 0

# Explicit subOrder is signed +32768. Source zero falls back to effect kind.
def sub_order(effect: EffectKind, ability: U32, scope: U32, +explicit: U32) -> U32:
  L.pick(U32.is_ne(explicit, 32768), explicit,
    (32768 + default_sub(effect, ability, scope) : U32))

# Flags: 1 SwitchIn suffix, 2 RedirectTarget suffix, 4 onAllyTryHitSide,
# 8 Pokémon effect holder. Effect creation order matters only for suffixes.
def creation_order(flags: U32, value: U32) -> U32:
  L.pick(U32.is_ne((flags .&. 3 : U32), 0), value, 0)

# SwitchIn rank is speedOrder.indexOf(fieldPosition)+1: 0 means not found.
# Half-speed bias represents -0.5 too (e.g. a zero-speed Trick Room tie).
def speed_value(effect: EffectKind, ability: U32, flags: U32, +cached: U32, unboosted: U32) -> U32:
  match effect:
    case Ability{}:
      L.pick(U32.is_eq(ability, A.abilities_magicbounce()) &&
        U32.is_ne((flags .&. 4 : U32), 0), unboosted, cached)
    case _: cached

def speed(effect: EffectKind, ability: U32, +flags: U32, cached: U32, unboosted: U32, rank: U32) -> U32:
  +value = speed_value(effect, ability, flags, cached, unboosted)
  L.pick(U32.is_eq((flags .&. 8 : U32), 0), 32768,
    L.pick(U32.is_ne((flags .&. 1 : U32), 0),
      (value * 2 + 32769 - rank : U32), (value * 2 + 32768 : U32)))
