import Base
import ./Math.bend as M
import ./Lists.bend as L
import ./data/moves.bend as V

# Boost stages are encoded as the source's signed stage plus 6.
# Match the source's independent flooring at each arithmetic step.
def boosted(+value: U32, +stage: U32) -> U32:
  L.pick(U32.is_ge(stage, 6), (value * (stage - 4 : U32) / 2 : U32),
    (value * 2 / (8 - stage : U32) : U32))

def nature(+value: U32, +kind: U32) -> U32:
  L.pick(U32.is_eq(kind, 1), (((value * 110 : U32) .&. 65535 : U32) / 100 : U32),
    L.pick(U32.is_eq(kind, 2), (((value * 90 : U32) .&. 65535 : U32) / 100 : U32), value))

def stat(base: U32, iv: U32, ev: U32, level: U32, kind: U32) -> U32:
  nature(M.stat(base, iv, ev, level), kind)

def max_hp(+base: U32, iv: U32, ev: U32, level: U32, +fixed: U32) -> U32:
  L.pick(U32.is_ne(fixed, 0), fixed, M.hp(base, iv, ev, level))

def pp(+id: U32, +base: U32, no_boost: U32) -> U32:
  L.pick(U32.is_ne(no_boost, 0) || U32.is_eq(id, V.moves_trumpcard()), base, M.pp(base, 3))

# Pokemon.damage/heal kernels. Event dispatch and battle messages are callers'
# responsibilities; these functions only implement bounded HP arithmetic.
def damage(+hp: U32, amount: U32) -> U32:
  U32.min(hp, amount)

def remaining(+hp: U32, amount: U32) -> U32:
  (hp - damage(hp, amount) : U32)

def healed(+hp: U32, +maximum: U32, amount: U32) -> U32:
  L.pick(U32.is_eq(hp, 0) || U32.is_ge(hp, maximum), 0,
    U32.min(amount, (maximum - hp : U32)))
