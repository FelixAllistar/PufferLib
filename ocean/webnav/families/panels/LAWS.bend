import Base
import ./Model.bend as M

law terminal_absorbs:
  for active: U32
  for panels: U32
  for accordion: Bool
  for target: U32
  for medium: U32
  for success: Bool
  for action: M.Action
  for elapsed: U32
  for deadline: U32
  {M.step(M.S{active, panels, accordion, target, medium, M.Finished{success}}, action, elapsed, deadline) == M.S{active, panels, accordion, target, medium, M.Finished{success}} : M.State}

law timeout_absorbs:
  for active: U32
  for panels: U32
  for accordion: Bool
  for target: U32
  for medium: U32
  for action: M.Action
  for elapsed: U32
  for deadline: U32
  {M.step(M.S{active, panels, accordion, target, medium, M.TimedOut{}}, action, elapsed, deadline) == M.S{active, panels, accordion, target, medium, M.TimedOut{}} : M.State}

law expired_preempts_action:
  for active: U32
  for panels: U32
  for accordion: Bool
  for target: U32
  for medium: U32
  for action: M.Action
  {M.guarded(True{}, M.S{active, panels, accordion, target, medium, M.Running{}}, action) == M.S{active, panels, accordion, target, medium, M.TimedOut{}} : M.State}

law invalid_header_preserves_state:
  for s: M.State
  for panel: U32
  {M.header_valid(False{}, s, panel) == s : M.State}

law hidden_link_preserves_state:
  for s: M.State
  for correct: Bool
  {M.link_visible(False{}, s, correct) == s : M.State}

law visible_link_uses_goal:
  for s: M.State
  for correct: Bool
  {M.link_visible(True{}, s, correct) == M.finish(s, correct) : M.State}

law medium_absent_target_accepts_second_tab:
  {M.step(M.S{1, 2, False{}, 0, 1, M.Running{}}, M.Header{2}, 250, 10000) == M.S{2, 2, False{}, 0, 1, M.Finished{True{}}} : M.State}

law medium_present_target_rejects_second_tab:
  {M.step(M.S{1, 2, False{}, 0, 2, M.Running{}}, M.Header{2}, 250, 10000) == M.S{2, 2, False{}, 0, 2, M.Finished{False{}}} : M.State}

law accordion_closes_selected_panel:
  {M.step(M.S{2, 3, True{}, 0, 0, M.Running{}}, M.Header{2}, 250, 10000) == M.S{0, 3, True{}, 0, 0, M.Running{}} : M.State}

law tabs_keep_selected_panel:
  {M.step(M.S{2, 3, False{}, 0, 0, M.Running{}}, M.Header{2}, 250, 10000) == M.S{2, 3, False{}, 0, 0, M.Running{}} : M.State}

law closed_submit_fails:
  {M.step(M.S{0, 2, True{}, 0, 0, M.Running{}}, M.Submit{}, 250, 10000) == M.S{2, 2, True{}, 0, 0, M.Finished{False{}}} : M.State}

law open_submit_succeeds:
  {M.step(M.S{1, 2, True{}, 0, 0, M.Running{}}, M.Submit{}, 250, 10000) == M.S{2, 2, True{}, 0, 0, M.Finished{True{}}} : M.State}

law untimed_failure_reward:
  for elapsed: U32
  for deadline: U32
  {M.timed(M.Finished{False{}}, elapsed, deadline) == (0.0 - 1.0 : F32) : F32}

law timeout_reward:
  {M.raw(M.TimedOut{}) == (0.0 - 1.0 : F32) : F32}
