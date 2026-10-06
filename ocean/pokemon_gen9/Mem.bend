import Base
import ./EffectStore.bend as Effects

type World is Type:
  MemoryWorld{words: Array<U32>, effects: Effects.Heap}

# Pure affine memory state. C supplies catalog/state words and copies results;
# all random draws and domain rules execute in Bend.
type Mem<-A: Data> is Type:
  Run{call: World -> World & Result<&2, &2, U32, A>}

def run(-A: Data, m: Mem<A>, a: World) -> World & Result<&2, &2, U32, A>:
  Run{call} = m
  call(a)

def Mem.pure(-A: Data, value: A) -> Mem<A>:
  Run{a => (a, Done{value})}

def bind_result(-A: Data, -B: Data, f: A -> Mem<B>,
  pair: World & Result<&2, &2, U32, A>) -> World & Result<&2, &2, U32, B>:
  (a, result) = pair
  match result:
    case Fail{code}: (a, Fail{code})
    case Done{value}: run(B, f(value), a)

def Mem.bind(-A: Data, -B: Data, m: Mem<A>, f: A -> Mem<B>) -> Mem<B>:
  Run{a => bind_result(A, B, f, run(A, m, a))}

def get_result(effects: Effects.Heap, pair: Array<U32> & U32) -> World & Result<&2, &2, U32, U32>:
  (a, value) = pair
  (MemoryWorld{a,effects}, Done{value})

def get_world(at: U32, world: World) -> World & Result<&2,&2,U32,U32>:
  MemoryWorld{words,effects} = world
  get_result(effects,words[at])

def get(at: U32) -> Mem<U32>:
  Run{a => get_world(at,a)}

def put_world(at: U32, value: U32, world: World) -> World & Result<&2,&2,U32,Unit>:
  MemoryWorld{words,effects} = world
  (MemoryWorld{words[at] <- value,effects},Done{Unit{}})

def put(at: U32, value: U32) -> Mem<Unit>:
  Run{a => put_world(at,value,a)}

def heap_world(world: World) -> World & Result<&2,&2,U32,Effects.Heap>:
  MemoryWorld{words,+effects} = world
  (MemoryWorld{words,effects},Done{effects})

def heap() -> Mem<Effects.Heap>:
  Run{world => heap_world(world)}

def replace_heap(effects: Effects.Heap, world: World) -> World & Result<&2,&2,U32,Unit>:
  MemoryWorld{words,_} = world
  (MemoryWorld{words,effects},Done{Unit{}})

def set_heap(effects: Effects.Heap) -> Mem<Unit>:
  Run{world => replace_heap(effects,world)}

def raise(-A: Data, code: U32) -> Mem<A>:
  Run{a => (a, Fail{code})}

def from_result(-A: Data, result: Result<&2, &2, U32, A>) -> Mem<A>:
  match result:
    case Fail{code}: raise(A, code)
    case Done{value}: Mem.pure(A, value)

def branch(-A: Data, yes: Bool, left: Mem<A>, right: Mem<A>) -> Mem<A>:
  match yes:
    case True{}: left
    case False{}: right

def when(yes: Bool, left: Mem<Unit>) -> Mem<Unit>:
  branch(Unit, yes, left, Mem.pure(Unit, Unit{}))
