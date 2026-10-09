import Base
import ./Value.bend as V
import ./Model.bend as M
import ./Query.bend as Q

law stale_write_is_atomic:
  {M.update([M.R{7, 2, [V.Integer{10}]}], 7, 1, [V.Integer{20}]) == M.W{[M.R{7, 2, [V.Integer{10}]}], M.Conflict{}} : M.Write}

law missing_write_preserves_store:
  {M.update([M.R{7, 2, [V.Integer{10}]}], 8, 2, [V.Integer{20}]) == M.W{[M.R{7, 2, [V.Integer{10}]}], M.Missing{}} : M.Write}

law update_preserves_identity:
  {M.update([M.R{7, 2, [V.Integer{10}]}], 7, 2, [V.Integer{20}]) == M.W{[M.R{7, 3, [V.Integer{20}]}], M.Applied{}} : M.Write}

law revision_cannot_wrap:
  {M.update([M.R{7, 4294967295, [V.Integer{10}]}], 7, 4294967295, [V.Integer{20}]) == M.W{[M.R{7, 4294967295, [V.Integer{10}]}], M.Invalid{}} : M.Write}

law negative_integer_sorts_before_zero:
  {V.less(V.Integer{4294967295}, V.Integer{0}) == True{} : Bool}

law filter_then_sort_then_page:
  {Q.query([M.R{3, 0, [V.Integer{2}]}, M.R{1, 0, [V.Integer{3}]}, M.R{2, 0, [V.Integer{2}]}], Q.Less{0n, V.Integer{3}}, 0n, False{}, 1n, 1n) == [M.R{3, 0, [V.Integer{2}]}] : +List<M.Record>}

law duplicate_identity_is_rejected:
  {M.insert([M.R{7, 2, [V.Integer{10}]}], M.R{7, 0, [V.Integer{20}]}) == M.W{[M.R{7, 2, [V.Integer{10}]}], M.Conflict{}} : M.Write}

law text_filter_uses_public_value:
  {Q.matches(Q.Contains{0n, V.Text{"pending"}}, M.R{9, 0, [V.Text{"order pending review"}]}) == True{} : Bool}
