import Base
import ./Model.bend as M

# These are the executable specifications for the two task cores.  The
# browser and wire are deliberately outside this file: laws describe the
# event semantics after the harness has supplied the observed event.

law sequence_miss_is_identity:
  for one: M.Rect
  for two: M.Rect
  for clicks: U32
  for first: M.Hit
  {M.sequence_record_hit(M.S{one, two, clicks, first, M.Running{}}, M.Miss{}) == M.S{one, two, clicks, first, M.Running{}} : M.SequenceState}

law sequence_first_one_waits:
  for one: M.Rect
  for two: M.Rect
  {M.sequence_record(M.S{one, two, 0, M.Miss{}, M.Running{}}, M.One{}) == M.S{one, two, 1, M.One{}, M.Running{}} : M.SequenceState}

law sequence_first_two_waits:
  for one: M.Rect
  for two: M.Rect
  {M.sequence_record(M.S{one, two, 0, M.Miss{}, M.Running{}}, M.Two{}) == M.S{one, two, 1, M.Two{}, M.Running{}} : M.SequenceState}

law sequence_correct_second_click_succeeds:
  for one: M.Rect
  for two: M.Rect
  {M.sequence_record(M.S{one, two, 1, M.One{}, M.Running{}}, M.Two{}) == M.S{one, two, 2, M.One{}, M.Finished{True{}}} : M.SequenceState}

law sequence_wrong_second_click_fails:
  for one: M.Rect
  for two: M.Rect
  {M.sequence_record(M.S{one, two, 1, M.One{}, M.Running{}}, M.One{}) == M.S{one, two, 2, M.One{}, M.Finished{False{}}} : M.SequenceState}

law sequence_overlap_uses_topmost_button:
  {M.hit_at(M.R{10, 20}, M.R{10, 20}, 25, 35) == M.Two{} : M.Hit}

law sequence_left_edge_is_inside:
  {M.hit_at(M.R{10, 20}, M.R{100, 100}, 10, 20) == M.One{} : M.Hit}

law sequence_right_edge_is_outside:
  {M.hit_at(M.R{10, 20}, M.R{100, 100}, 50, 35) == M.Miss{} : M.Hit}

law sequence_reset_clears_history:
  for one: M.Rect
  for two: M.Rect
  for clicks: U32
  for first: M.Hit
  for outcome: M.Outcome
  {M.sequence_reset(M.S{one, two, clicks, first, outcome}) == M.S{one, two, 0, M.Miss{}, M.Running{}} : M.SequenceState}

law sequence_terminal_absorbs_action:
  for one: M.Rect
  for two: M.Rect
  for success: Bool
  for action: M.SequenceAction
  {M.sequence_step(M.S{one, two, 2, M.One{}, M.Finished{success}}, action, 0) == M.S{one, two, 2, M.One{}, M.Finished{success}} : M.SequenceState}

law sequence_timeout_absorbs_action:
  for one: M.Rect
  for two: M.Rect
  for action: M.SequenceAction
  {M.sequence_step(M.S{one, two, 1, M.One{}, M.TimedOut{}}, action, 0) == M.S{one, two, 1, M.One{}, M.TimedOut{}} : M.SequenceState}

law sequence_deadline_preempts_action:
  for s: M.SequenceState
  for action: M.SequenceAction
  {M.sequence_step(s, action, 10000) == M.sequence_timeout(s) : M.SequenceState}

law sequence_running_reward_is_zero:
  for elapsed: U32
  {M.timed_reward(M.Running{}, elapsed) == 0.0 : F32}

law sequence_success_reward_is_positive:
  {M.raw_reward(M.Finished{True{}}) == 1.0 : F32}

law sequence_failure_reward_is_negative:
  {M.raw_reward(M.Finished{False{}}) == (0.0 - 1.0 : F32) : F32}

law choose_select_preserves_options:
  for valid: Bool
  for options: +List<+List<U32>>
  for target: +List<U32>
  for selected: Nat
  for index: Nat
  {M.choose_options(M.choose_select_valid(valid, options, target, selected, index)) == options : +List<+List<U32>>}

law choose_select_preserves_target:
  for valid: Bool
  for options: +List<+List<U32>>
  for target: +List<U32>
  for selected: Nat
  for index: Nat
  {M.choose_target(M.choose_select_valid(valid, options, target, selected, index)) == target : +List<U32>}

law choose_invalid_select_is_identity:
  {M.choose_select(M.C{[[65], [66], [67]], [66], 0n, M.Running{}}, 3n) == M.C{[[65], [66], [67]], [66], 0n, M.Running{}} : M.ChooseState}

law choose_select_last_write_wins:
  {M.choose_select(M.choose_select(M.C{[[65], [66], [67]], [66], 0n, M.Running{}}, 1n), 2n) == M.choose_select(M.C{[[65], [66], [67]], [66], 0n, M.Running{}}, 2n) : M.ChooseState}

law choose_submit_compares_visible_text:
  {M.choose_submit(M.choose_select(M.C{[[65], [65], [66]], [65], 0n, M.Running{}}, 1n)) == M.C{[[65], [65], [66]], [65], 1n, M.Finished{True{}}} : M.ChooseState}

law choose_submit_rejects_different_text:
  {M.choose_submit(M.C{[[65], [66], [67]], [65], 2n, M.Running{}}) == M.C{[[65], [66], [67]], [65], 2n, M.Finished{False{}}} : M.ChooseState}

law choose_reset_clears_selection:
  for options: +List<+List<U32>>
  for target: +List<U32>
  for selected: Nat
  for outcome: M.Outcome
  {M.choose_reset(M.C{options, target, selected, outcome}) == M.C{options, target, 0n, M.Running{}} : M.ChooseState}

law choose_terminal_absorbs_action:
  for options: +List<+List<U32>>
  for target: +List<U32>
  for selected: Nat
  for success: Bool
  for action: M.ChooseAction
  {M.choose_step(M.C{options, target, selected, M.Finished{success}}, action, 0) == M.C{options, target, selected, M.Finished{success}} : M.ChooseState}

law choose_deadline_preempts_action:
  for s: M.ChooseState
  for action: M.ChooseAction
  {M.choose_step(s, action, 10000) == M.choose_timeout(s) : M.ChooseState}

law choose_timeout_reward_is_negative:
  for elapsed: U32
  {M.timed_reward(M.TimedOut{}, elapsed) == (0.0 - 1.0 : F32) : F32}
