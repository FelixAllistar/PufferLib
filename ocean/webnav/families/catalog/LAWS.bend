import Base
import ./Model.bend as M
import ./Food.bend as F
import ./Search.bend as S

law correct_page_wrong_property_is_partial_success:
  {M.act(M.S{3, 3, 1, M.Running{}}, M.Select{2}) ==
   M.S{3, 3, 1, M.Finished{2}} : M.State}

law wrong_page_right_property_is_partial_failure:
  {M.act(M.S{2, 3, 1, M.Running{}}, M.Select{1}) ==
   M.S{2, 3, 1, M.Finished{1}} : M.State}

law terminal_absorbs:
  for action: M.Action
  for elapsed: U32
  {M.step(M.S{2, 3, 1, M.Finished{2}}, action, elapsed, 15000) ==
   M.S{2, 3, 1, M.Finished{2}} : M.State}

law food_remove_zero_saturates:
  {F.adjust(True{}, 0) == 0 : U32}

law food_quantity_two_fails_named_item:
  {F.item_ok(0, 2, 3, 3, 7, 0, 31) == False{} : Bool}

law food_type_rejects_ineligible_item:
  {F.item_ok(1, 1, 1, 3, 7, 2, 16) == False{} : Bool}

law food_terminal_absorbs:
  for action: F.Action
  {F.step(F.S{F.Finished{False{}}}, action, 100, 20000) ==
   F.S{F.Finished{False{}}} : F.State}

law search_wrong_query_fake_result_fails:
  {S.act(S.S{1, 4, False{}, False{}, S.Running{}},
    S.Result{4, True{}}) ==
   S.S{1, 4, False{}, False{}, S.Finished{False{}}} : S.State}

law search_right_position_succeeds:
  {S.act(S.S{2, 4, True{}, False{}, S.Running{}},
    S.Result{4, False{}}) ==
   S.S{2, 4, True{}, False{}, S.Finished{True{}}} : S.State}

law search_requery_returns_page_one:
  {S.act(S.S{3, 4, False{}, True{}, S.Running{}},
    S.Search{True{}}) ==
   S.S{1, 4, True{}, False{}, S.Running{}} : S.State}

law search_page_rechecks_current_input:
  {S.act(S.S{1, 4, False{}, False{}, S.Running{}},
    S.Page{2, True{}}) ==
   S.S{2, 4, True{}, False{}, S.Running{}} : S.State}

law search_terminal_absorbs:
  for action: S.Action
  {S.step(S.S{2, 4, True{}, True{}, S.Finished{True{}}},
    action, 100, 20000) ==
   S.S{2, 4, True{}, True{}, S.Finished{True{}}} : S.State}
