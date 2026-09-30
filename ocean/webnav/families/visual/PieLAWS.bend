import Base
import ./PieModel.bend as M

law closed_item_does_nothing:
  {M.step(M.S{[65,66,67,68],66,False{},0,False{},M.Running{}},
    M.Choose{1n}, 100, 10000) ==
    M.S{[65,66,67,68],66,False{},0,False{},M.Running{}} : M.State}

law ordinary_spreader_waits:
  {M.step(M.S{[65,66,67,68],66,False{},0,False{},M.Running{}},
    M.Toggle{}, 100, 10000) ==
    M.S{[65,66,67,68],66,True{},1600,False{},M.Running{}} : M.State}

law nodelay_spreader_is_ready:
  {M.step(M.S{[65,66,67,68],66,False{},0,True{},M.Running{}},
    M.Toggle{}, 100, 10000) ==
    M.S{[65,66,67,68],66,True{},100,True{},M.Running{}} : M.State}

law premature_choice_does_nothing:
  {M.step(M.S{[65,66,67,68],66,True{},1600,False{},M.Running{}},
    M.Choose{1n}, 1599, 10000) ==
    M.S{[65,66,67,68],66,True{},1600,False{},M.Running{}} : M.State}

law matching_choice_succeeds:
  {M.step(M.S{[65,66,67,68],66,True{},1600,False{},M.Running{}},
    M.Choose{1n}, 1600, 10000) ==
    M.S{[65,66,67,68],66,True{},1600,False{},M.Finished{True{}}} : M.State}

law wrong_choice_fails:
  {M.step(M.S{[65,66,67,68],66,True{},1600,False{},M.Running{}},
    M.Choose{0n}, 1600, 10000) ==
    M.S{[65,66,67,68],66,True{},1600,False{},M.Finished{False{}}} : M.State}
