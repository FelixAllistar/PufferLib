import Base
import ./Policy.bend as P

# Cells are bit positions 0..8. The LCG supplies deterministic draws to both
# this model and the original page's randi/sample calls in the browser oracle.
type Outcome is Data:
  Running{}
  Win{}
  Draw{}
  Loss{}
  TimedOut{}

type State is Data:
  S{xmask: U32, omask: U32, random: U32, outcome: Outcome}

type Action is Data:
  Wait{}
  Click{index: U32}

def bit(index: U32) -> U32:
  (1 << U32.to_nat(index) : U32)

def occupied(xmask: U32, omask: U32) -> U32:
  (xmask .|. omask : U32)

def free(mask: U32, index: U32) -> Bool:
  U32.is_eq((mask .&. bit(index) : U32), 0)

def line(mask: U32, +cells: U32) -> Bool:
  U32.is_eq((mask .&. cells : U32), cells)

def winner(+mask: U32) -> Bool:
  line(mask, 7) || line(mask, 56) || line(mask, 448) ||
  line(mask, 73) || line(mask, 146) || line(mask, 292) ||
  line(mask, 273) || line(mask, 84)

def full(mask: U32) -> Bool:
  U32.is_eq(mask, 511)

def next_random(random: U32) -> U32:
  ((random * 73 + 19 : U32) % 10000 : U32)

def free_count(n: Nat, +mask: U32) -> U32:
  match n:
    case 0n: 0
    case 1n+ +p:
      (Bool.to_u32(free(mask, U32.from_nat(p))) + free_count(p, mask) : U32)

def choose(n: Nat, +mask: U32, +rank: U32, +index: U32) -> U32:
  match n:
    case 0n: 0
    case 1n+ +p:
      +is_free = free(mask, index)
      +found = is_free && U32.is_eq(rank, 0)
      +next_rank = Bool.pick(U32, is_free, (rank - 1 : U32), rank)
      Bool.pick(U32, found, index,
        choose(p, mask, next_rank, (index + 1 : U32)))

def finish_opponent(+xmask: U32, +omask: U32, random: U32,
                    index: U32) -> State:
  +after = (omask .|. bit(index) : U32)
  S{xmask, after, random,
    Bool.pick(Outcome, winner(after), Loss{},
      Bool.pick(Outcome, full(occupied(xmask, after)), Draw{}, Running{}))}

def random_reply(+xmask: U32, +omask: U32, chance: U32) -> State:
  +sample = next_random(chance)
  +before = occupied(xmask, omask)
  +count = free_count(9n, before)
  +rank = ((sample * count : U32) / 10000 : U32)
  +index = choose(9n, before, rank, 0)
  finish_opponent(xmask, omask, sample, index)

def smart_choice(smart: Bool, +xmask: U32, +omask: U32,
                 chance: U32, index: U32) -> State:
  match smart:
    case True{}: finish_opponent(xmask, omask, chance, index)
    case False{}: random_reply(xmask, omask, chance)

def smart_reply(+xmask: U32, +omask: U32, chance: U32) -> State:
  +index = P.determine(occupied(xmask, omask), omask, xmask)
  smart_choice(U32.is_lt(index, 9), xmask, omask, chance, index)

def opponent_mode(random_branch: Bool, xmask: U32, omask: U32,
                  chance: U32) -> State:
  match random_branch:
    case True{}: random_reply(xmask, omask, chance)
    case False{}: smart_reply(xmask, omask, chance)

def opponent(+xmask: U32, +omask: U32, random: U32) -> State:
  +chance = next_random(random)
  opponent_mode(U32.is_lt((chance / 100 : U32), 55) ||
    U32.is_eq(free_count(9n, occupied(xmask, omask)), 9),
    xmask, omask, chance)

def opening(+seed: U32) -> State:
  +first = next_random((seed % 10000 : U32))
  Bool.pick(State, U32.is_gt((first / 100 : U32), 50),
    opponent(0, 0, first), S{0, 0, first, Running{}})

def after_player(+xmask: U32, +omask: U32, +random: U32,
                 index: U32) -> State:
  +placed = (xmask .|. bit(index) : U32)
  Bool.pick(State, winner(placed), S{placed, omask, random, Win{}},
    Bool.pick(State, full(occupied(placed, omask)),
      S{placed, omask, random, Draw{}}, opponent(placed, omask, random)))

def click_free(valid: Bool, s: State, index: U32) -> State:
  match valid:
    case False{}: s
    case True{}:
      S{xmask, omask, random, outcome} = s
      after_player(xmask, omask, random, index)

def click_valid(valid: Bool, s: State, +index: U32) -> State:
  match valid:
    case False{}: s
    case True{}:
      S{+xmask, +omask, random, outcome} = s
      click_free(free(occupied(xmask, omask), index),
        S{xmask, omask, random, Running{}}, index)

def click(s: State, +index: U32) -> State:
  click_valid(U32.is_lt(index, 9), s, index)

def active(s: State, action: Action) -> State:
  match action:
    case Wait{}: s
    case Click{index}: click(s, index)

def guard(expired: Bool, s: State, action: Action) -> State:
  match expired:
    case True{}:
      S{xmask, omask, random, outcome} = s
      S{xmask, omask, random, TimedOut{}}
    case False{}: active(s, action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{xmask, omask, random, Running{}}:
      guard(U32.is_ge(elapsed, deadline),
        S{xmask, omask, random, Running{}}, action)
    case S{xmask, omask, random, Win{}}: S{xmask, omask, random, Win{}}
    case S{xmask, omask, random, Draw{}}: S{xmask, omask, random, Draw{}}
    case S{xmask, omask, random, Loss{}}: S{xmask, omask, random, Loss{}}
    case S{xmask, omask, random, TimedOut{}}: S{xmask, omask, random, TimedOut{}}

def raw(outcome: Outcome) -> F32:
  match outcome:
    case Running{}: 0.0
    case Win{}: 1.0
    case Draw{}: (0.0 - 0.5 : F32)
    case Loss{}: (0.0 - 0.75 : F32)
    case TimedOut{}: (0.0 - 1.0 : F32)

def timed(outcome: Outcome, elapsed: U32, deadline: U32) -> F32:
  match outcome:
    case Running{}: 0.0
    case Win{}: (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32)
    case Draw{}: (0.0 - 0.5 : F32)
    case Loss{}: (0.0 - 0.75 : F32)
    case TimedOut{}: (0.0 - 1.0 : F32)
