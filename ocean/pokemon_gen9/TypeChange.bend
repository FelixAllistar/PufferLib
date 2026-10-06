import Base
import ./Mem.bend as S
import ./Catalog.bend as C
import ./PokemonTypes.bend as Types
import ./ValueArray.bend as Arrays
import ./TextMem.bend as Texts
import ./TextJoin.bend as Join
import ./EventValue.bend as V
import ./data/TypeTexts.bend as Text

type Input is Data:
  StringType{id: U32}
  ArrayTypes{reference: U32}

def species_immune(+number: U32) -> Bool:
  U32.is_eq(number,493) || U32.is_eq(number,773)

def rejection(enforce: Bool, stellar: Bool, number: U32, tera: U32) -> Bool:
  Bool.not(enforce) && (stellar || species_immune(number) || U32.is_ne(tera,0))

def contains_stellar(input: Input) -> S.Mem<Bool>:
  match input:
    case StringType{id}: S.Mem.pure(Bool,U32.is_eq(id,Text.stellar()))
    case ArrayTypes{reference}: Arrays.includes_text(reference,Text.stellar())

def after_stellar(stellar: Bool, +at: U32) -> S.Mem<Bool>:
  S.branch(Bool,stellar,S.Mem.pure(Bool,rejection(False{},True{},0,0)),
    (do S.Mem<Bool>:
      species : U32 <- S.get((at + 1 : U32))
      +number : U32 <- C.species_field(species,51)
      S.branch(Bool,species_immune(number),S.Mem.pure(Bool,rejection(False{},False{},number,0)),
        (do S.Mem<Bool>:
          tera : U32 <- S.get((at + Types.tera_offset() : U32))
          return rejection(False{},False{},number,tera)))))

def blocked(+at: U32, input: Input, enforce: Bool) -> S.Mem<Bool>:
  S.branch(Bool,enforce,S.Mem.pure(Bool,rejection(True{},False{},0,0)),
    (do S.Mem<Bool>:
      stellar : Bool <- contains_stellar(input)
      after_stellar(stellar,at)))

def array_input(input: Input) -> S.Mem<U32>:
  match input:
    case StringType{+id}:
      S.branch(U32,U32.is_ne(id,0),
        (do S.Mem<U32>:
          units : +List<U32> <- Texts.get(id)
          Arrays.make([V.VText{id}])),S.raise(U32,3))
    case ArrayTypes{reference}: S.Mem.pure(U32,reference)

# Source sets these three fields before join, and assigns apparentType only
# after join succeeds. Preserve that order even on unsupported coercion faults.
def commit(+at: U32, input: Input) -> S.Mem<Bool>:
  do S.Mem<Bool>:
    +reference : U32 <- array_input(input)
    S.put((at + Types.types_offset() : U32),reference)
    S.put((at + Types.added_offset() : U32),0)
    S.put((at + Types.known_offset() : U32),1)
    apparent : U32 <- Join.join(reference,[47])
    S.put((at + Types.apparent_offset() : U32),apparent)
    return True{}

def set(+at: U32, +input: Input, enforce: Bool) -> S.Mem<Bool>:
  do S.Mem<Bool>:
    reject : Bool <- blocked(at,input,enforce)
    S.branch(Bool,reject,S.Mem.pure(Bool,False{}),commit(at,input))

def add(+at: U32, +id: U32) -> S.Mem<Bool>:
  do S.Mem<Bool>:
    tera : U32 <- S.get((at + Types.tera_offset() : U32))
    S.branch(Bool,U32.is_ne(tera,0),S.Mem.pure(Bool,False{}),
      (do S.Mem<Bool>:
        units : +List<U32> <- Texts.get(id)
        S.put((at + Types.added_offset() : U32),id)
        return True{}))
