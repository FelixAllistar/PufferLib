import Base
import ../records/Model.bend as R
import ../records/Value.bend as V
import ../records/Schema.bend as C
import ./Model.bend as M

law cancel_cannot_commit:
  for rows: +List<R.Record>
  for id: U32
  for version: U32
  for cells: +List<V.Value>
  for error: M.Error
  {M.persisted(M.cancel(M.S{rows, M.Editing{id, version, cells}, error})) == rows : +List<R.Record>}

law draft_edit_does_not_persist:
  {M.persisted(M.edit(M.S{[R.R{7, 1, [V.Integer{3}]}], M.Editing{7, 1, [V.Integer{3}]}, M.Clean{}}, [C.F{C.Integer{0, 10}, True{}}], 0n, V.Integer{8})) == [R.R{7, 1, [V.Integer{3}]}] : +List<R.Record>}

law commit_is_idempotent:
  for rows: +List<R.Record>
  for id: U32
  for version: U32
  for fields: +List<C.Field>
  {M.commit(M.S{rows, M.Committed{id, version}, M.Clean{}}, fields) == M.S{rows, M.Committed{id, version}, M.Clean{}} : M.State}

law invalid_commit_preserves_draft_and_store:
  {M.commit(M.S{[R.R{7, 1, [V.Integer{3}]}], M.Editing{7, 1, [V.Integer{99}]}, M.Clean{}}, [C.F{C.Integer{0, 10}, True{}}]) == M.S{[R.R{7, 1, [V.Integer{3}]}], M.Editing{7, 1, [V.Integer{99}]}, M.Invalid{}} : M.State}

law stale_commit_preserves_concurrent_update:
  {M.commit(M.S{[R.R{7, 2, [V.Integer{4}]}], M.Editing{7, 1, [V.Integer{8}]}, M.Clean{}}, [C.F{C.Integer{0, 10}, True{}}]) == M.S{[R.R{7, 2, [V.Integer{4}]}], M.Editing{7, 1, [V.Integer{8}]}, M.Conflict{}} : M.State}

law successful_commit_changes_only_selected_record:
  {M.commit(M.S{[R.R{7, 1, [V.Integer{3}]}, R.R{9, 0, [V.Integer{5}]}], M.Editing{7, 1, [V.Integer{8}]}, M.Clean{}}, [C.F{C.Integer{0, 10}, True{}}]) == M.S{[R.R{7, 2, [V.Integer{8}]}, R.R{9, 0, [V.Integer{5}]}], M.Committed{7, 2}, M.Clean{}} : M.State}

law readonly_edit_is_rejected:
  {M.edit(M.S{[R.R{7, 1, [V.Integer{3}]}], M.Editing{7, 1, [V.Integer{3}]}, M.Clean{}}, [C.F{C.Integer{0, 10}, False{}}], 0n, V.Integer{8}) == M.S{[R.R{7, 1, [V.Integer{3}]}], M.Editing{7, 1, [V.Integer{3}]}, M.Invalid{}} : M.State}
