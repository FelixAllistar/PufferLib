import Base
import ./OtherModel.bend as O
import ./Model.bend as M
import ../../miniwob/Text.bend as T

law finished_absorbs:
  for kind: Nat
  for selected: U32
  for requested: U32
  for secondary: U32
  for fields: +List<T.Field>
  for success: Bool
  for action: O.Action
  for elapsed: U32
  for deadline: U32
  {O.step(O.S{kind, selected, requested, secondary, fields,
    M.Finished{success}}, action, elapsed, deadline) ==
    O.S{kind, selected, requested, secondary, fields,
    M.Finished{success}} : O.State}

# Preserve the original form-sequence-2 checker: any selected radio suffices.
law any_radio_suffices:
  {O.submit(O.S{1n, 2, 1, 3,
    [T.F{Nil{}, 0n, 0n, Nil{}}, T.F{Nil{}, 0n, 0n, Nil{}},
     T.F{[49], 0n, 0n, [49]}], M.Running{}}, 0) ==
    O.S{1n, 2, 1, 3,
    [T.F{Nil{}, 0n, 0n, Nil{}}, T.F{Nil{}, 0n, 0n, Nil{}},
     T.F{[49], 0n, 0n, [49]}], M.Finished{True{}}} : O.State}

law dropdown_requires_correct_button:
  {O.submit(O.S{2n, 3, 3, 2, Nil{}, M.Running{}}, 1) ==
    O.S{2n, 3, 3, 2, Nil{}, M.Finished{False{}}} : O.State}

law dropdown_and_button_succeed:
  {O.submit(O.S{2n, 3, 3, 2, Nil{}, M.Running{}}, 2) ==
    O.S{2n, 3, 3, 2, Nil{}, M.Finished{True{}}} : O.State}

law movie_words_ignore_case_and_outer_spaces:
  {O.movie_field_ok(T.F{[32, 65, 32], 0n, 0n, [97]}, False{}) ==
    True{} : Bool}

law movie_year_keeps_leading_space:
  {O.movie_field_ok(T.F{[32, 50, 48, 48, 48], 0n, 0n, [50, 48, 48, 48]},
    True{}) == False{} : Bool}
