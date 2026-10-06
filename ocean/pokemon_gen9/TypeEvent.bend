import Base
import ./Mem.bend as S
import ./EventListener.bend as Listener
import ./ListenerExecution.bend as Core
import ./Collect.bend as Collect
import ./Registry.bend as Registry
import ./EffectMem.bend as Effects
import ./EventValue.bend as V
import ./EventContext.bend as Context
import ./TypeBodies.bend as Bodies
import ./ValueArray.bend as Arrays
import ./data/callbacks.bend as B

# A closed Type executor below generic callbacks, so stat callbacks can ask
# for live types without an import cycle. It shares native discovery, ordering,
# suppression gates and holder binding. The inventory independently checks that
# the pinned Type functions are exactly Roost and the two species bodies.
def invoke(listener: Listener.ListenerRow, +current: Context.Current, relay: U32, target: U32) -> S.Mem<U32>:
  Listener.ListenerData{+effect,Registry.H{callback,tag,_,_,_,_},state,_,_,_,_,_,holder} = listener
  S.branch(U32,U32.is_eq(tag,0),
    (do S.Mem<U32>:
      bound : U32 <- Effects.bind_holder(state,holder)
      body : Bodies.BodyKind <- Bodies.kind(effect,callback)
      value : V.Value <- Bodies.call(body,Context.enter_handler(current,Context.BoundEffect{effect},bound),
        V.VObject{8,relay},target)
      S.from_result(U32,Arrays.reference(value))),S.raise(U32,4))

def execute(ids: +List<U32>, +listeners: +List<Listener.ListenerRow>, +current: Context.Current,
  +relay: U32, +target: U32) -> S.Mem<U32>:
  match ids:
    case Nil{}: S.Mem.pure(U32,relay)
    case Con{index,tail}:
      do S.Mem<U32>:
        +listener : Listener.ListenerRow <- S.from_result(Listener.ListenerRow,Core.at(listeners,U32.to_nat(index)))
        allowed : Bool <- Core.listener_gate(listener,B.callbacks_ontype())
        next : U32 <- S.branch(U32,allowed,invoke(listener,current,relay,target),S.Mem.pure(U32,relay))
        execute(tail,listeners,current,next,target)

def call(+current: Context.Current, +target: U32, relay: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    child : Context.Current <- S.from_result(Context.Current,Context.enter_run(current,
      Context.E{B.callbacks_ontype(),V.VObject{1,target},V.VNull{},V.VNull{},Context.NoModifier{}}))
    xs : +List<Collect.Collected> <- Collect.scalar(V.VObject{1,target},B.callbacks_ontype(),0)
    +listeners : +List<Listener.ListenerRow> <- S.from_result(+List<Listener.ListenerRow>,Collect.runnable(xs))
    ids : +List<U32> <- Core.ordered(listeners,B.callbacks_ontype(),False{})
    execute(ids,listeners,child,relay,target)
