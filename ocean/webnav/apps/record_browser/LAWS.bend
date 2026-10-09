import Base
import ./Model.bend as M
import ../../primitives/browser/Model.bend as B
import ../../primitives/text/Model.bend as E
import ../../primitives/records/Model.bend as R
import ../../primitives/records/Value.bend as V

law query_draft_is_distinct_from_applied_query:
  {M.act(M.Editing{E.Insert{[120]}}, M.S{B.S{0, 17, 50, 0, Nil{}, Nil{}, B.Ready{}}, Nil{}, E.F{Nil{}, 0n, 0n}, "old", 0, False{}, 0n, True{}}) == M.S{B.S{0, 17, 50, 0, Nil{}, Nil{}, B.Ready{}}, Nil{}, E.F{[120], 1n, 1n}, "old", 0, False{}, 0n, True{}} : M.State}

law nonvisible_record_cannot_be_opened:
  {M.act(M.Open{2}, M.S{B.S{0, 17, 50, 0, Nil{}, Nil{}, B.Ready{}}, [R.R{1, 0, [V.Text{"one"}, V.Integer{3}, V.Integer{0}]}], E.F{Nil{}, 0n, 0n}, "", 0, False{}, 0n, False{}}) == M.S{B.S{0, 17, 50, 0, Nil{}, Nil{}, B.Ready{}}, [R.R{1, 0, [V.Text{"one"}, V.Integer{3}, V.Integer{0}]}], E.F{Nil{}, 0n, 0n}, "", 0, False{}, 0n, False{}} : M.State}

law back_keeps_list_query:
  {M.act(M.Back{}, M.S{B.S{3, 17, 50, 0, [0], Nil{}, B.Ready{}}, Nil{}, E.F{Nil{}, 0n, 0n}, "pending", 1, True{}, 2n, False{}}) == M.S{B.S{0, 17, 50, 0, Nil{}, [3], B.Loading{50}}, Nil{}, E.F{Nil{}, 0n, 0n}, "pending", 1, True{}, 2n, False{}} : M.State}

law loading_disables_page_actions:
  {M.act(M.Filter{2}, M.S{B.S{0, 17, 50, 0, Nil{}, Nil{}, B.Loading{50}}, Nil{}, E.F{Nil{}, 0n, 0n}, "", 0, False{}, 0n, False{}}) == M.S{B.S{0, 17, 50, 0, Nil{}, Nil{}, B.Loading{50}}, Nil{}, E.F{Nil{}, 0n, 0n}, "", 0, False{}, 0n, False{}} : M.State}
