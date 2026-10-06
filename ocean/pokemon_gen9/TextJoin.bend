import Base
import ./Mem.bend as S
import ./Lists.bend as L
import ./TextMem.bend as Texts
import ./TextStore.bend as StoreTexts
import ./ValueArray.bend as Arrays
import ./EffectStore.bend as Store
import ./EventValue.bend as V

# The type API supplies string arrays. This additionally preserves JS join
# semantics for indexed holes/null/undefined. Other element coercions and
# overridden prototypes/accessors require their own source model.
def element(value: V.Value) -> S.Mem<+List<U32>>:
  match value:
    case V.VText{id}: Texts.get(id)
    case V.VNull{}: S.Mem.pure(+List<U32>,Nil{})
    case V.VUndefined{}: S.Mem.pure(+List<U32>,Nil{})
    case _: S.raise(+List<U32>,4)

def fragments(n: Nat, +index: U32, +cells: +List<Store.Field>) -> S.Mem<+List<+List<U32>>>:
  match n:
    case 0n: S.Mem.pure(+List<+List<U32>>,Nil{})
    case 1n+p:
      do S.Mem<+List<+List<U32>>>:
        head : +List<U32> <- element(Store.field_get(cells,index))
        tail : +List<+List<U32>> <- fragments(p,(index + 1 : U32),cells)
        return Con{head,tail}

def assemble(parts: +List<+List<U32>>, +separator: +List<U32>) -> +List<U32>:
  match parts:
    case Nil{}: Nil{}
    case Con{head,Nil{}}: head
    case Con{head,tail}: L.append(head,L.append(separator,assemble(tail,separator)))

def join_shape(data: Arrays.Shape, separator: +List<U32>) -> S.Mem<U32>:
  Arrays.ArrayShape{extent,cells} = data
  do S.Mem<U32>:
    parts : +List<+List<U32>> <- fragments(U32.to_nat(extent),0,cells)
    Texts.intern(assemble(parts,separator))

def join(reference: U32, +separator: +List<U32>) -> S.Mem<U32>:
  do S.Mem<U32>:
    data : Arrays.Shape <- Arrays.read_shape(reference)
    S.branch(U32,StoreTexts.valid(separator),join_shape(data,separator),S.raise(U32,3))
