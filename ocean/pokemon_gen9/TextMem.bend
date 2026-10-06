import Base
import ./Mem.bend as S
import ./Catalog.bend as C
import ./EffectStore.bend as Effects
import ./TextStore.bend as Texts
import ./data/Layout.bend as Layout

def static_units(+id: U32) -> S.Mem<+List<U32>>:
  do S.Mem<+List<U32>>:
    +at : U32 = (Layout.textUnits_start() + id * 2 : U32)
    count : U32 <- S.get(at)
    ptr : U32 <- S.get((at + 1 : U32))
    C.read_list(U32.to_nat(count),ptr)

def get(+id: U32) -> S.Mem<+List<U32>>:
  S.branch(+List<U32>,U32.is_lt(id,Layout.strings_count()),static_units(id),
    (do S.Mem<+List<U32>>:
      heap : Effects.Heap <- S.heap()
      S.from_result(+List<U32>,Texts.read(Effects.text_heap(heap),id))))

# The chain bound derives from the immutable exported table. Hash collisions
# compare full contents; the hash alone never establishes string equality.
def static_find(n: Nat, +id: U32, +units: +List<U32>) -> S.Mem<Maybe<&2,U32>>:
  match n:
    case 0n:
      S.branch(Maybe<&2,U32>,U32.is_eq(id,0),S.Mem.pure(Maybe<&2,U32>,None{}),S.raise(Maybe<&2,U32>,3))
    case 1n+p:
      S.branch(Maybe<&2,U32>,U32.is_eq(id,0),S.Mem.pure(Maybe<&2,U32>,None{}),
        S.branch(Maybe<&2,U32>,U32.is_lt(id,Layout.strings_count()),
          (do S.Mem<Maybe<&2,U32>>:
            stored : +List<U32> <- static_units(id)
            S.branch(Maybe<&2,U32>,Texts.same(stored,units),S.Mem.pure(Maybe<&2,U32>,Some{id}),
              (do S.Mem<Maybe<&2,U32>>:
                next : U32 <- S.get((Layout.textNext_start() + id : U32))
                static_find(p,next,units)))),S.raise(Maybe<&2,U32>,3)))

def save_interned(heap: Effects.Heap, result: Texts.Interned) -> S.Mem<U32>:
  Texts.InternedText{texts,id} = result
  do S.Mem<U32>:
    S.set_heap(Effects.put_text_heap(heap,texts))
    return id

def intern_found(found: Maybe<&2,U32>, hashed: U32, units: +List<U32>) -> S.Mem<U32>:
  match found:
    case Some{id}: S.Mem.pure(U32,id)
    case None{}:
      do S.Mem<U32>:
        +heap : Effects.Heap <- S.heap()
        result : Texts.Interned <- S.from_result(Texts.Interned,
          Texts.insert(Effects.text_heap(heap),Layout.strings_count(),hashed,units))
        save_interned(heap,result)

def intern_nonempty(+units: +List<U32>) -> S.Mem<U32>:
  do S.Mem<U32>:
    +hashed : U32 = Texts.hash(units)
    head : U32 <- S.get((Layout.textBuckets_start() + hashed % Layout.textBuckets_count() : U32))
    found : Maybe<&2,U32> <- static_find(U32.to_nat(Layout.strings_count()),head,units)
    intern_found(found,hashed,units)

def intern_valid(units: +List<U32>) -> S.Mem<U32>:
  match units:
    case Nil{}: S.Mem.pure(U32,0)
    case Con{head,tail}: intern_nonempty(Con{head,tail})

def intern(+units: +List<U32>) -> S.Mem<U32>:
  S.branch(U32,Texts.valid(units),intern_valid(units),S.raise(U32,3))
