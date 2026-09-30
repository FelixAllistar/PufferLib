import Base
import ./Model.bend as M

law requested_grid_point_succeeds:
  {M.step(M.S{12, 25, M.Running{}}, M.Click{12}, 500, 10000) ==
    M.S{12, 12, M.Finished{True{}}} : M.State}

law wrong_grid_point_fails:
  {M.step(M.S{12, 25, M.Running{}}, M.Click{13}, 500, 10000) ==
    M.S{12, 13, M.Finished{False{}}} : M.State}

law exact_deadline_preempts_grid_click:
  {M.step(M.S{12, 25, M.Running{}}, M.Click{12}, 10000, 10000) ==
    M.S{12, 25, M.TimedOut{}} : M.State}

law grid_terminal_absorbs:
  for goal: U32
  for selected: U32
  for success: Bool
  for action: M.Action
  for elapsed: U32
  for deadline: U32
  {M.step(M.S{goal, selected, M.Finished{success}}, action, elapsed, deadline) ==
    M.S{goal, selected, M.Finished{success}} : M.State}
