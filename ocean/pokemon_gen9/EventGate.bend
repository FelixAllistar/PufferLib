import Base
import ./Mem.bend as S
import ./Catalog.bend as C
import ./Registry.bend as Registry
import ./Suppress.bend as Suppress
import ./data/callbacks.bend as B

# Pre-callback checks from Battle.singleEvent/runEvent. This is a gate, not
# callback execution. Requested event identity controls the exceptions even
# when getCallback aliases SwitchIn to an onStart function.
type Info is Data:
  G{status: U32, ability: Bool, item: Bool, weather: Bool, breakable: Bool}

def ability_info(id: U32) -> S.Mem<Info>:
  do S.Mem<Info>:
    breaks : U32 <- C.ability(id, 0)
    return G{0,True{},False{},False{},U32.is_ne(breaks, 0)}

def condition_info(+id: U32) -> S.Mem<Info>:
  do S.Mem<Info>:
    +kind : U32 <- C.condition(id, 0)
    is_status : U32 <- C.condition(id, 14)
    ability : U32 <- C.condition(id, 1)
    breaks : U32 <- C.ability(ability, 0)
    return G{Bool.pick(U32, U32.is_ne(is_status, 0), id, 0), U32.is_eq(kind, 1),
      U32.is_eq(kind, 2), U32.is_eq(kind, 4), U32.is_ne(breaks, 0)}

def info(effect: Registry.Effect) -> S.Mem<Info>:
  match effect:
    case Registry.AbilityEffect{id}: ability_info(id)
    case Registry.ItemEffect{_}: S.Mem.pure(Info, G{0,False{},True{},False{},False{}})
    case Registry.ConditionEffect{id}: condition_info(id)
    case Registry.MoveEffect{_}: S.Mem.pure(Info, G{0,False{},False{},False{},False{}})
    case Registry.SpeciesEffect{_}: S.Mem.pure(Info, G{0,False{},False{},False{},False{}})
    case Registry.FormatEffect{}: S.Mem.pure(Info, G{0,False{},False{},False{},False{}})

def item_exempt(+event: U32, run_event: Bool) -> Bool:
  U32.is_eq(event, B.callbacks_onstart()) || U32.is_eq(event, B.callbacks_ontakeitem()) ||
    Bool.pick(Bool, run_event, U32.is_eq(event, B.callbacks_onswitchin()), U32.is_eq(event, B.callbacks_onsetability()))

def weather_exempt(+event: U32, run_event: Bool) -> Bool:
  Bool.pick(Bool, run_event,
    U32.is_eq(event, B.callbacks_onresidual()) || U32.is_eq(event, B.callbacks_onend()),
    U32.is_eq(event, B.callbacks_onfieldstart()) || U32.is_eq(event, B.callbacks_onfieldresidual()) ||
      U32.is_eq(event, B.callbacks_onfieldend()))

def check_info(effect: Info, +event: U32, +at: U32, +pokemon: Bool, +run_event: Bool) -> S.Mem<Bool>:
  G{+status, +ability, item, weather, breakable} = effect
  do S.Mem<Bool>:
    current : U32 <- S.get((at + 13 : U32))
    +changed : Bool = U32.is_ne(status, 0) &&
      (run_event && Bool.not(pokemon) || pokemon && U32.is_ne(current, status))
    S.branch(Bool, changed, S.Mem.pure(Bool, False{}),
      (do S.Mem<Bool>:
        mold_breaker : Bool <- S.branch(Bool, ability && breakable &&
          (run_event || U32.is_eq(event, B.callbacks_onswitchin())),
          Suppress.breaker(at), S.Mem.pure(Bool, False{}))
        item_ignored : Bool <- S.branch(Bool, item && pokemon && Bool.not(item_exempt(event, run_event)),
          Suppress.item(at, False{}), S.Mem.pure(Bool, False{}))
        ability_ignored : Bool <- S.branch(Bool, ability && pokemon && U32.is_ne(event, B.callbacks_onend()),
          Suppress.ability(at), S.Mem.pure(Bool, False{}))
        weather_ignored : Bool <- S.branch(Bool, (weather || run_event && U32.is_eq(event, B.callbacks_onweather())) &&
          Bool.not(weather_exempt(event, run_event)), Suppress.weather(), S.Mem.pure(Bool, False{}))
        return Bool.not(mold_breaker || item_ignored || ability_ignored || weather_ignored)))

def check(effect: Registry.Effect, event: U32, at: U32, pokemon: Bool, +run_event: Bool) -> S.Mem<Bool>:
  do S.Mem<Bool>:
    +depth : U32 <- S.get(536)
    +lines : U32 <- S.get(537)
    S.branch(Bool, U32.is_ge(depth, 8), S.raise(Bool, 5),
      S.branch(Bool, Bool.not(run_event) && U32.is_gt(lines, 1000), S.raise(Bool, 6),
        (do S.Mem<Bool>:
          metadata : Info <- info(effect)
          check_info(metadata, event, at, pokemon, run_event))))
