import Base

# Exact upper half of an unsigned 32x32 product; no floating point.
def mul_hi(+a: U32, +b: U32) -> U32:
  +al = (a .&. 65535 : U32)
  +ah = (a >> 16n : U32)
  +bl = (b .&. 65535 : U32)
  +bh = (b >> 16n : U32)
  +t = (ah * bl + ((al * bl : U32) >> 16n : U32) : U32)
  u = (al * bh + (t .&. 65535 : U32) : U32)
  (ah * bh + (t >> 16n : U32) + (u >> 16n : U32) : U32)

# Modifier values use Showdown's Q12 representation.
def ratio(numerator: U32, denominator: U32) -> U32:
  (numerator * 4096 / denominator : U32)

def signed_shift_12(+word: U32) -> U32:
  Bool.pick(U32, U32.is_ge(word, 2147483648),
    ((word >> 12n : U32) .|. 4293918720 : U32), (word >> 12n : U32))

def chain(previous: U32, next: U32) -> U32:
  signed_shift_12((previous * next + 2048 : U32))

def modify(value: U32, modifier: U32) -> U32:
  +product = (value * modifier : U32)
  ((product >> 12n : U32) + ((product .&. 4095 : U32) + 2047 : U32) / 4096 : U32)

# IEEE double rounds large integer products before Showdown's ToUint32. The
# diagnostic value codec permits 48-bit integers, so retain the original high
# bits when determining the discarded precision. No floating point is needed.
def bit_length(limit: Nat, word: Nat, count: U32) -> U32:
  match limit:
    case 0n: count
    case 1n+p:
      match word:
        case 0n: count
        case 1n+rest: bit_length(p,((rest + 1n : Nat) / 2n : Nat),(count + 1 : U32))

def round_product_word(+low: U32, +drop: U32) -> U32:
  +step : U32 = (1 << U32.to_nat(U32.min(drop,31)) : U32)
  +remainder : U32 = (low .&. (step - 1 : U32) : U32)
  +half : U32 = (step / 2 : U32)
  up : Bool = U32.is_ne(drop,0) && (U32.is_gt(remainder,half) ||
    U32.is_eq(remainder,half) && U32.is_ne((low .&. step : U32),0))
  (low - remainder + Bool.to_u32(up) * step : U32)

def integer_product_word(+value: Nat, +modifier: U32) -> U32:
  +base : Nat = (U32.to_nat(65536) * U32.to_nat(65536) : Nat)
  +low : U32 = U32.from_nat((value % base : Nat))
  high : Nat = ((value / base : Nat) * U32.to_nat(modifier) + U32.to_nat(mul_hi(low,modifier)) : Nat)
  +width : U32 = bit_length(48n,high,0)
  round_product_word((low * modifier : U32),(U32.max(width,21) - 21 : U32))

def modify_integer(value: Nat, modifier: U32) -> U32:
  +product : U32 = integer_product_word(value,modifier)
  ((product >> 12n : U32) + ((product .&. 4095 : U32) + 2047 : U32) / 4096 : U32)

def hp(base: U32, iv: U32, ev: U32, level: U32) -> U32:
  ((2 * base + iv + ev / 4 + 100) * level / 100 + 10 : U32)

def stat(base: U32, iv: U32, ev: U32, level: U32) -> U32:
  ((2 * base + iv + ev / 4) * level / 100 + 5 : U32)

def pp(base: U32, ups: U32) -> U32:
  (base * (5 + ups) / 5 : U32)
