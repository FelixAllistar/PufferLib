import Base
import ./Model.bend as M
import ./Policy.bend as P

law win_precedes_opponent:
  {M.step(M.S{3, 24, 17, M.Running{}}, M.Click{2}, 100, 10000) ==
   M.S{7, 24, 17, M.Win{}} : M.State}

law marked_cell_is_inert:
  {M.step(M.S{1, 8, 17, M.Running{}}, M.Click{0}, 100, 10000) ==
   M.S{1, 8, 17, M.Running{}} : M.State}

law deadline_preempts_move:
  {M.step(M.S{1, 8, 17, M.Running{}}, M.Click{2}, 10000, 10000) ==
   M.S{1, 8, 17, M.TimedOut{}} : M.State}

law diagonal_is_win:
  {M.winner(273) == True{} : Bool}

law smart_helper_uses_last_direction:
  {P.directions(323, 323) == 3 : U32}

law smart_move_wins_before_blocking:
  {P.determine(27, 3, 24) == 2 : U32}

law player_opens_after_low_start_draw:
  {M.opening(2) == M.S{0, 0, 165, M.Running{}} : M.State}

law computer_opening_consumes_three_draws:
  {M.opening(70) == M.S{0, 8, 3847, M.Running{}} : M.State}
