import Base
import ./Mem.bend as S
import ./Registry.bend as Registry
import ./EventContext.bend as Context
import ./EventValue.bend as V
import ./EventGate.bend as Gate
import ./Callbacks.bend as Callbacks
import ./EffectMem.bend as Effects

type Selection is Data:
  DefaultCallback{}
  SuppliedCallback{handler: Registry.Handler}

def event_id(event: Context.EventData) -> U32:
  Context.E{id,_,_,_,_} = event
  id

def target_value(event: Context.EventData) -> V.Value:
  Context.E{_,target,_,_,_} = event
  target

def pokemon_at(value: V.Value) -> U32:
  match value:
    case V.VObject{family,at}: Bool.pick(U32,U32.is_eq(family,1),at,0)
    case _: 0

def selected(effect: Registry.Effect, callback: U32, choice: Selection) -> S.Mem<Registry.Lookup>:
  match choice:
    case DefaultCallback{}: Registry.get(effect,callback)
    case SuppliedCallback{+handler}:
      S.branch(Registry.Lookup,Registry.truthy(Registry.Found{handler}),
        S.Mem.pure(Registry.Lookup,Registry.Found{handler}),Registry.get(effect,callback))

def restore(parent: Context.Current, +relay: V.Value, returned: Callbacks.Returned) -> Callbacks.Returned:
  Callbacks.ReturnedData{_,value} = returned
  Callbacks.ReturnedData{parent,V.single_result(relay,value)}

def invoke(lookup: Registry.Lookup, effect: Registry.Effect, parent: Context.Current,
  +child: Context.Current, +relay: V.Value, target: U32) -> S.Mem<Callbacks.Returned>:
  match lookup:
    case Registry.Missing{}: S.Mem.pure(Callbacks.Returned,Callbacks.ReturnedData{parent,relay})
    case Registry.Found{handler}:
      do S.Mem<Callbacks.Returned>:
        state : U32 <- Effects.ensure(Effects.current_state(child))
        returned : Callbacks.Returned <- Callbacks.call(effect,handler,Effects.replace_state(child,state),
          Callbacks.ArgsData{relay,target})
        return restore(parent,relay,returned)

# Callback collection is not part of singleEvent: the source reads the
# requested raw callback or its explicit custom callback. SwitchIn's getCallback
# alias is used by listener discovery, and must not be invented here.
def call(+effect: Registry.Effect, choice: Selection, state: U32, +event: Context.EventData,
  +parent: Context.Current, relay: V.Value) -> S.Mem<Callbacks.Returned>:
  do S.Mem<Callbacks.Returned>:
    lines : U32 <- S.get(537)
    child : Context.Current <- S.from_result(Context.Current,
      Context.enter_single(parent,Context.BoundEffect{effect},state,event,lines))
    +initial : V.Value = V.initial(relay,True{})
    +target : U32 = pokemon_at(target_value(event))
    metadata : Gate.Info <- Gate.info(effect)
    allowed : Bool <- Gate.check_info(metadata,event_id(event),target,U32.is_ne(target,0),False{})
    S.branch(Callbacks.Returned,allowed,
      (do S.Mem<Callbacks.Returned>:
        lookup : Registry.Lookup <- selected(effect,event_id(event),choice)
        invoke(lookup,effect,parent,child,initial,target)),
      S.Mem.pure(Callbacks.Returned,Callbacks.ReturnedData{parent,initial}))
