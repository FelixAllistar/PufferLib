import Base
import ./Math.bend as M
import ./Mem.bend as S
import ./GenRng.bend as R
import ./Lists.bend as L

# Gen 9 numerical stages from BattleActions.getDamage/modifyDamage. Callers
# resolve immunity, base power, stats, STAB and event results separately.
# These stages deliberately retain event boundaries instead of hiding callbacks
# inside a numerical damage formula. Defense must be nonzero.
def base(level: U32, power: U32, attack: U32, defense: U32) -> U32:
  ((2 * level / 5 + 2) * power * attack / defense / 50 : U32)

def added(value: U32, bond_second: Bool) -> U32:
  +amount = (value + 2 : U32)
  Bool.pick(U32, bond_second, M.modify(amount, 1024), amount)

# Default critical multiplier is floor(3*x/2), then unsigned 32-bit truncation.
# Split the product so overflow occurs after division, as in JavaScript.
def critical(+value: U32, crit: Bool) -> U32:
  Bool.pick(U32, crit, (value / 2 * 3 + value % 2 : U32), value)

def rolled(value: U32, roll: U32) -> U32:
  (value * (100 - roll : U32) / 100 : U32)

def randomize(value: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    roll : U32 <- R.random(16)
    return rolled(value, roll)

# Effectiveness exponent is encoded as signed exponent +6, clamped to [0,12].
def effectiveness(+value: U32, +encoded: U32) -> U32:
  +stage = U32.min(encoded, 12)
  Bool.pick(U32, U32.is_ge(stage, 6),
    (value << U32.to_nat((U32.max(stage, 6) - 6 : U32)) : U32),
    (value >> U32.to_nat((6 - U32.min(stage, 6) : U32)) : U32))

def burned(+value: U32, applies: Bool) -> U32:
  Bool.pick(U32, applies, M.modify(value, 2048), value)

def finish(+value: U32, bypass: Bool) -> U32:
  +amount = Bool.pick(U32, bypass, M.modify(value, 1024), value)
  Bool.pick(U32, U32.is_eq(amount, 0), 1, (amount .&. 65535 : U32))

# Confusion truncates to 16 bits BEFORE the random roll, unlike ordinary damage.
def confusion_base(level: U32, power: U32, attack: U32, defense: U32) -> U32:
  ((base(level, power, attack, defense) + 2 : U32) .&. 65535 : U32)

def confusion(level: U32, power: U32, attack: U32, defense: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    amount : U32 <- randomize(confusion_base(level, power, attack, defense))
    return U32.max(amount, 1)

# willCrit: 0 undefined, 1 false, 2 true. Even a guaranteed ratio-4 crit
# consumes a draw when willCrit is undefined. A prevented crit still draws.
def crit_denominator(ratio: U32) -> U32:
  L.at([0, 24, 8, 2, 1], U32.to_nat(U32.min(ratio, 4)))

def crit_chance(+ratio: U32) -> S.Mem<Bool>:
  S.branch(Bool, U32.is_eq(ratio, 0), S.Mem.pure(Bool, False{}),
    R.chance(1, crit_denominator(ratio)))

def crit_result(+will: U32, ratio: U32, allowed: Bool) -> S.Mem<Bool>:
  do S.Mem<Bool>:
    result : Bool <- S.branch(Bool, U32.is_eq(will, 0), crit_chance(ratio),
      S.Mem.pure(Bool, U32.is_eq(will, 2)))
    return result && allowed
