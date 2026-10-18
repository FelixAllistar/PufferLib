"""Independent PokerKit differential checks; never imported by training."""
import ctypes as C
import random
import subprocess
from pokerkit import Automation, Card, Mode, NoLimitTexasHoldem, StandardHighHand
from eval import Native, text_card


def fixtures():
    native = Native(6)
    # Short all-in preserves raise rights; two shorts cumulatively reopen.
    for bb, expected in ((1000, False), (200, True)):
        native.reset([150, bb, 1000, 1000, 1000, 1000], list(range(17)))
        for action in (0, 0, 0, 0, 150, 200 if expected else 0):
            native.step(action)
        assert native.snapshot()[0][0] == 2
        assert any(a > 0 for a in native.actions()) == expected
    values = [int(line) for line in subprocess.check_output(
        ["build/poker/fixtures"], text=True).splitlines()]
    assert values == [200, 250, 50, 51, 50]
    # Independent Bend layers versus actual all-in settlement in the native game.
    deck = [51, 47, 50, 49, 46, 45, 26, 21, 8, 9, 12, 13, 43, 39, 35, 0, 5]
    native.reset([50, 150, 200, 100, 1000, 1000], deck)
    for action in (200, 0, -1, -1, 0):
        native.step(action)
    meta, rows, _ = native.snapshot()
    assert meta[0] == 255
    assert [r[0] for r in rows] == values[:3] + [0, 1000, 1000]
    # Board makes the tied best hand; the third player contributes one odd chip.
    deck = [50, 45, 16, 17, 18, 19, 8, 9, 12, 13, 20, 21, 0, 5, 10, 15, 28]
    native.reset([1, 50, 50, 1000, 1000, 1000], deck)
    for action in (0, -1, -1, -1):
        native.step(action)
    assert [r[0] for r in native.snapshot()[1]] == [0] + values[3:] + [1000] * 3
    hu = Native(2)
    hu.reset([50, 20000], list(range(9)))
    assert hu.snapshot()[0][0] == 255
    assert sum(r[0] for r in hu.snapshot()[1]) == 20050
    print("PASS Bend/native side pots, short-all-in reopening, short blind", flush=True)


def requests():
    # Compaction must retain table+seat identity and every terminal payoff.
    native = Native(6)
    lib = native.lib
    lib.poker_requests.argtypes = [C.c_void_p, C.c_int, C.POINTER(C.c_uint16),
        C.POINTER(C.c_uint8), C.POINTER(C.c_float), C.POINTER(C.c_uint8),
        C.POINTER(C.c_int32)]
    lib.poker_respond.argtypes = [C.c_void_p, C.c_int, C.POINTER(C.c_uint16),
        C.POINTER(C.c_int32), C.POINTER(C.c_uint8), C.POINTER(C.c_float)]
    obs_size, tables = 106, 64
    ids, seats = (C.c_uint16 * tables)(), (C.c_uint8 * tables)()
    obs, masks = (C.c_float * (tables * obs_size))(), (C.c_uint8 * (tables * 8))()
    amounts, actions = (C.c_int32 * (tables * 8))(), (C.c_int32 * tables)()
    terminals, rewards = (C.c_uint8 * tables)(), (C.c_float * (6 * tables))()
    rng = random.Random(13)
    pending = {}
    finished = set()
    for t in range(tables):
        deck = (C.c_uint8 * 17)(*rng.sample(range(52), 17))
        stacks = (C.c_uint32 * 6)(*[3000 + 100 * t] * 6)
        lib.poker_reset(native.storage, t, t % 6, stacks, deck)
    sequence = 0
    while True:
        count = lib.poker_requests(native.storage, tables, ids, seats, obs, masks, amounts)
        if not count:
            break
        for i in range(count):
            key = (ids[i], seats[i])
            assert ids[i] not in finished
            assert obs[i * obs_size + 13] == 1
            # A previous transition can only close on this SAME seat's request.
            if key in pending:
                assert pending[key] < sequence
            pending[key] = sequence
            legal = [a for a in range(8) if masks[8 * i + a]]
            actions[i] = amounts[8 * i + rng.choice(legal)]
            sequence += 1
        lib.poker_respond(native.storage, count, ids, actions, terminals, rewards)
        for i in range(count):
            if terminals[i]:
                assert abs(sum(rewards[p * count + i] for p in range(6))) < 0.001
                finished.add(ids[i])
                for p in range(6):
                    pending.pop((ids[i], p), None)
            else:
                assert all(rewards[p * count + i] == 0 for p in range(6))
    assert len(finished) == tables and not pending
    print(f"PASS acting-seat batching tables={tables} decisions={sequence}", flush=True)


def verify(players, hands):
    rng = random.Random(991 + players)
    native = Native(players)
    automations = tuple(a for a in Automation if a not in
                        (Automation.HOLE_DEALING, Automation.BOARD_DEALING,
                         Automation.CARD_BURNING, Automation.CHIPS_PUSHING,
                         Automation.HAND_KILLING))
    decisions = 0
    oracle_disagreements = 0
    for hand in range(hands):
        deck = rng.sample(range(52), 52)
        # PokerKit prices a short posted BB at its actual amount; our game keeps
        # the nominal BB. Short posted blinds have separate expected-outcome tests.
        stacks = [rng.choice([100, 150, 199, 3000, 10000, 20000])
                  for _ in range(players)] if hand % 4 == 0 else [20000] * players
        state = NoLimitTexasHoldem.create_state(
            automations, True, 0, (50, 100), 100, stacks, players, mode=Mode.CASH_GAME)
        for seat in range(players):
            state.deal_hole("".join(text_card(c) for c in deck[seat * 2:seat * 2 + 2]))
        native.reset(stacks, deck)
        board_count = 0
        history = []
        while True:
            while state.can_burn_card():
                state.burn_card("??")
            while state.can_deal_board():
                count = 3 if board_count == 0 else 1
                cards = deck[2 * players + board_count:2 * players + board_count + count]
                state.deal_board("".join(map(text_card, cards)))
                board_count += count
                while state.can_burn_card():
                    state.burn_card("??")
            if (state.actor_index is not None and state.checking_or_calling_amount == 0
                    and sum(live and stack > 0 for live, stack in
                            zip(state.statuses, state.stacks)) == 1):
                state.check_or_call()  # Native kernel skips this forced BB check.
                continue
            meta, rows, _ = native.snapshot()
            context = (players, hand, stacks, history, meta, state.actor_index,
                       state.bets, state.stacks)
            ending = state.can_kill_hand() or state.can_push_chips() or not state.status
            assert (meta[0] == 255) == ending, context
            if ending:
                # PokerKit awards the entire remainder to the first winner.
                # Our explicit rule awards one chip each clockwise. Use its
                # independently constructed pots BEFORE mucking losers (which
                # otherwise merges distinct side pots), and hand rankings, then apply
                # our declared odd-chip convention without changing either game.
                expected = [a + b for a, b in zip(state.stacks, state.bets)]
                board = "".join(map(text_card, deck[2 * players:2 * players + 5]))
                for pot in state.pots:
                    eligible = list(pot.player_indices)
                    strength = {p: StandardHighHand.from_game(
                        "".join(map(text_card, deck[2 * p:2 * p + 2])), board)
                        for p in eligible}
                    best = max(strength.values())
                    winners = [p for p in eligible if strength[p] == best]
                    share, odd = divmod(pot.unraked_amount, len(winners))
                    for i, p in enumerate(winners):
                        expected[p] += share + (i < odd)
                assert [row[0] for row in rows] == expected, (
                    context, [row[0] for row in rows], expected, deck[:2 * players + 5])
                break
            assert meta[0] == state.actor_index, context
            assert [row[0] for row in rows] == state.stacks, context
            assert [row[3] for row in rows] == state.bets, context
            assert meta[2] == state.total_pot_amount, context
            actions = native.actions()
            minimum = state.min_completion_betting_or_raising_to_amount
            raises = [a for a in actions if a > 0]
            if bool(raises) != (minimum is not None):
                # PokerKit 0.7.7 initializes the last full raise to zero, so a
                # first underraise can incorrectly reopen previous callers.
                # This disagreement is reported, with native rights checked by
                # separate explicit short-all-in fixtures and Bend laws.
                assert not raises and minimum is not None, (context, raises, minimum)
                assert state.completion_betting_or_raising_amount < 100, context
                assert meta[3] < rows[meta[0]][4], context
                oracle_disagreements += 1
            if raises:
                assert min(raises) == minimum, (context, raises, minimum)
                for amount in raises:
                    assert state.can_complete_bet_or_raise_to(amount), (context, amount)
            if hand % 3 == 0:
                action = native.bot(1)
            else:
                action = rng.choices(actions, [1 if a else 5 for a in actions])[0]
                if action > 0 and rng.randrange(3) == 0:
                    action = rng.randint(minimum, state.max_completion_betting_or_raising_to_amount)
            history.append(action)
            if action < 0:
                state.fold()
            elif action:
                state.complete_bet_or_raise_to(action)
            else:
                state.check_or_call()
            native.step(action)
            decisions += 1
            assert len(history) < 4096
    print(f"PASS shared-action PokerKit players={players} hands={hands} "
          f"decisions={decisions} oracle_raise_disagreements={oracle_disagreements}", flush=True)


def ranks(samples=20000):
    rng = random.Random(812)
    native = Native()
    ranks_seen = {}
    for _ in range(samples):
        count = rng.choice((5, 6, 7))
        cards = rng.sample(range(52), count)
        rank = native.lib.poker_rank((C.c_uint8 * count)(*cards), count)
        expected = StandardHighHand.from_game("".join(map(text_card, cards)))
        reference = expected.entry.index
        if rank in ranks_seen:
            assert reference == ranks_seen[rank]
        ranks_seen[rank] = reference
        rng.shuffle(cards)
        assert native.lib.poker_rank((C.c_uint8 * count)(*cards), count) == rank
        mapping = rng.sample(range(4), 4)
        cards = [(c & ~3) | mapping[c & 3] for c in cards]
        assert native.lib.poker_rank((C.c_uint8 * count)(*cards), count) == rank
    pairs = sorted(ranks_seen.items())
    assert all(a[1] < b[1] for a, b in zip(pairs, pairs[1:]))
    print(f"PASS exact ranks/order/suit symmetry samples={samples}", flush=True)


if __name__ == "__main__":
    fixtures()
    requests()
    ranks()
    for count in (2, 6, 9):
        verify(count, 1000)
