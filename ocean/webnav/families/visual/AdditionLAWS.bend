import Base
import ../../miniwob/Text.bend as T
import ./AdditionModel.bend as M

law exact_sum_succeeds:
  {M.step(M.S{7,8,T.F{[49,53],2n,2n,Nil{}},True{},M.Running{}},
    M.Submit{},100,15000) ==
    M.S{7,8,T.F{[49,53],2n,2n,Nil{}},False{},M.Finished{True{}}} : M.State}

law leading_zero_fails:
  {M.step(M.S{7,8,T.F{[48,49,53],3n,3n,Nil{}},True{},M.Running{}},
    M.Submit{},100,15000) ==
    M.S{7,8,T.F{[48,49,53],3n,3n,Nil{}},False{},M.Finished{False{}}} : M.State}

law exact_deadline_preempts_submit:
  {M.step(M.S{7,8,T.F{[49,53],2n,2n,Nil{}},True{},M.Running{}},
    M.Submit{},15000,15000) ==
    M.S{7,8,T.F{[49,53],2n,2n,Nil{}},True{},M.TimedOut{}} : M.State}
