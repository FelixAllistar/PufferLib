import Base
import ./Model.bend as M
import ./ListModel.bend as L
import ./TextModel.bend as Tm
import ./AgreementModel.bend as A
import ../../miniwob/Text.bend as T

law terminal_absorbs:
  for position: U32
  for height: U32
  for client: U32
  for offset: U32
  for bottom: Bool
  for success: Bool
  for action: M.Action
  for elapsed: U32
  for deadline: U32
  {M.step(M.S{position, height, client, offset, bottom, M.Finished{success}}, action, elapsed, deadline) == M.S{position, height, client, offset, bottom, M.Finished{success}} : M.State}

law top_accepts_nine_pixels:
  {M.step(M.S{9, 300, 100, 100, False{}, M.Running{}}, M.Submit{}, 100, 10000) == M.S{9, 300, 100, 100, False{}, M.Finished{True{}}} : M.State}

law top_rejects_ten_pixels:
  {M.step(M.S{10, 300, 100, 100, False{}, M.Running{}}, M.Submit{}, 100, 10000) == M.S{10, 300, 100, 100, False{}, M.Finished{False{}}} : M.State}

law bottom_clamps_scroll:
  {M.step(M.S{100, 300, 100, 100, True{}, M.Running{}}, M.Scroll{999}, 100, 10000) == M.S{200, 300, 100, 100, True{}, M.Running{}} : M.State}

law deadline_preempts_submit:
  {M.step(M.S{200, 300, 100, 100, True{}, M.Running{}}, M.Submit{}, 10000, 10000) == M.S{200, 300, 100, 100, True{}, M.TimedOut{}} : M.State}

law list_requires_exact_count:
  {L.correct(2, 1, 3, 2) == False{} : Bool}

law list_accepts_two_named_values:
  {L.correct(2, 3, 3, 2) == True{} : Bool}

law selecting_reveals_native_option_top:
  {L.step(L.S{54, 187, 88, 11, 17, 0, 8, 1, 0, 0}, L.Select{3, True{}}, 100, 15000) ==
    L.S{51, 187, 88, 11, 17, 8, 8, 1, 4, 0} : L.State}

law deselecting_preserves_scroll:
  {L.step(L.S{51, 187, 88, 11, 17, 8, 8, 1, 4, 0}, L.Select{3, False{}}, 100, 15000) ==
    L.S{51, 187, 88, 11, 17, 0, 8, 1, 4, 0} : L.State}

law selecting_reveals_persistent_anchor:
  {L.step(L.S{99, 187, 88, 11, 17, 16, 144, 2, 5, 0},
    L.Select{7, True{}}, 100, 15000) ==
    L.S{68, 187, 88, 11, 17, 144, 144, 2, 5, 0} : L.State}

law deselecting_retains_anchor_with_remaining_option:
  {L.step(L.S{0, 187, 88, 11, 17, 258, 258, 2, 2, 0},
    L.Select{1, False{}}, 100, 15000) ==
    L.S{0, 187, 88, 11, 17, 256, 258, 2, 2, 0} : L.State}

law deselecting_last_reveals_old_anchor:
  {L.step(L.S{99, 187, 88, 11, 17, 1024, 1025, 2, 1, 0},
    L.Select{10, False{}}, 100, 15000) ==
    L.S{0, 187, 88, 11, 17, 0, 1025, 2, 1, 0} : L.State}

law lower_index_selection_keeps_old_anchor:
  {L.step(L.S{0, 187, 88, 11, 17, 128, 129, 2, 8, 0},
    L.Select{0, True{}}, 100, 15000) ==
    L.S{48, 187, 88, 11, 17, 129, 129, 2, 8, 0} : L.State}

law unchanged_selected_property_does_not_scroll:
  {L.step(L.S{54, 187, 88, 11, 17, 8, 8, 1, 4, 0},
    L.Select{3, True{}}, 100, 15000) ==
    L.S{54, 187, 88, 11, 17, 8, 8, 1, 4, 0} : L.State}

law text_strips_answer_punctuation:
  {Tm.clean([65, 46, 32, 49, 33]) == [65, 49] : +List<U32>}

law agreement_scroll_enables_name:
  {A.scrolled(A.S{0, 300, 85, 1, False{}, T.F{Nil{}, 0n, 0n, Nil{}}, 0}, 300) ==
    A.S{215, 300, 85, 1, True{}, T.F{Nil{}, 0n, 0n, Nil{}}, 0} : A.State}

law agreement_cancel_mode_succeeds_without_scroll:
  {A.step(A.S{0, 300, 85, 0, False{}, T.F{Nil{}, 0n, 0n, Nil{}}, 0},
    A.Cancel{}, 100, 10000) ==
    A.S{0, 300, 85, 0, False{}, T.F{Nil{}, 0n, 0n, Nil{}}, 1} : A.State}
