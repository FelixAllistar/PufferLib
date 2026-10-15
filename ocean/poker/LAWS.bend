import Base
import ./Model.bend as M

law add_successor:
  for +a: Nat
  for +b: Nat
  {Nat.add(a, 1n+b) == 1n+Nat.add(a, b) : Nat}

law transfer_conserves_all_chips:
  for +amount: Nat
  for +stack: Nat
  for +committed: Nat
  {M.balance(M.transfer(amount, stack, committed)) ==
   Nat.add(stack, committed) : Nat}

law terminal_absorption:
  for amount: U32
  for stack: U32
  for committed: U32
  {M.step(M.Finished{}, amount, M.C{stack, committed}) ==
   M.C{stack, committed} : M.Chips}

law hidden_card_independence:
  for a: U32
  for b: U32
  {M.visible(False{}, a) == M.visible(False{}, b) : U32}

law short_raise_preserves_minimum:
  for increment: U32
  for previous: U32
  {M.next_minimum(False{}, increment, previous) == previous : U32}

law full_raise_sets_minimum:
  for increment: U32
  for previous: U32
  {M.next_minimum(True{}, increment, previous) == increment : U32}

law short_allin_does_not_reopen:
  {M.raise_right(150, 200) == False{} : Bool}

law cumulative_short_allins_reopen:
  {M.raise_right(200, 200) == True{} : Bool}

law short_call_is_allin:
  {M.payment(37, 100) == 37 : U32}

law call_conserves_chips:
  {M.step(M.Playing{}, M.payment(37, 100), M.C{37, 150}) ==
   M.C{0, 187} : M.Chips}

law main_pot_includes_folded_money:
  {M.pot([50, 150, 200, 100], 0, 50) == 200 : U32}

law side_pot_excludes_short_stack:
  {M.pot([50, 150, 200, 100], 50, 150) == 250 : U32}

law uncalled_excess_is_single_contributor:
  {M.pot([50, 150, 200, 100], 150, 200) == 50 : U32}

law first_winner_gets_odd_chip:
  {M.share(101, 2, 0) == 51 : U32}

law second_winner_gets_even_share:
  {M.share(101, 2, 1) == 50 : U32}

law split_conserves_pot:
  {(M.share(101, 2, 0) + M.share(101, 2, 1) : U32) == 101 : U32}
