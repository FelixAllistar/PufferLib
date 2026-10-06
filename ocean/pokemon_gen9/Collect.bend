import Base
import ./StatTable.bend as StatTable
import ./Mem.bend as S
import ./Lists.bend as L
import ./State.bend as State
import ./Catalog.bend as C
import ./Registry.bend as R
import ./EventListener.bend as Run
import ./ListenerPriority.bend as Priority
import ./EffectInit.bend as Roots
import ./EffectMem.bend as Effects
import ./EffectStore.bend as Store
import ./EventValue.bend as V
import ./Number.bend as Number
import ./data/callbacks.bend as B

# Source find*EventHandlers, default Gen 9 singles. Collection is read-only:
# suppression belongs to execution, and duration-only listeners retain their
# missing callback and end continuation rather than being dropped.
type EndAction is Data:
  NoEnd{}
  ClearStatus{pokemon: U32}
  RemoveVolatile{pokemon: U32, condition: U32}
  ClearAbility{pokemon: U32}
  ClearItem{pokemon: U32}
  RemoveSlot{side: U32, pokemon: U32, condition: U32}
  RemoveSide{side: U32, condition: U32}
  RemovePseudo{condition: U32}
  ClearWeather{}
  ClearTerrain{}

type Collected is Data:
  CollectedData{listener: Run.ListenerRow, callback_present: Bool, end: EndAction,
    target: V.Value, scope: U32, requested_callback: U32}

def append(xs: +List<Collected>, ys: +List<Collected>) -> +List<Collected>:
  match xs:
    case Nil{}: ys
    case Con{head,tail}: Con{head,append(tail,ys)}

def size(xs: +List<Collected>) -> U32:
  match xs:
    case Nil{}: 0
    case Con{_,tail}: (1 + size(tail) : U32)

def prefixes_allowed(+callback: U32) -> Bool:
  Bool.not(U32.is_eq(callback,B.callbacks_onbeforeturn()) || U32.is_eq(callback,B.callbacks_onupdate()) ||
    U32.is_eq(callback,B.callbacks_onweather()) || U32.is_eq(callback,B.callbacks_onweatherchange()) ||
    U32.is_eq(callback,B.callbacks_onterrainchange()))

def state_field(+state: U32, key: U32) -> S.Mem<V.Value>:
  S.branch(V.Value,U32.is_eq(state,0),S.Mem.pure(V.Value,V.VUndefined{}),Effects.read(state,key))

def unsigned(value: V.Value) -> U32:
  match value:
    case V.VNumber{number}: Number.trunc(number)
    case _: 0

def redirect_key(value: V.Value) -> U32:
  match value:
    case V.VNumber{Number.Finite{False{},numerator,1}}: (U32.from_nat(numerator) + 1 : U32)
    case _: 0

def condition_scope(target: V.Value, slot: V.Value) -> U32:
  match target:
    case V.VObject{2,_}: Bool.pick(U32,V.truthy(slot),2,1)
    case V.VObject{3,_}: 3
    case _: 0

def switch_rank(n: Nat, +at: U32, +position: U32, +index: U32) -> S.Mem<U32>:
  match n:
    case 0n: S.Mem.pure(U32,0)
    case 1n+p:
      do S.Mem<U32>:
        here : U32 <- S.get(at)
        S.branch(U32,U32.is_eq(here,position),S.Mem.pure(U32,(index + 1 : U32)),
          switch_rank(p,(at + 1 : U32),position,(index + 1 : U32)))

def pokemon_holder(+at: U32) -> S.Mem<Priority.HolderFacts>:
  do S.Mem<Priority.HolderFacts>:
    cached : U32 <- S.get((at + 95 : U32))
    unboosted : U32 <- StatTable.current(at,4)
    position : U32 <- S.get((at + 31 : U32))
    count : U32 <- S.get(530)
    rank : U32 <- switch_rank(U32.to_nat(count),531,
      (((at - 576 : U32) / 768 : U32) + 2 * position : U32),0)
    ability_state : U32 <- S.get(Roots.ability_state(at))
    order : V.Value <- state_field(ability_state,Store.order_key())
    return Priority.HolderData{at,True{},cached,U32.min(unboosted,10000),rank,redirect_key(order),V.VObject{1,at}}

def holder(value: V.Value) -> S.Mem<Priority.HolderFacts>:
  match value:
    case V.VObject{1,at}: pokemon_holder(at)
    case _: S.Mem.pure(Priority.HolderFacts,Priority.HolderData{0,False{},0,0,0,0,value})

def holder_is_pokemon(facts: Priority.HolderFacts) -> Bool:
  Priority.HolderData{_,pokemon,_,_,_,_,_} = facts
  pokemon

def missing_descriptor(lookup: R.Lookup, callback: U32) -> R.Handler:
  match lookup:
    case R.Missing{}: R.H{callback,4,0,32768,32768,0}
    case R.Found{R.H{_,_,order,priority,sub,_}}: R.H{callback,4,order,priority,sub,0}

def selected(lookup: R.Lookup, +effect: R.Effect, +callback: U32, +state: U32,
  +facts: Priority.HolderFacts, end: EndAction, duration: Bool) -> S.Mem<+List<Collected>>:
  match lookup:
    case R.Found{handler}:
      do S.Mem<+List<Collected>>:
        target : V.Value <- state_field(state,Store.target_key())
        slot : V.Value <- state_field(state,15)
        +scope : U32 = condition_scope(target,slot)
        creation : V.Value <- state_field(state,Store.order_key())
        listener : Run.ListenerRow <- Priority.resolve(effect,handler,callback,state,scope,unsigned(creation),facts,0)
        return [CollectedData{listener,True{},end,V.VUndefined{},scope,callback}]
    case R.Missing{}:
      S.branch(+List<Collected>,duration,
        (do S.Mem<+List<Collected>>:
          remaining : V.Value <- state_field(state,5)
          S.branch(+List<Collected>,V.truthy(remaining),
            (do S.Mem<+List<Collected>>:
              raw : R.Lookup <- R.get_raw(effect,callback)
              target : V.Value <- state_field(state,Store.target_key())
              slot : V.Value <- state_field(state,15)
              +scope : U32 = condition_scope(target,slot)
              creation : V.Value <- state_field(state,Store.order_key())
              listener : Run.ListenerRow <- Priority.resolve(effect,missing_descriptor(raw,callback),callback,
                state,scope,unsigned(creation),facts,0)
              return [CollectedData{listener,False{},end,V.VUndefined{},scope,callback}]),
            S.Mem.pure(+List<Collected>,Nil{}))),S.Mem.pure(+List<Collected>,Nil{}))

def candidate(+effect: R.Effect, +callback: U32, state: U32, +facts: Priority.HolderFacts,
  end: EndAction, duration: Bool) -> S.Mem<+List<Collected>>:
  do S.Mem<+List<Collected>>:
    found : R.Lookup <- R.callback(effect,callback,holder_is_pokemon(facts))
    selected(found,effect,callback,state,facts,end,duration)

# End modes for condition dictionaries: volatile, slot, side, pseudo-weather.
def dictionary_end(mode: Nat, side: U32, pokemon: U32, id: U32, custom: Bool) -> EndAction:
  match mode:
    case 0n: RemoveVolatile{pokemon,id}
    case 1n: RemoveSlot{side,pokemon,id}
    case 2n: Bool.pick(EndAction,custom,NoEnd{},RemoveSide{side,id})
    case _: Bool.pick(EndAction,custom,NoEnd{},RemovePseudo{id})

def dictionary_member(value: V.Value, +id: U32, callback: U32, facts: Priority.HolderFacts,
  end: EndAction, duration: Bool) -> S.Mem<+List<Collected>>:
  match value:
    case V.VObject{7,state}: candidate(R.ConditionEffect{id},callback,state,facts,end,duration)
    # With no duration lookup the source permits falsy state values: priority
    # reads use optional chaining, and execution creates a state only if needed.
    # Truthy primitive states and duration reads remain outside this transport
    # model; do not pretend they have ordinary effect-object semantics.
    case _:
      S.branch(+List<Collected>,Bool.not(duration) && Bool.not(V.truthy(value)),
        candidate(R.ConditionEffect{id},callback,0,facts,end,False{}),S.raise(+List<Collected>,3))

def dictionary_fields(fields: +List<Store.Field>, +callback: U32, +facts: Priority.HolderFacts,
  +mode: Nat, +side: U32, +pokemon: U32, +custom: Bool, +duration: Bool) -> S.Mem<+List<Collected>>:
  match fields:
    case Nil{}: S.Mem.pure(+List<Collected>,Nil{})
    case Con{Store.FieldData{+key,value},tail}:
      do S.Mem<+List<Collected>>:
        S.branch(+List<Collected>,U32.is_ge(key,65536),
          (do S.Mem<+List<Collected>>:
            +id : U32 = (key - 65536 : U32)
            head : +List<Collected> <- dictionary_member(value,id,callback,facts,
              dictionary_end(mode,side,pokemon,id,custom),duration)
            rest : +List<Collected> <- dictionary_fields(tail,callback,facts,mode,side,pokemon,custom,duration)
            return append(head,rest)),S.raise(+List<Collected>,3))

def dictionary(+reference: U32, callback: U32, facts: Priority.HolderFacts,
  mode: Nat, side: U32, pokemon: U32, custom: Bool, duration: Bool) -> S.Mem<+List<Collected>>:
  S.branch(+List<Collected>,U32.is_eq(reference,0),S.Mem.pure(+List<Collected>,Nil{}),
    (do S.Mem<+List<Collected>>:
      heap : Store.Heap <- S.heap()
      fields : +List<Store.Field> <- S.from_result(+List<Store.Field>,Store.get_fields(heap,reference))
      dictionary_fields(fields,callback,facts,mode,side,pokemon,custom,duration)))

def pokemon(+at: U32, +callback: U32, +duration: Bool) -> S.Mem<+List<Collected>>:
  do S.Mem<+List<Collected>>:
    +facts : Priority.HolderFacts <- pokemon_holder(at)
    status : U32 <- S.get((at + 13 : U32))
    status_state : U32 <- S.get(Roots.status_state(at))
    a : +List<Collected> <- candidate(R.ConditionEffect{status},callback,status_state,facts,ClearStatus{at},duration)
    volatile_map : U32 <- S.get(Roots.volatile_map(at))
    b : +List<Collected> <- dictionary(volatile_map,callback,facts,0n,0,at,False{},duration)
    ability : U32 <- S.get((at + 5 : U32))
    ability_state : U32 <- S.get(Roots.ability_state(at))
    c : +List<Collected> <- candidate(R.AbilityEffect{ability},callback,ability_state,facts,ClearAbility{at},duration)
    item : U32 <- S.get((at + 6 : U32))
    item_state : U32 <- S.get(Roots.item_state(at))
    d : +List<Collected> <- candidate(R.ItemEffect{item},callback,item_state,facts,ClearItem{at},duration)
    species : U32 <- S.get((at + 2 : U32))
    species_state : U32 <- S.get(Roots.species_state(at))
    e : +List<Collected> <- candidate(R.SpeciesEffect{species},callback,species_state,facts,NoEnd{},False{})
    +side : U32 = ((at - 576 : U32) / 768 : U32)
    position : U32 <- S.get((at + 31 : U32))
    slot_map : U32 <- S.get((Roots.side_maps(side) + 1 + position : U32))
    f : +List<Collected> <- dictionary(slot_map,callback,facts,1n,side,at,False{},duration)
    return append(a,append(b,append(c,append(d,append(e,f)))))

def side(+id: U32, callback: U32, duration: Bool, +custom: U32) -> S.Mem<+List<Collected>>:
  do S.Mem<+List<Collected>>:
    facts : Priority.HolderFacts <- holder(Bool.pick(V.Value,U32.is_eq(custom,0),V.VObject{2,id},V.VObject{1,custom}))
    reference : U32 <- S.get(Roots.side_maps(id))
    dictionary(reference,callback,facts,2n,id,0,U32.is_ne(custom,0),duration)

def field(+callback: U32, +duration: Bool, +custom: U32) -> S.Mem<+List<Collected>>:
  do S.Mem<+List<Collected>>:
    +facts : Priority.HolderFacts <- holder(Bool.pick(V.Value,U32.is_eq(custom,0),V.VObject{3,0},V.VObject{1,custom}))
    map : U32 <- S.get(Roots.pseudo_weather_map())
    a : +List<Collected> <- dictionary(map,callback,facts,3n,0,0,U32.is_ne(custom,0),duration)
    weather : U32 <- S.get(533)
    weather_state : U32 <- S.get(Roots.weather_state())
    b : +List<Collected> <- candidate(R.ConditionEffect{weather},callback,weather_state,facts,
      Bool.pick(EndAction,U32.is_eq(custom,0),ClearWeather{},NoEnd{}),duration)
    terrain : U32 <- S.get(534)
    terrain_state : U32 <- S.get(Roots.terrain_state())
    c : +List<Collected> <- candidate(R.ConditionEffect{terrain},callback,terrain_state,facts,
      Bool.pick(EndAction,U32.is_eq(custom,0),ClearTerrain{},NoEnd{}),duration)
    return append(a,append(b,c))

# The pinned default format has no runtime callback properties. The catalog
# exporter asserts that fact. Dynamic onEvent registrations are outside this
# format's current configuration; never infer their absence for other formats.
def battle(callback: U32, duration: Bool, +custom: U32) -> S.Mem<+List<Collected>>:
  do S.Mem<+List<Collected>>:
    facts : Priority.HolderFacts <- holder(Bool.pick(V.Value,U32.is_eq(custom,0),V.VObject{4,0},V.VObject{1,custom}))
    state : U32 <- S.get(Roots.format_state())
    candidate(R.FormatEffect{},callback,state,facts,NoEnd{},duration)

def is_active(+at: U32) -> S.Mem<Bool>:
  S.branch(Bool,U32.is_eq(at,0),S.Mem.pure(Bool,False{}),
    (do S.Mem<Bool>:
      flags : U32 <- S.get((at + 23 : U32))
      return U32.is_ne((flags .&. 1 : U32),0)))

def active(+id: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    +position : U32 <- S.get((State.header() + 10 + id : U32))
    S.branch(U32,U32.is_lt(position,6),S.get(Roots.party_slot(id,position)),S.Mem.pure(U32,0))

def living_active(id: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    +at : U32 <- active(id)
    S.branch(U32,U32.is_eq(at,0),S.Mem.pure(U32,0),
      (do S.Mem<U32>:
        hp : U32 <- S.get((at + 7 : U32))
        return Bool.pick(U32,U32.is_eq(hp,0),0,at)))

def optional_pokemon(+at: U32, +callback: U32) -> S.Mem<+List<Collected>>:
  S.branch(+List<Collected>,U32.is_eq(at,0) || U32.is_eq(callback,0),S.Mem.pure(+List<Collected>,Nil{}),
    pokemon(at,callback,False{}))

def neighbors(+side_id: U32, +callback: U32) -> S.Mem<+List<Collected>>:
  do S.Mem<+List<Collected>>:
    +ally : U32 <- living_active(side_id)
    +foe : U32 <- living_active((1 - side_id : U32))
    ally_code : U32 <- C.callback_info(callback,1)
    +any_code : U32 <- C.callback_info(callback,2)
    foe_code : U32 <- C.callback_info(callback,3)
    a : +List<Collected> <- optional_pokemon(ally,ally_code)
    b : +List<Collected> <- optional_pokemon(ally,any_code)
    c : +List<Collected> <- optional_pokemon(foe,foe_code)
    d : +List<Collected> <- optional_pokemon(foe,any_code)
    return append(a,append(b,append(c,d)))

def one_side(+id: U32, +target_side: U32, +callback: U32, bubble_down: Bool) -> S.Mem<+List<Collected>>:
  do S.Mem<+List<Collected>>:
    +prefixed : Bool = prefixes_allowed(callback)
    foe_code : U32 <- C.callback_info(callback,3)
    +own_code : U32 = Bool.pick(U32,U32.is_eq(id,target_side),callback,Bool.pick(U32,prefixed,foe_code,0))
    +any_code : U32 <- C.callback_info(callback,2)
    a : +List<Collected> <- S.branch(+List<Collected>,bubble_down,
      (do S.Mem<+List<Collected>>:
        +at : U32 <- active(id)
        S.branch(+List<Collected>,U32.is_ne(at,0),
          (do S.Mem<+List<Collected>>:
            x : +List<Collected> <- optional_pokemon(at,own_code)
            y : +List<Collected> <- S.branch(+List<Collected>,prefixed,optional_pokemon(at,any_code),
              S.Mem.pure(+List<Collected>,Nil{}))
            return append(x,y)),S.raise(+List<Collected>,3))),S.Mem.pure(+List<Collected>,Nil{}))
    b : +List<Collected> <- S.branch(+List<Collected>,U32.is_ne(own_code,0),side(id,own_code,False{},0),
      S.Mem.pure(+List<Collected>,Nil{}))
    c : +List<Collected> <- S.branch(+List<Collected>,prefixed && U32.is_ne(any_code,0),side(id,any_code,False{},0),
      S.Mem.pure(+List<Collected>,Nil{}))
    return append(a,append(b,c))

def sides(+target_side: U32, +callback: U32, +bubble_down: Bool) -> S.Mem<+List<Collected>>:
  do S.Mem<+List<Collected>>:
    a : +List<Collected> <- one_side(0,target_side,callback,bubble_down)
    b : +List<Collected> <- one_side(1,target_side,callback,bubble_down)
    return append(a,b)

type TargetCollection is Data:
  TargetData{listeners: +List<Collected>, bubble: Bool, side: U32, down: Bool}

def pokemon_target(+at: U32, +callback: U32, source: U32) -> S.Mem<TargetCollection>:
  do S.Mem<TargetCollection>:
    target_active : Bool <- is_active(at)
    source_active : Bool <- is_active(source)
    S.branch(TargetCollection,target_active || source_active,
      (do S.Mem<TargetCollection>:
        +side_id : U32 = ((at - 576 : U32) / 768 : U32)
        a : +List<Collected> <- pokemon(at,callback,False{})
        b : +List<Collected> <- S.branch(+List<Collected>,prefixes_allowed(callback),neighbors(side_id,callback),
          S.Mem.pure(+List<Collected>,Nil{}))
        return TargetData{append(a,b),True{},side_id,False{}}),
      S.Mem.pure(TargetCollection,TargetData{Nil{},False{},0,False{}}))

def target_part(target: V.Value, callback: U32, source: U32) -> S.Mem<TargetCollection>:
  match target:
    case V.VObject{1,at}: pokemon_target(at,callback,source)
    case V.VObject{2,id}: S.Mem.pure(TargetCollection,TargetData{Nil{},True{},id,True{}})
    case V.VObject{4,_}: S.Mem.pure(TargetCollection,TargetData{Nil{},False{},0,False{}})
    case _: S.raise(TargetCollection,3)

def finish_target(parts: TargetCollection, +callback: U32, source: U32) -> S.Mem<+List<Collected>>:
  TargetData{a,bubble,side_id,down} = parts
  do S.Mem<+List<Collected>>:
    source_code : U32 <- C.callback_info(callback,4)
    b : +List<Collected> <- S.branch(+List<Collected>,prefixes_allowed(callback),optional_pokemon(source,source_code),
      S.Mem.pure(+List<Collected>,Nil{}))
    c : +List<Collected> <- S.branch(+List<Collected>,bubble,sides(side_id,callback,down),S.Mem.pure(+List<Collected>,Nil{}))
    d : +List<Collected> <- field(callback,False{},0)
    e : +List<Collected> <- battle(callback,False{},0)
    return append(a,append(b,append(c,append(d,e))))

def scalar(target: V.Value, +callback: U32, +source: U32) -> S.Mem<+List<Collected>>:
  do S.Mem<+List<Collected>>:
    parts : TargetCollection <- target_part(target,callback,source)
    finish_target(parts,callback,source)

def indexed_listener(listener: Run.ListenerRow, index: U32) -> Run.ListenerRow:
  Run.ListenerData{effect,handler,state,holder,pokemon,key,redirect,_,value} = listener
  Run.ListenerData{effect,handler,state,holder,pokemon,key,redirect,index,value}

def listener_identity(listener: Run.ListenerRow) -> +List<V.Value>:
  Run.ListenerData{_,_,state,_,_,_,_,_,holder} = listener
  [V.VObject{7,state},holder]

def indexed(xs: +List<Collected>, +target: U32, +index: U32) -> +List<Collected>:
  match xs:
    case Nil{}: Nil{}
    case Con{CollectedData{listener,present,end,_,scope,callback},tail}:
      Con{CollectedData{indexed_listener(listener,index),present,end,V.VObject{1,target},scope,callback},
        indexed(tail,target,index)}

def vector(targets: +List<U32>, +callback: U32, +source: U32, +index: U32) -> S.Mem<+List<Collected>>:
  match targets:
    case Nil{}: S.Mem.pure(+List<Collected>,Nil{})
    case Con{+target,tail}:
      do S.Mem<+List<Collected>>:
        head : +List<Collected> <- scalar(V.VObject{1,target},callback,source)
        rest : +List<Collected> <- vector(tail,callback,source,(index + 1 : U32))
        return append(indexed(head,target,index),rest)

def prepend_runnable(listener: Run.ListenerRow, rest: Result<&2,&2,U32,+List<Run.ListenerRow>>)
  -> Result<&2,&2,U32,+List<Run.ListenerRow>>:
  match rest:
    case Fail{code}: Fail{code}
    case Done{tail}: Done{Con{listener,tail}}

def runnable(xs: +List<Collected>) -> Result<&2,&2,U32,+List<Run.ListenerRow>>:
  match xs:
    case Nil{}: Done{Nil{}}
    case Con{CollectedData{listener,present,_,_,_,_},tail}:
      Bool.pick(Result<&2,&2,U32,+List<Run.ListenerRow>>,present,
        prepend_runnable(listener,runnable(tail)),Fail{4})
