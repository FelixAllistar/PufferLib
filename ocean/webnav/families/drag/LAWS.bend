import Base
import ./Model.bend as M
import ./DirectionModel.bend as D
import ./ResizeModel.bend as R
import ./SortModel.bend as S
import ./ShapeModel.bend as H
import ./CubeModel.bend as C

law strict_inside_one_pixel:
  {M.contained(M.S{267, 267, 266, 266, 0, M.Running{}}) == True{} : Bool}

law touching_edge_fails:
  {M.contained(M.S{266, 267, 266, 266, 0, M.Running{}}) == False{} : Bool}

law thirty_pixels_fails:
  {M.contained(M.S{296, 267, 266, 266, 0, M.Running{}}) == False{} : Bool}

law moving_requires_drag:
  {M.step(M.S{256, 256, 266, 266, 0, M.Running{}}, M.Move{270, 270}, 50, 10000) ==
   M.S{256, 256, 266, 266, 0, M.Running{}} : M.State}

law deadline_preempts_submit:
  {M.step(M.S{267, 267, 266, 266, 0, M.Running{}}, M.Submit{}, 10000, 10000) ==
   M.S{267, 267, 266, 266, 0, M.TimedOut{}} : M.State}

law circle_wrong_axis_is_not_success:
  {D.success(D.S{2700, 2800, 2700, 2750, 0, 0, M.Running{}}) ==
   False{} : Bool}

law circle_strict_left_is_success:
  {D.success(D.S{2700, 2800, 2699, 2800, 0, 0, M.Running{}}) ==
   True{} : Bool}

law resize_width_ignores_height:
  {R.success(R.S{60, 50, 60, 70, 0, 1, 0, M.Running{}}) ==
   False{} : Bool}

law resize_strict_smaller:
  {R.success(R.S{60, 50, 59, 50, 0, 0, 0, M.Running{}}) ==
   True{} : Bool}

law sortable_drop_checks_start_index:
  {S.step(3, S.S{[2, 1, 3, 4, 5], [0, 0, 0, 0, 0], 2, 2, 1, 1,
    M.Running{}}, S.Up{}, 200, 10000) ==
   S.S{[2, 1, 3, 4, 5], [0, 0, 0, 0, 0], 0, 2, 1, 1,
    M.Loss{}} : S.State}

law sortable_press_release_without_drag_stays_active:
  {S.step(3, S.S{[1, 2, 3, 4, 5], [0, 0, 0, 0, 0], 2, 0, 2, 1,
    M.Running{}}, S.Up{}, 200, 10000) ==
   S.S{[1, 2, 3, 4, 5], [0, 0, 0, 0, 0], 0, 0, 2, 1,
    M.Running{}} : S.State}

law numeric_sort_allows_equal_values:
  {S.ascending([2, 1, 3, 4], [100, 100, 101, 120], 0) == True{} : Bool}

law shape_border_is_outside:
  {H.inside(2580, 2610, 2, 5, 70, 50) == False{} : Bool}

law shape_three_of_five_partial_positive:
  {H.raw(6, H.Finished{3}) == 0.38 : F32}

law cube_starts_on_six:
  {C.step(C.S{6, 6, 0, M.Running{}}, C.Submit{}, 100, 15000) ==
   C.S{6, 6, 0, M.Win{}} : C.State}
