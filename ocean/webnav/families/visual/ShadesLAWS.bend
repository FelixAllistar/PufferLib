import Base
import ./ShadesModel.bend as M

law toggle_selects_first:
  {M.step(M.S{[0,1,0],0,0,M.Running{}},M.Toggle{0},100,15000) ==
    M.S{[0,1,0],1,0,M.Running{}} : M.State}

law exact_shades_succeed:
  {M.step(M.S{[0,1,0],5,0,M.Running{}},M.Submit{},100,15000) ==
    M.S{[0,1,0],5,0,M.Finished{True{}}} : M.State}

law missing_shade_fails:
  {M.step(M.S{[0,1,0],1,0,M.Running{}},M.Submit{},100,15000) ==
    M.S{[0,1,0],1,0,M.Finished{False{}}} : M.State}

law exact_deadline_preempts_submit:
  {M.step(M.S{[0,1,0],5,0,M.Running{}},M.Submit{},15000,15000) ==
    M.S{[0,1,0],5,0,M.TimedOut{}} : M.State}
