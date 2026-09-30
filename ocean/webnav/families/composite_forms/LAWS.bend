import Base
import ./Model.bend as M

law finished_absorbs:
  for slider: U32
  for checked: U32
  for goal_slider: U32
  for goal_checkbox: U32
  for success: Bool
  for action: M.Action
  for elapsed: U32
  for deadline: U32
  {M.step(M.S{slider, checked, goal_slider, goal_checkbox, M.Finished{success}},
    action, elapsed, deadline) ==
    M.S{slider, checked, goal_slider, goal_checkbox, M.Finished{success}} : M.State}

law min_slider_first_checkbox_succeeds:
  {M.submit(M.S{0, 1, 0, 1, M.Running{}}) ==
    M.S{0, 1, 0, 1, M.Finished{True{}}} : M.State}

law max_slider_third_checkbox_succeeds:
  {M.submit(M.S{20, 4, 20, 3, M.Running{}}) ==
    M.S{20, 4, 20, 3, M.Finished{True{}}} : M.State}

law extra_checkbox_fails:
  {M.submit(M.S{20, 5, 20, 3, M.Running{}}) ==
    M.S{20, 5, 20, 3, M.Finished{False{}}} : M.State}
