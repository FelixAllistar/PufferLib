import Base
import ./DrawModel.bend as M

law click_replaces_user_point:
  for task: M.Task
  for a: M.Point
  for b: M.Point
  for c: M.Point
  for radius: F32
  for old: M.Point
  for fresh: M.Point
  {M.step(M.S{task, a, b, c, radius, True{}, old, M.Running{}},
    M.Click{fresh}, 100, 10000) ==
    M.S{task, a, b, c, radius, True{}, fresh, M.Running{}} : M.State}

law empty_submit_fails:
  for task: M.Task
  for a: M.Point
  for b: M.Point
  for c: M.Point
  for radius: F32
  for user: M.Point
  {M.step(M.S{task, a, b, c, radius, False{}, user, M.Running{}},
    M.Submit{}, 100, 10000) ==
    M.S{task, a, b, c, radius, False{}, user,
      M.Finished{(0.0 - 1.0 : F32), False{}}} : M.State}

law outside_distance_fails:
  for distance: F32
  for limit: F32
  {M.finish_near(False{}, distance, limit) ==
    M.Finished{(0.0 - 1.0 : F32), False{}} : M.Outcome}

law outside_angle_fails:
  for total: F32
  for larger: F32
  {M.bisect_result(True{}, total, larger) ==
    M.Finished{(0.0 - 1.0 : F32), False{}} : M.Outcome}

law exact_deadline_preempts_submit:
  for task: M.Task
  for a: M.Point
  for b: M.Point
  for c: M.Point
  for radius: F32
  for user: M.Point
  {M.step(M.S{task, a, b, c, radius, True{}, user, M.Running{}},
    M.Submit{}, 10000, 10000) ==
    M.S{task, a, b, c, radius, True{}, user, M.TimedOut{}} : M.State}

law draw_terminal_absorbs:
  for task: M.Task
  for a: M.Point
  for b: M.Point
  for c: M.Point
  for radius: F32
  for has_user: Bool
  for user: M.Point
  for reward: F32
  for scaled: Bool
  for action: M.Action
  for elapsed: U32
  for deadline: U32
  {M.step(M.S{task, a, b, c, radius, has_user, user,
    M.Finished{reward, scaled}}, action, elapsed, deadline) ==
    M.S{task, a, b, c, radius, has_user, user,
    M.Finished{reward, scaled}} : M.State}
