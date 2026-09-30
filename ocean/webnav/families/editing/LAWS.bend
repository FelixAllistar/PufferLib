import Base
import ./Model.bend as M
import ./Highlight.bend as H
import ./Editor.bend as E
import ./Terminal.bend as T

law punctuation_is_ignored:
  {M.equal(M.clean([76, 111, 114, 101, 109, 33, 32]),
    [76, 111, 114, 101, 109]) == True{} : Bool}

law capital_is_significant:
  {M.equal(M.clean([108, 111, 114, 101, 109]),
    [76, 111, 114, 101, 109]) == False{} : Bool}

law submit_correct_answer:
  {M.act(M.S{[76, 111, 114, 101, 109],
      [76, 111, 114, 101, 109, 33], True{}, M.Running{}},
      M.Submit{}) ==
   M.S{[76, 111, 114, 101, 109],
      [76, 111, 114, 101, 109, 33], False{},
      M.Finished{True{}}} : M.State}

law timeout_preempts_submit:
  {M.step(M.S{[65], [65], True{}, M.Running{}},
    M.Submit{}, 10000, 10000) ==
   M.S{[65], [65], True{}, M.TimedOut{}} : M.State}

law highlight_ignores_interparagraph_whitespace:
  {H.equal(H.compact([65, 32, 10, 66, 13]),
    H.compact([65, 66])) == True{} : Bool}

law highlight_partial_selection_fails:
  {H.act(H.S{[65, 66, 67], [65, 66, 67], 0n, 2n,
      H.Running{}}, H.Submit{}) ==
   H.S{[65, 66, 67], [65, 66, 67], 0n, 2n,
      H.Finished{False{}}} : H.State}

law highlight_exact_selection_succeeds:
  {H.act(H.S{[65, 32, 66], [65, 66], 0n, 3n,
      H.Running{}}, H.Submit{}) ==
   H.S{[65, 32, 66], [65, 66], 0n, 3n,
      H.Finished{True{}}} : H.State}

law editor_only_target_span_styled:
  {E.correct([0,1,1,1,0],0,1,4,0,0) == True{} : Bool}

law editor_rejects_extra_style:
  {E.correct([1,1,1,1,0],0,1,4,0,0) == False{} : Bool}

law editor_toggles_selected_bold_off:
  {E.format([1,1,0],0,2,1) == [0,0,0] : +List<U32>}

law editor_color_is_inert_until_picker_opens:
  {E.act(E.S{0,1,3,0,1,0,1,False{},[0],E.Running{}},
    E.Color{1}) ==
   E.S{0,1,3,0,1,0,1,False{},[0],E.Running{}} : E.State}

law editor_open_picker_applies_color_and_closes:
  {E.act(E.act(E.S{0,1,3,0,1,0,1,False{},[0],E.Running{}},
    E.OpenColor{}),E.Color{1}) ==
   E.S{0,1,3,0,1,0,1,False{},[8],E.Running{}} : E.State}

law editor_unstyled_all_submit_is_inert:
  {E.act(E.S{0,3,0,0,3,0,0,False{},[0,0,0],E.Running{}},
    E.Submit{}) ==
   E.S{0,3,0,0,3,0,0,False{},[0,0,0],E.Running{}} : E.State}

law editor_unstyled_single_submit_fails:
  {E.act(E.S{0,1,0,0,3,0,0,False{},[0,0,0],E.Running{}},
    E.Submit{}) ==
   E.S{0,1,0,0,3,0,0,False{},[0,0,0],E.Finished{False{}}} : E.State}

law terminal_matching_deletion_succeeds:
  {T.result(T.Present{0,1},1,1) == T.D{0,7,1} : T.Decision}

law terminal_wrong_extension_fails:
  {T.result(T.Present{0,2},1,1) == T.D{0,7,2} : T.Decision}
