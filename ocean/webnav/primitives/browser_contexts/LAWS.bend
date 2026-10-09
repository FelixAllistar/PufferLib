import Base
import ../browser/Model.bend as B
import ./Model.bend as M

law switch_roundtrip_retains_browsers:
  for first: B.State
  for second: B.State
  {M.switch(M.switch(M.S{[M.T{1, first}, M.T{2, second}], 1, 3, 16, 4, 42}, 2), 1) == M.S{[M.T{1, first}, M.T{2, second}], 1, 3, 16, 4, 42} : M.State}

law missing_tab_preserves_state:
  for first: B.State
  {M.switch(M.S{[M.T{1, first}], 1, 2, 16, 4, 42}, 2) == M.S{[M.T{1, first}], 1, 2, 16, 4, 42} : M.State}

law background_load_advances:
  {M.step(M.S{[M.T{1, B.S{1, 4, 50, 0, [0], Nil{}, B.Loading{50}}}, M.T{2, B.S{2, 4, 75, 0, Nil{}, Nil{}, B.Loading{75}}}], 1, 3, 16, 4, 42}, M.Wait{}, 25) == M.S{[M.T{1, B.S{1, 4, 50, 0, [0], Nil{}, B.Loading{25}}}, M.T{2, B.S{2, 4, 75, 0, Nil{}, Nil{}, B.Loading{50}}}], 1, 3, 16, 4, 42} : M.State}

law navigation_changes_only_active_tab:
  for second: B.State
  {M.browser_action(M.S{[M.T{1, B.S{1, 4, 50, 0, [0], Nil{}, B.Ready{}}}, M.T{2, second}], 1, 3, 16, 4, 42}, B.Navigate{3}) == M.S{[M.T{1, B.S{3, 4, 50, 0, [1, 0], Nil{}, B.Loading{50}}}, M.T{2, second}], 1, 3, 16, 4, 42} : M.State}

law background_open_retains_current_tab:
  for first: B.State
  {M.open(M.S{[M.T{1, first}], 1, 2, 16, 4, 42}, 2, False{}) == M.S{[M.T{1, first}, M.fresh(42, 2, 4, 2)], 1, 3, 16, 4, 42} : M.State}

law full_capacity_rejects_open:
  for first: B.State
  {M.open(M.S{[M.T{1, first}], 1, 2, 1, 4, 42}, 2, True{}) == M.S{[M.T{1, first}], 1, 2, 1, 4, 42} : M.State}

law close_active_selects_last_survivor:
  for first: B.State
  for second: B.State
  for third: B.State
  {M.close(M.S{[M.T{1, first}, M.T{2, second}, M.T{3, third}], 1, 4, 16, 4, 42}, 1) == M.S{[M.T{2, second}, M.T{3, third}], 3, 4, 16, 4, 42} : M.State}

law close_background_retains_active:
  for first: B.State
  for second: B.State
  {M.close(M.S{[M.T{1, first}, M.T{2, second}], 1, 3, 16, 4, 42}, 2) == M.S{[M.T{1, first}], 1, 3, 16, 4, 42} : M.State}

law close_last_creates_fresh_blank:
  for first: B.State
  {M.close(M.S{[M.T{1, first}], 1, 2, 16, 4, 42}, 1) == M.S{[M.fresh(42, 2, 4, 0)], 2, 3, 16, 4, 42} : M.State}
