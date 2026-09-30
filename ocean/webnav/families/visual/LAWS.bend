import Base
import ./Model.bend as M

law matching_color_succeeds:
  {M.step(M.S{2, 8, 4, 11, 8, False{}, M.Running{}},
    M.Click{2}, 100, 10000) ==
    M.S{2, 8, 4, 11, 8, False{}, M.Finished{True{}}} : M.State}

law wrong_color_fails:
  {M.step(M.S{2, 8, 4, 11, 8, False{}, M.Running{}},
    M.Click{3}, 100, 10000) ==
    M.S{2, 8, 4, 11, 8, False{}, M.Finished{False{}}} : M.State}

law query_swatch_click_fails:
  {M.step(M.S{2, 8, 4, 11, 8, True{}, M.Running{}},
    M.Click{0}, 100, 10000) ==
    M.S{2, 8, 4, 11, 8, True{}, M.Finished{False{}}} : M.State}

law exact_deadline_preempts_color_click:
  {M.step(M.S{2, 8, 4, 11, 8, True{}, M.Running{}},
    M.Click{2}, 10000, 10000) ==
    M.S{2, 8, 4, 11, 8, True{}, M.TimedOut{}} : M.State}

law terminal_absorbs:
  for a: U32
  for b: U32
  for c: U32
  for d: U32
  for expected: U32
  for swatch: Bool
  for success: Bool
  for action: M.Action
  for elapsed: U32
  for deadline: U32
  {M.step(M.S{a,b,c,d,expected,swatch,M.Finished{success}},
    action, elapsed, deadline) ==
    M.S{a,b,c,d,expected,swatch,M.Finished{success}} : M.State}
