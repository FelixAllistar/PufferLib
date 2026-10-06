import Base
import ./Mem.bend as S
import ./Catalog.bend as C
import ./EventValue.bend as V
import ./ValueArray.bend as Arrays
import ./PokemonTypes.bend as Types
import ./TextJoin.bend as Join
import ./data/Layout.bend as Layout
import ./data/TypeTexts.bend as Text

# Per-battle identities follow actual resolved source array aliases. Equal
# contents do not merge separate Dex arrays. Cache cells are private roots.
def cache_at(group: U32) -> U32:
  (Layout.species_type_cache_start() + group : U32)

def text_values(ids: +List<U32>) -> +List<V.Value>:
  match ids:
    case Nil{}: Nil{}
    case Con{id,tail}: Con{V.VText{Text.type_text(U32.to_nat(id))},text_values(tail)}

def create(+group: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    +at : U32 = (Layout.speciesTypeArrays_start() + group * Layout.speciesTypeArrays_stride() : U32)
    count : U32 <- S.get(at)
    ptr : U32 <- S.get((at + 1 : U32))
    ids : +List<U32> <- C.read_list(U32.to_nat(count),ptr)
    +ref : U32 <- Arrays.make(text_values(ids))
    S.put(cache_at(group),ref)
    return ref

def shared(display: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    +group : U32 <- C.species_field(display,30)
    S.branch(U32,U32.is_gt(group,0) && U32.is_lt(group,Layout.speciesTypeArrays_count()),
      (do S.Mem<U32>:
        +cached : U32 <- S.get(cache_at(group))
        S.branch(U32,U32.is_eq(cached,0),create(group),S.Mem.pure(U32,cached))),S.raise(U32,3))

def pokemon(+at: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    display : U32 <- S.get((at + 77 : U32))
    +ref : U32 <- shared(display)
    S.put((at + Types.types_offset() : U32),ref)
    S.put((at + Types.base_types_offset() : U32),ref)
    S.put((at + Types.added_offset() : U32),0)
    S.put((at + Types.tera_offset() : U32),0)
    S.put((at + Types.known_offset() : U32),1)
    apparent : U32 <- Join.join(ref,[47])
    S.put((at + Types.apparent_offset() : U32),apparent)

# Attach after the constructor's effect states/maps, keeping their registration
# sequence separate from shared raw arrays. Absent party slots remain absent.
def party(n: Nat, +slot: U32) -> S.Mem<Unit>:
  match n:
    case 0n: S.Mem.pure(Unit,Unit{})
    case 1n+p:
      do S.Mem<Unit>:
        +at : U32 <- S.get((4160 + slot : U32))
        S.branch(Unit,U32.is_eq(at,0),S.Mem.pure(Unit,Unit{}),pokemon(at))
        party(p,(slot + 1 : U32))
