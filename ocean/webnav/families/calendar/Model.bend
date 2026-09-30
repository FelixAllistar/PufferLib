import Base

# The four datepicker tasks and the separate daily drag task share a deadline,
# but retain different controls and completion rules.
type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{task: U32, month: U32, selected: U32, popup: Bool, target: U32,
    window: U32, duration: U32, name: U32, start: U32, end: U32,
    modal: Bool, scroll: U32, outcome: Outcome}

type Action is Data:
  Wait{}
  Click{ref: U32}
  Down{ref: U32}
  Move{ref: U32}
  Up{ref: U32}
  Create{name_matches: Bool}
  Scroll{halves: U32}

def days_kind(month: Nat) -> U32:
  match month:
    case 0n: 0
    case 2n: 29
    case 4n: 30
    case 6n: 30
    case 9n: 30
    case 11n: 30
    case _: 31

def days(month: U32) -> U32:
  days_kind(U32.to_nat(month))

def first_month_kind(task: Nat) -> U32:
  match task:
    case 1n: 12
    case 2n: 11
    case _: 1

def first_month(task: U32) -> U32:
  first_month_kind(U32.to_nat(task))

def ordinal_before(month: Nat) -> U32:
  match month:
    case 0n: 0
    case 1n+ +p: (days(U32.from_nat(p)) + ordinal_before(p) : U32)

def valid_day(+month: U32, +day: U32) -> Bool:
  U32.is_ge(month, 1) && U32.is_le(month, 12) &&
  U32.is_ge(day, 1) && U32.is_le(day, days(month))

def day_ordinal(month: U32, day: U32) -> U32:
  (ordinal_before(U32.to_nat(month)) + day : U32)

def window_start(window: U32) -> U32:
  (16 + window * 8 : U32)

def window_end(window: U32) -> U32:
  (24 + window * 8 : U32)

# The original correctEventWindow checks the event's end against both bounds;
# it never checks that its start is within the requested interval.
def daily_window(+window: U32, duration: U32, +start: U32, +end: U32) -> Bool:
  +created_end = (end + 1 : U32)
  U32.is_ge(end, start) && U32.is_eq((created_end - start : U32), duration) &&
  U32.is_ge(created_end, window_start(window)) &&
  U32.is_le(created_end, window_end(window))

def finish(s: State, success: Bool) -> State:
  S{task, month, selected, popup, target, window, duration, name,
    start, end, modal, scroll, outcome} = s
  S{task, month, selected, popup, target, window, duration, name,
    start, end, modal, scroll, Finished{success}}

def timeout(s: State) -> State:
  S{task, month, selected, popup, target, window, duration, name,
    start, end, modal, scroll, outcome} = s
  S{task, month, selected, popup, target, window, duration, name,
    start, end, modal, scroll, TimedOut{}}

def date_day(can: Bool, +s: State, +task: U32, +month: U32,
             +selected: U32, +target: U32, +window: U32,
             +duration: U32, +name: U32, +start: U32, +end: U32,
             +modal: Bool, +scroll: U32, +outcome: Outcome, day: U32) -> State:
  match can:
    case False{}: s
    case True{}: S{task, month, day_ordinal(month, day), False{}, target,
                    window, duration, name, start, end, modal, scroll, outcome}

def date_click_kind(ref_kind: Nat, +s: State, +ref: U32, +task: U32,
                    +month: U32, +selected: U32, +popup: Bool,
                    +target: U32, +window: U32, +duration: U32,
                    +name: U32, +start: U32, +end: U32,
                    +modal: Bool, +scroll: U32, +outcome: Outcome) -> State:
  match ref_kind:
    case 1n: S{task, month, selected, True{}, target, window, duration, name,
               start, end, modal, scroll, outcome}
    case 2n: finish(s, U32.is_eq(selected, target) && U32.is_gt(selected, 0))
    case 3n:
      +next = Bool.pick(U32, popup && U32.is_gt(month, first_month(task)), (month - 1 : U32), month)
      S{task, next, selected, popup, target, window, duration, name,
        start, end, modal, scroll, outcome}
    case 4n:
      +next = Bool.pick(U32, popup && U32.is_lt(month, 12), (month + 1 : U32), month)
      S{task, next, selected, popup, target, window, duration, name,
        start, end, modal, scroll, outcome}
    case _:
      +day = (ref - 4 : U32)
      date_day(popup && U32.is_ge(ref, 5) && U32.is_le(ref, 35) &&
               valid_day(month, day), s, task, month, selected, target,
               window, duration, name, start, end, modal, scroll, outcome, day)

def date_click(+s: State, +ref: U32) -> State:
  S{+task, +month, +selected, +popup, +target, +window, +duration, +name,
    +start, +end, +modal, +scroll, +outcome} = s
  date_click_kind(U32.to_nat(ref), s, ref, task, month, selected, popup,
                  target, window, duration, name, start, end, modal, scroll, outcome)

def daily_click_kind(ref: Nat, +s: State, +task: U32, +month: U32,
                     +selected: U32, +popup: Bool, +target: U32,
                     +window: U32, +duration: U32, +name: U32,
                     +scroll: U32, +outcome: Outcome) -> State:
  match ref:
    case 54n: S{task, month, selected, popup, target, window, duration, name,
                48, 48, False{}, scroll, outcome}
    case _: s

def daily_click(+s: State, ref: U32) -> State:
  S{+task, +month, +selected, +popup, +target, +window, +duration, +name,
    start, end, modal, +scroll, +outcome} = s
  daily_click_kind(U32.to_nat(ref), s, task, month, selected, popup,
                   target, window, duration, name, scroll, outcome)

def daily_down_valid(can: Bool, +s: State, +ref: U32, +task: U32,
                     +month: U32, +selected: U32, +popup: Bool,
                     +target: U32, +window: U32, +duration: U32,
                     +name: U32, +end: U32, +modal: Bool,
                     +scroll: U32, +outcome: Outcome) -> State:
  match can:
    case False{}: s
    case True{}: S{task, month, selected, popup, target, window, duration,
                    name, (ref - 1 : U32), 48, modal, scroll, outcome}

def daily_down(+s: State, +ref: U32) -> State:
  S{+task, +month, +selected, +popup, +target, +window, +duration, +name,
    +start, +end, +modal, +scroll, +outcome} = s
  daily_down_valid(U32.is_ge(ref, 1) && U32.is_le(ref, 48) &&
                   U32.is_eq(start, 48) && Bool.not(modal), s, ref,
                   task, month, selected, popup, target, window, duration,
                   name, end, modal, scroll, outcome)

def daily_move_valid(can: Bool, +s: State, +ref: U32, +task: U32,
                     +month: U32, +selected: U32, +popup: Bool,
                     +target: U32, +window: U32, +duration: U32,
                     +name: U32, +start: U32, +modal: Bool,
                     +scroll: U32, +outcome: Outcome) -> State:
  match can:
    case False{}: s
    case True{}: S{task, month, selected, popup, target, window, duration,
                    name, start, (ref - 1 : U32), modal, scroll, outcome}

def daily_move(+s: State, +ref: U32) -> State:
  S{+task, +month, +selected, +popup, +target, +window, +duration, +name,
    +start, end, +modal, +scroll, +outcome} = s
  daily_move_valid(U32.is_lt(start, 48) && Bool.not(modal) &&
                   U32.is_ge(ref, 1) && U32.is_le(ref, 48),
                   s, ref, task, month, selected, popup, target, window,
                   duration, name, start, modal, scroll, outcome)

def daily_up_valid(can: Bool, +s: State, +task: U32, +month: U32,
                   +selected: U32, +popup: Bool, +target: U32,
                   +window: U32, +duration: U32, +name: U32,
                   +start: U32, +end: U32, +scroll: U32,
                   +outcome: Outcome) -> State:
  match can:
    case False{}: s
    case True{}: S{task, month, selected, popup, target, window, duration,
                    name, start, end, True{}, scroll, outcome}

def daily_up(+s: State, ref: U32) -> State:
  S{+task, +month, +selected, +popup, +target, +window, +duration, +name,
    +start, +end, modal, +scroll, +outcome} = s
  daily_up_valid(U32.is_eq(ref, 52) && U32.is_lt(start, 48) &&
                 U32.is_lt(end, 48) &&
                 Bool.not(U32.is_eq((end + 1 : U32), start)),
                 s, task, month, selected, popup, target, window, duration,
                 name, start, end, scroll, outcome)

def daily_create(+s: State, name_matches: Bool) -> State:
  S{+task, +month, +selected, +popup, +target, +window, +duration, +name,
    +start, +end, modal, +scroll, +outcome} = s
  match modal:
    case False{}: s
    case True{}: finish(S{task, month, selected, popup, target, window, duration,
                          name, start, end, False{}, scroll, outcome},
                         name_matches && daily_window(window, duration, start, end))

def daily_scroll(s: State, +halves: U32) -> State:
  S{task, month, selected, popup, target, window, duration, name,
    start, end, modal, scroll, outcome} = s
  S{task, month, selected, popup, target, window, duration, name,
    start, end, modal, Bool.pick(U32, U32.is_le(halves, 40), halves, 40), outcome}

def click_task(task: Nat, s: State, ref: U32) -> State:
  match task:
    case 4n: daily_click(s, ref)
    case _: date_click(s, ref)

def act(+s: State, action: Action) -> State:
  S{+task, month, selected, popup, target, window, duration, name,
    start, end, modal, scroll, outcome} = s
  match action:
    case Wait{}: s
    case Click{ref}: click_task(U32.to_nat(task), s, ref)
    case Down{ref}: daily_down(s, ref)
    case Move{ref}: daily_move(s, ref)
    case Up{ref}: daily_up(s, ref)
    case Create{name_matches}: daily_create(s, name_matches)
    case Scroll{halves}: daily_scroll(s, halves)

def guarded(expired: Bool, +s: State, action: Action) -> State:
  match expired:
    case True{}: timeout(s)
    case False{}: act(s, action)

def step(+s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{task, month, selected, popup, target, window, duration, name,
           start, end, modal, scroll, Running{}}:
      guarded(U32.is_ge(elapsed, deadline),
              S{task, month, selected, popup, target, window, duration, name,
                start, end, modal, scroll, Running{}}, action)
    case S{task, month, selected, popup, target, window, duration, name,
           start, end, modal, scroll, outcome}:
      S{task, month, selected, popup, target, window, duration, name,
        start, end, modal, scroll, outcome}

def raw(outcome: Outcome) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{True{}}: 1.0
    case _: (0.0 - 1.0 : F32)

def timed(outcome: Outcome, elapsed: U32, deadline: U32) -> F32:
  match outcome:
    case Finished{True{}}: (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32)
    case Running{}: 0.0
    case Finished{False{}}: (0.0 - 1.0 : F32)
    case TimedOut{}: (0.0 - 1.0 : F32)
