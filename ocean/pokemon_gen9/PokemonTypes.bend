import Base
import ./Mem.bend as S
import ./EventContext.bend as Context
import ./EventValue.bend as V
import ./ValueArray.bend as Arrays
import ./TypeEvent.bend as Event
import ./data/TypeTexts.bend as Text

# Persistent array reference and exact text IDs. No copying to two flat slots:
# initial species arrays and enforced setType array arguments can be shared.
def known_offset() -> U32:
  99

def apparent_offset() -> U32:
  100

def base_types_offset() -> U32:
  101

def types_offset() -> U32:
  96
def added_offset() -> U32:
  97
def tera_offset() -> U32:
  98

def tera_shortcut(+tera: U32, pretera: Bool) -> Bool:
  Bool.not(pretera) && U32.is_ne(tera,0) && U32.is_ne(tera,Text.stellar())

def ordinary(+at: U32, current: Context.Current, exclude_added: Bool) -> S.Mem<U32>:
  do S.Mem<U32>:
    initial : U32 <- S.get((at + types_offset() : U32))
    +ref : U32 <- Event.call(current,at,initial)
    size : U32 <- Arrays.length(ref)
    S.branch(Unit,U32.is_eq(size,0),
      (do S.Mem<Unit>:
        ignored : U32 <- Arrays.push(ref,V.VText{Text.normal()})
        return Unit{}),S.Mem.pure(Unit,Unit{}))
    +added : U32 <- S.get((at + added_offset() : U32))
    S.branch(U32,Bool.not(exclude_added) && U32.is_ne(added,0),
      Arrays.concat_one(ref,V.VText{added}),S.Mem.pure(U32,ref))

def get(+at: U32, +current: Context.Current, +exclude_added: Bool, +pretera: Bool) -> S.Mem<U32>:
  do S.Mem<U32>:
    +tera : U32 <- S.get((at + tera_offset() : U32))
    S.branch(U32,tera_shortcut(tera,pretera),Arrays.make([V.VText{tera}]),ordinary(at,current,exclude_added))

def member(ids: +List<U32>, +ref: U32) -> S.Mem<Bool>:
  match ids:
    case Nil{}: S.Mem.pure(Bool,False{})
    case Con{text,tail}:
      do S.Mem<Bool>:
        yes : Bool <- Arrays.includes_text(ref,text)
        S.branch(Bool,yes,S.Mem.pure(Bool,True{}),member(tail,ref))

def has(at: U32, current: Context.Current, ids: +List<U32>) -> S.Mem<Bool>:
  do S.Mem<Bool>:
    ref : U32 <- get(at,current,False{},False{})
    member(ids,ref)
