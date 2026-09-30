import Base
import ../../miniwob/training/TextGenerate.bend as T
import ../panels/Generate.bend as G

def query(ending: Bool, prefix: String, suffix: String) -> String:
  match ending:
    case True{}: "Enter an item that starts with \"" ++ prefix ++ "\" and ends with \"" ++ suffix ++ "\"."
    case False{}: "Enter an item that starts with \"" ++ prefix ++ "\"."

def generate(task: U32, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +prefix = T.country_prefix(seed)
  +suffix = T.country_suffix(seed)
  +ending = Bool.not(U32.is_eq((G.mix((seed + 7177 : U32)) % 4 : U32), 0))
  +body = (base + 32 : U32)
  a = G.clear_blocks(16n, base, a)
  a = a[base] <- 2
  a = a[(base + 1 : U32)] <- task
  a = a[(base + 3 : U32)] <- seed
  a = a[(base + 12 : U32)] <- 10000
  a = T.initial(10, 1, body, a)
  a = a[(body + 12 : U32)] <- Bool.to_u32(ending)
  a = a[(body + 13 : U32)] <- U32.from_nat(List.length(&2, U32, prefix))
  a = a[(body + 14 : U32)] <- U32.from_nat(List.length(&2, U32, suffix))
  a = T.write_units(prefix, (body + 160 : U32), a)
  a = T.write_units(suffix, (body + 192 : U32), a)
  T.write_units(T.units(query(ending, T.string(prefix), T.string(suffix))), (base + 320 : U32), a)
