import Base
import ./Mem.bend as S
import ./EffectStore.bend as Store
import ./EffectMem.bend as Effects
import ./EventValue.bend as V
import ./Number.bend as N

# Ordinary sparse/full boost objects, not initialized effect states. Property
# identities 32..38 are atk/def/spa/spd/spe/accuracy/evasion. Preserve aliases
# and property insertion order when ModifyBoost writes an absent sparse field.
def key(index: U32) -> U32:
  (32 + index : U32)

def stage_value(+encoded: U32) -> V.Value:
  V.VNumber{N.Finite{U32.is_lt(encoded,6),
    U32.to_nat((U32.max(encoded,6) - U32.min(encoded,6) : U32)),1}}

def bounded_stage(+magnitude: Nat) -> U32:
  Bool.pick(U32,Nat.is_ge(magnitude,6n),6,U32.from_nat(magnitude))

def stage(value: V.Value) -> Result<&2,&2,U32,U32>:
  match value:
    case V.VNumber{N.Finite{negative,+numerator,+denominator}}:
      Bool.pick(Result<&2,&2,U32,U32>,U32.is_ne(denominator,0) &&
        Nat.is_eq((numerator % U32.to_nat(U32.max(denominator,1)) : Nat),0n),
        Done{Bool.pick(U32,negative,
          (6 - bounded_stage((numerator / U32.to_nat(U32.max(denominator,1)) : Nat)) : U32),
          (6 + bounded_stage((numerator / U32.to_nat(U32.max(denominator,1)) : Nat)) : U32))},Fail{3})
    case _: Fail{3}

def object_reference(value: V.Value) -> Result<&2,&2,U32,U32>:
  match value:
    case V.VObject{7,id}: Done{id}
    case _: Fail{3}

def positive(value: V.Value) -> Bool:
  match value:
    case V.VNumber{number}: N.positive(number)
    case _: False{}

def zero_positive_evasion(+fields: +List<Store.Field>) -> +List<Store.Field>:
  Bool.pick(+List<Store.Field>,positive(Store.field_get(fields,key(6))),
    Store.field_set(fields,key(6),Store.number(0)),fields)

def unaware_fields(+fields: +List<Store.Field>, +holder: U32, +pokemon: U32,
  +actor: U32, +target: U32) -> +List<Store.Field>:
  +defenses : +List<Store.Field> = Bool.pick(+List<Store.Field>,U32.is_eq(holder,actor) && U32.is_eq(pokemon,target),
    Store.field_set(Store.field_set(Store.field_set(fields,key(1),Store.number(0)),key(3),Store.number(0)),
      key(6),Store.number(0)),fields)
  offense : +List<Store.Field> = Bool.pick(+List<Store.Field>,U32.is_eq(pokemon,actor) && U32.is_eq(holder,target),
    Store.field_set(Store.field_set(Store.field_set(Store.field_set(defenses,key(0),Store.number(0)),
      key(1),Store.number(0)),key(2),Store.number(0)),key(5),Store.number(0)),defenses)
  Bool.pick(+List<Store.Field>,U32.is_eq(holder,pokemon),fields,offense)

def replace(reference: U32, fields: +List<Store.Field>) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    heap : Store.Heap <- S.heap()
    S.set_heap(Store.put_fields(heap,reference,fields))

def fields(reference: U32) -> S.Mem<+List<Store.Field>>:
  do S.Mem<+List<Store.Field>>:
    heap : Store.Heap <- S.heap()
    S.from_result(+List<Store.Field>,Store.get_fields(heap,reference))

def clear_positive_evasion(+reference: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    original : +List<Store.Field> <- fields(reference)
    replace(reference,zero_positive_evasion(original))

def unaware(+reference: U32, holder: U32, pokemon: U32, actor: U32, target: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    original : +List<Store.Field> <- fields(reference)
    replace(reference,unaware_fields(original,holder,pokemon,actor,target))

def flat_fields(n: Nat, +at: U32, +index: U32) -> S.Mem<+List<Store.Field>>:
  match n:
    case 0n: S.Mem.pure(+List<Store.Field>,Nil{})
    case 1n+p:
      do S.Mem<+List<Store.Field>>:
        value : U32 <- S.get((at + 24 + index : U32))
        rest : +List<Store.Field> <- flat_fields(p,at,(index + 1 : U32))
        return Con{Store.FieldData{key(index),stage_value(value)},rest}

def clone(+at: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    +reference : U32 <- S.get((at + 109 : U32))
    original : +List<Store.Field> <- S.branch(+List<Store.Field>,U32.is_eq(reference,0),
      flat_fields(7n,at,0),fields(reference))
    Effects.raw_object(original)

def sparse(index: U32, value: V.Value) -> S.Mem<U32>:
  Effects.raw_object([Store.FieldData{key(index),value}])

# Persistent Pokemon.boosts at root 109. Modified relay objects above retain
# their broader integer/clamping contract; stored mutation helpers admit -6..6.
def stored_stage(value: V.Value) -> Result<&2,&2,U32,U32>:
  match value:
    case V.VNumber{N.Finite{negative,+numerator,+denominator}}:
      Bool.pick(Result<&2,&2,U32,U32>,U32.is_ne(denominator,0) &&
        Nat.is_eq((numerator % U32.to_nat(U32.max(denominator,1)) : Nat),0n) &&
        Nat.is_le((numerator / U32.to_nat(U32.max(denominator,1)) : Nat),6n),
        Done{Bool.pick(U32,negative,
          (6 - U32.from_nat((numerator / U32.to_nat(U32.max(denominator,1)) : Nat)) : U32),
          (6 + U32.from_nat((numerator / U32.to_nat(U32.max(denominator,1)) : Nat)) : U32))},Fail{3})
    case _: Fail{3}

def current(+at: U32, +index: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    +reference : U32 <- S.get((at + 109 : U32))
    S.branch(U32,U32.is_eq(reference,0),S.get((at + 24 + index : U32)),
      (do S.Mem<U32>:
        value : V.Value <- Effects.read(reference,key(index))
        S.from_result(U32,stored_stage(value))))

def stat_stage(+at: U32, +index: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    +reference : U32 <- S.get((at + 109 : U32))
    S.branch(U32,U32.is_eq(reference,0),S.get((at + 24 + index : U32)),
      (do S.Mem<U32>:
        value : V.Value <- Effects.read(reference,key(index))
        S.from_result(U32,stage(value))))

def write(+at: U32, +index: U32, +encoded: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +reference : U32 <- S.get((at + 109 : U32))
    S.when(U32.is_ne(reference,0),Effects.write(reference,key(index),stage_value(encoded)))
    S.put((at + 24 + index : U32),encoded)

def attach(+at: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    original : +List<Store.Field> <- flat_fields(7n,at,0)
    reference : U32 <- Effects.raw_object(original)
    S.put((at + 109 : U32),reference)

def party(n: Nat, +slot: U32) -> S.Mem<Unit>:
  match n:
    case 0n: S.Mem.pure(Unit,Unit{})
    case 1n+p:
      do S.Mem<Unit>:
        +at : U32 <- S.get((4160 + slot : U32))
        S.when(U32.is_ne(at,0),attach(at))
        party(p,(slot + 1 : U32))

def zeros() -> +List<Store.Field>:
  [Store.FieldData{32,Store.number(0)},Store.FieldData{33,Store.number(0)},
   Store.FieldData{34,Store.number(0)},Store.FieldData{35,Store.number(0)},
   Store.FieldData{36,Store.number(0)},Store.FieldData{37,Store.number(0)},
   Store.FieldData{38,Store.number(0)}]

# Transform loops source properties, rather than destination properties as
# setSpecies does. Existing destination order/identity survives assignments.
def copied(source: +List<Store.Field>, destination: +List<Store.Field>) -> +List<Store.Field>:
  match source:
    case Nil{}: destination
    case Con{Store.FieldData{property,value},rest}:
      copied(rest,Store.field_set(destination,property,value))

def cleared(original: +List<Store.Field>) -> +List<Store.Field>:
  match original:
    case Nil{}: Nil{}
    case Con{Store.FieldData{property,_},rest}:
      Con{Store.FieldData{property,Store.number(0)},cleared(rest)}

def sync(n: Nat, +at: U32, +index: U32) -> S.Mem<Unit>:
  match n:
    case 0n: S.Mem.pure(Unit,Unit{})
    case 1n+p:
      do S.Mem<Unit>:
        value : U32 <- current(at,index)
        S.put((at + 24 + index : U32),value)
        sync(p,at,(index + 1 : U32))

def clear_existing(+at: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +reference : U32 <- S.get((at + 109 : U32))
    S.branch(Unit,U32.is_eq(reference,0),
      (do S.Mem<Unit>:
        # Rootless diagnostics still have the full seven flat fields.
        S.put((at + 24 : U32),6)
        S.put((at + 25 : U32),6)
        S.put((at + 26 : U32),6)
        S.put((at + 27 : U32),6)
        S.put((at + 28 : U32),6)
        S.put((at + 29 : U32),6)
        S.put((at + 30 : U32),6)),
      (do S.Mem<Unit>:
        original : +List<Store.Field> <- fields(reference)
        replace(reference,cleared(original))
        sync(7n,at,0)))

# Only the boost-object replacement stage of clearVolatile.
def fresh(+at: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    +reference : U32 <- Effects.raw_object(zeros())
    S.put((at + 109 : U32),reference)
    sync(7n,at,0)
    return reference

# Only the boost assignment stage used by Baton Pass/copyVolatileFrom.
def share(+at: U32, target: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    +reference : U32 <- S.get((target + 109 : U32))
    S.branch(U32,U32.is_ne(reference,0),(do S.Mem<U32>:
      S.put((at + 109 : U32),reference)
      sync(7n,at,0)
      return reference),S.raise(U32,3))

# Only the boost-copy stage of Transform; this never replaces destination.
def copy_from(+at: U32, target: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    source : U32 <- S.get((target + 109 : U32))
    +destination : U32 <- S.get((at + 109 : U32))
    values : +List<Store.Field> <- fields(source)
    original : +List<Store.Field> <- fields(destination)
    replace(destination,copied(values,original))
    sync(7n,at,0)
    return destination
