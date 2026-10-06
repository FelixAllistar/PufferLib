import Base
import ./Mem.bend as S
import ./Catalog.bend as C
import ./Registry.bend as Registry
import ./Events.bend as Events
import ./Queue.bend as Q
import ./EventListener.bend as Run
import ./EventValue.bend as V

# Resolved holder facts, not policy observations. A collector supplies cached
# speed, unboosted Magic Bounce speed, and the switch-in speedOrder rank.
type HolderFacts is Data:
  HolderData{at: U32, pokemon: Bool, cached_speed: U32, unboosted_speed: U32,
    switch_rank: U32, redirect_order: U32, value: V.Value}

type EffectMeta is Data:
  EffectMetadata{kind: Events.EffectKind, ability: U32}

def metadata(effect: Registry.Effect) -> S.Mem<EffectMeta>:
  match effect:
    case Registry.AbilityEffect{id}: S.Mem.pure(EffectMeta,EffectMetadata{Events.Ability{},id})
    case Registry.ItemEffect{_}: S.Mem.pure(EffectMeta,EffectMetadata{Events.Item{},0})
    case Registry.FormatEffect{}: S.Mem.pure(EffectMeta,EffectMetadata{Events.Format{},0})
    case Registry.ConditionEffect{+id}:
      do S.Mem<EffectMeta>:
        kind : U32 <- C.condition(id,0)
        ability : U32 <- C.condition(id,1)
        return EffectMetadata{Events.kind(U32.to_nat(kind)),ability}
    case _: S.Mem.pure(EffectMeta,EffectMetadata{Events.Other{},0})

def resolved(+effect: Registry.Effect, +handler: Registry.Handler, state: U32, scope: U32,
  creation: U32, holder: HolderFacts, index: U32, callback_flags: U32, meta: EffectMeta) -> Run.ListenerRow:
  Registry.H{_,_,order,priority,sub,_} = handler
  HolderData{at,+pokemon,cached,unboosted,rank,redirect_order,value} = holder
  EffectMetadata{+kind,+ability} = meta
  +flags : U32 = (callback_flags .|. Bool.pick(U32,pokemon,8,0) : U32)
  Run.ListenerData{effect,handler,state,at,pokemon,
    Q.E{0,order,priority,Events.speed(kind,ability,flags,cached,unboosted,rank),
      Events.sub_order(kind,ability,scope,sub),Events.creation_order(flags,creation)},
    Bool.pick(U32,pokemon,redirect_order,0),index,value}

def resolve(+effect: Registry.Effect, handler: Registry.Handler, callback: U32, state: U32,
  scope: U32, creation: U32, holder: HolderFacts, index: U32) -> S.Mem<Run.ListenerRow>:
  do S.Mem<Run.ListenerRow>:
    meta : EffectMeta <- metadata(effect)
    flags : U32 <- C.callback_info(callback,0)
    return resolved(effect,handler,state,scope,creation,holder,index,flags,meta)
