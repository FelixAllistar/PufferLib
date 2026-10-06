import Base
import ./EventListener.bend as Listener
import ./Mem.bend as S
import ./Lists.bend as L
import ./RunEvent.bend as Run
import ./EventContext.bend as Context
import ./EventValue.bend as V
import ./Callbacks.bend as Callbacks
import ./SingleEvent.bend as Single
import ./Number.bend as Number
import ./data/callbacks.bend as B

# Original runEvent array-target loop. Listener discovery attaches an index
# to every listener, even field/side listeners; holder and target are distinct.
# Truthy returns do not update targetRelayVars except on a fast exit. Keep
# this source behavior rather than treating each target as a scalar runEvent.
type VectorProgress is Data:
  VectorData{current: Context.Current, values: +List<V.Value>, visited: +List<U32>, stopped: Bool}

type VectorExecuted is Data:
  VectorResult{current: Context.Current, values: +List<V.Value>, visited: +List<U32>}

def value_at(values: +List<V.Value>, index: Nat) -> V.Value:
  match values:
    case Nil{}: V.VUndefined{}
    case Con{head,tail}:
      match index:
        case 0n: head
        case 1n+p: value_at(tail,p)

def value_replace(values: +List<V.Value>, index: Nat, +value: V.Value) -> +List<V.Value>:
  match values:
    case Nil{}: Nil{}
    case Con{head,tail}:
      match index:
        case 0n: Con{value,tail}
        case 1n+p: Con{head,value_replace(tail,p,value)}

def all_falsy(values: +List<V.Value>) -> Bool:
  match values:
    case Nil{}: True{}
    case Con{head,tail}: Bool.not(V.truthy(head)) && all_falsy(tail)

def is_zero(value: V.Value) -> Bool:
  match value:
    case V.VNumber{Number.Finite{_,numerator,_}}: Nat.is_eq(numerator,0n)
    case _: False{}

def accepts(+value: V.Value, event: U32) -> Bool:
  V.truthy(value) || U32.is_eq(event,B.callbacks_ondamaginghit()) && is_zero(value)

def listener_index(listener: Listener.ListenerRow) -> U32:
  Listener.ListenerData{_,_,_,_,_,_,_,index,_} = listener
  index

def target_event(event: Context.EventData, target: U32) -> Context.EventData:
  Context.E{id,_,source,effect,modifier} = event
  Context.E{id,V.VObject{1,target},source,effect,modifier}

def target_current(current: Context.Current, target: U32) -> Context.Current:
  Context.C{effect,state,event,depth} = current
  Context.C{effect,state,target_event(event,target),depth}

def current_of(progress: VectorProgress) -> Context.Current:
  VectorData{current,_,_,_} = progress
  current

def values_of(progress: VectorProgress) -> +List<V.Value>:
  VectorData{_,values,_,_} = progress
  values

def stopped(progress: VectorProgress) -> Bool:
  VectorData{_,_,_,yes} = progress
  yes

def after_parts(+values: +List<V.Value>, visited: +List<U32>, index: U32,
  +fast_exit: Bool, returned: Callbacks.Returned, listener_id: U32) -> VectorProgress:
  Callbacks.ReturnedData{current,+value} = returned
  +exit : Bool = V.defined(value) && (Bool.not(V.truthy(value)) || fast_exit)
  +next : +List<V.Value> = Bool.pick(+List<V.Value>,exit,value_replace(values,U32.to_nat(index),value),values)
  VectorData{current,next,L.append(visited,[listener_id]),exit && all_falsy(next)}

def after(progress: VectorProgress, index: U32, fast_exit: Bool,
  returned: Callbacks.Returned, listener_id: U32) -> VectorProgress:
  VectorData{_,values,visited,_} = progress
  after_parts(values,visited,index,fast_exit,returned,listener_id)

def execute(ids: +List<U32>, +listeners: +List<Listener.ListenerRow>, +targets: +List<U32>,
  +progress: VectorProgress, +event: U32, +has_relay: Bool, +fast_exit: Bool) -> S.Mem<VectorProgress>:
  match ids:
    case Nil{}: S.Mem.pure(VectorProgress,progress)
    case Con{+listener_id,tail}:
      do S.Mem<VectorProgress>:
        +listener : Listener.ListenerRow <- S.from_result(Listener.ListenerRow,Run.at(listeners,U32.to_nat(listener_id)))
        +index : U32 = listener_index(listener)
        +value : V.Value = value_at(values_of(progress),U32.to_nat(index))
        +next : VectorProgress <- S.branch(VectorProgress,accepts(value,event),
          (do S.Mem<VectorProgress>:
            allowed : Bool <- Run.listener_gate(listener,event)
            S.branch(VectorProgress,allowed,
              (do S.Mem<VectorProgress>:
                +target : U32 = L.at(targets,U32.to_nat(index))
                returned : Callbacks.Returned <- Run.invoke(listener,target_current(current_of(progress),target),
                  Bool.pick(V.Value,has_relay,value,V.VBool{True{}}),target)
                return after(progress,index,fast_exit,returned,listener_id)),S.Mem.pure(VectorProgress,progress))),
          S.Mem.pure(VectorProgress,progress))
        S.branch(VectorProgress,stopped(next),S.Mem.pure(VectorProgress,next),
          execute(tail,listeners,targets,next,event,has_relay,fast_exit))

def finish_parts(parent_event: Context.EventData, parent_depth: U32, progress: VectorProgress) -> VectorExecuted:
  VectorData{current,values,visited,_} = progress
  VectorResult{Context.leave_run(parent_event,parent_depth,current),values,visited}

def finish(parent: Context.Current, progress: VectorProgress) -> VectorExecuted:
  Context.C{_,_,event,depth} = parent
  finish_parts(event,depth,progress)

def call(+listeners: +List<Listener.ListenerRow>, +targets: +List<U32>, values: +List<V.Value>,
  +event: Context.EventData, +parent: Context.Current, has_relay: Bool, +fast_exit: Bool) -> S.Mem<VectorExecuted>:
  do S.Mem<VectorExecuted>:
    child : Context.Current <- S.from_result(Context.Current,Context.enter_run(parent,event))
    +event_code : U32 = Single.event_id(event)
    ids : +List<U32> <- Run.ordered(listeners,event_code,fast_exit)
    progress : VectorProgress <- execute(ids,listeners,targets,VectorData{child,values,Nil{},False{}},
      event_code,has_relay,fast_exit)
    return finish(parent,progress)
