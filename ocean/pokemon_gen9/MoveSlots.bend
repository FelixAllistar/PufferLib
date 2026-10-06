import Base
import ./Mem.bend as S
import ./Catalog.bend as C
import ./State.bend as State
import ./EffectStore.bend as Store
import ./EffectMem.bend as Effects
import ./ValueArray.bend as Arrays
import ./EventValue.bend as V
import ./StatTable.bend as Numbers
import ./data/Layout.bend as Layout
import ./data/moves.bend as Moves
import ./TextMem.bend as Texts
import ./Lists.bend as Lists

# Ordinary slot records use exact JS primitives, not effect-state IDs.
# Existing keys id=1/target=2; move=40, pp=41, maxpp=42, disabled=43,
# disabledSource=44, used=45, virtual=46. Current/base arrays are roots 107/108.
def initial_fields(name: U32, id: U32, +pp: U32, target: U32) -> +List<Store.Field>:
  [Store.FieldData{40,V.VText{name}},Store.FieldData{1,V.VText{id}},
   Store.FieldData{41,Store.number(pp)},Store.FieldData{42,Store.number(pp)},
   Store.FieldData{2,V.VText{target}},Store.FieldData{43,V.VBool{False{}}},
   Store.FieldData{44,V.VText{0}},Store.FieldData{45,V.VBool{False{}}}]

def record_ref(value: V.Value) -> Result<&2,&2,U32,U32>:
  match value:
    case V.VObject{7,reference}: Done{reference}
    case _: Fail{3}

def text_id(value: V.Value) -> Result<&2,&2,U32,U32>:
  match value:
    case V.VText{id}: Done{id}
    case _: Fail{3}

def flat_records(n: Nat, +at: U32, +index: U32) -> S.Mem<+List<V.Value>>:
  match n:
    case 0n: S.Mem.pure(+List<V.Value>,Nil{})
    case 1n+p:
      do S.Mem<+List<V.Value>>:
        +slot : U32 = State.slot(at,index)
        +id : U32 <- S.get(slot)
        name : U32 <- C.move_text(id,1)
        text : U32 <- C.move_text(id,0)
        pp : U32 <- S.get((slot + 1 : U32))
        target_id : U32 <- S.get((slot + 7 : U32))
        target : U32 <- S.get((Layout.targetTexts_start() + target_id : U32))
        reference : U32 <- Effects.raw_object(initial_fields(name,text,pp,target))
        tail : +List<V.Value> <- flat_records(p,at,(index + 1 : U32))
        return Con{V.VObject{7,reference},tail}

def attach(+at: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +count : U32 <- S.get((at + 14 : U32))
    S.branch(Unit,U32.is_le(count,4),(do S.Mem<Unit>:
      records : +List<V.Value> <- flat_records(U32.to_nat(count),at,0)
      +base : U32 <- Arrays.make(records)
      current : U32 <- Arrays.clone(base)
      S.put((at + 108 : U32),base)
      S.put((at + 107 : U32),current)),S.raise(Unit,3))

def party(n: Nat, +slot: U32) -> S.Mem<Unit>:
  match n:
    case 0n: S.Mem.pure(Unit,Unit{})
    case 1n+p:
      do S.Mem<Unit>:
        +at : U32 <- S.get((4160 + slot : U32))
        S.when(U32.is_ne(at,0),attach(at))
        party(p,(slot + 1 : U32))

def slot(+at: U32, index: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    reference : U32 <- S.get((at + 107 : U32))
    value : V.Value <- Arrays.get(reference,index)
    S.from_result(U32,record_ref(value))

def count(+at: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    +reference : U32 <- S.get((at + 107 : U32))
    S.branch(U32,U32.is_eq(reference,0),S.get((at + 14 : U32)),Arrays.length(reference))

def find(n: Nat, +at: U32, +index: U32, +text: U32) -> S.Mem<U32>:
  match n:
    case 0n: S.Mem.pure(U32,0)
    case 1n+p:
      do S.Mem<U32>:
        +reference : U32 <- slot(at,index)
        value : V.Value <- Effects.read(reference,1)
        S.branch(U32,Arrays.is_text(value,text),S.Mem.pure(U32,reference),
          find(p,at,(index + 1 : U32),text))

def lookup(+at: U32, move: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    count_ : U32 <- count(at)
    text : U32 <- C.move_text(move,0)
    find(U32.to_nat(count_),at,0,text)

def integer_field(reference: U32, key: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    value : V.Value <- Effects.read(reference,key)
    S.from_result(U32,Numbers.integer(value))

def text_field(reference: U32, key: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    value : V.Value <- Effects.read(reference,key)
    S.from_result(U32,text_id(value))

def move_from_text(+text: U32) -> S.Mem<U32>:
  S.branch(U32,U32.is_lt(text,Layout.strings_count()),
    (do S.Mem<U32>:
      +id : U32 <- S.get((Layout.moveByText_start() + text : U32))
      S.branch(U32,U32.is_ne(id,0),S.Mem.pure(U32,id),S.raise(U32,3))),S.raise(U32,3))

def target_from_text(+text: U32) -> S.Mem<U32>:
  S.branch(U32,U32.is_lt(text,Layout.strings_count()),
    (do S.Mem<U32>:
      +id : U32 <- S.get((Layout.targetByText_start() + text : U32))
      S.branch(U32,U32.is_ne(id,0),S.Mem.pure(U32,id),S.raise(U32,3))),S.raise(U32,3))

def disabled_value(value: V.Value, hidden: U32) -> Result<&2,&2,U32,U32>:
  match value:
    case V.VBool{flag}: Done{Bool.to_u32(flag)}
    case V.VText{id}: Bool.pick(Result<&2,&2,U32,U32>,U32.is_eq(id,hidden),Done{2},Fail{3})
    case V.VUndefined{}: Done{0}
    case _: Fail{3}

def hidden_text() -> U32:
  # Emitted independently from source primitives, not a guessed literal ID.
  Layout.slot_hidden_text()

def field_id(+at: U32, +index: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    +reference : U32 <- S.get((at + 107 : U32))
    S.branch(U32,U32.is_eq(reference,0),S.get(State.slot(at,index)),
      (do S.Mem<U32>:
        record : U32 <- slot(at,index)
        text : U32 <- text_field(record,1)
        move_from_text(text)))

def field_number(+at: U32, +index: U32, key: U32, offset: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    +reference : U32 <- S.get((at + 107 : U32))
    S.branch(U32,U32.is_eq(reference,0),S.get((State.slot(at,index) + offset : U32)),
      (do S.Mem<U32>:
        record : U32 <- slot(at,index)
        integer_field(record,key)))

def field_disabled(+at: U32, +index: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    +reference : U32 <- S.get((at + 107 : U32))
    S.branch(U32,U32.is_eq(reference,0),S.get((State.slot(at,index) + 3 : U32)),
      (do S.Mem<U32>:
        record : U32 <- slot(at,index)
        value : V.Value <- Effects.read(record,43)
        S.from_result(U32,disabled_value(value,hidden_text()))))

def field_target(+at: U32, +index: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    +reference : U32 <- S.get((at + 107 : U32))
    S.branch(U32,U32.is_eq(reference,0),S.get((State.slot(at,index) + 7 : U32)),
      (do S.Mem<U32>:
        record : U32 <- slot(at,index)
        text : U32 <- text_field(record,2)
        target_from_text(text)))

def mark_used(+at: U32, +index: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    reference : U32 <- S.get((at + 107 : U32))
    S.when(U32.is_ne(reference,0),(do S.Mem<Unit>:
      record : U32 <- slot(at,index)
      Effects.write(record,45,V.VBool{True{}})))
    S.put((State.slot(at,index) + 4 : U32),1)

def put_pp(+at: U32, +index: U32, +pp: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    reference : U32 <- S.get((at + 107 : U32))
    S.when(U32.is_ne(reference,0),(do S.Mem<Unit>:
      record : U32 <- slot(at,index)
      Effects.write(record,41,Store.number(pp))))
    S.put((State.slot(at,index) + 1 : U32),pp)

def effect_name(kind: Nat, id: U32) -> S.Mem<U32>:
  match kind:
    case 0n: C.move_text(id,1)
    case 1n: C.ability_text(id,1)
    case 2n: C.item_text(id,1)
    case 3n: C.condition_text(id,1)
    case _: S.raise(U32,3)

def put_disabled(+at: U32, +index: U32, +hidden: Bool, effect_kind: U32, +effect: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    reference : U32 <- S.get((at + 107 : U32))
    S.when(U32.is_ne(reference,0),(do S.Mem<Unit>:
      +record : U32 <- slot(at,index)
      name : U32 <- S.branch(U32,U32.is_eq(effect,0),text_field(record,40),
        effect_name(U32.to_nat(effect_kind),effect))
      Effects.write(record,43,Bool.pick(V.Value,hidden,V.VText{hidden_text()},V.VBool{True{}}))
      Effects.write(record,44,V.VText{name})))
    S.put((State.slot(at,index) + 3 : U32),Bool.pick(U32,hidden,2,1))

def sync_slots(n: Nat, +at: U32, +index: U32) -> S.Mem<Unit>:
  match n:
    case 0n: S.Mem.pure(Unit,Unit{})
    case 1n+p:
      do S.Mem<Unit>:
        +destination : U32 = State.slot(at,index)
        id : U32 <- field_id(at,index)
        pp : U32 <- field_number(at,index,41,1)
        maximum : U32 <- field_number(at,index,42,2)
        disabled : U32 <- field_disabled(at,index)
        record : U32 <- slot(at,index)
        used : V.Value <- Effects.read(record,45)
        target : U32 <- field_target(at,index)
        S.put(destination,id)
        S.put((destination + 1 : U32),pp)
        S.put((destination + 2 : U32),maximum)
        S.put((destination + 3 : U32),disabled)
        S.put((destination + 4 : U32),Bool.to_u32(V.truthy(used)))
        S.put((destination + 7 : U32),target)
        sync_slots(p,at,(index + 1 : U32))

def sync(+at: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    reference : U32 <- S.get((at + 107 : U32))
    S.when(U32.is_ne(reference,0),(do S.Mem<Unit>:
      +size : U32 <- count(at)
      S.branch(Unit,U32.is_le(size,4),(do S.Mem<Unit>:
        S.put((at + 14 : U32),size)
        sync_slots(U32.to_nat(size),at,0)),S.raise(Unit,3))))

def peek(+at: U32, +index: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    size : U32 <- count(at)
    S.branch(U32,U32.is_lt(index,size),slot(at,index),S.Mem.pure(U32,0))

# Only the move-array part of clearVolatile; callers own its other resets.
def restore(+at: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    base : U32 <- S.get((at + 108 : U32))
    +current : U32 <- Arrays.clone(base)
    S.put((at + 107 : U32),current)
    sync(at)
    return current

def install(+at: U32, +reference: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    size : U32 <- Arrays.length(reference)
    S.branch(U32,U32.is_le(size,4),(do S.Mem<U32>:
      S.put((at + 107 : U32),reference)
      sync(at)
      return reference),S.raise(U32,3))

def transformed_fields(name: U32, id: U32, +base_pp: U32, target: U32) -> +List<Store.Field>:
  [Store.FieldData{40,V.VText{name}},Store.FieldData{1,V.VText{id}},
   Store.FieldData{41,Store.number(U32.min(5,base_pp))},Store.FieldData{42,Store.number(U32.min(5,base_pp))},
   Store.FieldData{2,V.VText{target}},Store.FieldData{43,V.VBool{False{}}},
   Store.FieldData{45,V.VBool{False{}}},Store.FieldData{46,V.VBool{True{}}}]

def transformed_name(move: U32, original: U32, hp_type: U32) -> S.Mem<U32>:
  S.branch(U32,U32.is_eq(move,Moves.moves_hiddenpower()),(do S.Mem<U32>:
    prefix : +List<U32> <- Texts.get(Layout.slot_hidden_power_prefix_text())
    suffix : +List<U32> <- Texts.get(hp_type)
    Texts.intern(Lists.append(prefix,suffix))),S.Mem.pure(U32,original))

def copy_transformed(n: Nat, +target_at: U32, +index: U32, +destination: U32, +hp_type: U32) -> S.Mem<Unit>:
  match n:
    case 0n: S.Mem.pure(Unit,Unit{})
    case 1n+p:
      do S.Mem<Unit>:
        +record : U32 <- slot(target_at,index)
        +id_text : U32 <- text_field(record,1)
        +move : U32 <- move_from_text(id_text)
        original : U32 <- text_field(record,40)
        name : U32 <- transformed_name(move,original,hp_type)
        base_pp : U32 <- C.move(move,5)
        target : U32 <- text_field(record,2)
        next : U32 <- Effects.raw_object(transformed_fields(name,id_text,base_pp,target))
        length : U32 <- Arrays.push(destination,V.VObject{7,next})
        copy_transformed(p,target_at,(index + 1 : U32),destination,hp_type)

# The slot-copy stage of transformInto, not the full lifecycle. Assign the
# empty array before reading target.moveSlots: a self target then has no slots.
def transform(+at: U32, +target_at: U32, hp_type: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    +destination : U32 <- Arrays.make(Nil{})
    S.put((at + 107 : U32),destination)
    +size : U32 <- count(target_at)
    S.branch(U32,U32.is_le(size,4),(do S.Mem<U32>:
      copy_transformed(U32.to_nat(size),target_at,0,destination,hp_type)
      sync(at)
      return destination),S.raise(U32,3))
