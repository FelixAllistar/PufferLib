import Base
import ./Model.bend as M

law finished_absorbs:
  for origin: U32
  for destination: U32
  for date: U32
  for goal_origin: U32
  for goal_destination: U32
  for goal_date: U32
  for shortest: Bool
  for flights: +List<M.Flight>
  for phase: Nat
  for errors: U32
  for fake: Bool
  for success: Bool
  for action: M.Action
  for elapsed: U32
  for deadline: U32
  {M.step(M.S{origin, destination, date, goal_origin, goal_destination,
    goal_date, shortest, flights, phase, errors, fake, M.Finished{success}},
    action, elapsed, deadline) ==
    M.S{origin, destination, date, goal_origin, goal_destination,
    goal_date, shortest, flights, phase, errors, fake, M.Finished{success}} : M.State}

law blank_search_marks_three_errors:
  {M.search(M.S{0, 0, 0, 1, 2, 1, False{}, Nil{}, 0n, 0,
    False{}, M.Running{}}) ==
    M.S{0, 0, 0, 1, 2, 1, False{}, Nil{}, 0n, 7,
    True{}, M.Running{}} : M.State}

law correct_search_clears_stale_errors:
  {M.search(M.S{1, 2, 1, 1, 2, 1, False{},
    [M.F{100, 20}], 0n, 7, True{}, M.Running{}}) ==
    M.S{1, 2, 1, 1, 2, 1, False{},
    [M.F{100, 20}], 1n, 0, False{}, M.Running{}} : M.State}

law valid_wrong_route_shows_fake_results:
  {M.search(M.S{3, 2, 1, 1, 2, 1, False{},
    [M.F{100, 20}], 0n, 0, False{}, M.Running{}}) ==
    M.S{3, 2, 1, 1, 2, 1, False{},
    [M.F{100, 20}], 1n, 0, True{}, M.Running{}} : M.State}

law fake_best_ranked_booking_fails:
  {M.book(M.S{1, 2, 1, 1, 2, 1, False{},
    [M.F{100, 20}, M.F{200, 30}], 1n, 0, True{}, M.Running{}}, 0n) ==
    M.S{1, 2, 1, 1, 2, 1, False{},
    [M.F{100, 20}, M.F{200, 30}], 1n, 0, True{},
    M.Finished{False{}}} : M.State}

law tied_cheapest_second_flight_succeeds:
  {M.book_success(False{}, False{},
    [M.F{100, 20}, M.F{100, 30}, M.F{200, 10}], 1n) ==
    True{} : Bool}

law exact_deadline_preempts_flight_booking:
  {M.step(M.S{1, 2, 1, 1, 2, 1, False{},
    [M.F{100, 20}], 1n, 0, False{}, M.Running{}},
    M.Book{0n}, 30000, 30000) ==
    M.S{1, 2, 1, 1, 2, 1, False{},
    [M.F{100, 20}], 1n, 0, False{}, M.TimedOut{}} : M.State}
