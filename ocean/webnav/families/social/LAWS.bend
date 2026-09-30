import Base
import ./Model.bend as M

law matching_username_and_action_succeeds:
  for post: U32
  {M.act(M.S{post, 3, M.Running{}}, M.Press{3, True{}}) ==
   M.S{post, 3, M.Finished{True{}}} : M.State}

law wrong_username_fails:
  for post: U32
  for expected: U32
  {M.act(M.S{post, expected, M.Running{}}, M.Press{expected, False{}}) ==
   M.S{post, expected, M.Finished{False{}}} : M.State}

law menu_toggle_closes:
  for expected: U32
  {M.act(M.S{2, expected, M.Running{}}, M.Menu{2}) ==
   M.S{0, expected, M.Running{}} : M.State}

law terminal_absorbs:
  for post: U32
  for expected: U32
  for action: M.Action
  for elapsed: U32
  {M.step(M.S{post, expected, M.Finished{True{}}}, action, elapsed, 15000) ==
   M.S{post, expected, M.Finished{True{}}} : M.State}

law toggle_same_control_twice_clears:
  {M.toggle(M.toggle(0, 2), 2) == 0 : U32}

law all_posts_reject_missing_target:
  {M.mark_ok(1, 0, True{}, 2) == False{} : Bool}

law some_posts_allow_unselected_match:
  {M.mark_ok(2, 0, True{}, 2) == True{} : Bool}

law some_posts_reject_other_control:
  {M.mark_ok(2, 1, True{}, 2) == False{} : Bool}

law some_posts_require_exact_count:
  {M.accepted(2, True{}, 1, 2) == False{} : Bool}
