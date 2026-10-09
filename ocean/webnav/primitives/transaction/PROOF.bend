import Base
import ./LAWS.bend as L

def L.cancel_cannot_commit(rows, id, version, cells, error):
  {==}
def L.draft_edit_does_not_persist():
  {==}
def L.commit_is_idempotent(rows, id, version, fields):
  {==}
def L.invalid_commit_preserves_draft_and_store():
  {==}
def L.stale_commit_preserves_concurrent_update():
  {==}
def L.successful_commit_changes_only_selected_record():
  {==}
def L.readonly_edit_is_rejected():
  {==}
