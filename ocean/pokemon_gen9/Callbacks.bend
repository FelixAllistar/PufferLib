import Base
import ./Mem.bend as S
import ./Math.bend as M
import ./Number.bend as N
import ./Catalog.bend as C
import ./Registry.bend as Registry
import ./EventContext.bend as Context
import ./EventValue.bend as V
import ./Suppress.bend as Suppress
import ./data/abilities.bend as A
import ./data/items.bend as I
import ./data/conditions.bend as D
import ./data/callbacks.bend as B
import ./data/species.bend as P
import ./EffectMem.bend as Effects
import ./EffectStore.bend as EffectStore
import ./BoostTable.bend as BoostTable
import ./Weight.bend as Weight
import ./WeatherQuery.bend as Weather
import ./TerrainQuery.bend as Terrain
import ./TypeBodies.bend as Types
import ./PokemonTypes.bend as LiveTypes
import ./data/TypeTexts.bend as TypeText

# These are executable bodies, distinct from Registry's source descriptors.
# Other function bodies fail explicitly until ported; literals are complete.
type StatCondition is Data:
  HasStatus{}
  HalfHP{}
  BaseNFE{}
  BasePikachu{}
  NoDynamax{}
  FieldWeather{first: U32, second: U32}
  PokemonWeather{first: U32, second: U32}
  UnburdenActive{}
  FieldTerrain{terrain: U32}
  TypeWeather{type_text: U32, weather: U32}

type BodyData is Data:
  UnportedBody{}
  ChainBody{modifier: U32}
  DirectBody{modifier: U32}
  ParalysisSpeed{}
  ConditionalChain{condition: StatCondition, modifier: U32}
  ConditionalDirect{condition: StatCondition, modifier: U32}
  EffectCounterChain{modifier: U32}
  EvasionBoosts{}
  UnawareBoosts{}
  DoubleWeight{}
  HalfWeight{}
  TypeArrayBody{kind: Types.BodyKind}

def terrain_ability(+id: U32, +callback: U32) -> BodyData:
  Bool.pick(BodyData,U32.is_eq(id,A.abilities_surgesurfer()) && U32.is_eq(callback,B.callbacks_onmodifyspe()),
    ConditionalChain{FieldTerrain{D.conditions_electricterrain()},8192},
    Bool.pick(BodyData,U32.is_eq(id,A.abilities_hadronengine()) && U32.is_eq(callback,B.callbacks_onmodifyspa()),
      ConditionalChain{FieldTerrain{D.conditions_electricterrain()},5461},
      Bool.pick(BodyData,U32.is_eq(id,A.abilities_grasspelt()) && U32.is_eq(callback,B.callbacks_onmodifydef()),
        ConditionalChain{FieldTerrain{D.conditions_grassyterrain()},6144},UnportedBody{})))

def environment_ability(+id: U32, +callback: U32) -> BodyData:
  Bool.pick(BodyData,U32.is_eq(callback,B.callbacks_onmodifyspe()),
    Bool.pick(BodyData,U32.is_eq(id,A.abilities_chlorophyll()),
      ConditionalChain{PokemonWeather{D.conditions_sunnyday(),D.conditions_desolateland()},8192},
      Bool.pick(BodyData,U32.is_eq(id,A.abilities_swiftswim()),
        ConditionalChain{PokemonWeather{D.conditions_raindance(),D.conditions_primordialsea()},8192},
        Bool.pick(BodyData,U32.is_eq(id,A.abilities_sandrush()),
          ConditionalChain{FieldWeather{D.conditions_sandstorm(),D.conditions_sandstorm()},8192},
          Bool.pick(BodyData,U32.is_eq(id,A.abilities_slushrush()),
            ConditionalChain{FieldWeather{D.conditions_hail(),D.conditions_snowscape()},8192},terrain_ability(id,callback))))),
    Bool.pick(BodyData,U32.is_eq(callback,B.callbacks_onmodifyspa()) && U32.is_eq(id,A.abilities_solarpower()),
      ConditionalChain{PokemonWeather{D.conditions_sunnyday(),D.conditions_desolateland()},6144},
      Bool.pick(BodyData,U32.is_eq(callback,B.callbacks_onmodifyatk()) && U32.is_eq(id,A.abilities_orichalcumpulse()),
        ConditionalChain{PokemonWeather{D.conditions_sunnyday(),D.conditions_desolateland()},5461},terrain_ability(id,callback))))

def other_ability_body(+id: U32, +callback: U32) -> BodyData:
  Bool.pick(BodyData,U32.is_eq(id,A.abilities_defeatist()) &&
    (U32.is_eq(callback,B.callbacks_onmodifyatk()) || U32.is_eq(callback,B.callbacks_onmodifyspa())),
    ConditionalChain{HalfHP{},2048},
    Bool.pick(BodyData,U32.is_eq(id,A.abilities_guts()) && U32.is_eq(callback,B.callbacks_onmodifyatk()) ||
      U32.is_eq(id,A.abilities_marvelscale()) && U32.is_eq(callback,B.callbacks_onmodifydef()) ||
      U32.is_eq(id,A.abilities_quickfeet()) && U32.is_eq(callback,B.callbacks_onmodifyspe()),
      ConditionalChain{HasStatus{},6144},
      Bool.pick(BodyData,U32.is_eq(id,A.abilities_slowstart()) &&
        (U32.is_eq(callback,B.callbacks_onmodifyatk()) || U32.is_eq(callback,B.callbacks_onmodifyspe())),
        EffectCounterChain{2048},Bool.pick(BodyData,U32.is_eq(id,A.abilities_unaware()) &&
          U32.is_eq(callback,B.callbacks_onanymodifyboost()),UnawareBoosts{},environment_ability(id,callback)))))

type Args is Data:
  ArgsData{relay: V.Value, target: U32}

type Returned is Data:
  ReturnedData{current: Context.Current, value: V.Value}

def ability_body(+id: U32, +callback: U32) -> BodyData:
  Bool.pick(BodyData,U32.is_eq(callback,B.callbacks_onmodifyweight()) && U32.is_eq(id,A.abilities_heavymetal()),
    DoubleWeight{},Bool.pick(BodyData,U32.is_eq(callback,B.callbacks_onmodifyweight()) && U32.is_eq(id,A.abilities_lightmetal()),
    HalfWeight{},Bool.pick(BodyData,U32.is_eq(callback,B.callbacks_onmodifyatk()) &&
    (U32.is_eq(id,A.abilities_hugepower()) || U32.is_eq(id,A.abilities_purepower())),ChainBody{8192},
    Bool.pick(BodyData,U32.is_eq(id,A.abilities_furcoat()) && U32.is_eq(callback,B.callbacks_onmodifydef()),
      ChainBody{8192},Bool.pick(BodyData,U32.is_eq(id,A.abilities_hustle()) && U32.is_eq(callback,B.callbacks_onmodifyatk()),
        DirectBody{6144},other_ability_body(id,callback))))))

def other_item_body(+id: U32, +callback: U32) -> BodyData:
  Bool.pick(BodyData,U32.is_eq(id,I.items_choiceband()) && U32.is_eq(callback,B.callbacks_onmodifyatk()) ||
    U32.is_eq(id,I.items_choicescarf()) && U32.is_eq(callback,B.callbacks_onmodifyspe()) ||
    U32.is_eq(id,I.items_choicespecs()) && U32.is_eq(callback,B.callbacks_onmodifyspa()),
    ConditionalChain{NoDynamax{},6144},
    Bool.pick(BodyData,U32.is_eq(id,I.items_eviolite()) &&
      (U32.is_eq(callback,B.callbacks_onmodifydef()) || U32.is_eq(callback,B.callbacks_onmodifyspd())),
      ConditionalChain{BaseNFE{},6144},
      Bool.pick(BodyData,U32.is_eq(id,I.items_lightball()) &&
        (U32.is_eq(callback,B.callbacks_onmodifyatk()) || U32.is_eq(callback,B.callbacks_onmodifyspa())),
        ConditionalChain{BasePikachu{},8192},
        Bool.pick(BodyData,U32.is_eq(id,I.items_ironball()) && U32.is_eq(callback,B.callbacks_onmodifyspe()),
          ChainBody{2048},UnportedBody{}))))

def item_body(+id: U32, +callback: U32) -> BodyData:
  Bool.pick(BodyData,U32.is_eq(id,I.items_floatstone()) && U32.is_eq(callback,B.callbacks_onmodifyweight()),HalfWeight{},
    Bool.pick(BodyData,U32.is_eq(id,I.items_assaultvest()) && U32.is_eq(callback,B.callbacks_onmodifyspd()),
      ChainBody{6144},other_item_body(id,callback)))

def weather_condition_body(+id: U32, +callback: U32) -> BodyData:
  Bool.pick(BodyData,U32.is_eq(id,D.conditions_sandstorm()) && U32.is_eq(callback,B.callbacks_onmodifyspd()),
    ConditionalDirect{TypeWeather{TypeText.rock(),D.conditions_sandstorm()},6144},
    Bool.pick(BodyData,U32.is_eq(id,D.conditions_snowscape()) && U32.is_eq(callback,B.callbacks_onmodifydef()),
      ConditionalDirect{TypeWeather{TypeText.ice(),D.conditions_snowscape()},6144},UnportedBody{}))

def condition_body(+id: U32, +callback: U32) -> BodyData:
  Bool.pick(BodyData,U32.is_eq(id,D.conditions_tailwind()) && U32.is_eq(callback,B.callbacks_onmodifyspe()),
    ChainBody{8192},Bool.pick(BodyData,U32.is_eq(id,D.conditions_par()) && U32.is_eq(callback,B.callbacks_onmodifyspe()),
      ParalysisSpeed{},Bool.pick(BodyData,U32.is_eq(callback,B.callbacks_onmodifyboost()) &&
        (U32.is_eq(id,D.conditions_foresight()) || U32.is_eq(id,D.conditions_miracleeye())),
        EvasionBoosts{},Bool.pick(BodyData,U32.is_eq(id,D.conditions_unburden()) &&
          U32.is_eq(callback,B.callbacks_onmodifyspe()),ConditionalChain{UnburdenActive{},8192},weather_condition_body(id,callback)))))

def condition_dispatch(+id: U32, +callback: U32) -> S.Mem<BodyData>:
  do S.Mem<BodyData>:
    +ability : U32 <- C.condition(id,1)
    +item : U32 <- C.condition(id,2)
    return Bool.pick(BodyData,U32.is_eq(callback,B.callbacks_ontype()),TypeArrayBody{Types.condition_kind(id,callback)},
      Bool.pick(BodyData,U32.is_ne(ability,0),ability_body(ability,callback),
        Bool.pick(BodyData,U32.is_ne(item,0),item_body(item,callback),condition_body(id,callback))))

def body(effect: Registry.Effect, callback: U32) -> S.Mem<BodyData>:
  match effect:
    case Registry.AbilityEffect{id}: S.Mem.pure(BodyData,ability_body(id,callback))
    case Registry.ItemEffect{id}: S.Mem.pure(BodyData,item_body(id,callback))
    case Registry.ConditionEffect{id}: condition_dispatch(id,callback)
    case Registry.MoveEffect{_}: S.Mem.pure(BodyData,UnportedBody{})
    case Registry.SpeciesEffect{id}:
      do S.Mem<BodyData>:
        kind : Types.BodyKind <- Types.kind(Registry.SpeciesEffect{id},callback)
        return TypeArrayBody{kind}
    case Registry.FormatEffect{}: S.Mem.pure(BodyData,UnportedBody{})

# Stat callbacks receive integer relays. Reject an out-of-contract fraction
# instead of silently moving its truncation before the multiplication.
def integer_scalar(+number: N.Scalar) -> Result<&2,&2,U32,U32>:
  match number:
    case N.Finite{_,+numerator,+denominator}:
      +den : Nat = U32.to_nat(U32.max(denominator,1))
      Bool.pick(Result<&2,&2,U32,U32>,U32.is_ne(denominator,0) &&
        Nat.is_eq((numerator % den : Nat),0n) && Nat.is_le((numerator / den : Nat),U32.to_nat(4294967295)),
        Done{N.trunc(number)},Fail{3})
    case _: Done{0}

def integer(value: V.Value) -> Result<&2,&2,U32,U32>:
  match value:
    case V.VNumber{number}: integer_scalar(number)
    case _: Fail{3}

def numeric_return(current: Context.Current, number: U32) -> Returned:
  ReturnedData{current,V.VNumber{N.Finite{False{},U32.to_nat(number),1}}}

def direct_return(current: Context.Current, relay: V.Value, modifier: U32) -> S.Mem<Returned>:
  do S.Mem<Returned>:
    number : U32 <- S.from_result(U32,integer(relay))
    return numeric_return(current,M.modify(number,modifier))

def paralysis_result(quick_feet: Bool, pair: Context.Current & U32) -> Returned:
  (current,+number) = pair
  numeric_return(current,Bool.pick(U32,quick_feet,number,(number / 2 : U32)))

def stat_condition(condition: StatCondition, +target: U32, +current: Context.Current) -> S.Mem<Bool>:
  match condition:
    case HasStatus{}:
      do S.Mem<Bool>:
        status : U32 <- S.get((target + 13 : U32))
        return U32.is_ne(status,0)
    case HalfHP{}:
      do S.Mem<Bool>:
        hp : U32 <- S.get((target + 7 : U32))
        maxhp : U32 <- S.get((target + 8 : U32))
        return U32.is_le(hp,(maxhp / 2 : U32))
    case BaseNFE{}:
      do S.Mem<Bool>:
        species : U32 <- S.get((target + 2 : U32))
        nfe : U32 <- C.species_field(species,11)
        return U32.is_ne(nfe,0)
    case BasePikachu{}:
      do S.Mem<Bool>:
        species : U32 <- S.get((target + 2 : U32))
        base : U32 <- C.species_field(species,1)
        return U32.is_eq(base,P.species_pikachu())
    case NoDynamax{}:
      do S.Mem<Bool>:
        flags : U32 <- S.get((target + 23 : U32))
        return U32.is_eq((flags .&. 256 : U32),0)
    case FieldWeather{first,second}: Weather.field_condition(first,second)
    case PokemonWeather{first,second}: Weather.pokemon_condition(target,current,first,second)
    case FieldTerrain{terrain}: Terrain.matches([terrain],current,V.VUndefined{})
    case TypeWeather{type_text,+weather}:
      do S.Mem<Bool>:
        matches : Bool <- LiveTypes.has(target,current,[type_text])
        # Source evaluates hasType first, including its nested Type event and
        # mutations, even if weather is absent or suppressed.
        S.branch(Bool,matches,Weather.pokemon_condition(target,current,weather,weather),S.Mem.pure(Bool,False{}))
    case UnburdenActive{}:
      do S.Mem<Bool>:
        held : U32 <- S.get((target + 6 : U32))
        ignored : Bool <- Suppress.ability(target)
        return U32.is_eq(held,0) && Bool.not(ignored)

def counter_current(+current: Context.Current, counter: V.Value, modifier: U32) -> Context.Current:
  Bool.pick(Context.Current,V.truthy(counter),Context.chain_current(current,modifier),current)

def pokemon_reference(value: V.Value) -> U32:
  match value:
    case V.VObject{1,at}: at
    case _: 0

def run_body(kind: BodyData, +current: Context.Current, args: Args) -> S.Mem<Returned>:
  match kind:
    case UnportedBody{}: S.raise(Returned,4)
    case ChainBody{modifier}:
      S.Mem.pure(Returned,ReturnedData{Context.chain_current(current,modifier),V.VUndefined{}})
    case DirectBody{modifier}:
      ArgsData{relay,_} = args
      direct_return(current,relay,modifier)
    case ParalysisSpeed{}:
      ArgsData{relay,+target} = args
      do S.Mem<Returned>:
        number : U32 <- S.from_result(U32,integer(relay))
        ability : U32 <- S.get((target + 5 : U32))
        ignored : Bool <- Suppress.ability(target)
        return paralysis_result(U32.is_eq(ability,A.abilities_quickfeet()) && Bool.not(ignored),
          Context.final_current(current,number))
    case ConditionalChain{condition,modifier}:
      ArgsData{_,target} = args
      do S.Mem<Returned>:
        active : Bool <- stat_condition(condition,target,current)
        return ReturnedData{Bool.pick(Context.Current,active,Context.chain_current(current,modifier),current),V.VUndefined{}}
    case ConditionalDirect{condition,modifier}:
      ArgsData{relay,target} = args
      do S.Mem<Returned>:
        active : Bool <- stat_condition(condition,target,current)
        S.branch(Returned,active,direct_return(current,relay,modifier),
          S.Mem.pure(Returned,ReturnedData{current,V.VUndefined{}}))
    case EffectCounterChain{modifier}:
      do S.Mem<Returned>:
        counter : V.Value <- Effects.read(Effects.current_state(current),EffectStore.counter_key())
        return ReturnedData{counter_current(current,counter,modifier),V.VUndefined{}}
    case EvasionBoosts{}:
      ArgsData{relay,_} = args
      do S.Mem<Returned>:
        reference : U32 <- S.from_result(U32,BoostTable.object_reference(relay))
        BoostTable.clear_positive_evasion(reference)
        return ReturnedData{current,V.VUndefined{}}
    case UnawareBoosts{}:
      ArgsData{relay,+target} = args
      do S.Mem<Returned>:
        reference : U32 <- S.from_result(U32,BoostTable.object_reference(relay))
        holder : V.Value <- Effects.read(Effects.current_state(current),EffectStore.target_key())
        actor : U32 <- S.get(548)
        opponent : U32 <- S.get(535)
        BoostTable.unaware(reference,pokemon_reference(holder),target,actor,opponent)
        return ReturnedData{current,V.VUndefined{}}
    case DoubleWeight{}:
      ArgsData{relay,_} = args
      do S.Mem<Returned>:
        number : N.Scalar <- S.from_result(N.Scalar,Weight.double_value(relay))
        return ReturnedData{current,V.VNumber{number}}
    case HalfWeight{}:
      ArgsData{relay,_} = args
      do S.Mem<Returned>:
        number : U32 <- S.from_result(U32,Weight.half_value(relay))
        return numeric_return(current,number)
    case TypeArrayBody{kind}:
      ArgsData{relay,target} = args
      do S.Mem<Returned>:
        value : V.Value <- Types.call(kind,current,relay,target)
        return ReturnedData{current,value}

def call(effect: Registry.Effect, handler: Registry.Handler, +current: Context.Current, args: Args) -> S.Mem<Returned>:
  Registry.H{callback,+tag,_,_,_,payload} = handler
  S.branch(Returned,U32.is_eq(tag,0),
    (do S.Mem<Returned>:
      kind : BodyData <- body(effect,callback)
      run_body(kind,current,args)),
    (do S.Mem<Returned>:
      value : V.Value <- S.from_result(V.Value,V.literal_kind(U32.to_nat(tag),payload))
      return ReturnedData{current,value}))
