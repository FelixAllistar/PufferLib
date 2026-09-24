import Base
import ../../miniwob/Widgets.bend as W
import ../../miniwob/Text.bend as T
import ./Model.bend as M
import ./Generate.bend as G

law terminal_absorbs_click:
  {M.step(M.make(M.Ascending{}, 0, Nil{}, 0, 0, 0, 0, 0, 0, 0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, M.Finished{1.0, True{}}), M.Click{0, 0, 0}, 10, 10000) == M.make(M.Ascending{}, 0, Nil{}, 0, 0, 0, 0, 0, 0, 0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, M.Finished{1.0, True{}}) : M.State}

law deadline_preempts_action:
  {M.step(M.make(M.Ascending{}, 0, Nil{}, 0, 0, 0, 0, 0, 0, 0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, M.Running{}), M.Click{0, 0, 0}, 10000, 10000) == M.make(M.Ascending{}, 0, Nil{}, 0, 0, 0, 0, 0, 0, 0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, M.TimedOut{}) : M.State}

law ascending_reveals_after_one:
  {M.step(M.make(M.Ascending{}, 0, Nil{}, 0, 0, 0, 0, 0, 0, 0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, M.Running{}), M.Click{0, 0, 0}, 1, 10000) == M.make(M.Ascending{}, 1, Nil{}, 1, 0, 0, 0, 0, 0, 0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, M.Running{}) : M.State}

law ascending_wrong_click_keeps_partial_reward:
  {M.step(M.make(M.Ascending{}, 1, Nil{}, 1, 0, 0, 0, 0, 0, 0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, M.Running{}), M.Click{3, 0, 0}, 100, 10000) == M.finish(M.make(M.Ascending{}, 1, Nil{}, 1, 0, 0, 0, 0, 0, 0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, M.Running{}), (U32.to_f32(1) / 5.0 : F32), True{}) : M.State}

law generate_less_than_boundary:
  {M.allowed_candidate(0, 5, 4) == True{} : Bool}

law generate_rejects_equal_boundary:
  {M.allowed_candidate(0, 5, 5) == False{} : Bool}

law generate_even_requirement:
  {M.allowed_candidate(3, 0, 8) == True{} : Bool}

law generate_without_candidate_even_follows_original:
  {M.generate_submit(0n, 3, 0, 0, M.make(M.GenerateNumber{}, 0, Nil{}, 0, 0, 0, 0, 3, 0, 0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, M.Running{})) == M.finish(M.make(M.GenerateNumber{}, 0, Nil{}, 0, 0, 0, 0, 3, 0, 0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, M.Running{}), 1.0, True{}) : M.State}

law generate_without_candidate_odd_fails:
  {M.generate_submit(0n, 2, 0, 0, M.make(M.GenerateNumber{}, 0, Nil{}, 0, 0, 0, 0, 2, 0, 0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, M.Running{})) == M.finish(M.make(M.GenerateNumber{}, 0, Nil{}, 0, 0, 0, 0, 2, 0, 0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, M.Running{}), (0.0 - 1.0 : F32), False{}) : M.State}

law guess_with_feedback_remembers_number:
  {M.with_feedback(M.make(M.GuessNumber{}, 0, Nil{}, 0, 0, 5, 0, 0, 0, 0, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{}), 1, 4) == M.make(M.GuessNumber{}, 0, Nil{}, 4, 0, 5, 0, 0, 0, 1, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{}) : M.State}

law guess_value_remembers_number:
  {M.guess_value(4, False{}, M.make(M.GuessNumber{}, 0, Nil{}, 0, 0, 5, 0, 0, 0, 0, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{})) == M.make(M.GuessNumber{}, 0, Nil{}, 4, 0, 5, 0, 0, 0, 1, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{}) : M.State}

law guess_result_remembers_number:
  {M.guess_result(M.ParsedGuess{4, False{}}, M.make(M.GuessNumber{}, 0, Nil{}, 0, 0, 5, 0, 0, 0, 0, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{})) == M.make(M.GuessNumber{}, 0, Nil{}, 4, 0, 5, 0, 0, 0, 1, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{}) : M.State}

law guess_reads_field:
  {M.guess(M.make(M.GuessNumber{}, 0, Nil{}, 0, 0, 5, 0, 0, 0, 0, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{})) == M.make(M.GuessNumber{}, 0, Nil{}, 4, 0, 5, 0, 0, 0, 1, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{}) : M.State}

law guess_step_matches_parser:
  {M.step(M.make(M.GuessNumber{}, 0, Nil{}, 0, 0, 5, 0, 0, 0, 0, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{}), M.Submit{}, 10, 15000) == M.guess(M.make(M.GuessNumber{}, 0, Nil{}, 0, 0, 5, 0, 0, 0, 0, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{})) : M.State}

law elapsed_below_deadline_is_not_expired:
  {U32.is_ge(10, 15000) == False{} : Bool}

law step_matches_deadline_dispatch:
  {M.step(M.make(M.GuessNumber{}, 0, Nil{}, 0, 0, 5, 0, 0, 0, 0, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{}), M.Submit{}, 10, 15000) == M.deadline_step(U32.is_ge(10, 15000), M.make(M.GuessNumber{}, 0, Nil{}, 0, 0, 5, 0, 0, 0, 0, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{}), M.Submit{}) : M.State}

law guess_submit_remembers_number:
  {M.submit(M.make(M.GuessNumber{}, 0, Nil{}, 0, 0, 5, 0, 0, 0, 0, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{})) == M.make(M.GuessNumber{}, 0, Nil{}, 4, 0, 5, 0, 0, 0, 1, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{}) : M.State}

law guess_active_remembers_number:
  {M.active(M.make(M.GuessNumber{}, 0, Nil{}, 0, 0, 5, 0, 0, 0, 0, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{}), M.Submit{}) == M.make(M.GuessNumber{}, 0, Nil{}, 4, 0, 5, 0, 0, 0, 1, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{}) : M.State}

law guess_deadline_remembers_number:
  {M.deadline_step(False{}, M.make(M.GuessNumber{}, 0, Nil{}, 0, 0, 5, 0, 0, 0, 0, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{}), M.Submit{}) == M.make(M.GuessNumber{}, 0, Nil{}, 4, 0, 5, 0, 0, 0, 1, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{}) : M.State}

law guess_low_reports_higher:
  {M.step(M.make(M.GuessNumber{}, 0, Nil{}, 0, 0, 5, 0, 0, 0, 0, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{}), M.Submit{}, 10, 15000) == M.make(M.GuessNumber{}, 0, Nil{}, 4, 0, 5, 0, 0, 0, 1, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{}) : M.State}

law guess_parser_reads_digit:
  {M.parsed([U32.to_nat(52)]) == M.ParsedGuess{4, False{}} : M.GuessParse}

law guess_feedback_remembers_the_submitted_number:
  {M.step(M.make(M.GuessNumber{}, 0, Nil{}, 0, 0, 5, 0, 0, 0, 0, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{}), M.Submit{}, 10, 15000) == M.make(M.GuessNumber{}, 0, Nil{}, 4, 0, 5, 0, 0, 0, 1, 0, 0, T.F{[52], 1n, 1n, Nil{}}, M.Running{}) : M.State}

law greatest_wrong_selection_rewards_point_one:
  {M.greatest_submit(True{}, False{}, M.make(M.Greatest{}, 0, Nil{}, 0, 0, 0, 0, 0, 0, 0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, M.Running{})) == M.finish(M.make(M.Greatest{}, 0, Nil{}, 0, 0, 0, 0, 0, 0, 0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, M.Running{}), 0.1, True{}) : M.State}

law hot_boundary_is_warm:
  {M.heat(10, 10, 19, 10) == 2 : U32}

law hot_radius_is_hot:
  {M.heat(10, 10, 14, 10) == 1 : U32}

law cold_boundary_is_ice_cold:
  {M.heat(10, 10, 50, 10) == 4 : U32}

law checkbox_toggle_changes_only_selection:
  {W.toggle_at([W.N{W.Checkbox{}, False{}, True{}}, W.N{W.Checkbox{}, False{}, False{}}], 0n) == [W.N{W.Checkbox{}, True{}, True{}}, W.N{W.Checkbox{}, False{}, False{}}] : +List<W.Node>}

law odd_even_wrong_selection_is_a_third:
  {M.odd_rows([W.N{W.Button{}, True{}, False{}}, W.N{W.Button{}, False{}, True{}}]) == (((0.0 - 1.0 : F32) / 3.0 + 0.0) : F32) : F32}

law odd_even_empty_selection_matches_original_truthy_empty_object:
  {M.odd_row_score(False{}, False{}, True{}, False{}) == ((0.0 - 1.0 : F32) / 3.0 : F32) : F32}

law odd_even_reselection_preserves_other_rows:
  {M.clear_pair([W.N{W.Button{}, True{}, False{}}, W.N{W.Button{}, False{}, True{}}, W.N{W.Button{}, False{}, False{}}, W.N{W.Button{}, False{}, True{}}], 0n, 2n, 1n) == [W.N{W.Button{}, True{}, False{}}, W.N{W.Button{}, False{}, True{}}, W.N{W.Button{}, True{}, False{}}, W.N{W.Button{}, False{}, True{}}] : +List<W.Node>}

law blank_guess_does_not_finish:
  {M.step(M.make(M.GuessNumber{}, 0, Nil{}, 0, 0, 5, 0, 0, 0, 0, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, M.Running{}), M.Submit{}, 10, 15000) == M.make(M.GuessNumber{}, 0, Nil{}, 0, 0, 5, 0, 0, 0, 4, 0, 0, T.F{Nil{}, 0n, 0n, Nil{}}, M.Running{}) : M.State}

law exact_signed_answer_format:
  {G.signed_text((0 - 1 : U32)) == "-1" : String}
