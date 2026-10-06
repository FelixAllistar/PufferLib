import Base
import ./Mem.bend as S
import ./EffectStore.bend as Effects
import ./EventValue.bend as V
import ./EventContext.bend as Context

def save_allocation(allocated: Effects.Allocated) -> S.Mem<U32>:
  Effects.Allocation{heap,reference} = allocated
  do S.Mem<U32>:
    S.set_heap(heap)
    return reference

def initialize(fields: +List<Effects.Field>, pokemon: Bool, active: Bool) -> S.Mem<U32>:
  do S.Mem<U32>:
    heap : Effects.Heap <- S.heap()
    allocated : Effects.Allocated <- S.from_result(Effects.Allocated,
      Effects.allocate(heap,fields,Effects.AutomaticOrder{},pokemon,active))
    save_allocation(allocated)

def empty_state() -> S.Mem<U32>:
  initialize(Nil{},False{},False{})

def raw_object(fields: +List<Effects.Field>) -> S.Mem<U32>:
  do S.Mem<U32>:
    heap : Effects.Heap <- S.heap()
    allocated : Effects.Allocated <- S.from_result(Effects.Allocated,Effects.allocate_raw(heap,fields))
    save_allocation(allocated)

# Zero means the source's absent/falsy state. A supplied nonzero reference
# must name an existing object; never silently substitute another state.
def ensure(+reference: U32) -> S.Mem<U32>:
  S.branch(U32,U32.is_eq(reference,0),empty_state(),
    (do S.Mem<U32>:
      heap : Effects.Heap <- S.heap()
      fields : +List<Effects.Field> <- S.from_result(+List<Effects.Field>,Effects.get_fields(heap,reference))
      return reference))

def read(reference: U32, key: U32) -> S.Mem<V.Value>:
  do S.Mem<V.Value>:
    heap : Effects.Heap <- S.heap()
    S.from_result(V.Value,Effects.read(heap,reference,key))

# Source `in` tests presence even when the stored value is undefined/falsy.
def has(reference: U32, key: U32) -> S.Mem<Bool>:
  do S.Mem<Bool>:
    heap : Effects.Heap <- S.heap()
    fields : +List<Effects.Field> <- S.from_result(+List<Effects.Field>,Effects.get_fields(heap,reference))
    return Effects.field_has(fields,key)

def write(reference: U32, key: U32, value: V.Value) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    heap : Effects.Heap <- S.heap()
    next : Effects.Heap <- S.from_result(Effects.Heap,Effects.write(heap,reference,key,value))
    S.set_heap(next)

# Only runEvent's function branch attaches the holder. singleEvent leaves
# supplied fields untouched, including a missing or different target.
def bind_holder(reference: U32, holder: V.Value) -> S.Mem<U32>:
  do S.Mem<U32>:
    +state : U32 <- ensure(reference)
    write(state,Effects.target_key(),holder)
    return state

def current_state(current: Context.Current) -> U32:
  Context.C{_,state,_,_} = current
  state

def replace_state(current: Context.Current, state: U32) -> Context.Current:
  Context.C{effect,_,event,depth} = current
  Context.C{effect,state,event,depth}
