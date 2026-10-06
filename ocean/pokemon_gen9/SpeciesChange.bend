import Base
import ./Mem.bend as S
import ./Catalog.bend as C
import ./TypeInit.bend as Types
import ./TypeChange.bend as Change
import ./TextJoin.bend as Join
import ./Spread.bend as Spread
import ./EventContext.bend as Context
import ./EventValue.bend as V
import ./Registry.bend as Registry
import ./CollectedEvent.bend as Events
import ./RunEvent.bend as Run
import ./Callbacks.bend as Callbacks
import ./data/callbacks.bend as B
import ./data/Layout.bend as Layout

# Value-object namespaces: resolved Species=5; other resolved effects=16..20;
# battle's empty effect=21. These are immutable Dex identities, not heap IDs.
def effect_value(effect: Context.EffectRef) -> V.Value:
  match effect:
    case Context.EmptyEffect{}: V.VObject{21,0}
    case Context.BoundEffect{raw}:
      match raw:
        case Registry.MoveEffect{id}: V.VObject{16,id}
        case Registry.AbilityEffect{id}: V.VObject{17,id}
        case Registry.ItemEffect{id}: V.VObject{18,id}
        case Registry.ConditionEffect{id}: V.VObject{19,id}
        case Registry.SpeciesEffect{id}: V.VObject{5,id}
        case Registry.FormatEffect{}: V.VObject{20,0}

def default_source(parent: Context.Current) -> V.Value:
  Context.C{effect,_,_,_} = parent
  effect_value(effect)

def commit(+at: U32, +raw: U32, +display: U32, is_transform: Bool) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    canonical : U32 <- C.species_field(display,27)
    S.put((at + 1 : U32),canonical)
    S.put((at + 77 : U32),display)
    reference : U32 <- Types.shared(display)
    changed : Bool <- Change.set(at,Change.ArrayTypes{reference},True{})
    raw_reference : U32 <- Types.shared(raw)
    apparent : U32 <- Join.join(raw_reference,[47])
    S.put((at + 100 : U32),apparent)
    added : U32 <- C.species_field(display,52)
    S.put((at + 97 : U32),added)
    S.put((at + 99 : U32),1)
    weight : U32 <- C.species_field(display,24)
    S.put((at + 22 : U32),weight)
    Spread.apply(at,display,is_transform)

def species_id(value: V.Value) -> S.Mem<U32>:
  match value:
    case V.VObject{5,+display}:
      S.branch(U32,U32.is_gt(display,0) && U32.is_lt(display,Layout.species_count()),
        S.Mem.pure(U32,display),S.raise(U32,3))
    case _: S.raise(U32,4)

def returned(+at: U32, +raw: U32, is_transform: Bool, result: Callbacks.Returned) -> S.Mem<Callbacks.Returned>:
  Callbacks.ReturnedData{+current,+value} = result
  S.branch(Callbacks.Returned,V.truthy(value),
    (do S.Mem<Callbacks.Returned>:
      display : U32 <- species_id(value)
      commit(at,raw,display,is_transform)
      return Callbacks.ReturnedData{current,value}),
    S.Mem.pure(Callbacks.Returned,Callbacks.ReturnedData{current,V.VNull{}}))

def execution_returned(result: Run.Executed) -> Callbacks.Returned:
  Run.ExecutedData{value,_} = result
  value

# Native discovery and depth checks run before any Pokemon mutation. The
# pinned default format has no applicable ModifySpecies handler; unexpected
# executable handlers still fail through ordinary callback dispatch.
def set(+at: U32, +raw: U32, source: V.Value, is_transform: Bool,
  parent: Context.Current) -> S.Mem<Callbacks.Returned>:
  do S.Mem<Callbacks.Returned>:
    result : Run.Executed <- Events.scalar(Context.E{B.callbacks_onmodifyspecies(),
      V.VObject{1,at},V.VNull{},source,Context.NoModifier{}},parent,V.VObject{5,raw},
      False{},False{},Context.EmptyEffect{})
    returned(at,raw,is_transform,execution_returned(result))
