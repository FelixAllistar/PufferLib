import Base
import ./Registry.bend as Registry
import ./EventValue.bend as V
import ./Math.bend as M

# Effect-state IDs refer to persistent objects; restoring a scope restores the
# reference, never rolls back mutations to that object's fields.
type EffectRef is Data:
  EmptyEffect{}
  BoundEffect{effect: Registry.Effect}

type Modifier is Data:
  NoModifier{}
  Q12{value: U32}

type EventData is Data:
  E{id: U32, target: V.Value, source: V.Value, source_effect: V.Value, modifier: Modifier}

type Current is Data:
  C{effect: EffectRef, state: U32, event: EventData, depth: U32}

type Frame is Data:
  SingleParent{current: Current}
  RunParent{event: EventData, depth: U32}
  HandlerParent{effect: EffectRef, state: U32}

type Machine is Data:
  VM{current: Current, stack: +List<Frame>, last_number: U32}

type Command is Data:
  EnterSingle{effect: EffectRef, state: U32, event: EventData, lines: U32}
  EnterRun{event: EventData}
  EnterHandler{effect: EffectRef, state: U32}
  LeaveSingle{}
  LeaveRun{}
  LeaveHandler{}
  Chain{modifier: U32}
  FinalModify{number: U32}

def single_event(event: EventData) -> EventData:
  E{id,target,source,source_effect,_} = event
  E{id,target,source,source_effect,NoModifier{}}

def run_event(event: EventData) -> EventData:
  E{id,+target,source,source_effect,_} = event
  E{id,Bool.pick(V.Value,V.truthy(target),target,V.VObject{4,0}),source,source_effect,Q12{4096}}

def enter_single(parent: Current, effect: EffectRef, state: U32, event: EventData, +lines: U32)
  -> Result<&2, &2, U32, Current>:
  C{_,_,_,+depth} = parent
  Bool.pick(Result<&2,&2,U32,Current>,U32.is_ge(depth,8),Fail{5},
    Bool.pick(Result<&2,&2,U32,Current>,U32.is_gt(lines,1000),Fail{6},
      Done{C{effect,state,single_event(event),(depth + 1:U32)}}))

def enter_run(parent: Current, event: EventData) -> Result<&2, &2, U32, Current>:
  C{effect,state,_,+depth} = parent
  Bool.pick(Result<&2,&2,U32,Current>,U32.is_ge(depth,8),Fail{5},
    Done{C{effect,state,run_event(event),(depth + 1:U32)}})

def enter_handler(parent: Current, effect: EffectRef, state: U32) -> Current:
  C{_,_,event,depth} = parent
  C{effect,state,event,depth}

def leave_run(event: EventData, depth: U32, child: Current) -> Current:
  C{effect,state,_,_} = child
  C{effect,state,event,depth}

def leave_handler(effect: EffectRef, state: U32, child: Current) -> Current:
  C{_,_,event,depth} = child
  C{effect,state,event,depth}

# tr(undefined * 4096) == 0 in singleEvent's modifier-less event object.
def modifier_value(modifier: Modifier) -> U32:
  match modifier:
    case NoModifier{}: 0
    case Q12{value}: value

def chain_event(event: EventData, modifier: U32) -> EventData:
  E{id,target,source,source_effect,previous} = event
  E{id,target,source,source_effect,Q12{M.chain(modifier_value(previous),modifier)}}

def chain_current(current: Current, modifier: U32) -> Current:
  C{effect,state,event,depth} = current
  C{effect,state,chain_event(event,modifier),depth}

def final_event(event: EventData, number: U32) -> EventData & U32:
  E{id,target,source,source_effect,modifier} = event
  (E{id,target,source,source_effect,Q12{4096}},M.modify_integer(U32.to_nat(number),modifier_value(modifier)))

def final_current_parts(effect: EffectRef, state: U32, depth: U32, pair: EventData & U32) -> Current & U32:
  (event,number) = pair
  (C{effect,state,event,depth},number)

# Numeric callers provide a resolved integer (e.g. onModifySpe/par). General
# JavaScript fractions are not silently truncated before multiplication.
def final_current(current: Current, number: U32) -> Current & U32:
  C{effect,state,event,depth} = current
  final_current_parts(effect,state,depth,final_event(event,number))

def pushed(frame: Frame, stack: +List<Frame>, last: U32, result: Result<&2,&2,U32,Current>)
  -> Result<&2,&2,U32,Machine>:
  match result:
    case Fail{code}: Fail{code}
    case Done{current}: Done{VM{current,Con{frame,stack},last}}

def popped(command: Command, child: Current, stack: +List<Frame>, last: U32)
  -> Result<&2,&2,U32,Machine>:
  match command:
    case LeaveSingle{}:
      match stack:
        case Con{SingleParent{parent},tail}: Done{VM{parent,tail,last}}
        case _: Fail{3}
    case LeaveRun{}:
      match stack:
        case Con{RunParent{event,depth},tail}: Done{VM{leave_run(event,depth,child),tail,last}}
        case _: Fail{3}
    case LeaveHandler{}:
      match stack:
        case Con{HandlerParent{effect,state},tail}: Done{VM{leave_handler(effect,state,child),tail,last}}
        case _: Fail{3}
    case _: Fail{3}

def final_machine(stack: +List<Frame>, pair: Current & U32) -> Machine:
  (current,number) = pair
  VM{current,stack,number}

def run_parent(current: Current) -> Frame:
  C{_,_,event,depth} = current
  RunParent{event,depth}

def handler_parent(current: Current) -> Frame:
  C{effect,state,_,_} = current
  HandlerParent{effect,state}

def step(machine: Machine, command: Command) -> Result<&2,&2,U32,Machine>:
  VM{+current,stack,last} = machine
  match command:
    case EnterSingle{effect,state,event,lines}:
      pushed(SingleParent{current},stack,last,enter_single(current,effect,state,event,lines))
    case EnterRun{event}:
      pushed(run_parent(current),stack,last,enter_run(current,event))
    case EnterHandler{effect,state}:
      Done{VM{enter_handler(current,effect,state),Con{handler_parent(current),stack},last}}
    case Chain{modifier}: Done{VM{chain_current(current,modifier),stack,last}}
    case FinalModify{number}: Done{final_machine(stack,final_current(current,number))}
    case _: popped(command,current,stack,last)
