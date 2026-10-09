import Base
import ./Model.bend as M

law waiting_does_not_finish:
  {M.step(M.Open{}, M.Wait{}) == M.Open{} : M.State}

law submission_preserves_the_policy_response:
  for reply: M.Reply
  {M.response(M.step(M.Open{}, M.Submit{reply})) == Some{reply} : Maybe<&2, M.Reply>}

law submitted_response_cannot_be_replaced:
  for reply: M.Reply
  for action: M.Action
  {M.step(M.Submitted{reply}, action) == M.Submitted{reply} : M.State}

law budget_expiry_is_not_a_submission:
  {M.response(M.step(M.Open{}, M.Expire{})) == None{} : Maybe<&2, M.Reply>}

law expired_session_cannot_submit:
  for action: M.Action
  {M.step(M.Expired{}, action) == M.Expired{} : M.State}
