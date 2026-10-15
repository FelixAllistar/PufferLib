import Base
import ./LAWS.bend as L
import ./Model.bend as M

def L.add_successor(a, b):
  match a:
    case 0n: {==}
    case 1n+ +p:
      Equal.cong(Nat, Nat, x => 1n+x, Nat.add(p, 1n+b),
        1n+Nat.add(p, b), L.add_successor(p, b))

def L.transfer_conserves_all_chips(amount, stack, committed):
  match amount stack:
    case 0n s: {==}
    case 1n+a 0n: {==}
    case 1n+ +a 1n+ +s:
      Equal.trans(Nat, M.balance(M.transfer(a, s, 1n+committed)),
        Nat.add(s, 1n+committed), 1n+Nat.add(s, committed),
        L.transfer_conserves_all_chips(a, s, 1n+committed),
        L.add_successor(s, committed))

def L.terminal_absorption(amount, stack, committed):
  {==}

def L.hidden_card_independence(a, b):
  {==}

def L.short_raise_preserves_minimum(increment, previous):
  {==}

def L.full_raise_sets_minimum(increment, previous):
  {==}

def L.short_allin_does_not_reopen():
  {==}

def L.cumulative_short_allins_reopen():
  {==}

def L.short_call_is_allin():
  {==}

def L.call_conserves_chips():
  {==}

def L.main_pot_includes_folded_money():
  {==}

def L.side_pot_excludes_short_stack():
  {==}

def L.uncalled_excess_is_single_contributor():
  {==}

def L.first_winner_gets_odd_chip():
  {==}

def L.second_winner_gets_even_share():
  {==}

def L.split_conserves_pot():
  {==}
