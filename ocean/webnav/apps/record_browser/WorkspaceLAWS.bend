import Base
import ./Workspace.bend as M
import ./Model.bend as L
import ../../primitives/browser/Model.bend as B
import ../../primitives/text/Model.bend as E
import ../../primitives/text/Number.bend as N
import ../../primitives/records/Value.bend as V
import ../../primitives/transaction/Model.bend as D

law decimal_overflow_is_invalid:
  {N.parse([50, 49, 52, 55, 52, 56, 51, 54, 52, 56]) == N.Invalid{} : N.Parsed}

law empty_input_is_not_zero:
  {N.field(Nil{}) == V.Text{""} : V.Value}

law modal_blocks_page_navigation:
  {M.act(M.Browse{L.Back{}}, M.S{L.S{B.S{1, 17, 50, 0, [0], Nil{}, B.Ready{}}, Nil{}, E.F{Nil{}, 0n, 0n}, "", 0, False{}, 0n, False{}}, D.Editing{1, 0, [V.Text{"Row 1"}, V.Integer{3}, V.Integer{0}]}, D.Clean{}, E.F{[51], 1n, 1n}, True{}}) == M.S{L.S{B.S{1, 17, 50, 0, [0], Nil{}, B.Ready{}}, Nil{}, E.F{Nil{}, 0n, 0n}, "", 0, False{}, 0n, False{}}, D.Editing{1, 0, [V.Text{"Row 1"}, V.Integer{3}, V.Integer{0}]}, D.Clean{}, E.F{[51], 1n, 1n}, True{}} : M.State}
