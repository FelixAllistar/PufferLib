import Base
import ./Mem.bend as S
import ./Suppress.bend as Suppress
import ./Registry.bend as Registry
import ./Catalog.bend as C
import ./EventContext.bend as Context
import ./data/conditions.bend as D
import ./data/abilities.bend as A
import ./data/items.bend as I
import ./Lists.bend as L

# Read-only original Field/Pokemon weather accessors. Conditions are IDs here;
# empty weather is zero. This does not set/clear weather or emit messages.
def visible(weather: U32, ignored: Bool) -> U32:
  Bool.pick(U32,ignored,0,weather)

def field() -> S.Mem<U32>:
  do S.Mem<U32>:
    weather : U32 <- S.get(533)
    ignored : Bool <- Suppress.weather()
    return visible(weather,ignored)

def is_sun_rain(+weather: U32) -> Bool:
  U32.is_eq(weather,D.conditions_sunnyday()) || U32.is_eq(weather,D.conditions_raindance()) ||
    U32.is_eq(weather,D.conditions_desolateland()) || U32.is_eq(weather,D.conditions_primordialsea())

def eligible_kind(effect: Registry.Effect) -> S.Mem<Bool>:
  match effect:
    case Registry.MoveEffect{_}: S.Mem.pure(Bool,True{})
    case Registry.ConditionEffect{id}:
      do S.Mem<Bool>:
        kind : U32 <- C.condition(id,0)
        return U32.is_eq(kind,4)
    case _: S.Mem.pure(Bool,False{})

def eligible(ref: Context.EffectRef) -> S.Mem<Bool>:
  match ref:
    case Context.EmptyEffect{}: S.Mem.pure(Bool,False{})
    case Context.BoundEffect{+effect}:
      do S.Mem<Bool>:
        kind : Bool <- eligible_kind(effect)
        identity : U32 <- Registry.identity(effect,0)
        megasol : U32 <- C.ability_text(A.abilities_megasol(),0)
        return kind || U32.is_eq(identity,megasol)

def source_effect(current: Context.Current) -> Context.EffectRef:
  Context.C{ref,_,_,_} = current
  ref

def mega_sol(ref: Context.EffectRef) -> S.Mem<Bool>:
  do S.Mem<Bool>:
    +actor : U32 <- S.get(548)
    S.branch(Bool,U32.is_eq(actor,0),S.Mem.pure(Bool,False{}),
      (do S.Mem<Bool>:
        ability : U32 <- S.get((actor + 5 : U32))
        S.branch(Bool,U32.is_eq(ability,A.abilities_megasol()),
          (do S.Mem<Bool>:
            ignored : Bool <- Suppress.ability(actor)
            allowed : Bool <- eligible(ref)
            return Bool.not(ignored) && allowed),S.Mem.pure(Bool,False{}))))

def umbrella(+at: U32, +weather: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    item : U32 <- S.get((at + 6 : U32))
    S.branch(U32,U32.is_eq(item,I.items_utilityumbrella()) && is_sun_rain(weather),
      (do S.Mem<U32>:
        ignored : Bool <- Suppress.item(at,False{})
        return Bool.pick(U32,ignored,weather,0)),S.Mem.pure(U32,weather))

def pokemon(at: U32, current: Context.Current) -> S.Mem<U32>:
  do S.Mem<U32>:
    weather : U32 <- field()
    override : Bool <- mega_sol(source_effect(current))
    S.branch(U32,override,S.Mem.pure(U32,D.conditions_sunnyday()),umbrella(at,weather))

def matches(ids: +List<U32>) -> S.Mem<Bool>:
  do S.Mem<Bool>:
    weather : U32 <- field()
    return L.contains(ids,weather)

def field_condition(first: U32, second: U32) -> S.Mem<Bool>:
  matches([first,second])

def pokemon_condition(at: U32, current: Context.Current, +first: U32, +second: U32) -> S.Mem<Bool>:
  do S.Mem<Bool>:
    +weather : U32 <- pokemon(at,current)
    return U32.is_eq(weather,first) || U32.is_eq(weather,second)
