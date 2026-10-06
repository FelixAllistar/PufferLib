import Base
import ./Mem.bend as S
import ./Catalog.bend as C
import ./Registry.bend as Registry
import ./EventValue.bend as V
import ./EventContext.bend as Context
import ./EffectMem.bend as Effects
import ./ValueArray.bend as Arrays
import ./data/TypeTexts.bend as Text
import ./data/conditions.bend as D
import ./data/species.bend as P
import ./data/abilities.bend as A
import ./data/callbacks.bend as B

# The pinned Type callback inventory contains three distinct original bodies.
# Species forms inherit the base-species callbacks in the resolved Dex.
type BodyKind is Data:
  UnportedType{}
  Roost{}
  Arceus{}
  Silvally{}

def type_was_key() -> U32:
  18

def condition_kind(+id: U32, callback: U32) -> BodyKind:
  Bool.pick(BodyKind,U32.is_eq(callback,B.callbacks_ontype()),
    Bool.pick(BodyKind,U32.is_eq(id,D.conditions_roost()),Roost{},
      Bool.pick(BodyKind,U32.is_eq(id,D.conditions_arceus()),Arceus{},
        Bool.pick(BodyKind,U32.is_eq(id,D.conditions_silvally()),Silvally{},UnportedType{}))),UnportedType{})

def species_base_kind(+base: U32) -> BodyKind:
  Bool.pick(BodyKind,U32.is_eq(base,P.species_arceus()),Arceus{},
    Bool.pick(BodyKind,U32.is_eq(base,P.species_silvally()),Silvally{},UnportedType{}))

def kind(effect: Registry.Effect, +callback: U32) -> S.Mem<BodyKind>:
  match effect:
    case Registry.ConditionEffect{id}: S.Mem.pure(BodyKind,condition_kind(id,callback))
    case Registry.SpeciesEffect{id}:
      S.branch(BodyKind,U32.is_eq(callback,B.callbacks_ontype()),
        (do S.Mem<BodyKind>:
          base : U32 <- C.species_field(id,1)
          return species_base_kind(base)),S.Mem.pure(BodyKind,UnportedType{}))
    case _: S.Mem.pure(BodyKind,UnportedType{})

def retains(+flags: U32, ability: U32, expected: U32) -> Bool:
  U32.is_ne((flags .&. 2 : U32),0) || U32.is_ne(ability,expected)

def item_text(lookup: Registry.Lookup) -> Result<&2,&2,U32,U32>:
  match lookup:
    case Registry.Missing{}: Done{Text.normal()}
    case Registry.Found{Registry.H{_,3,_,_,_,+payload}}:
      Done{Bool.pick(U32,U32.is_eq(payload,0),Text.normal(),payload)}
    case _: Fail{4}

def species_types(+target: U32, +relay: V.Value, expected: U32, callback: U32) -> S.Mem<V.Value>:
  do S.Mem<V.Value>:
    flags : U32 <- S.get((target + 23 : U32))
    ability : U32 <- S.get((target + 5 : U32))
    S.branch(V.Value,retains(flags,ability,expected),S.Mem.pure(V.Value,relay),
      (do S.Mem<V.Value>:
        held : U32 <- S.get((target + 6 : U32))
        lookup : Registry.Lookup <- Registry.get(Registry.ItemEffect{held},callback)
        text : U32 <- S.from_result(U32,item_text(lookup))
        ref : U32 <- Arrays.make([V.VText{text}])
        return V.VObject{8,ref}))

def call(body: BodyKind, current: Context.Current, +relay: V.Value, target: U32) -> S.Mem<V.Value>:
  match body:
    case UnportedType{}: S.raise(V.Value,4)
    case Roost{}:
      do S.Mem<V.Value>:
        ref : U32 <- S.from_result(U32,Arrays.reference(relay))
        Effects.write(Effects.current_state(current),type_was_key(),relay)
        filtered : U32 <- Arrays.filter_text(ref,Text.flying())
        return V.VObject{8,filtered}
    case Arceus{}: species_types(target,relay,A.abilities_multitype(),B.callbacks_onplate())
    case Silvally{}: species_types(target,relay,A.abilities_rkssystem(),B.callbacks_onmemory())
