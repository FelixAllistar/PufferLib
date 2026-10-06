import ./ListenerExecution.bend as Core
import Base
import ./EventListener.bend as Listener
import ./Mem.bend as S
import ./Lists.bend as L
import ./Catalog.bend as C
import ./Queue.bend as Q
import ./Registry.bend as Registry
import ./EventContext.bend as Context
import ./EventValue.bend as V
import ./EventGate.bend as Gate
import ./Callbacks.bend as Callbacks
import ./SingleEvent.bend as Single
import ./EffectMem.bend as Effects
import ./data/callbacks.bend as B

# These are already-collected listeners. Full holder discovery/onEffect
# insertion and array-target relay handling are separate engine operations.
def sort_mode(event: U32, fast_exit: Bool) -> Nat:
  Core.sort_mode(event,fast_exit)

def at(listeners: +List<Listener.ListenerRow>, index: Nat) -> Result<&2,&2,U32,Listener.ListenerRow>:
  Core.at(listeners,index)

def key_fields(listener: Listener.ListenerRow, index: U32) -> +List<U32>:
  Core.key_fields(listener,index)

def ordered(listeners: +List<Listener.ListenerRow>, event: U32, fast_exit: Bool) -> S.Mem<+List<U32>>:
  Core.ordered(listeners,event,fast_exit)

def context_event(current: Context.Current) -> Context.EventData:
  Context.C{_,_,event,_} = current
  event

def restored_handler(parent: Context.Current, returned: Callbacks.Returned) -> Callbacks.Returned:
  Context.C{effect,state,_,_} = parent
  Callbacks.ReturnedData{child,value} = returned
  Callbacks.ReturnedData{Context.leave_handler(effect,state,child),value}

def invoke(listener: Listener.ListenerRow, +current: Context.Current, +relay: V.Value, +target: U32) -> S.Mem<Callbacks.Returned>:
  Listener.ListenerData{+effect,+handler,state,_,_,_,_,_,holder_value} = listener
  Registry.H{_,tag,_,_,_,_} = handler
  S.branch(Callbacks.Returned,U32.is_eq(tag,0),
    (do S.Mem<Callbacks.Returned>:
      bound_state : U32 <- Effects.bind_holder(state,holder_value)
      returned : Callbacks.Returned <- Callbacks.call(effect,handler,
        Context.enter_handler(current,Context.BoundEffect{effect},bound_state),Callbacks.ArgsData{relay,target})
      return restored_handler(current,returned)),
    Callbacks.call(effect,handler,current,Callbacks.ArgsData{relay,target}))

def listener_gate(listener: Listener.ListenerRow, event: U32) -> S.Mem<Bool>:
  Core.listener_gate(listener,event)

type Progress is Data:
  ProgressData{current: Context.Current, relay: V.Value, passed: V.Value, has_relay: Bool,
    stopped: Bool, visited: +List<U32>}

type Executed is Data:
  ExecutedData{returned: Callbacks.Returned, visited: +List<U32>}

def visiting(progress: Progress, index: U32) -> Progress:
  ProgressData{current,relay,passed,has_relay,stopped,visited} = progress
  ProgressData{current,relay,passed,has_relay,stopped,L.append(visited,[index])}

def progress_after(parent: Progress, next: V.Relay, returned: Callbacks.Returned) -> Progress:
  ProgressData{_,_,passed,+has_relay,_,visited} = parent
  V.R{+value,stopped} = next
  Callbacks.ReturnedData{current,_} = returned
  ProgressData{current,value,Bool.pick(V.Value,has_relay,value,passed),has_relay,stopped,visited}

def returned_progress(+parent: Progress, +returned: Callbacks.Returned, fast_exit: Bool, index: U32) -> Progress:
  ProgressData{_,relay,_,_,_,_} = parent
  Callbacks.ReturnedData{_,value} = returned
  progress_after(visiting(parent,index),V.replace(relay,value,fast_exit),returned)

def progress_current(progress: Progress) -> Context.Current:
  ProgressData{current,_,_,_,_,_} = progress
  current

def progress_passed(progress: Progress) -> V.Value:
  ProgressData{_,_,passed,_,_,_} = progress
  passed

def progress_stopped(progress: Progress) -> Bool:
  ProgressData{_,_,_,_,stopped,_} = progress
  stopped

def execute_ids(ids: +List<U32>, +listeners: +List<Listener.ListenerRow>, +progress: Progress,
  +event: U32, +target: U32, +fast_exit: Bool) -> S.Mem<Progress>:
  match ids:
    case Nil{}: S.Mem.pure(Progress,progress)
    case Con{+index,tail}:
      do S.Mem<Progress>:
        +listener : Listener.ListenerRow <- S.from_result(Listener.ListenerRow,at(listeners,U32.to_nat(index)))
        allowed : Bool <- listener_gate(listener,event)
        +next : Progress <- S.branch(Progress,allowed,
          (do S.Mem<Progress>:
            returned : Callbacks.Returned <- invoke(listener,progress_current(progress),progress_passed(progress),target)
            return returned_progress(progress,returned,fast_exit,index)),S.Mem.pure(Progress,progress))
        S.branch(Progress,progress_stopped(next),S.Mem.pure(Progress,next),
          execute_ids(tail,listeners,next,event,target,fast_exit))

def finish_parts(parent_event: Context.EventData, parent_depth: U32, current: Context.Current,
  relay: V.Value, visited: +List<U32>, event: Context.EventData) -> Executed:
  Context.E{_,_,_,_,modifier} = event
  ExecutedData{Callbacks.ReturnedData{Context.leave_run(parent_event,parent_depth,current),
    V.final_value(relay,Context.modifier_value(modifier))},visited}

def finish(parent: Context.Current, progress: Progress) -> Executed:
  Context.C{_,_,parent_event,parent_depth} = parent
  ProgressData{+current,relay,_,_,_,visited} = progress
  finish_parts(parent_event,parent_depth,current,relay,visited,context_event(current))

def has_relay(value: V.Value) -> Bool:
  match value:
    case V.VUndefined{}: False{}
    case V.VNull{}: False{}
    case _: True{}

# Only the scalar target path. Indexed/array relays require their own source
# loop (including DamagingHit's zero exception); never flatten them into this.
def call_scalar(+listeners: +List<Listener.ListenerRow>, +event: Context.EventData, +parent: Context.Current,
  +relay: V.Value, +fast_exit: Bool) -> S.Mem<Executed>:
  do S.Mem<Executed>:
    child : Context.Current <- S.from_result(Context.Current,Context.enter_run(parent,event))
    +event_code : U32 = Single.event_id(event)
    ids : +List<U32> <- ordered(listeners,event_code,fast_exit)
    +initial : V.Value = V.initial(relay,False{})
    target : U32 = Single.pokemon_at(Single.target_value(Context.run_event(event)))
    progress : Progress <- execute_ids(ids,listeners,ProgressData{child,initial,initial,has_relay(relay),False{},Nil{}},
      event_code,target,fast_exit)
    return finish(parent,progress)
