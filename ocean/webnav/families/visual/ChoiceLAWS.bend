import Base
import ./ChoiceModel.bend as M

law matching_button_succeeds:
  {M.step(M.S{5,M.Running{}},M.Choose{5},100,10000) ==
    M.S{5,M.Finished{True{}}} : M.State}

law wrong_button_fails:
  {M.step(M.S{5,M.Running{}},M.Choose{4},100,10000) ==
    M.S{5,M.Finished{False{}}} : M.State}

law exact_deadline_preempts_choice:
  {M.step(M.S{5,M.Running{}},M.Choose{5},10000,10000) ==
    M.S{5,M.TimedOut{}} : M.State}

law terminal_absorbs:
  for goal: U32
  for elapsed: U32
  for deadline: U32
  for choice: U32
  for success: Bool
  {M.step(M.S{goal,M.Finished{success}},M.Choose{choice},elapsed,deadline) ==
    M.S{goal,M.Finished{success}} : M.State}
