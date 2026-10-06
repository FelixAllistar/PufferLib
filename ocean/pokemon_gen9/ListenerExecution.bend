import Base
import ./EventListener.bend as Listener
import ./Mem.bend as S
import ./Queue.bend as Q
import ./EventGate.bend as Gate
import ./data/callbacks.bend as B

# Ordering and gates shared below executable callback dispatch.
def sort_mode(+event: U32, fast_exit: Bool) -> Nat:
  Bool.pick(Nat,U32.is_eq(event,B.callbacks_oninvulnerability()) || U32.is_eq(event,B.callbacks_ontryhit()) ||
    U32.is_eq(event,B.callbacks_ondamaginghit()) || U32.is_eq(event,B.callbacks_onentryhazard()),2n,
    Bool.pick(Nat,fast_exit,1n,0n))

def count(listeners: +List<Listener.ListenerRow>) -> U32:
  match listeners:
    case Nil{}: 0
    case Con{_,tail}: (1 + count(tail) : U32)

def at(listeners: +List<Listener.ListenerRow>, index: Nat) -> Result<&2,&2,U32,Listener.ListenerRow>:
  match listeners:
    case Nil{}: Fail{3}
    case Con{head,tail}:
      match index:
        case 0n: Done{head}
        case 1n+p: at(tail,p)

def key_fields(listener: Listener.ListenerRow, index: U32) -> +List<U32>:
  Listener.ListenerData{_,_,_,_,_,Q.E{_,order,priority,speed,sub,creation},redirect,target,_} = listener
  [index,order,priority,speed,sub,creation,redirect,target]

def store_keys(listeners: +List<Listener.ListenerRow>, +index: U32) -> S.Mem<Unit>:
  match listeners:
    case Nil{}: S.Mem.pure(Unit,Unit{})
    case Con{head,tail}:
      do S.Mem<Unit>:
        Q.store(key_fields(head,index),Q.slot(12288,index))
        store_keys(tail,(index + 1 : U32))

def read_ids(n: Nat, +index: U32) -> S.Mem<+List<U32>>:
  match n:
    case 0n: S.Mem.pure(+List<U32>,Nil{})
    case 1n+p:
      do S.Mem<+List<U32>>:
        id : U32 <- S.get(Q.slot(12288,index))
        rest : +List<U32> <- read_ids(p,(index + 1 : U32))
        return Con{id,rest}

def ordered(+listeners: +List<Listener.ListenerRow>, event: U32, fast_exit: Bool) -> S.Mem<+List<U32>>:
  do S.Mem<+List<U32>>:
    +size : U32 = count(listeners)
    S.branch(+List<U32>,U32.is_le(size,64),
      (do S.Mem<+List<U32>>:
        store_keys(listeners,0)
        Q.sort_mode_buffer(sort_mode(event,fast_exit),12288,size)
        read_ids(U32.to_nat(size),0)),S.raise(+List<U32>,3))

def listener_gate(listener: Listener.ListenerRow, event: U32) -> S.Mem<Bool>:
  Listener.ListenerData{effect,_,_,holder,pokemon,_,_,_,_} = listener
  do S.Mem<Bool>:
    metadata : Gate.Info <- Gate.info(effect)
    Gate.check_info(metadata,event,holder,pokemon,True{})

