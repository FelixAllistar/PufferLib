import Base
import ./Mem.bend as S
import ./EffectMem.bend as Effects
import ./EffectInit.bend as Roots
import ./EventValue.bend as V
import ./EventContext.bend as Context
import ./PokemonTypes.bend as Types
import ./Suppress.bend as Suppress
import ./data/TypeTexts.bend as Text
import ./data/abilities.bend as A
import ./data/items.bend as I
import ./data/conditions.bend as D

# Gen 9 Pokemon.isGrounded. Preserve source early returns and null (ability
# immunity) separately from false (Flying, volatile effects or Air Balloon).
def levitation_ability(+ability: U32, ignored: Bool) -> Bool:
  Bool.not(ignored) && (U32.is_eq(ability,A.abilities_levitate()) || U32.is_eq(ability,A.abilities_eelevate()))

def item_result(item: U32) -> V.Value:
  V.VBool{U32.is_ne(item,I.items_airballoon())}

def present(+reference: U32, condition: U32) -> S.Mem<Bool>:
  S.branch(Bool,U32.is_eq(reference,0),S.Mem.pure(Bool,False{}),Effects.has(reference,Roots.condition_key(condition)))

def volatile_condition(at: U32, condition: U32) -> S.Mem<Bool>:
  do S.Mem<Bool>:
    reference : U32 <- S.get(Roots.volatile_map(at))
    present(reference,condition)

def after_magnet_rise(at: U32, item: U32) -> S.Mem<V.Value>:
  do S.Mem<V.Value>:
    telekinesis : Bool <- volatile_condition(at,D.conditions_telekinesis())
    return Bool.pick(V.Value,telekinesis,V.VBool{False{}},item_result(item))

def after_ability(+at: U32, item: U32) -> S.Mem<V.Value>:
  do S.Mem<V.Value>:
    rise : Bool <- volatile_condition(at,D.conditions_magnetrise())
    S.branch(V.Value,rise,S.Mem.pure(V.Value,V.VBool{False{}}),after_magnet_rise(at,item))

def effective_levitation(+at: U32) -> S.Mem<Bool>:
  do S.Mem<Bool>:
    +ability : U32 <- S.get((at + 5 : U32))
    S.branch(Bool,levitation_ability(ability,False{}),
      (do S.Mem<Bool>:
        ignored : Bool <- Suppress.ability(at)
        return levitation_ability(ability,ignored)),S.Mem.pure(Bool,False{}))

def ability_step(+at: U32, +item: U32) -> S.Mem<V.Value>:
  do S.Mem<V.Value>:
    levitating : Bool <- effective_levitation(at)
    S.branch(V.Value,levitating,
      (do S.Mem<V.Value>:
        ignored : Bool <- Suppress.breaker(at)
        S.branch(V.Value,ignored,after_ability(at,item),S.Mem.pure(V.Value,V.VNull{}))),after_ability(at,item))

def flying(+at: U32, +current: Context.Current, negate_immunity: Bool) -> S.Mem<Bool>:
  S.branch(Bool,negate_immunity,S.Mem.pure(Bool,False{}),
    (do S.Mem<Bool>:
      matches : Bool <- Types.has(at,current,[Text.flying()])
      S.branch(Bool,matches,
        (do S.Mem<Bool>:
          unknown : Bool <- Types.has(at,current,[Text.unknown()])
          exception : Bool <- S.branch(Bool,unknown,volatile_condition(at,D.conditions_roost()),S.Mem.pure(Bool,False{}))
          return Bool.not(exception)),S.Mem.pure(Bool,False{}))))

def type_step(+at: U32, current: Context.Current, negate_immunity: Bool, item: U32) -> S.Mem<V.Value>:
  do S.Mem<V.Value>:
    airborne : Bool <- flying(at,current,negate_immunity)
    S.branch(V.Value,airborne,S.Mem.pure(V.Value,V.VBool{False{}}),ability_step(at,item))

def item_step(+at: U32, current: Context.Current, negate_immunity: Bool) -> S.Mem<V.Value>:
  do S.Mem<V.Value>:
    ignored : Bool <- Suppress.item(at,False{})
    +held : U32 <- S.branch(U32,ignored,S.Mem.pure(U32,0),S.get((at + 6 : U32)))
    S.branch(V.Value,U32.is_eq(held,I.items_ironball()),S.Mem.pure(V.Value,V.VBool{True{}}),
      type_step(at,current,negate_immunity,held))

def after_ingrain(+at: U32, current: Context.Current, negate_immunity: Bool) -> S.Mem<V.Value>:
  do S.Mem<V.Value>:
    smackdown : Bool <- volatile_condition(at,D.conditions_smackdown())
    S.branch(V.Value,smackdown,S.Mem.pure(V.Value,V.VBool{True{}}),item_step(at,current,negate_immunity))

def after_gravity(+at: U32, current: Context.Current, negate_immunity: Bool) -> S.Mem<V.Value>:
  do S.Mem<V.Value>:
    ingrain : Bool <- volatile_condition(at,D.conditions_ingrain())
    S.branch(V.Value,ingrain,S.Mem.pure(V.Value,V.VBool{True{}}),after_ingrain(at,current,negate_immunity))

def get(at: U32, current: Context.Current, negate_immunity: Bool) -> S.Mem<V.Value>:
  do S.Mem<V.Value>:
    reference : U32 <- S.get(Roots.pseudo_weather_map())
    gravity : Bool <- present(reference,D.conditions_gravity())
    S.branch(V.Value,gravity,S.Mem.pure(V.Value,V.VBool{True{}}),after_gravity(at,current,negate_immunity))
