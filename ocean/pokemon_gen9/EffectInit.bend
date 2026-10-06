import Base
import ./Mem.bend as S
import ./EffectMem.bend as Mem
import ./EffectStore.bend as Store
import ./EventValue.bend as V
import ./Catalog.bend as C
import ./data/Layout.bend as Layout
import ./data/conditions.bend as D

# Private roots. These are object identities, never player observations.
def format_state() -> U32:
  524
def weather_state() -> U32:
  525
def terrain_state() -> U32:
  526
def current_state() -> U32:
  527
def pseudo_weather_map() -> U32:
  529
def side_maps(side: U32) -> U32:
  (4192 + side * 8 : U32)
def party_slot(side: U32, position: U32) -> U32:
  (4160 + side * 6 + position : U32)
def species_state(at: U32) -> U32:
  (at + 89 : U32)
def status_state(at: U32) -> U32:
  (at + 90 : U32)
def ability_state(at: U32) -> U32:
  (at + 91 : U32)
def item_state(at: U32) -> U32:
  (at + 92 : U32)
def volatile_map(at: U32) -> U32:
  (at + 93 : U32)

# Condition dictionaries use a separate property namespace, preserving source
# insertion order. Values alias actual persistent effect objects.
def condition_key(id: U32) -> U32:
  (65536 + id : U32)

def id_fields(text: U32) -> +List<Store.Field>:
  [Store.FieldData{1,V.VText{text}}]

def holder_fields(text: U32, target: U32) -> +List<Store.Field>:
  [Store.FieldData{1,V.VText{text}},Store.FieldData{2,V.VObject{1,target}}]

def sleep_clause_fields(text: U32) -> +List<Store.Field>:
  [Store.FieldData{1,V.VText{text}},Store.FieldData{6,V.VNull{}},
    Store.FieldData{14,V.VUndefined{}},Store.FieldData{5,V.VUndefined{}}]

# Default format constructor, before any players/start events. Sleep Clause's
# FieldStart/PseudoWeatherChange callbacks are absent at the pin. Its state
# initially has no target and consumes no creation-order counter.
def battle() -> S.Mem<Unit>:
  do S.Mem<Unit>:
    format : U32 <- Mem.initialize(id_fields(Layout.format_id_text()),False{},False{})
    S.put(format_state(),format)
    weather : U32 <- Mem.initialize(id_fields(0),False{},False{})
    S.put(weather_state(),weather)
    terrain : U32 <- Mem.initialize(id_fields(0),False{},False{})
    S.put(terrain_state(),terrain)
    +pseudo : U32 <- Mem.raw_object(Nil{})
    S.put(pseudo_weather_map(),pseudo)
    state : U32 <- Mem.initialize(id_fields(0),False{},False{})
    S.put(current_state(),state)
    text : U32 <- C.condition_text(D.conditions_sleepclausemod(),0)
    rule : U32 <- Mem.initialize(sleep_clause_fields(text),False{},False{})
    Mem.write(pseudo,condition_key(D.conditions_sleepclausemod()),V.VObject{7,rule})

# Constructor-final objects. clearVolatile creates the final empty volatile
# map and leaves the four initialized states intact. Pokémon remain inactive.
def pokemon(+at: U32, sid: U32, ability: U32, item: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    species_text : U32 <- C.species_text(sid,0)
    species : U32 <- Mem.initialize(id_fields(species_text),False{},False{})
    S.put(species_state(at),species)
    status : U32 <- Mem.empty_state()
    S.put(status_state(at),status)
    ability_text : U32 <- C.ability_text(ability,0)
    abi : U32 <- Mem.initialize(holder_fields(ability_text,at),True{},False{})
    S.put(ability_state(at),abi)
    item_text : U32 <- C.item_text(item,0)
    held : U32 <- Mem.initialize(holder_fields(item_text,at),True{},False{})
    S.put(item_state(at),held)
    volatile : U32 <- Mem.raw_object(Nil{})
    S.put(volatile_map(at),volatile)

def side(+id: U32) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    conditions : U32 <- Mem.raw_object(Nil{})
    S.put(side_maps(id),conditions)
    slot : U32 <- Mem.raw_object(Nil{})
    S.put((side_maps(id) + 1 : U32),slot)
