import Base
import ./Controller.bend as C

law terminal_controls_are_closed:
  for actor: U32
  for viewer: U32
  for kind: Nat
  for call: U32
  for raising: U32
  for high: U32
  for lower: U32
  for upper: U32
  for target: U32
  {C.allowed(True{}, actor, viewer, kind, call, raising, high, lower, upper, target) == 0 : U32}

law free_fold_is_rejected:
  {C.allowed(False{}, 0, 0, 0n, 0, 1, 100, 200, 20000, 200) == 0 : U32}

law below_minimum_raise_is_rejected:
  {C.allowed(False{}, 0, 0, 2n, 100, 1, 200, 400, 20000, 300) == 0 : U32}

law short_allin_is_accepted:
  {C.allowed(False{}, 0, 0, 2n, 100, 1, 200, 400, 300, 300) == 1 : U32}

law wrong_actor_is_rejected:
  {C.allowed(False{}, 1, 0, 1n, 100, 0, 200, 400, 20000, 0) == 0 : U32}

law finished_phase:
  for actor: U32
  for viewer: U32
  {C.phase(True{}, actor, viewer) == 2 : U32}

law session_records_terminal_return:
  {C.record(150, 0, C.S{3, 1, 200, 100}) == C.S{4, 2, 350, 100} : C.Session}

law raise_target_clips_short_stack:
  {C.raise_target(100, 400, 300) == 300 : U32}

law unopened_three_blinds:
  {C.preset(1n, 0, 100, 150, 50, 100, 200, 20000) == 300 : U32}

law reraise_three_times_current_bet:
  {C.preset(1n, 0, 350, 500, 250, 100, 600, 20000) == 1050 : U32}

law half_pot_bet:
  {C.preset(1n, 1, 0, 1200, 0, 100, 100, 20000) == 600 : U32}

law pot_raise_calls_before_sizing:
  {C.preset(3n, 2, 400, 1600, 300, 100, 700, 20000) == 2300 : U32}

law three_quarter_pot_rounds_down:
  {C.preset(2n, 3, 0, 101, 0, 100, 1, 20000) == 75 : U32}

law oversized_preset_is_allin:
  {C.preset(2n, 0, 1000, 2000, 900, 100, 1900, 2200) == 2200 : U32}
