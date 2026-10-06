import Base
import ./ValueArray.bend as Values
import ./EventListener.bend as EventListener
import ./LAWS.bend as L
import ./GenTeam.bend as Team
import ./Init.bend as Init
import ./Queue.bend as Queue
import ./Damage.bend as Damage
import ./Events.bend as Events
import ./Pokemon.bend as Pokemon
import ./Boosts.bend as Boosts
import ./Registry.bend as Registry
import ./HP.bend as HP
import ./Suppress.bend as Suppress
import ./MoveView.bend as MoveView
import ./Request.bend as Request
import ./EventGate.bend as EventGate
import ./EventValue.bend as EventValue
import ./EventContext.bend as EventContext
import ./Callbacks.bend as Callbacks
import ./SingleEvent.bend as SingleEvent
import ./RunEvent.bend as RunEvent
import ./VectorEvent.bend as VectorEvent
import ./ListenerPriority.bend as ListenerPriority
import ./EffectStore.bend as EffectStore
import ./Collect.bend as Collect

def L.stat_copy_keeps_destination_property_order():
  {==}
def L.stat_copy_does_not_insert_source_only_properties(source):
  {==}
def L.transform_species_change_retains_base_stat_identity(previous,fresh):
  {==}
def L.ordinary_species_change_replaces_base_stat_identity(previous,fresh):
  {==}
def L.zero_maximum_initializes_all_health_fields(hp,base_maximum,spread_hp):
  {==}
def L.constructor_move_slot_has_no_virtual_property(name,id,pp,target):
  {==}
def L.transformed_move_slot_has_no_disabled_source_property(name,id,pp,target):
  {==}
def L.transformed_move_slot_is_virtual(name,id,pp,target):
  {==}
def L.empty_transform_boost_copy_preserves_destination(destination):
  {==}
def L.transform_boost_copy_keeps_old_order_and_appends_new_keys():
  {==}
def L.clearing_boosts_keeps_own_key_order():
  {==}

def L.modifier_rounds_half_down():
  {==}
def L.modifier_keeps_above_half():
  {==}
def L.modifier_identity():
  {==}
def L.scale_full_word():
  {==}
def L.scale_preserves_exclusive_bound():
  {==}
def L.seed_zero_first_step():
  {==}
def L.swap_pop_uses_old_last():
  {==}
def L.filtering_preserves_sampling_order():
  {==}
def L.damage_cannot_underflow():
  {==}
def L.healing_cannot_revive():
  {==}
def L.healing_stops_at_max_hp():
  {==}
def L.negative_boost_floors():
  {==}
def L.tera_precedes_move():
  {==}
def L.unordered_effect_follows_ordered():
  {==}
def L.damage_zero_has_minimum_one():
  {==}
def L.damage_truncates_after_minimum():
  {==}
def L.critical_rounds_down():
  {==}
def L.confusion_truncates_before_roll():
  {==}
def L.slot_condition_precedes_side_condition():
  {==}
def L.switch_in_keeps_negative_half_speed():
  {==}
def L.boost_caps_at_plus_six():
  {==}
def L.boost_caps_at_minus_six():
  {==}
def L.capped_boost_reports_actual_delta():
  {==}
def L.chacha_quarter_vector():
  {==}
def L.sodium_output_byte_order():
  {==}
def L.negative_truncation_wraps():
  {==}
def L.positive_fraction_is_not_zero_damage():
  {==}
def L.nan_truncates_to_zero():
  {==}
def L.transformed_notransform_precedes_unsuppressible():
  {==}
def L.shield_does_not_override_gastro_acid():
  {==}
def L.primal_orb_ignores_inactive_item_suppression():
  {==}
def L.fling_checks_klutz_before_ignore_klutz():
  {==}
def L.hidden_disabled_stays_selectable_in_restricted_request():
  {==}
def L.no_pp_precedes_hidden_disable():
  {==}
def L.hidden_trapping_is_not_a_public_trapped_flag():
  {==}
def L.maybe_trapping_requires_a_switch_candidate():
  {==}
def L.single_null_stays_null():
  {==}
def L.run_null_defaults_true():
  {==}
def L.undefined_does_not_stop_falsy_relay():
  {==}
def L.empty_text_stops_run_event():
  {==}
def L.objects_are_truthy_even_with_zero_id():
  {==}
def L.negative_fraction_skips_final_modifier():
  {==}
def L.modifier_chain_uses_signed_shift():
  {==}
def L.run_event_defaults_target_and_modifier():
  {==}
def L.single_event_has_no_modifier():
  {==}
def L.event_modifier_keeps_source_double_rounding():
  {==}
def L.huge_power_chains_attack():
  {==}
def L.hustle_modifies_attack_directly():
  {==}
def L.signed_literal_zero_is_falsy():
  {==}
def L.negative_literal_at_encoding_zero_is_truthy():
  {==}
def L.hit_event_sort_precedes_fast_exit_sort():
  {==}
def L.ordinary_fast_exit_uses_redirect_sort():
  {==}
def L.vector_damaging_hit_accepts_numeric_zero():
  {==}
def L.vector_damaging_hit_skips_boolean_false():
  {==}
def L.vector_try_hit_skips_numeric_zero():
  {==}
def L.undefined_return_preserves_every_relay(old,fast):
  {==}
def L.undefined_single_return_preserves_every_relay(old):
  {==}
def L.every_object_reference_is_truthy(family,id):
  {==}
def L.handler_scope_restores_references_and_keeps_event(parent_effect,parent_state,child_effect,child_state,child_event,depth):
  {==}
def L.run_scope_restores_event_and_keeps_effect_references(effect,state,parent_event,parent_depth,child_event,child_depth):
  {==}
def L.handler_entry_preserves_event_and_depth(parent_effect,parent_state,child_effect,child_state,event,depth):
  {==}
def L.single_scope_clears_only_modifier(id,target,source,source_effect,modifier):
  {==}
def L.every_boolean_relay_skips_final_modifier(value,modifier):
  {==}
def L.clear_effect_state_preserves_target_and_resets_order(target,id,order,other):
  {==}
def L.updating_effect_field_keeps_property_order(old,next,other):
  {==}
def L.empty_effect_state_does_not_advance_creation_order(counter,pokemon,active):
  {==}
def L.undefined_effect_counter_keeps_event_modifier(current,modifier):
  {==}
def L.false_effect_counter_keeps_event_modifier(current,modifier):
  {==}
def L.raw_allocation_keeps_creation_order(counter,fields):
  {==}
def L.raw_allocation_keeps_fields_without_initializing(counter,fields):
  {==}

def L.indexed_listener_preserves_effect_state_and_holder(listener,index):
  EventListener.ListenerData{_,_,_,_,_,_,_,_,_} = listener
  {==}
def L.unaware_does_not_modify_its_own_boosts(fields,actor,target):
  {==}
def L.positive_evasion_is_cleared_without_changing_other_fields():
  {==}
def L.negative_boost_is_clamped_before_stat_calculation():
  {==}
def L.non_speed_stat_is_not_capped(number):
  {==}
def L.wonder_room_swaps_base_defense_for_calculate_stat():
  {==}
def L.trick_room_speed_zero_wraps_to_thirteen_bits():
  {==}
def L.ordinary_action_speed_wraps_to_thirteen_bits():
  {==}
def L.heavy_metal_keeps_signed_magnitude_before_finalization(negative,magnitude):
  {==}
def L.light_metal_truncates_odd_weight():
  {==}
def L.negative_weight_is_halved_before_unsigned_conversion():
  {==}
def L.effective_weight_is_at_least_one_hectogram():
  {==}
def L.suppressed_weather_is_empty(weather):
  {==}
def L.unsuppressed_weather_keeps_its_identity(weather):
  {==}
def L.rejected_weight_does_not_double_its_magnitude(negative,magnitude):
  {==}
def L.absent_ending_clears_only_cached_ending():
  {==}
def L.null_ending_keeps_unrelated_suppression_flags():
  {==}
def L.true_ending_keeps_unrelated_suppression_flags():
  {==}
def L.every_object_ending_suppresses_the_active_holder(family,reference):
  {==}
def L.explicit_terrain_target_overrides_parent_target(current,family,reference):
  {==}
def L.absent_terrain_target_uses_parent_event_target(target):
  {==}
def L.empty_terrain_membership_never_matches(terrain):
  {==}
def L.empty_array_has_a_zero_length_property():
  {==}
def L.sparse_array_hole_reads_undefined():
  {==}
def L.array_index_cannot_expose_length_metadata(value):
  {==}
def L.rejected_array_does_not_construct_its_cells(values):
  {==}
def L.filtering_skips_holes_but_keeps_present_undefined():
  {==}
def L.undefined_is_never_a_type_name(text):
  {==}
def L.filtering_preserves_a_nested_array_reference(ref,text):
  {==}
def L.transformed_type_callbacks_keep_the_incoming_array(ability,expected):
  {==}
def L.absent_plate_or_memory_uses_normal():
  {==}
def L.empty_plate_or_memory_uses_normal():
  {==}
def L.pretera_queries_never_take_the_tera_shortcut(tera):
  {==}
def L.stellar_queries_use_the_ordinary_type_event():
  {==}
def L.species_type_values_keep_source_order():
  {==}
def L.weather_defense_rounds_before_item_chain():
  {==}
def L.suppressed_levitation_never_matches(ability):
  {==}
def L.eelevate_also_confers_ground_immunity():
  {==}
def L.no_item_does_not_float_after_other_checks():
  {==}
def L.condition_presence_does_not_depend_on_value(value,tail):
  {==}

def L.changing_text_storage_keeps_every_effect_object(next,counter,tree,previous,replacement,reference):
  {==}
def L.object_writes_keep_immutable_text_snapshots(next,counter,tree,texts,reference,fields):
  {==}
def L.hash_collision_does_not_make_strings_equal():
  {==}

def L.one_join_fragment_never_adds_separators(units,separator):
  {==}
def L.empty_join_fragments_still_have_separators():
  {==}
def L.enforced_type_change_skips_all_rejection_predicates(stellar,number,tera):
  {==}
def L.arceus_and_silvally_reject_ordinary_type_changes():
  {==}

def L.assigning_a_hole_preserves_numeric_property_order():
  {==}
def L.last_index_assignment_keeps_maximum_array_length():
  {==}
def L.named_maximum_property_is_outside_indexed_assignment(data,value):
  Values.ArrayShape{_,_} = data
  {==}
