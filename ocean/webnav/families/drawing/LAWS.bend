import Base
import ./Model.bend as M
import ./Points.bend as P

law released_move_is_inert:
  for points: +List<P.Point>
  for point: P.Point
  {M.step(M.S{points, False{}, M.Running{}}, M.Move{point}, 100) ==
   M.S{points, False{}, M.Running{}} : M.State}

law new_stroke_replaces_previous_samples:
  for points: +List<P.Point>
  for point: P.Point
  {M.step(M.S{points, False{}, M.Running{}}, M.Down{point}, 100) ==
   M.S{[point], True{}, M.Running{}} : M.State}

law timeout_preempts_pointer_down:
  for points: +List<P.Point>
  for point: P.Point
  {M.step(M.S{points, False{}, M.Running{}}, M.Down{point}, 10000) ==
   M.S{points, False{}, M.TimedOut{}} : M.State}

law terminal_absorbs_pointer:
  for points: +List<P.Point>
  for active: Bool
  for action: M.Action
  for elapsed: U32
  {M.step(M.S{points, active, M.TimedOut{}}, action, elapsed) ==
   M.S{points, active, M.TimedOut{}} : M.State}


law submit_deadline_preempts_scoring:
  for points: +List<P.Point>
  for active: Bool
  for task: U32
  for center: P.Point
  for horizontal: Bool
  {M.step_task(M.S{points, active, M.Running{}}, M.Submit{}, 10000, task, center, horizontal) ==
   M.S{points, active, M.TimedOut{}} : M.State}

law finished_reward_absorbs_all_actions:
  for points: +List<P.Point>
  for active: Bool
  for reward: F32
  for action: M.Action
  for elapsed: U32
  for task: U32
  for center: P.Point
  for horizontal: Bool
  {M.step_task(M.S{points, active, M.Finished{reward}}, action, elapsed, task, center, horizontal) ==
   M.S{points, active, M.Finished{reward}} : M.State}
