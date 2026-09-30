import Base
import ./Model.bend as M
import ./TimeModel.bend as T
import ./UnicodeModel.bend as U

law leap_day_normalizes:
  {M.parse([50, 48, 50, 52, 45, 48, 50, 45, 50, 57]) ==
   M.Date{2024, 2, 29} : M.Value}

law ordinary_february_rejects_leap_day:
  {M.parse([50, 48, 50, 51, 45, 48, 50, 45, 50, 57]) ==
   M.Empty{} : M.Value}

law submit_compares_normalized_value:
  {M.step(M.S{2024, 2, 29, M.Date{2024, 2, 29}, M.Running{}},
          M.Submit{}, 100, 20000) ==
   M.S{2024, 2, 29, M.Date{2024, 2, 29}, M.Finished{True{}}} : M.State}

law deadline_preempts_submit:
  {M.step(M.S{2024, 2, 29, M.Date{2024, 2, 29}, M.Running{}},
          M.Submit{}, 20000, 20000) ==
   M.S{2024, 2, 29, M.Date{2024, 2, 29}, M.TimedOut{}} : M.State}

law terminal_absorbs:
  for year: U32
  for month: U32
  for day: U32
  for value: M.Value
  for success: Bool
  for action: M.Action
  for elapsed: U32
  {M.step(M.S{year, month, day, value, M.Finished{success}},
          action, elapsed, 20000) ==
   M.S{year, month, day, value, M.Finished{success}} : M.State}

law time_midnight_normalizes:
  {T.parse([48, 48, 58, 48, 48]) == T.Time{0, 0} : T.Value}

law time_rejects_twenty_four:
  {T.parse([50, 52, 58, 48, 48]) == T.Empty{} : T.Value}

law time_submit_exact_value:
  {T.step(T.S{23, 59, T.Time{23, 59}, T.Running{}},
          T.Submit{}, 100, 20000) ==
   T.S{23, 59, T.Time{23, 59}, T.Finished{True{}}} : T.State}

law unicode_duplicate_label_succeeds:
  {U.step(U.S{3, U.Running{}}, U.Click{3}, 100, 10000) ==
   U.S{3, U.Finished{True{}}} : U.State}

law unicode_wrong_label_fails:
  {U.step(U.S{3, U.Running{}}, U.Click{4}, 100, 10000) ==
   U.S{3, U.Finished{False{}}} : U.State}

law unicode_deadline_preempts_click:
  {U.step(U.S{3, U.Running{}}, U.Click{3}, 10000, 10000) ==
   U.S{3, U.TimedOut{}} : U.State}
