import Base
import ./Mem.bend as S
import ./Collect.bend as Collect
import ./EventListener.bend as Listener
import ./EventContext.bend as Context
import ./EventValue.bend as V
import ./data/callbacks.bend as B

# Pinned default Gen 9 has no TryTerrain listeners. Keep original discovery
# and the nested runEvent depth check, and reject an unexpected nonempty list.
# This closed event specialization is checked against every resolved Dex record
# in test_terrain.py; it does not accept dynamic event registrations.
def parent_target(current: Context.Current) -> V.Value:
  Context.C{_,_,Context.E{_,target,_,_,_},_} = current
  target

def inherited_target(current: Context.Current, +requested: V.Value) -> V.Value:
  Bool.pick(V.Value,V.truthy(requested),requested,parent_target(current))

def child_target(child: Context.Current) -> V.Value:
  Context.C{_,_,Context.E{_,target,_,_,_},_} = child
  target

def empty_terrain(listeners: +List<Listener.ListenerRow>) -> S.Mem<U32>:
  match listeners:
    case Nil{}: S.get(534)
    case Con{_,_}: S.raise(U32,4)

def effective(+current: Context.Current, requested: V.Value) -> S.Mem<U32>:
  do S.Mem<U32>:
    child : Context.Current <- S.from_result(Context.Current,Context.enter_run(current,
      Context.E{B.callbacks_ontryterrain(),inherited_target(current,requested),V.VUndefined{},
        V.VUndefined{},Context.NoModifier{}}))
    xs : +List<Collect.Collected> <- Collect.scalar(child_target(child),B.callbacks_ontryterrain(),0)
    listeners : +List<Listener.ListenerRow> <- S.from_result(+List<Listener.ListenerRow>,Collect.runnable(xs))
    empty_terrain(listeners)

def contains(ids: +List<U32>, +terrain: U32) -> Bool:
  match ids:
    case Nil{}: False{}
    case Con{head,tail}: U32.is_eq(head,terrain) || contains(tail,terrain)

def matches(ids: +List<U32>, current: Context.Current, requested: V.Value) -> S.Mem<Bool>:
  do S.Mem<Bool>:
    terrain : U32 <- effective(current,requested)
    return contains(ids,terrain)
