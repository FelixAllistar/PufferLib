import Base
import ./Mem.bend as S
import ./EffectStore.bend as Store
import ./EffectMem.bend as Effects
import ./EventValue.bend as V
import ./Number.bend as N

# Ordinary stat objects. HP uses key 39; atk..spe share keys 32..36 with
# boost objects. Object identities distinguish these unrelated namespaces.
def key(+index: U32) -> U32:
  Bool.pick(U32,U32.is_eq(index,0),39,(31 + index : U32))

def integer(value: V.Value) -> Result<&2,&2,U32,U32>:
  match value:
    case V.VNumber{N.Finite{negative,+numerator,+denominator}}:
      Bool.pick(Result<&2,&2,U32,U32>,Bool.not(negative) && U32.is_ne(denominator,0) &&
        Nat.is_eq((numerator % U32.to_nat(U32.max(denominator,1)) : Nat),0n) &&
        Nat.is_le((numerator / U32.to_nat(U32.max(denominator,1)) : Nat),U32.to_nat(4294967295)),
        Done{U32.from_nat((numerator / U32.to_nat(U32.max(denominator,1)) : Nat))},Fail{3})
    case _: Fail{3}

# Source loops the destination's properties, retaining their insertion order.
# This pure operation also keeps absent-source values as undefined. Callers
# using bounded numeric stat readers must establish their numeric domain.
def copied(destination: +List<Store.Field>, +source: +List<Store.Field>) -> +List<Store.Field>:
  match destination:
    case Nil{}: Nil{}
    case Con{Store.FieldData{+property,_},tail}:
      Con{Store.FieldData{property,Store.field_get(source,property)},copied(tail,source)}

def fields(reference: U32) -> S.Mem<+List<Store.Field>>:
  do S.Mem<+List<Store.Field>>:
    heap : Store.Heap <- S.heap()
    S.from_result(+List<Store.Field>,Store.get_fields(heap,reference))

def read(reference: U32, index: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    value : V.Value <- Effects.read(reference,key(index))
    S.from_result(U32,integer(value))

# Rootless states are the existing flat diagnostic fixtures. Constructed
# Pokemon receive roots below and their persistent objects are authoritative.
def current(+at: U32, +index: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    +reference : U32 <- S.get((at + 102 : U32))
    S.branch(U32,U32.is_eq(reference,0),S.get((at + 17 + index : U32)),
      read(reference,(index + 1 : U32)))

def flat_fields(n: Nat, +at: U32, +index: U32) -> S.Mem<+List<Store.Field>>:
  match n:
    case 0n: S.Mem.pure(+List<Store.Field>,Nil{})
    case 1n+p:
      do S.Mem<+List<Store.Field>>:
        value : U32 <- S.get((at + 16 + index : U32))
        rest : +List<Store.Field> <- flat_fields(p,at,(index + 1 : U32))
        return Con{Store.FieldData{key(index),Store.number(value)},rest}

def attach(+at: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    stored : +List<Store.Field> <- flat_fields(5n,at,1)
    base : +List<Store.Field> <- flat_fields(6n,at,0)
    stored_reference : U32 <- Effects.raw_object(stored)
    base_reference : U32 <- Effects.raw_object(base)
    S.put((at + 102 : U32),stored_reference)
    S.put((at + 103 : U32),base_reference)

def party(n: Nat, +slot: U32) -> S.Mem<Unit>:
  match n:
    case 0n: S.Mem.pure(Unit,Unit{})
    case 1n+p:
      do S.Mem<Unit>:
        +at : U32 <- S.get((4160 + slot : U32))
        S.when(U32.is_ne(at,0),attach(at))
        party(p,(slot + 1 : U32))

def copy_into(+reference: U32, source: +List<Store.Field>) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    original : +List<Store.Field> <- fields(reference)
    heap : Store.Heap <- S.heap()
    S.set_heap(Store.put_fields(heap,reference,copied(original,source)))
