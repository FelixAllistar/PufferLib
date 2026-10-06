import Base
import ./Mem.bend as S
import ./Stats.bend as Stats
import ./Math.bend as Math
import ./BoostTable.bend as Boosts
import ./EffectStore.bend as Store
import ./EffectMem.bend as Effects
import ./EffectInit.bend as Roots
import ./EventValue.bend as V
import ./EventContext.bend as Context
import ./Callbacks.bend as Callbacks
import ./CollectedEvent.bend as Events
import ./RunEvent.bend as Run
import ./data/callbacks.bend as B
import ./data/conditions.bend as D
import ./Weight.bend as Weight
import ./StatTable.bend as StatTable

# Original Pokemon stat helpers, default singles format. Remaining executable
# event bodies still fail explicitly. Returning the restored context keeps
# nested callers from relying on a separate mutable event context.
type Computed is Data:
  ComputedData{current: Context.Current, number: U32}

def swapped(+stat: U32, room: Bool) -> U32:
  Bool.pick(U32,room,Bool.pick(U32,U32.is_eq(stat,1),3,
    Bool.pick(U32,U32.is_eq(stat,3),1,stat)),stat)

def speed_limit(+stat: U32, +number: U32) -> U32:
  Bool.pick(U32,U32.is_eq(stat,4),U32.min(number,10000),number)

def action_speed(+number: U32, room: Bool) -> U32:
  (Bool.pick(U32,room,(10000 - number : U32),number) .&. 8191 : U32)

def fields_present(+reference: U32) -> S.Mem<+List<Store.Field>>:
  S.branch(+List<Store.Field>,U32.is_eq(reference,0),S.Mem.pure(+List<Store.Field>,Nil{}),
    Boosts.fields(reference))

def pseudo_weather(condition: U32, presence: Bool) -> S.Mem<Bool>:
  do S.Mem<Bool>:
    reference : U32 <- S.get(Roots.pseudo_weather_map())
    +fields : +List<Store.Field> <- fields_present(reference)
    +key : U32 = Roots.condition_key(condition)
    return Bool.pick(Bool,presence,Store.field_has(fields,key),V.truthy(Store.field_get(fields,key)))

def value_result(result: Run.Executed) -> Callbacks.Returned:
  Run.ExecutedData{returned,_} = result
  returned

def stat_event(callback: U32, target: U32, parent: Context.Current, relay: V.Value) -> S.Mem<Callbacks.Returned>:
  do S.Mem<Callbacks.Returned>:
    result : Run.Executed <- Events.scalar(Context.E{callback,V.VObject{1,target},V.VNull{},V.VNull{},
      Context.NoModifier{}},parent,relay,False{},False{},Context.EmptyEffect{})
    return value_result(result)

def boost_result(returned: Callbacks.Returned, stat: U32, base: U32) -> S.Mem<Computed>:
  Callbacks.ReturnedData{current,value} = returned
  do S.Mem<Computed>:
    reference : U32 <- S.from_result(U32,Boosts.object_reference(value))
    boost : V.Value <- Effects.read(reference,Boosts.key(stat))
    stage : U32 <- S.from_result(U32,Boosts.stage(boost))
    return ComputedData{current,Stats.boosted(base,stage)}

def calculate_finish(modifier: U32, result: Computed) -> Computed:
  ComputedData{current,number} = result
  ComputedData{current,Math.modify(number,modifier)}

def calculate(+at: U32, +stat: U32, boost: V.Value, +modifier: U32,
  +stat_user: U32, parent: Context.Current) -> S.Mem<Computed>:
  do S.Mem<Computed>:
    room : Bool <- pseudo_weather(D.conditions_wonderroom(),True{})
    base : U32 <- StatTable.current(at,swapped(stat,room))
    reference : U32 <- Boosts.sparse(stat,boost)
    returned : Callbacks.Returned <- stat_event(B.callbacks_onmodifyboost(),
      Bool.pick(U32,U32.is_eq(stat_user,0),at,stat_user),parent,V.VObject{7,reference})
    result : Computed <- boost_result(returned,stat,base)
    return calculate_finish(Bool.pick(U32,U32.is_eq(modifier,0),4096,modifier),result)

def get_boosted(+at: U32, +stat: U32, +base: U32, unboosted: Bool, unmodified: Bool,
  +parent: Context.Current) -> S.Mem<Computed>:
  S.branch(Computed,unboosted,S.Mem.pure(Computed,ComputedData{parent,base}),
    S.branch(Computed,unmodified,
      (do S.Mem<Computed>:
        stage : U32 <- Boosts.stat_stage(at,stat)
        return ComputedData{parent,Stats.boosted(base,U32.min(stage,12))}),
      (do S.Mem<Computed>:
        reference : U32 <- Boosts.clone(at)
        returned : Callbacks.Returned <- stat_event(B.callbacks_onmodifyboost(),at,parent,V.VObject{7,reference})
        boost_result(returned,stat,base))))

def stat_callback(index: Nat) -> U32:
  match index:
    case 0n: B.callbacks_onmodifyatk()
    case 1n: B.callbacks_onmodifydef()
    case 2n: B.callbacks_onmodifyspa()
    case 3n: B.callbacks_onmodifyspd()
    case _: B.callbacks_onmodifyspe()

def numeric_result(returned: Callbacks.Returned, stat: U32) -> S.Mem<Computed>:
  Callbacks.ReturnedData{current,value} = returned
  do S.Mem<Computed>:
    number : U32 <- S.from_result(U32,Callbacks.integer(value))
    return ComputedData{current,speed_limit(stat,number)}

def weight_result(returned: Callbacks.Returned) -> S.Mem<Computed>:
  Callbacks.ReturnedData{current,value} = returned
  do S.Mem<Computed>:
    number : U32 <- S.from_result(U32,Callbacks.integer(value))
    return ComputedData{current,Weight.minimum(number)}

def weight(+at: U32, parent: Context.Current) -> S.Mem<Computed>:
  do S.Mem<Computed>:
    number : U32 <- S.get((at + 22 : U32))
    returned : Callbacks.Returned <- stat_event(B.callbacks_onmodifyweight(),at,parent,Store.number(number))
    weight_result(returned)

def get_modified(at: U32, +stat: U32, unmodified: Bool, result: Computed) -> S.Mem<Computed>:
  ComputedData{+current,+number} = result
  S.branch(Computed,unmodified,S.Mem.pure(Computed,ComputedData{current,speed_limit(stat,number)}),
    (do S.Mem<Computed>:
      returned : Callbacks.Returned <- stat_event(stat_callback(U32.to_nat(stat)),at,current,Store.number(number))
      numeric_result(returned,stat)))

def get(+at: U32, +stat: U32, unboosted: Bool, +unmodified: Bool, parent: Context.Current) -> S.Mem<Computed>:
  do S.Mem<Computed>:
    base : U32 <- StatTable.current(at,stat)
    room : Bool <- pseudo_weather(D.conditions_wonderroom(),True{})
    +selected : U32 = swapped(stat,unmodified && room)
    result : Computed <- get_boosted(at,selected,base,unboosted,unmodified,parent)
    get_modified(at,selected,unmodified,result)

def action_result(result: Computed, room: Bool) -> Computed:
  ComputedData{current,number} = result
  ComputedData{current,action_speed(number,room)}

def action(at: U32, parent: Context.Current) -> S.Mem<Computed>:
  do S.Mem<Computed>:
    result : Computed <- get(at,4,False{},False{},parent)
    room : Bool <- pseudo_weather(D.conditions_trickroom(),False{})
    return action_result(result,room)

def update_result(at: U32, +result: Computed) -> S.Mem<Computed>:
  ComputedData{_,number} = result
  do S.Mem<Computed>:
    S.put((at + 95 : U32),number)
    return result

def update(+at: U32, parent: Context.Current) -> S.Mem<Computed>:
  do S.Mem<Computed>:
    result : Computed <- action(at,parent)
    update_result(at,result)

def number_of(result: Computed) -> U32:
  ComputedData{_,number} = result
  number

def current_of(result: Computed) -> Context.Current:
  ComputedData{current,_} = result
  current

# The original reads getStat a second time whenever it finds a new best stat.
# Preserve those event calls/allocations/RNG draws, and the first-stat tie rule.
def best_scan(n: Nat, +at: U32, +index: U32, +unboosted: Bool, +unmodified: Bool,
  parent: Context.Current, +chosen: U32, +best: U32) -> S.Mem<Computed>:
  match n:
    case 0n: S.Mem.pure(Computed,ComputedData{parent,chosen})
    case 1n+p:
      do S.Mem<Computed>:
        +checked : Computed <- get(at,index,unboosted,unmodified,parent)
        +better : Bool = U32.is_gt(number_of(checked),best)
        +result : Computed <- S.branch(Computed,better,get(at,index,unboosted,unmodified,current_of(checked)),
          S.Mem.pure(Computed,checked))
        best_scan(p,at,(index + 1 : U32),unboosted,unmodified,current_of(result),
          Bool.pick(U32,better,index,chosen),Bool.pick(U32,better,number_of(result),best))

def best(at: U32, unboosted: Bool, unmodified: Bool, parent: Context.Current) -> S.Mem<Computed>:
  best_scan(5n,at,0,unboosted,unmodified,parent,0,0)
