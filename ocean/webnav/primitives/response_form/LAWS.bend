import Base
import ./Model.bend as M
import ../text/Model.bend as E
import ../finish/Model.bend as F

law submitted_absorbs:
  for +reply: F.Reply
  for +kind: F.TaskType
  for +status: F.Status
  for +data: E.Field
  for +detail: E.Field
  for +focus: M.Focus
  for +error: M.Error
  for action: M.Action
  {M.step(M.S{F.Submitted{reply}, kind, status, data, detail, focus, error}, action) ==
    M.S{F.Submitted{reply}, kind, status, data, detail, focus, error} : M.State}

law expired_absorbs:
  for +kind: F.TaskType
  for +status: F.Status
  for +data: E.Field
  for +detail: E.Field
  for +focus: M.Focus
  for +error: M.Error
  for action: M.Action
  {M.step(M.S{F.Expired{}, kind, status, data, detail, focus, error}, action) ==
    M.S{F.Expired{}, kind, status, data, detail, focus, error} : M.State}

law invalid_stays_open:
  for +kind: F.TaskType
  for +status: F.Status
  for +data: E.Field
  for +detail: E.Field
  for +focus: M.Focus
  for error: M.Error
  for +why: M.Error
  {M.step(M.S{F.Open{}, kind, status, data, detail, focus, error}, M.Reject{why}) ==
    M.S{F.Open{}, kind, status, data, detail, focus, why} : M.State}

law finish_uses_chosen_fields:
  for +kind: F.TaskType
  for +status: F.Status
  for +data: E.Field
  for +detail: E.Field
  for focus: M.Focus
  for error: M.Error
  for +bytes: +List<U32>
  for +explanation: +List<U32>
  {M.step(M.S{F.Open{}, kind, status, data, detail, focus, error}, M.Finish{bytes, explanation}) ==
    M.S{F.Submitted{F.R{kind, status, bytes, explanation}}, kind, status, data, detail, M.None{}, M.Clean{}} : M.State}

law blur_preserves_drafts:
  for +kind: F.TaskType
  for +status: F.Status
  for +data: E.Field
  for +detail: E.Field
  for focus: M.Focus
  for +error: M.Error
  {M.step(M.S{F.Open{}, kind, status, data, detail, focus, error}, M.Blur{}) ==
    M.S{F.Open{}, kind, status, data, detail, M.None{}, error} : M.State}
