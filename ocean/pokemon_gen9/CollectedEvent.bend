import Base
import ./EventListener.bend as Listener
import ./Mem.bend as S
import ./Collect.bend as Collect
import ./RunEvent.bend as Run
import ./VectorEvent.bend as Vector
import ./Registry.bend as Registry
import ./ListenerPriority.bend as Priority
import ./EffectMem.bend as Effects
import ./EventContext.bend as Context
import ./EventValue.bend as V

# Original runEvent front half joined to the checked execution loops. Validate
# event depth before collecting or allocating an onEffect listener. The latter
# uses raw lookup, not SwitchIn's getCallback alias, and allocates its state
# before sorting even when the callback is a scalar literal.
def event_id(event: Context.EventData) -> U32:
  Context.E{id,_,_,_,_} = event
  id

def target(event: Context.EventData) -> V.Value:
  Context.E{_,value,_,_,_} = event
  value

def source_value(value: V.Value) -> U32:
  match value:
    case V.VObject{1,at}: at
    case _: 0

def source(event: Context.EventData) -> U32:
  Context.E{_,_,value,_,_} = event
  source_value(value)

def on_effect_lookup(lookup: Registry.Lookup, effect: Registry.Effect, callback: U32,
  value: V.Value, listeners: +List<Listener.ListenerRow>, array: Bool) -> S.Mem<+List<Listener.ListenerRow>>:
  match lookup:
    case Registry.Missing{}: S.Mem.pure(+List<Listener.ListenerRow>,listeners)
    case Registry.Found{handler}:
      S.branch(+List<Listener.ListenerRow>,array,S.raise(+List<Listener.ListenerRow>,7),
        (do S.Mem<+List<Listener.ListenerRow>>:
          state : U32 <- Effects.empty_state()
          facts : Priority.HolderFacts <- Collect.holder(value)
          listener : Listener.ListenerRow <- Priority.resolve(effect,handler,callback,state,0,0,facts,0)
          return Con{listener,listeners}))

def on_effect_ref(ref: Context.EffectRef, +callback: U32, value: V.Value,
  listeners: +List<Listener.ListenerRow>, array: Bool) -> S.Mem<+List<Listener.ListenerRow>>:
  match ref:
    case Context.EmptyEffect{}: S.raise(+List<Listener.ListenerRow>,7)
    case Context.BoundEffect{+effect}:
      do S.Mem<+List<Listener.ListenerRow>>:
        lookup : Registry.Lookup <- Registry.get(effect,callback)
        on_effect_lookup(lookup,effect,callback,value,listeners,array)

def with_on_effect(+listeners: +List<Listener.ListenerRow>, enabled: Bool, ref: Context.EffectRef,
  callback: U32, value: V.Value, array: Bool) -> S.Mem<+List<Listener.ListenerRow>>:
  S.branch(+List<Listener.ListenerRow>,enabled,on_effect_ref(ref,callback,value,listeners,array),
    S.Mem.pure(+List<Listener.ListenerRow>,listeners))

def scalar(+event: Context.EventData, +parent: Context.Current, relay: V.Value,
  fast_exit: Bool, on_effect: Bool, ref: Context.EffectRef) -> S.Mem<Run.Executed>:
  do S.Mem<Run.Executed>:
    checked : Context.Current <- S.from_result(Context.Current,Context.enter_run(parent,event))
    +normalized : Context.EventData = Context.run_event(event)
    xs : +List<Collect.Collected> <- Collect.scalar(target(normalized),event_id(event),source(event))
    rows : +List<Listener.ListenerRow> <- S.from_result(+List<Listener.ListenerRow>,Collect.runnable(xs))
    listeners : +List<Listener.ListenerRow> <- with_on_effect(rows,on_effect,ref,event_id(event),target(normalized),False{})
    Run.call_scalar(listeners,event,parent,relay,fast_exit)

def vector(+targets: +List<U32>, values: +List<V.Value>, +event: Context.EventData,
  +parent: Context.Current, has_relay: Bool, fast_exit: Bool, on_effect: Bool,
  ref: Context.EffectRef) -> S.Mem<Vector.VectorExecuted>:
  do S.Mem<Vector.VectorExecuted>:
    checked : Context.Current <- S.from_result(Context.Current,Context.enter_run(parent,event))
    xs : +List<Collect.Collected> <- Collect.vector(targets,event_id(event),source(event),0)
    rows : +List<Listener.ListenerRow> <- S.from_result(+List<Listener.ListenerRow>,Collect.runnable(xs))
    listeners : +List<Listener.ListenerRow> <- with_on_effect(rows,on_effect,ref,event_id(event),V.VUndefined{},True{})
    Vector.call(listeners,targets,values,event,parent,has_relay,fast_exit)
