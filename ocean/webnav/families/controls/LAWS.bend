import Base
import ./Model.bend as M
import ../../miniwob/Text.bend as T

def slider_at_zero() -> M.State:
  M.S{M.Slider{}, 0, 5, False{}, 0, 0, 0, 3, 0, 0,
    0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, 0, 0, 0, M.Running{}}

def slider_at_middle() -> M.State:
  M.S{M.Slider{}, 0, 5, False{}, 2, 0, 0, 3, 0, 0,
    0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, 0, 0, 0, M.Running{}}

def wrong_combo() -> M.State:
  M.S{M.SliderThree{}, 0, 20, False{}, 1, 2, 3, 1, 2, 4,
    0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, 0, 0, 0, M.Running{}}

def small_select() -> M.State:
  M.S{M.ChooseList{}, 0, 0, False{}, 0, 0, 0, 0, 0, 0,
    0, 2, 3, T.F{Nil{}, 0n, 0n, Nil{}}, 0, 0, 0, M.Running{}}

def zero_spinner() -> M.State:
  M.S{M.Spinner{}, 0, 0, False{}, 0, 0, 0, 4294967295, 0, 0,
    0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, 0, 0, 0, M.Running{}}

law horizontal_left_is_min:
  {M.slider_value(0, 20, False{}, 0) == 0 : U32}

law horizontal_right_is_max:
  {M.slider_value(0, 20, False{}, 1000) == 20 : U32}

law vertical_top_is_max:
  {M.slider_value(0, 20, True{}, 0) == 20 : U32}

law vertical_bottom_is_min:
  {M.slider_value(0, 20, True{}, 1000) == 0 : U32}

law fraction_clamps_above_track:
  {M.slider_value(0, 20, False{}, 2000) == 20 : U32}

law decrement_at_encoded_zero_is_saturated:
  {M.stepped(slider_at_zero(), 1, False{}) == slider_at_zero() : M.State}

law increment_then_decrement_is_reversible:
  {M.stepped(M.stepped(slider_at_middle(), 1, True{}), 1, False{}) == slider_at_middle() : M.State}

law wrong_third_slider_fails_combination:
  {M.score(wrong_combo()) == (0.0 - 1.0 : F32) : F32}

law spinner_down_from_zero_is_minus_one:
  {M.spin_step(zero_spinner(), False{}) ==
    M.S{M.Spinner{}, 0, 0, False{}, 4294967295, 0, 0, 4294967295, 0, 0,
      0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, 0, 0, 0, M.Running{}} : M.State}

law spinner_keydown_is_prevented:
  {M.action(zero_spinner(), M.Key{4, 38}) == zero_spinner() : M.State}

law select_out_of_bounds_is_ignored:
  {M.action(small_select(), M.Select{9}) == small_select() : M.State}

law wait_changes_nothing:
  for +s: M.State
  {M.action(s, M.Wait{}) == s : M.State}

law timeout_beats_submit_at_deadline:
  {M.step(slider_at_middle(), M.Submit{}, 10000, 10000) == M.timed_out(slider_at_middle()) : M.State}

law finished_is_terminal:
  for task: M.Task
  for low: U32
  for high: U32
  for vertical: Bool
  for one: U32
  for two: U32
  for three: U32
  for g1: U32
  for g2: U32
  for g3: U32
  for selected: U32
  for goal: U32
  for count: U32
  for color: T.Field
  for desired: U32
  for focus: U32
  for drag: U32
  for elapsed: U32
  for limit: U32
  {M.step(M.S{task, low, high, vertical, one, two, three, g1, g2, g3,
    selected, goal, count, color, desired, focus, drag, M.Finished{}},
    M.Submit{}, elapsed, limit) ==
   M.S{task, low, high, vertical, one, two, three, g1, g2, g3,
    selected, goal, count, color, desired, focus, drag, M.Finished{}} : M.State}

# Stock proofs normalize the integer metric; native tests check numeric F32 rewards.
law exact_color_has_zero_distance:
  {M.color_distance(1193046, 1193046) == 0 : U32}

law opposite_colors_have_max_distance:
  {M.color_distance(0, 16777215) == 765 : U32}

law exact_color_has_smaller_distance:
  {U32.is_lt(M.color_distance(0, 0), M.color_distance(16777215, 0)) == True{} : Bool}
