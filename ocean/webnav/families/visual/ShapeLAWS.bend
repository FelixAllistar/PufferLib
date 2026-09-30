import Base
import ./ShapeModel.bend as M

law broad_type_can_match_multiple:
  {M.step(M.S{False{},
    [M.Item{0,0,0,0,1,65},M.Item{1,0,2,1,1,66}],
    M.D{0,0,1},Nil{},M.Running{}},M.ClickShape{1n},100,10000) ==
    M.S{False{},
      [M.Item{0,0,0,0,1,65},M.Item{1,0,2,1,1,66}],
      M.D{0,0,1},Nil{},M.Finished{True{}}} : M.State}

law size_mismatch_fails:
  {M.step(M.S{False{},[M.Item{0,0,0,0,1,65}],
    M.D{2,0,65},Nil{},M.Running{}},M.ClickShape{0n},100,10000) ==
    M.S{False{},[M.Item{0,0,0,0,1,65}],
      M.D{2,0,65},Nil{},M.Finished{False{}}} : M.State}

law zero_count_succeeds:
  {M.step(M.S{True{},[M.Item{0,0,0,0,1,65}],
    M.D{0,0,203},[0,1,2,3,4],M.Running{}},
    M.ChooseCount{0n},100,10000) ==
    M.S{True{},[M.Item{0,0,0,0,1,65}],
      M.D{0,0,203},[0,1,2,3,4],M.Finished{True{}}} : M.State}

law blank_svg_fails:
  {M.step(M.S{False{},Nil{},M.D{0,0,0},Nil{},M.Running{}},
    M.ClickBlank{},100,10000) ==
    M.S{False{},Nil{},M.D{0,0,0},Nil{},M.Finished{False{}}} : M.State}
