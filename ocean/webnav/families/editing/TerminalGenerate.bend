import Base
import ./Generate.bend as G

def base_name(i: Nat) -> String:
  match i:
    case 0n: "cats"
    case 1n: "code"
    case 2n: "flowers"
    case 3n: "image"
    case _: "script"

def extension(i: Nat) -> String:
  match i:
    case 0n: ""
    case 1n: "png"
    case 2n: "txt"
    case 3n: "jpg"
    case 4n: "json"
    case _: "gpg"

def ext_code(+seed: U32, +index: U32) -> U32:
  (G.mix((seed + index * 127 + 301 : U32)) % 6 : U32)

def filename(+index: U32, +code: U32) -> String:
  base_name(U32.to_nat(index)) ++
  Bool.pick(String,U32.is_eq(code,0),"",
    "." ++ extension(U32.to_nat(code)))

def write_file(+seed: U32, +index: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  +code = ext_code(seed,index)
  +name = filename(index,code)
  a = a[(base + 48 + index : U32)] <- code
  a = a[(base + 56 + index : U32)] <- G.length(name)
  G.text(name,(base + 512 + index * 128 : U32),a)

def files(n: Nat, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      files(p,seed,base,write_file(seed,U32.from_nat(p),base,a))

def query_index(+code: Nat) -> String:
  match code:
    case 0n:
      "Use the terminal below to delete a file that has no file extension."
    case _:
      "Use the terminal below to delete a file ending with the extension ." ++
      extension(code)

def generate(task: U32, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +count = (3 + G.mix((seed + 107 : U32)) % 3 : U32)
  +chosen = (G.mix((seed + 701 : U32)) % count : U32)
  +expected = ext_code(seed,chosen)
  a = G.blocks(64n,base,a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 20000
  a[(base + 32 : U32)] <- expected
  a[(base + 33 : U32)] <- count
  a[(base + 40 : U32)] <- ((1 << U32.to_nat(count) : U32) - 1 : U32)
  a = files(U32.to_nat(count),seed,base,a)
  G.text(query_index(U32.to_nat(expected)),(base + 256 : U32),a)
