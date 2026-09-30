import Base
import ./Model.bend as M
import ./Generate.bend as G

law base_inbox_has_three_messages:
  for seed: U32
  {G.count_for(0, seed) == 3 : U32}

law timeout_precedes_click:
  for s: M.State
  for action: M.Action
  {M.running(True{}, s, action) == M.timeout(s) : M.State}

law terminal_success_absorbs_later_action:
  for selected: U32
  for focus: U32
  for goal: M.Goal
  for action: M.Action
  for elapsed: U32
  {M.step(M.S{M.Read{}, selected, focus, goal, M.Finished{True{}, True{}}},
    action, elapsed, 30000) ==
   M.S{M.Read{}, selected, focus, goal, M.Finished{True{}, True{}}} : M.State}

law terminal_failure_absorbs_later_action:
  for selected: U32
  for goal: M.Goal
  for action: M.Action
  {M.step(M.S{M.Reply{}, selected, 2, goal, M.Finished{False{}, False{}}},
    action, 1234, 30000) ==
   M.S{M.Reply{}, selected, 2, goal, M.Finished{False{}, False{}}} : M.State}

law irrelevant_star_keeps_reply_task_running:
  for selected: U32
  {M.act(M.S{M.Inbox{}, selected, 0, M.ReplyGoal{}, M.Running{}},
    M.Star{True{}}) ==
   M.S{M.Inbox{}, selected, 0, M.ReplyGoal{}, M.Running{}} : M.State}

law wrong_star_ends_important_task_untimed:
  {M.act(M.S{M.Read{}, 2, 0, M.ImportantGoal{}, M.Running{}},
    M.Star{False{}}) ==
   M.S{M.Read{}, 2, 0, M.ImportantGoal{}, M.Finished{False{}, False{}}} : M.State}

law wrong_trash_ends_delete_task_untimed:
  {M.act(M.S{M.Inbox{}, 2, 0, M.DeleteGoal{}, M.Running{}},
    M.Trash{False{}}) ==
   M.S{M.Inbox{}, 2, 0, M.DeleteGoal{}, M.Finished{False{}, False{}}} : M.State}

law wrong_reply_text_fails:
  {M.act(M.S{M.Reply{}, 1, 2, M.ReplyGoal{}, M.Running{}},
    M.SendReply{True{}, False{}}) ==
   M.S{M.Reply{}, 1, 2, M.ReplyGoal{}, M.Finished{False{}, False{}}} : M.State}

law forward_sender_identity_is_not_scored:
  for selected: U32
  {M.act(M.S{M.Forward{}, selected, 3, M.ForwardGoal{}, M.Running{}},
    M.SendForward{True{}, True{}}) ==
   M.S{M.Forward{}, selected, 3, M.ForwardGoal{}, M.Finished{True{}, True{}}} : M.State}

law wrong_forward_body_fails:
  {M.act(M.S{M.Forward{}, 2, 3, M.ForwardGoal{}, M.Running{}},
    M.SendForward{True{}, False{}}) ==
   M.S{M.Forward{}, 2, 3, M.ForwardGoal{}, M.Finished{False{}, False{}}} : M.State}

law search_result_opens_read:
  {M.act(M.S{M.Search{}, 0, 1, M.ForwardGoal{}, M.Running{}},
    M.OpenEmail{2}) ==
   M.S{M.Read{}, 2, 0, M.ForwardGoal{}, M.Running{}} : M.State}

law read_close_returns_inbox:
  {M.act(M.S{M.Read{}, 2, 0, M.ReplyGoal{}, M.Running{}},
    M.CloseEmail{}) ==
   M.S{M.Inbox{}, 0, 0, M.ReplyGoal{}, M.Running{}} : M.State}
