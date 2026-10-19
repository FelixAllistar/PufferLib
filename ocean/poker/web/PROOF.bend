import Base
import ./LAWS.bend as L

def L.terminal_controls_are_closed(actor, viewer, kind, call, raising, high, lower, upper, target):
  {==}
def L.free_fold_is_rejected():
  {==}
def L.below_minimum_raise_is_rejected():
  {==}
def L.short_allin_is_accepted():
  {==}
def L.wrong_actor_is_rejected():
  {==}
def L.finished_phase(actor, viewer):
  {==}
def L.session_records_terminal_return():
  {==}
def L.raise_target_clips_short_stack():
  {==}
def L.unopened_three_blinds():
  {==}
def L.reraise_three_times_current_bet():
  {==}
def L.half_pot_bet():
  {==}
def L.pot_raise_calls_before_sizing():
  {==}
def L.three_quarter_pot_rounds_down():
  {==}
def L.oversized_preset_is_allin():
  {==}
def L.choose_same(flag, value):
  match flag:
    case True{}: {==}
    case False{}: {==}
def L.cash_stack_carries(stack):
  L.choose_same(U32.is_eq(stack, 0), stack)
def L.empty_seat_rebuys():
  {==}
def L.unfinished_hand_does_not_score(hand, recorded):
  {==}
def L.completed_hand_scores_once():
  {==}
def L.exact_pair_name():
  {==}
def L.exact_two_pair_name():
  {==}
def L.exact_full_house_name():
  {==}
def L.wheel_straight_name():
  {==}
def L.pocket_pair_name():
  {==}
def L.suited_starting_hand_name():
  {==}
def L.live_hand_never_auto_deals(stack, paused, remaining):
  {==}
def L.busted_hand_waits_for_rebuy():
  {==}
def L.paused_result_does_not_deal():
  {==}
def L.result_waits_until_readable():
  {==}
def L.funded_result_continues():
  {==}
def L.unmatched_showdown_money_is_not_won():
  {==}
def L.side_pot_can_win_despite_net_loss():
  {==}
