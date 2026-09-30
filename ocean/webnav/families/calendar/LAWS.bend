import Base
import ./Model.bend as M

law leap_february:
  {M.days(2) == 29 : U32}

law november_boundary:
  {M.day_ordinal(11, 1) == 306 : U32}

law december_boundary:
  {M.day_ordinal(12, 31) == 366 : U32}

law invalid_february_day:
  {M.valid_day(2, 30) == False{} : Bool}

law january_prev_stays:
  {M.date_click(M.S{0, 1, 0, True{}, 1, 0, 0, 0, 48, 48,
                    False{}, 0, M.Running{}}, 3) ==
   M.S{0, 1, 0, True{}, 1, 0, 0, 0, 48, 48,
       False{}, 0, M.Running{}} : M.State}

law december_next_stays:
  {M.date_click(M.S{0, 12, 0, True{}, 1, 0, 0, 0, 48, 48,
                    False{}, 0, M.Running{}}, 4) ==
   M.S{0, 12, 0, True{}, 1, 0, 0, 0, 48, 48,
       False{}, 0, M.Running{}} : M.State}

law easy_previous_stays_in_december:
  {M.date_click(M.S{1, 12, 0, True{}, 336, 0, 0, 0, 48, 48,
                    False{}, 0, M.Running{}}, 3) ==
   M.S{1, 12, 0, True{}, 336, 0, 0, 0, 48, 48,
       False{}, 0, M.Running{}} : M.State}

law medium_previous_stays_in_november:
  {M.date_click(M.S{2, 11, 0, True{}, 306, 0, 0, 0, 48, 48,
                    False{}, 0, M.Running{}}, 3) ==
   M.S{2, 11, 0, True{}, 306, 0, 0, 0, 48, 48,
       False{}, 0, M.Running{}} : M.State}

law closed_day_ignored:
  {M.date_click(M.S{0, 12, 0, False{}, 336, 0, 0, 0, 48, 48,
                    False{}, 0, M.Running{}}, 5) ==
   M.S{0, 12, 0, False{}, 336, 0, 0, 0, 48, 48,
       False{}, 0, M.Running{}} : M.State}

law selection_requires_submit:
  {M.date_click(M.S{1, 12, 0, True{}, 336, 0, 0, 0, 48, 48,
                    False{}, 0, M.Running{}}, 5) ==
   M.S{1, 12, 336, False{}, 336, 0, 0, 0, 48, 48,
       False{}, 0, M.Running{}} : M.State}

law wrong_submission_fails:
  {M.date_click(M.S{1, 12, 337, False{}, 336, 0, 0, 0, 48, 48,
                    False{}, 0, M.Running{}}, 2) ==
   M.S{1, 12, 337, False{}, 336, 0, 0, 0, 48, 48,
       False{}, 0, M.Finished{False{}}} : M.State}

law daily_end_window_uses_end:
  {M.daily_window(0, 2, 14, 15) == True{} : Bool}

law daily_reverse_fails:
  {M.daily_window(0, 2, 20, 18) == False{} : Bool}

law daily_three_halfhours_possible:
  {M.daily_window(1, 3, 24, 26) == True{} : Bool}

law terminal_absorbs:
  for action: M.Action
  for elapsed: U32
  for deadline: U32
  for success: Bool
  {M.step(M.S{4, 0, 0, False{}, 0, 0, 2, 1, 16, 17,
              False{}, 0, M.Finished{success}}, action, elapsed, deadline) ==
   M.S{4, 0, 0, False{}, 0, 0, 2, 1, 16, 17,
       False{}, 0, M.Finished{success}} : M.State}

law timeout_absorbs:
  for action: M.Action
  for elapsed: U32
  for deadline: U32
  {M.step(M.S{0, 12, 0, False{}, 1, 0, 0, 0, 48, 48,
              False{}, 0, M.TimedOut{}}, action, elapsed, deadline) ==
   M.S{0, 12, 0, False{}, 1, 0, 0, 0, 48, 48,
       False{}, 0, M.TimedOut{}} : M.State}

law timeout_preempts_click:
  {M.step(M.S{0, 12, 0, False{}, 1, 0, 0, 0, 48, 48,
              False{}, 0, M.Running{}}, M.Click{1}, 20000, 20000) ==
   M.S{0, 12, 0, False{}, 1, 0, 0, 0, 48, 48,
       False{}, 0, M.TimedOut{}} : M.State}

law failure_unscaled:
  {M.timed(M.Finished{False{}}, 100, 20000) == (0.0 - 1.0 : F32) : F32}
