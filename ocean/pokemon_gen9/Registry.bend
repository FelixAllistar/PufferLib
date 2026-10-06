import Base
import ./Mem.bend as S
import ./Catalog.bend as C
import ./Lists.bend as L
import ./data/Layout.bend as T
import ./data/callbacks.bend as B

# All callback descriptors are imported from resolved Dex records. Executable
# callbacks retain tag 0 and require native handler dispatch; metadata does not
# substitute for their implementation. Tags 1/2/3 are bool/U32/text constants,
# 4 is metadata without a callback, 5 is signed tenths +32768 (e.g. Stall).
type Effect is Data:
  MoveEffect{id: U32}
  AbilityEffect{id: U32}
  ItemEffect{id: U32}
  ConditionEffect{id: U32}
  SpeciesEffect{id: U32}
  FormatEffect{}

type Handler is Data:
  H{callback: U32, tag: U32, order: U32, priority: U32, sub_order: U32, value: U32}

type Lookup is Data:
  Missing{}
  Found{handler: Handler}

def row(effect: Effect) -> U32:
  match effect:
    case MoveEffect{id}: (T.moves_start() + id * T.moves_stride() : U32)
    case AbilityEffect{id}: (T.abilities_start() + id * T.abilities_stride() : U32)
    case ItemEffect{id}: (T.items_start() + id * T.items_stride() : U32)
    case ConditionEffect{id}: (T.conditions_start() + id * T.conditions_stride() : U32)
    case SpeciesEffect{id}: (T.species_start() + id * T.species_stride() : U32)
    case FormatEffect{}: 0

def count_field(effect: Effect) -> U32:
  match effect:
    case AbilityEffect{_}: 5
    case ItemEffect{_}: 5
    case SpeciesEffect{_}: 28
    case _: 12

def valid(effect: Effect) -> Bool:
  match effect:
    case MoveEffect{+id}: U32.is_gt(id, 0) && U32.is_lt(id, T.moves_count())
    case AbilityEffect{+id}: U32.is_gt(id, 0) && U32.is_lt(id, T.abilities_count())
    case ItemEffect{+id}: U32.is_gt(id, 0) && U32.is_lt(id, T.items_count())
    case ConditionEffect{+id}: U32.is_gt(id, 0) && U32.is_lt(id, T.conditions_count())
    case SpeciesEffect{+id}: U32.is_gt(id, 0) && U32.is_lt(id, T.species_count())
    case FormatEffect{}: True{}

def identity(effect: Effect, field: U32) -> S.Mem<U32>:
  match effect:
    case MoveEffect{id}: C.move_text(id,field)
    case AbilityEffect{id}: C.ability_text(id,field)
    case ItemEffect{id}: C.item_text(id,field)
    case ConditionEffect{id}: C.condition_text(id,field)
    case SpeciesEffect{id}: C.species_text(id,field)
    case FormatEffect{}: S.Mem.pure(U32,Bool.pick(U32,U32.is_eq(field,0),T.format_id_text(),T.format_name_text()))

def decode(+xs: +List<U32>) -> Handler:
  H{L.at(xs, 0n), L.at(xs, 1n), L.at(xs, 2n), L.at(xs, 3n), L.at(xs, 4n), L.at(xs, 5n)}

def scan(n: Nat, +at: U32, +callback: U32) -> S.Mem<Lookup>:
  match n:
    case 0n: S.Mem.pure(Lookup, Missing{})
    case 1n+p:
      do S.Mem<Lookup>:
        key : U32 <- S.get(at)
        S.branch(Lookup, U32.is_eq(key, callback),
          (do S.Mem<Lookup>:
            xs : +List<U32> <- C.read_list(6n, at)
            return Found{decode(xs)}), scan(p, (at + 8 : U32), callback))

def get_table(+effect: Effect, callback: U32) -> S.Mem<Lookup>:
  do S.Mem<Lookup>:
    +at : U32 = (row(effect) + count_field(effect) : U32)
    count : U32 <- S.get(at)
    ptr : U32 <- S.get((at + 1 : U32))
    scan(U32.to_nat(count), ptr, callback)

def get_raw(effect: Effect, callback: U32) -> S.Mem<Lookup>:
  match effect:
    case FormatEffect{}: S.Mem.pure(Lookup,Missing{})
    case _: get_table(effect,callback)

def present(value: Lookup) -> Lookup:
  match value:
    case Missing{}: Missing{}
    case Found{+h}:
      H{_, tag, _, _, _, _} = h
      Bool.pick(Lookup, U32.is_eq(tag, 4), Missing{}, Found{h})

def get(effect: Effect, callback: U32) -> S.Mem<Lookup>:
  do S.Mem<Lookup>:
    raw : Lookup <- get_raw(effect, callback)
    return present(raw)

def truthy(value: Lookup) -> Bool:
  match value:
    case Missing{}: False{}
    case Found{H{_, +tag, _, _, _, payload}}: U32.is_ne(tag, 4) &&
      (U32.is_eq(tag, 0) || U32.is_ne(payload,Bool.pick(U32,U32.is_eq(tag,5),32768,0)))

def with_metadata(value: Lookup, meta: Lookup) -> Lookup:
  match value:
    case Missing{}: Missing{}
    case Found{H{code, tag, _, _, _, payload}}:
      match meta:
        case Missing{}: Found{H{code, tag, 0, 32768, 32768, payload}}
        case Found{H{_, _, order, priority, sub, _}}: Found{H{code, tag, order, priority, sub, payload}}

def start_alias(+effect: Effect) -> S.Mem<Lookup>:
  do S.Mem<Lookup>:
    value : Lookup <- get(effect, B.callbacks_onstart())
    meta : Lookup <- get_raw(effect, B.callbacks_onswitchin())
    return with_metadata(value, meta)

def ability_or_item(effect: Effect) -> S.Mem<Bool>:
  match effect:
    case AbilityEffect{_}: S.Mem.pure(Bool, True{})
    case ItemEffect{_}: S.Mem.pure(Bool, True{})
    case ConditionEffect{id}:
      do S.Mem<Bool>:
        +kind : U32 <- C.condition(id, 0)
        return U32.is_eq(kind, 1) || U32.is_eq(kind, 2)
    case _: S.Mem.pure(Bool, False{})

def fallback(value: Lookup, +effect: Effect, callback: U32, pokemon: Bool) -> S.Mem<Lookup>:
  match value:
    case Found{h}: S.Mem.pure(Lookup, Found{h})
    case Missing{}:
      S.branch(Lookup, pokemon && U32.is_eq(callback, B.callbacks_onswitchin()),
        (do S.Mem<Lookup>:
          eligible : Bool <- ability_or_item(effect)
          any : Lookup <- get(effect, B.callbacks_onanyswitchin())
          S.branch(Lookup, eligible && Bool.not(truthy(any)), start_alias(effect),
            S.Mem.pure(Lookup, Missing{}))), S.Mem.pure(Lookup, Missing{}))

def callback(+effect: Effect, +code: U32, pokemon: Bool) -> S.Mem<Lookup>:
  do S.Mem<Lookup>:
    current : Lookup <- get(effect, code)
    fallback(current, effect, code, pokemon)
