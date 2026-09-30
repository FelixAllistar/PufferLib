import Base
import ./Model.bend as M

law buy_before_first_tick_fails:
  {M.step(M.S{[5000, 4500], 4500, 0, 0, M.Running{}},
    M.Buy{}, 50, 10000) ==
    M.S{[5000, 4500], 4500, 0, 0, M.Finished{False{}}} : M.State}

law threshold_tie_succeeds_at_second_tick:
  {M.step(M.S{[5000, 4500], 4500, 0, 0, M.Running{}},
    M.Buy{}, 200, 10000) ==
    M.S{[5000, 4500], 4500, 2, 4500, M.Finished{True{}}} : M.State}

law above_threshold_fails_at_first_tick:
  {M.step(M.S{[5000, 4500], 4500, 0, 0, M.Running{}},
    M.Buy{}, 100, 10000) ==
    M.S{[5000, 4500], 4500, 1, 5000, M.Finished{False{}}} : M.State}

law wait_advances_to_second_price:
  {M.step(M.S{[5000, 4500], 4500, 0, 0, M.Running{}},
    M.Wait{}, 200, 10000) ==
    M.S{[5000, 4500], 4500, 2, 4500, M.Running{}} : M.State}

law exact_deadline_preempts_buy:
  {M.step(M.S{[5000, 4500], 4500, 2, 4500, M.Running{}},
    M.Buy{}, 10000, 10000) ==
    M.S{[5000, 4500], 4500, 99, 0, M.TimedOut{}} : M.State}

law finished_absorbs:
  for prices: +List<U32>
  for threshold: U32
  for tick: U32
  for visible: U32
  for success: Bool
  for action: M.Action
  for elapsed: U32
  for deadline: U32
  {M.step(M.S{prices, threshold, tick, visible,
    M.Finished{success}}, action, elapsed, deadline) ==
    M.S{prices, threshold, tick, visible,
    M.Finished{success}} : M.State}
