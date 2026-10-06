import Base
import ./Registry.bend as Registry
import ./Queue.bend as Q
import ./EventValue.bend as V

# Listener data must not depend on callback execution: native nested queries
# use discovery below the callback layer without introducing mutual recursion.
type ListenerRow is Data:
  ListenerData{effect: Registry.Effect, handler: Registry.Handler, state: U32,
    holder: U32, pokemon: Bool, key: Q.Entry, redirect_order: U32, target_index: U32,
    holder_value: V.Value}
