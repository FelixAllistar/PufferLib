import Base
import ./TicketModel.bend as T
import ./Model.bend as F

law finished_absorbs:
  for criterion: Nat
  for flights: +List<F.Flight>
  for grade: U32
  for action: T.Action
  for elapsed: U32
  for deadline: U32
  {T.step(T.S{criterion, flights, T.Finished{grade}}, action, elapsed, deadline) ==
    T.S{criterion, flights, T.Finished{grade}} : T.State}

law cheapest_middle_is_shaped:
  {T.rank(0n, [F.F{100, 10}, F.F{200, 20}, F.F{300, 30}, F.F{400, 40}], 1n) == 1 : U32}

law longest_best_succeeds:
  {T.rank(2n, [F.F{100, 10}, F.F{200, 20}, F.F{300, 30}, F.F{400, 40}], 3n) == 0 : U32}

law exact_deadline_preempts_ticket_purchase:
  {T.step(T.S{0n,
    [F.F{100, 10}, F.F{200, 20}, F.F{300, 30}, F.F{400, 40}],
    T.Running{}}, T.Buy{0n}, 10000, 10000) ==
    T.S{0n,
    [F.F{100, 10}, F.F{200, 20}, F.F{300, 30}, F.F{400, 40}],
    T.TimedOut{}} : T.State}
