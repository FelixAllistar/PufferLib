import Base
import ./StatTable.bend as StatTable
import ./MoveSlots.bend as MoveSlots
import ./Spread.bend as Spread
import ./TextStore.bend as TextStore
import ./TextJoin.bend as TextJoin
import ./TypeChange.bend as TypeChange
import ./ValueArray.bend as Values
import ./TypeBodies.bend as TypeBodies
import ./data/TypeTexts.bend as TypeTexts
import ./PokemonTypes.bend as PokemonTypes
import ./Grounded.bend as Grounded
import ./TypeInit.bend as TypeInit
import ./data/types.bend as TypeIds
import ./EventListener.bend as EventListener
import ./TerrainQuery.bend as Terrain
import ./BoostTable.bend as BoostTable
import ./LiveStats.bend as LiveStats
import ./Weight.bend as Weight
import ./WeatherQuery.bend as Weather
import ./Math.bend as M
import ./Rng.bend as R
import ./Lists.bend as L
import ./Stats.bend as Stats
import ./Queue.bend as Queue
import ./Damage.bend as Damage
import ./Events.bend as Events
import ./Boosts.bend as Boosts
import ./Sodium.bend as Sodium
import ./Number.bend as Number
import ./Suppress.bend as Suppress
import ./data/abilities.bend as A
import ./data/items.bend as I
import ./MoveView.bend as MoveView
import ./Request.bend as Request
import ./EventValue.bend as EventValue
import ./EventContext.bend as EventContext
import ./Callbacks.bend as Callbacks
import ./data/callbacks.bend as B
import ./Registry.bend as Registry
import ./RunEvent.bend as RunEvent
import ./VectorEvent.bend as VectorEvent
import ./EffectStore.bend as EffectStore
import ./Collect.bend as Collect

law modifier_rounds_half_down:
  {M.modify(1, 2048) == 0 : U32}

law modifier_keeps_above_half:
  {M.modify(1, 2049) == 1 : U32}

law modifier_identity:
  {M.modify(123, 4096) == 123 : U32}

law scale_full_word:
  {M.mul_hi(4294967295, 4294967295) == 4294967294 : U32}

law scale_preserves_exclusive_bound:
  {M.mul_hi(4294967295, 1024) == 1023 : U32}

law seed_zero_first_step:
  {R.next(R.S{0, 0, 0, 0}) == (R.S{0, 0, 38, 40643}, 0) : R.Seed & U32}

law swap_pop_uses_old_last:
  {L.pop([11, 22, 33, 44], 1) == [11, 44, 33] : +List<U32>}

law filtering_preserves_sampling_order:
  {L.filter_in([11, 22, 33, 44], [44, 22]) == [22, 44] : +List<U32>}

law damage_cannot_underflow:
  {Stats.remaining(20, 100) == 0 : U32}

law healing_cannot_revive:
  {Stats.healed(0, 300, 100) == 0 : U32}

law healing_stops_at_max_hp:
  {Stats.healed(290, 300, 100) == 10 : U32}

law negative_boost_floors:
  {Stats.boosted(101, 5) == 67 : U32}

law tera_precedes_move:
  {Queue.compare(Queue.E{0,106,32768,100,32768,0}, Queue.E{1,200,32838,1000,32768,0}) == LT{} : Cmp}

law unordered_effect_follows_ordered:
  {Queue.compare(Queue.E{0,0,32768,100,32768,0}, Queue.E{1,300,32768,100,32768,0}) == GT{} : Cmp}

law damage_zero_has_minimum_one:
  {Damage.finish(0, False{}) == 1 : U32}

law damage_truncates_after_minimum:
  {Damage.finish(65536, False{}) == 0 : U32}

law critical_rounds_down:
  {Damage.critical(101, True{}) == 151 : U32}

law confusion_truncates_before_roll:
  {Damage.confusion_base(100, 40, 195051, 100) == 3 : U32}

law slot_condition_precedes_side_condition:
  {Events.sub_order(Events.Condition{}, 0, 2, 32768) == 32771 : U32}

law switch_in_keeps_negative_half_speed:
  {Events.speed(Events.Other{}, 0, 9, 0, 0, 2) == 32767 : U32}

law boost_caps_at_plus_six:
  {Boosts.next(11, 32771) == 12 : U32}

law boost_caps_at_minus_six:
  {Boosts.next(1, 32765) == 0 : U32}

law capped_boost_reports_actual_delta:
  {Boosts.capped(11, 32771) == 32769 : U32}

# RFC 8439 section 2.1.1 independent quarter-round test vector.
law chacha_quarter_vector:
  {Sodium.quarter(Sodium.Q{286331153,16909060,2609737539,19088743}) ==
    Sodium.Q{3928658676,3407673550,1166100270,1484899515} : Sodium.Quarter}

law sodium_output_byte_order:
  {Sodium.swap_bytes(16909060) == 67305985 : U32}

law negative_truncation_wraps:
  {Number.trunc(Number.Finite{True{}, 1n, 1}) == 4294967295 : U32}

law positive_fraction_is_not_zero_damage:
  {Number.below_one(Number.Finite{False{}, 1n, 2}) == True{} : Bool}

law nan_truncates_to_zero:
  {Number.trunc(Number.NaN{}) == 0 : U32}

law transformed_notransform_precedes_unsuppressible:
  {Suppress.ignoring_ability(Suppress.F{A.abilities_disguise(),0,67,False{}},
    False{}, False{}, True{}, True{}) == True{} : Bool}

law shield_does_not_override_gastro_acid:
  {Suppress.ignoring_ability(Suppress.F{A.abilities_levitate(),I.items_abilityshield(),69,False{}},
    False{}, True{}, False{}, False{}) == True{} : Bool}

law primal_orb_ignores_inactive_item_suppression:
  {Suppress.ignoring_item(Suppress.F{A.abilities_klutz(),I.items_redorb(),0,False{}},
    True{}, True{}, True{}, False{}, False{}) == False{} : Bool}

law fling_checks_klutz_before_ignore_klutz:
  {Suppress.ignoring_item(Suppress.F{A.abilities_klutz(),I.items_abilityshield(),65,False{}},
    False{}, True{}, False{}, True{}, False{}) == True{} : Bool}

law hidden_disabled_stays_selectable_in_restricted_request:
  {MoveView.disabled(1, 2, True{}) == False{} : Bool}

law no_pp_precedes_hidden_disable:
  {MoveView.disabled(0, 2, True{}) == True{} : Bool}

law hidden_trapping_is_not_a_public_trapped_flag:
  {Request.visible_flags(2, 0, 0, 0, True{}) == 0 : U32}

law maybe_trapping_requires_a_switch_candidate:
  {Request.visible_flags(0, 0, 0, 1, False{}) == 0 : U32}

law single_null_stays_null:
  {EventValue.initial(EventValue.VNull{}, True{}) == EventValue.VNull{} : EventValue.Value}

law run_null_defaults_true:
  {EventValue.initial(EventValue.VNull{}, False{}) == EventValue.VBool{True{}} : EventValue.Value}

law undefined_does_not_stop_falsy_relay:
  {EventValue.replace(EventValue.VBool{False{}}, EventValue.VUndefined{}, True{}) ==
    EventValue.R{EventValue.VBool{False{}}, False{}} : EventValue.Relay}

law empty_text_stops_run_event:
  {EventValue.replace(EventValue.VBool{True{}}, EventValue.VText{0}, False{}) ==
    EventValue.R{EventValue.VText{0}, True{}} : EventValue.Relay}

law objects_are_truthy_even_with_zero_id:
  {EventValue.truthy(EventValue.VObject{0,0}) == True{} : Bool}

law negative_fraction_skips_final_modifier:
  {EventValue.final_value(EventValue.VNumber{Number.Finite{True{},3n,2}}, 8192) ==
    EventValue.VNumber{Number.Finite{True{},3n,2}} : EventValue.Value}

law modifier_chain_uses_signed_shift:
  {M.chain(65536,32768) == 4294443008 : U32}

law run_event_defaults_target_and_modifier:
  {EventContext.run_event(EventContext.E{7,EventValue.VNull{},EventValue.VNull{},
    EventValue.VUndefined{},EventContext.NoModifier{}}) ==
    EventContext.E{7,EventValue.VObject{4,0},EventValue.VNull{},EventValue.VUndefined{},
      EventContext.Q12{4096}} : EventContext.EventData}

law single_event_has_no_modifier:
  {EventContext.single_event(EventContext.E{7,EventValue.VNull{},EventValue.VNull{},
    EventValue.VUndefined{},EventContext.Q12{8192}}) ==
    EventContext.E{7,EventValue.VNull{},EventValue.VNull{},EventValue.VUndefined{},
      EventContext.NoModifier{}} : EventContext.EventData}

law event_modifier_keeps_source_double_rounding:
  {M.round_product_word(4294967295,11) == 0 : U32}

law huge_power_chains_attack:
  {Callbacks.ability_body(A.abilities_hugepower(),B.callbacks_onmodifyatk()) ==
    Callbacks.ChainBody{8192} : Callbacks.BodyData}

law hustle_modifies_attack_directly:
  {Callbacks.ability_body(A.abilities_hustle(),B.callbacks_onmodifyatk()) ==
    Callbacks.DirectBody{6144} : Callbacks.BodyData}

law signed_literal_zero_is_falsy:
  {Registry.truthy(Registry.Found{Registry.H{1,5,0,32768,32768,32768}}) == False{} : Bool}

law negative_literal_at_encoding_zero_is_truthy:
  {Registry.truthy(Registry.Found{Registry.H{1,5,0,32768,32768,0}}) == True{} : Bool}

law hit_event_sort_precedes_fast_exit_sort:
  {RunEvent.sort_mode(B.callbacks_ontryhit(),True{}) == 2n : Nat}

law ordinary_fast_exit_uses_redirect_sort:
  {RunEvent.sort_mode(B.callbacks_onmodifyspe(),True{}) == 1n : Nat}

law vector_damaging_hit_accepts_numeric_zero:
  {VectorEvent.accepts(EventValue.VNumber{Number.Finite{True{},0n,1}},B.callbacks_ondamaginghit()) == True{} : Bool}

law vector_damaging_hit_skips_boolean_false:
  {VectorEvent.accepts(EventValue.VBool{False{}},B.callbacks_ondamaginghit()) == False{} : Bool}

law vector_try_hit_skips_numeric_zero:
  {VectorEvent.accepts(EventValue.VNumber{Number.Finite{False{},0n,1}},B.callbacks_ontryhit()) == False{} : Bool}

# Universal scope/relay laws. These cover arbitrary typed references and
# values, but remain native properties, not source-to-model equivalence.
law undefined_return_preserves_every_relay:
  for old: EventValue.Value
  for fast: Bool
  {EventValue.replace(old,EventValue.VUndefined{},fast) == EventValue.R{old,False{}} : EventValue.Relay}

law undefined_single_return_preserves_every_relay:
  for old: EventValue.Value
  {EventValue.single_result(old,EventValue.VUndefined{}) == old : EventValue.Value}

law every_object_reference_is_truthy:
  for family: U32
  for id: U32
  {EventValue.truthy(EventValue.VObject{family,id}) == True{} : Bool}

law handler_scope_restores_references_and_keeps_event:
  for parent_effect: EventContext.EffectRef
  for parent_state: U32
  for child_effect: EventContext.EffectRef
  for child_state: U32
  for child_event: EventContext.EventData
  for depth: U32
  {EventContext.leave_handler(parent_effect,parent_state,
    EventContext.C{child_effect,child_state,child_event,depth}) ==
    EventContext.C{parent_effect,parent_state,child_event,depth} : EventContext.Current}

law run_scope_restores_event_and_keeps_effect_references:
  for effect: EventContext.EffectRef
  for state: U32
  for parent_event: EventContext.EventData
  for parent_depth: U32
  for child_event: EventContext.EventData
  for child_depth: U32
  {EventContext.leave_run(parent_event,parent_depth,
    EventContext.C{effect,state,child_event,child_depth}) ==
    EventContext.C{effect,state,parent_event,parent_depth} : EventContext.Current}

law handler_entry_preserves_event_and_depth:
  for parent_effect: EventContext.EffectRef
  for parent_state: U32
  for child_effect: EventContext.EffectRef
  for child_state: U32
  for event: EventContext.EventData
  for depth: U32
  {EventContext.enter_handler(EventContext.C{parent_effect,parent_state,event,depth},child_effect,child_state) ==
    EventContext.C{child_effect,child_state,event,depth} : EventContext.Current}

law single_scope_clears_only_modifier:
  for id: U32
  for target: EventValue.Value
  for source: EventValue.Value
  for source_effect: EventValue.Value
  for modifier: EventContext.Modifier
  {EventContext.single_event(EventContext.E{id,target,source,source_effect,modifier}) ==
    EventContext.E{id,target,source,source_effect,EventContext.NoModifier{}} : EventContext.EventData}

law every_boolean_relay_skips_final_modifier:
  for value: Bool
  for modifier: U32
  {EventValue.final_value(EventValue.VBool{value},modifier) == EventValue.VBool{value} : EventValue.Value}

law clear_effect_state_preserves_target_and_resets_order:
  for target: EventValue.Value
  for id: U32
  for order: EventValue.Value
  for other: EventValue.Value
  {EffectStore.clear_fields([EffectStore.FieldData{1,EventValue.VText{id}},EffectStore.FieldData{2,target},
    EffectStore.FieldData{3,order},EffectStore.FieldData{4,other}]) ==
    [EffectStore.FieldData{1,EventValue.VText{0}},EffectStore.FieldData{2,target},
      EffectStore.FieldData{3,EffectStore.number(0)}] : +List<EffectStore.Field>}

law updating_effect_field_keeps_property_order:
  for old: EventValue.Value
  for next: EventValue.Value
  for other: EventValue.Value
  {EffectStore.field_set([EffectStore.FieldData{4,old},EffectStore.FieldData{5,other}],4,next) ==
    [EffectStore.FieldData{4,next},EffectStore.FieldData{5,other}] : +List<EffectStore.Field>}

law empty_effect_state_does_not_advance_creation_order:
  for counter: U32
  for pokemon: Bool
  for active: Bool
  {EffectStore.initialize_fields(Nil{},EffectStore.AutomaticOrder{},counter,pokemon,active) ==
    Done{EffectStore.InitData{[EffectStore.FieldData{1,EventValue.VText{0}},
      EffectStore.FieldData{3,EffectStore.number(0)}],counter}} : Result<&2,&2,U32,EffectStore.Initialized>}

law undefined_effect_counter_keeps_event_modifier:
  for current: EventContext.Current
  for modifier: U32
  {Callbacks.counter_current(current,EventValue.VUndefined{},modifier) == current : EventContext.Current}

law false_effect_counter_keeps_event_modifier:
  for current: EventContext.Current
  for modifier: U32
  {Callbacks.counter_current(current,EventValue.VBool{False{}},modifier) == current : EventContext.Current}

law raw_allocation_keeps_creation_order:
  for counter: U32
  for fields: +List<EffectStore.Field>
  {EffectStore.allocation_order(EffectStore.allocate_raw(EffectStore.empty(counter),fields)) ==
    Done{counter} : Result<&2,&2,U32,U32>}

law raw_allocation_keeps_fields_without_initializing:
  for counter: U32
  for fields: +List<EffectStore.Field>
  {EffectStore.allocation_fields(EffectStore.allocate_raw(EffectStore.empty(counter),fields)) ==
    Done{fields} : Result<&2,&2,U32,+List<EffectStore.Field>>}

law indexed_listener_preserves_effect_state_and_holder:
  for listener: EventListener.ListenerRow
  for index: U32
  {Collect.listener_identity(Collect.indexed_listener(listener,index)) ==
    Collect.listener_identity(listener) : +List<EventValue.Value>}

law unaware_does_not_modify_its_own_boosts:
  for fields: +List<EffectStore.Field>
  for actor: U32
  for target: U32
  {BoostTable.unaware_fields(fields,576,576,actor,target) == fields : +List<EffectStore.Field>}

law positive_evasion_is_cleared_without_changing_other_fields:
  {BoostTable.zero_positive_evasion([EffectStore.FieldData{32,EffectStore.number(2)},
    EffectStore.FieldData{38,EffectStore.number(3)}]) ==
    [EffectStore.FieldData{32,EffectStore.number(2)},EffectStore.FieldData{38,EffectStore.number(0)}]
    : +List<EffectStore.Field>}

law negative_boost_is_clamped_before_stat_calculation:
  {BoostTable.stage(EventValue.VNumber{Number.Finite{True{},99n,1}}) == Done{0} : Result<&2,&2,U32,U32>}

law non_speed_stat_is_not_capped:
  for number: U32
  {LiveStats.speed_limit(0,number) == number : U32}

law wonder_room_swaps_base_defense_for_calculate_stat:
  {LiveStats.swapped(1,True{}) == 3 : U32}

law trick_room_speed_zero_wraps_to_thirteen_bits:
  {LiveStats.action_speed(0,True{}) == 1808 : U32}

law ordinary_action_speed_wraps_to_thirteen_bits:
  {LiveStats.action_speed(8192,False{}) == 0 : U32}

law heavy_metal_keeps_signed_magnitude_before_finalization:
  for negative: Bool
  for magnitude: Nat
  {Weight.doubled_magnitude(negative,magnitude) ==
    Number.Finite{negative,(magnitude * 2n : Nat),1} : Number.Scalar}

law light_metal_truncates_odd_weight:
  {Weight.halved(Number.Finite{False{},3n,1}) == 1 : U32}

law negative_weight_is_halved_before_unsigned_conversion:
  {Weight.halved(Number.Finite{True{},3n,1}) == 4294967295 : U32}

law effective_weight_is_at_least_one_hectogram:
  {Weight.minimum(0) == 1 : U32}

law suppressed_weather_is_empty:
  for weather: U32
  {Weather.visible(weather,True{}) == 0 : U32}

law unsuppressed_weather_keeps_its_identity:
  for weather: U32
  {Weather.visible(weather,False{}) == weather : U32}

law rejected_weight_does_not_double_its_magnitude:
  for negative: Bool
  for magnitude: Nat
  {Weight.double_checked(False{},negative,magnitude) == Fail{3} : Result<&2,&2,U32,Number.Scalar>}

law absent_ending_clears_only_cached_ending:
  {Suppress.ending_flags(4294967295,EventValue.VUndefined{}) == 4294967263 : U32}

law null_ending_keeps_unrelated_suppression_flags:
  {Suppress.ending_flags(255,EventValue.VNull{}) == 223 : U32}

law true_ending_keeps_unrelated_suppression_flags:
  {Suppress.ending_flags(223,EventValue.VBool{True{}}) == 255 : U32}

law every_object_ending_suppresses_the_active_holder:
  for family: U32
  for reference: U32
  {Suppress.ending_flags(65,EventValue.VObject{family,reference}) == 97 : U32}

law explicit_terrain_target_overrides_parent_target:
  for current: EventContext.Current
  for family: U32
  for reference: U32
  {Terrain.inherited_target(current,EventValue.VObject{family,reference}) ==
    EventValue.VObject{family,reference} : EventValue.Value}

law absent_terrain_target_uses_parent_event_target:
  for target: EventValue.Value
  {Terrain.inherited_target(EventContext.C{EventContext.EmptyEffect{},0,
    EventContext.E{1,target,EventValue.VNull{},EventValue.VNull{},EventContext.NoModifier{}},0},
    EventValue.VUndefined{}) == target : EventValue.Value}

law empty_terrain_membership_never_matches:
  for terrain: U32
  {Terrain.contains(Nil{},terrain) == False{} : Bool}

law empty_array_has_a_zero_length_property:
  {Values.dense_fields(Nil{},0) == [EffectStore.FieldData{4294967295,EffectStore.number(0)}]
    : +List<EffectStore.Field>}

law sparse_array_hole_reads_undefined:
  {Values.indexed(Values.ArrayShape{3,[EffectStore.FieldData{2,EventValue.VText{9}},
    EffectStore.FieldData{4294967295,EffectStore.number(3)}]},1) == EventValue.VUndefined{} : EventValue.Value}

law array_index_cannot_expose_length_metadata:
  for value: EventValue.Value
  {Values.indexed(Values.ArrayShape{1,[EffectStore.FieldData{0,value},
    EffectStore.FieldData{4294967295,EffectStore.number(1)}]},4294967295) ==
    EventValue.VUndefined{} : EventValue.Value}

law rejected_array_does_not_construct_its_cells:
  for values: +List<EventValue.Value>
  {Values.checked_dense(False{},values) == Fail{3} : Result<&2,&2,U32,+List<EffectStore.Field>>}

law filtering_skips_holes_but_keeps_present_undefined:
  {Values.filter_shape(Values.ArrayShape{4,[EffectStore.FieldData{1,EventValue.VUndefined{}},
    EffectStore.FieldData{3,EventValue.VText{9}},EffectStore.FieldData{4294967295,EffectStore.number(4)}]},9)
    == [EventValue.VUndefined{}] : +List<EventValue.Value>}

law undefined_is_never_a_type_name:
  for text: U32
  {Values.is_text(EventValue.VUndefined{},text) == False{} : Bool}

law filtering_preserves_a_nested_array_reference:
  for ref: U32
  for text: U32
  {Values.filter_shape(Values.ArrayShape{1,[EffectStore.FieldData{0,EventValue.VObject{8,ref}},
    EffectStore.FieldData{4294967295,EffectStore.number(1)}]},text) ==
    [EventValue.VObject{8,ref}] : +List<EventValue.Value>}

law transformed_type_callbacks_keep_the_incoming_array:
  for ability: U32
  for expected: U32
  {TypeBodies.retains(2,ability,expected) == True{} : Bool}

law absent_plate_or_memory_uses_normal:
  {TypeBodies.item_text(Registry.Missing{}) == Done{TypeTexts.normal()} : Result<&2,&2,U32,U32>}

law empty_plate_or_memory_uses_normal:
  {TypeBodies.item_text(Registry.Found{Registry.H{1,3,0,32768,32768,0}}) ==
    Done{TypeTexts.normal()} : Result<&2,&2,U32,U32>}

law pretera_queries_never_take_the_tera_shortcut:
  for tera: U32
  {PokemonTypes.tera_shortcut(tera,True{}) == False{} : Bool}

law stellar_queries_use_the_ordinary_type_event:
  {PokemonTypes.tera_shortcut(TypeTexts.stellar(),False{}) == False{} : Bool}

law species_type_values_keep_source_order:
  {TypeInit.text_values([TypeIds.types_normal(),TypeIds.types_flying()]) ==
    [EventValue.VText{TypeTexts.normal()},EventValue.VText{TypeTexts.flying()}] : +List<EventValue.Value>}

# Sandstorm/Snow modify the relay directly at priority 10. A later 1.5 item
# chain rounds separately: 101 -> 151 -> 226, rather than one 2.25 modifier.
law weather_defense_rounds_before_item_chain:
  {M.modify(M.modify(101,6144),6144) == 226 : U32}

law suppressed_levitation_never_matches:
  for ability: U32
  {Grounded.levitation_ability(ability,True{}) == False{} : Bool}

law eelevate_also_confers_ground_immunity:
  {Grounded.levitation_ability(A.abilities_eelevate(),False{}) == True{} : Bool}

law no_item_does_not_float_after_other_checks:
  {Grounded.item_result(0) == EventValue.VBool{True{}} : EventValue.Value}

law condition_presence_does_not_depend_on_value:
  for value: EventValue.Value
  for tail: +List<EffectStore.Field>
  {EffectStore.field_has(Con{EffectStore.FieldData{42,value},tail},42) == True{} : Bool}

law changing_text_storage_keeps_every_effect_object:
  for next: U32
  for counter: U32
  for tree: EffectStore.ObjectTree
  for previous: TextStore.Heap
  for replacement: TextStore.Heap
  for reference: U32
  {EffectStore.object_fields(EffectStore.put_text_heap(
    EffectStore.HeapData{next,counter,tree,previous},replacement),reference) ==
    EffectStore.tree_get(32n,tree,2147483648,reference) : Maybe<&2,+List<EffectStore.Field>>}

law object_writes_keep_immutable_text_snapshots:
  for next: U32
  for counter: U32
  for tree: EffectStore.ObjectTree
  for texts: TextStore.Heap
  for reference: U32
  for fields: +List<EffectStore.Field>
  {EffectStore.text_heap(EffectStore.put_fields(
    EffectStore.HeapData{next,counter,tree,texts},reference,fields)) == texts : TextStore.Heap}

law hash_collision_does_not_make_strings_equal:
  {TextStore.find([TextStore.TextEntry{1,2112,[65,97]}],2112,[66,66]) == None{} : Maybe<&2,U32>}

law one_join_fragment_never_adds_separators:
  for units: +List<U32>
  for separator: +List<U32>
  {TextJoin.assemble([units],separator) == units : +List<U32>}

law empty_join_fragments_still_have_separators:
  {TextJoin.assemble([Nil{},Nil{},[65],Nil{}],[47]) == [47,47,65,47] : +List<U32>}

law enforced_type_change_skips_all_rejection_predicates:
  for stellar: Bool
  for number: U32
  for tera: U32
  {TypeChange.rejection(True{},stellar,number,tera) == False{} : Bool}

law arceus_and_silvally_reject_ordinary_type_changes:
  {TypeChange.species_immune(493) && TypeChange.species_immune(773) == True{} : Bool}

law assigning_a_hole_preserves_numeric_property_order:
  {Values.assigned_shape(Values.ArrayShape{3,[EffectStore.FieldData{2,EventValue.VText{9}},
    EffectStore.FieldData{4294967295,EffectStore.number(3)}]},0,EventValue.VUndefined{}) ==
    Done{Values.ArrayShape{3,[EffectStore.FieldData{0,EventValue.VUndefined{}},
      EffectStore.FieldData{2,EventValue.VText{9}},EffectStore.FieldData{4294967295,EffectStore.number(3)}]}}
    : Result<&2,&2,U32,Values.Shape>}

# Keep the boundary in U32 arithmetic: expanding U32.to_nat(MAX) in a
# literal expected heap overwhelms the proof normalizer. Original JS/native
# fixtures additionally compare the full sparse boundary fields and length.
law last_index_assignment_keeps_maximum_array_length:
  {Values.assigned_extent(0,4294967294) == 4294967295 : U32}

law named_maximum_property_is_outside_indexed_assignment:
  for data: Values.Shape
  for value: EventValue.Value
  {Values.assigned_shape(data,4294967295,value) == Fail{3} : Result<&2,&2,U32,Values.Shape>}

law stat_copy_keeps_destination_property_order:
  {StatTable.copied([EffectStore.FieldData{36,EffectStore.number(9)},
    EffectStore.FieldData{32,EffectStore.number(8)}],
    [EffectStore.FieldData{32,EffectStore.number(10)},EffectStore.FieldData{36,EffectStore.number(20)}]) ==
    [EffectStore.FieldData{36,EffectStore.number(20)},EffectStore.FieldData{32,EffectStore.number(10)}]
    : +List<EffectStore.Field>}

law stat_copy_does_not_insert_source_only_properties:
  for source: +List<EffectStore.Field>
  {StatTable.copied(Nil{},source) == Nil{} : +List<EffectStore.Field>}

law transform_species_change_retains_base_stat_identity:
  for previous: U32
  for fresh: U32
  {Spread.base_reference(previous,fresh,True{}) == previous : U32}

law ordinary_species_change_replaces_base_stat_identity:
  for previous: U32
  for fresh: U32
  {Spread.base_reference(previous,fresh,False{}) == fresh : U32}

law zero_maximum_initializes_all_health_fields:
  for hp: U32
  for base_maximum: U32
  for spread_hp: U32
  {Spread.health_after(Spread.HealthData{hp,0,base_maximum},spread_hp) ==
    Spread.HealthData{spread_hp,spread_hp,spread_hp} : Spread.Health}

law constructor_move_slot_has_no_virtual_property:
  for name: U32
  for id: U32
  for pp: U32
  for target: U32
  {EffectStore.field_get(MoveSlots.initial_fields(name,id,pp,target),46) ==
    EventValue.VUndefined{} : EventValue.Value}

law transformed_move_slot_has_no_disabled_source_property:
  for name: U32
  for id: U32
  for pp: U32
  for target: U32
  {EffectStore.field_get(MoveSlots.transformed_fields(name,id,pp,target),44) ==
    EventValue.VUndefined{} : EventValue.Value}

law transformed_move_slot_is_virtual:
  for name: U32
  for id: U32
  for pp: U32
  for target: U32
  {EffectStore.field_get(MoveSlots.transformed_fields(name,id,pp,target),46) ==
    EventValue.VBool{True{}} : EventValue.Value}

law empty_transform_boost_copy_preserves_destination:
  for destination: +List<EffectStore.Field>
  {BoostTable.copied(Nil{},destination) == destination : +List<EffectStore.Field>}

law transform_boost_copy_keeps_old_order_and_appends_new_keys:
  {BoostTable.copied([EffectStore.FieldData{32,EffectStore.number(2)},
    EffectStore.FieldData{36,EventValue.VNumber{Number.Finite{True{},1n,1}}}],
    [EffectStore.FieldData{36,EffectStore.number(4)},EffectStore.FieldData{33,EffectStore.number(3)}]) ==
    [EffectStore.FieldData{36,EventValue.VNumber{Number.Finite{True{},1n,1}}},
     EffectStore.FieldData{33,EffectStore.number(3)},EffectStore.FieldData{32,EffectStore.number(2)}]
    : +List<EffectStore.Field>}

law clearing_boosts_keeps_own_key_order:
  {BoostTable.cleared([EffectStore.FieldData{38,EffectStore.number(2)},
    EffectStore.FieldData{32,EventValue.VNumber{Number.Finite{True{},3n,1}}}]) ==
    [EffectStore.FieldData{38,EffectStore.number(0)},EffectStore.FieldData{32,EffectStore.number(0)}]
    : +List<EffectStore.Field>}
