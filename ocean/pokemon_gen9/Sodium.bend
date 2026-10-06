import Base
import ./Math.bend as M

# sim/prng.ts SodiumRNG: one counter-zero ChaCha20 block per draw, fixed
# LibsodiumDRG nonce. First 32 output bytes become the key for the next draw;
# the next four bytes are interpreted big-endian. Seed words here are LE.
type Seed is Data:
  K{a: U32, b: U32, c: U32, d: U32, e: U32, f: U32, g: U32, h: U32}

type Quarter is Data:
  Q{a: U32, b: U32, c: U32, d: U32}

type Block is Data:
  B{a: Quarter, b: Quarter, c: Quarter, d: Quarter}

def rotl(+value: U32, +bits: Nat) -> U32:
  ((value << bits : U32) .|. (value >> (32n - bits : Nat) : U32) : U32)

def quarter(input: Quarter) -> Quarter:
  Q{a, +b, c, d} = input
  +a1 : U32 = (a + b : U32)
  +d1 = rotl(U32.xor(d, a1), 16n)
  +c1 : U32 = (c + d1 : U32)
  +b1 = rotl(U32.xor(b, c1), 12n)
  +a2 : U32 = (a1 + b1 : U32)
  +d2 = rotl(U32.xor(d1, a2), 8n)
  +c2 : U32 = (c1 + d2 : U32)
  b2 = rotl(U32.xor(b1, c2), 7n)
  Q{a2, b2, c2, d2}

def columns(input: Block) -> Block:
  B{a, b, c, d} = input
  B{quarter(a), quarter(b), quarter(c), quarter(d)}

def diagonal(input: Block) -> Block:
  B{Q{a0, b0, c0, d0}, Q{a1, b1, c1, d1}, Q{a2, b2, c2, d2}, Q{a3, b3, c3, d3}} = input
  B{Q{a0,b1,c2,d3}, Q{a1,b2,c3,d0}, Q{a2,b3,c0,d1}, Q{a3,b0,c1,d2}}

def undiagonal(input: Block) -> Block:
  B{Q{a0, b0, c0, d0}, Q{a1, b1, c1, d1}, Q{a2, b2, c2, d2}, Q{a3, b3, c3, d3}} = input
  B{Q{a0,b3,c2,d1}, Q{a1,b0,c3,d2}, Q{a2,b1,c0,d3}, Q{a3,b2,c1,d0}}

def rounds(n: Nat, input: Block) -> Block:
  match n:
    case 0n: input
    case 1n+p: rounds(p, undiagonal(columns(diagonal(columns(input)))))

def initial(seed: Seed) -> Block:
  K{a,b,c,d,e,f,g,h} = seed
  B{Q{1634760805,a,e,0}, Q{857760878,b,f,1935829324},
    Q{2036477234,c,g,1969841263}, Q{1797285236,d,h,1196573805}}

def add_quarter(left: Quarter, right: Quarter) -> Quarter:
  Q{a,b,c,d} = left
  Q{e,f,g,h} = right
  Q{(a + e : U32), (b + f : U32), (c + g : U32), (d + h : U32)}

def add(left: Block, right: Block) -> Block:
  B{a,b,c,d} = left
  B{e,f,g,h} = right
  B{add_quarter(a,e), add_quarter(b,f), add_quarter(c,g), add_quarter(d,h)}

def swap_bytes(+value: U32) -> U32:
  ((value << 24n : U32) .|. ((value .&. 65280 : U32) << 8n : U32) .|.
    ((value >> 8n : U32) .&. 65280 : U32) .|. (value >> 24n : U32) : U32)

def extract(block: Block) -> Seed & U32:
  B{Q{a0,b0,c0,_}, Q{a1,b1,_,_}, Q{a2,b2,_,_}, Q{a3,b3,_,_}} = block
  (K{a0,a1,a2,a3,b0,b1,b2,b3}, swap_bytes(c0))

def next(seed: Seed) -> Seed & U32:
  +input = initial(seed)
  extract(add(rounds(10n, input), input))

def scale(pair: Seed & U32, bound: U32) -> Seed & U32:
  (seed, value) = pair
  (seed, M.mul_hi(value, bound))

def random(seed: Seed, bound: U32) -> Seed & U32:
  scale(next(seed), bound)
