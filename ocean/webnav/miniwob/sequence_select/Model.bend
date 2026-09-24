import Base

# Independent pure models for the two first-wave tasks in this directory.
#
# The models deliberately consume the event that the browser observed.  For
# click-button-sequence the event is represented by a page coordinate and
# `hit_at` applies the DOM order (subbtn2 is later in the document and is
# therefore on top at an overlap).  For choose-list the high-level action is
# a selected option index; submit compares the visible option text, exactly as
# the original handler does.

type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

def outcome_status(o: Outcome) -> U32:
  match o:
    case Running{}: 0
    case Finished{success}: 1
    case TimedOut{}: 2

def outcome_success(o: Outcome) -> Bool:
  match o:
    case Running{}: False{}
    case Finished{success}: success
    case TimedOut{}: False{}

def raw_reward(o: Outcome) -> F32:
  match o:
    case Running{}: 0.0
    case Finished{success}: Bool.pick(F32, success, 1.0, (0.0 - 1.0 : F32))
    case TimedOut{}: (0.0 - 1.0 : F32)

def timed_reward(o: Outcome, elapsed: U32) -> F32:
  match o:
    case Running{}: 0.0
    case Finished{success}:
      Bool.pick(F32, success,
        F32.max(0.0, (1.0 - U32.to_f32(elapsed) / 10000.0 : F32)),
        (0.0 - 1.0 : F32))
    case TimedOut{}: (0.0 - 1.0 : F32)

def terminal(success: Bool) -> Outcome:
  Finished{success}

def timeout_outcome() -> Outcome:
  TimedOut{}

# ---------------------------------------------------------------------------
# click-button-sequence

type Rect is Data:
  R{left: U32, top: U32}

type Hit is Data:
  Miss{}
  One{}
  Two{}

type SequenceAction is Data:
  Wait{}
  Click{x: U32, y: U32}
  Timeout{}
  Reset{}

type SequenceState is Data:
  S{one: Rect, two: Rect, clicks: U32, first: Hit, outcome: Outcome}

def hit_code(hit: Hit) -> U32:
  match hit:
    case Miss{}: 0
    case One{}: 1
    case Two{}: 2

def code_hit(code: U32) -> Hit:
  match code:
    case 1: One{}
    case 2: Two{}
    case _: Miss{}

def inside_y(+top: U32, +y: U32) -> Bool:
  U32.is_ge(y, top) && U32.is_lt(y, (top + 40 : U32))

def inside_x(+left: U32, +x: U32) -> Bool:
  U32.is_ge(x, left) && U32.is_lt(x, (left + 40 : U32))

def inside(rect: Rect, +x: U32, +y: U32) -> Bool:
  match rect:
    case R{left, top}: inside_x(left, x) && inside_y(top, y)

# The second button is later in the HTML and is the hit target when the
# rectangles overlap.  This is the DOM event target, not the caller's intent.
def hit_flags(in_two: Bool, in_one: Bool) -> Hit:
  match in_two in_one:
    case True{} _: Two{}
    case False{} True{}: One{}
    case False{} False{}: Miss{}

def hit_at(one: Rect, two: Rect, +x: U32, +y: U32) -> Hit:
  hit_flags(inside(two, x, y), inside(one, x, y))

def first_correct(first: Hit, second: Hit) -> Bool:
  match first second:
    case One{} Two{}: True{}
    case _ _: False{}

def sequence_reset(s: SequenceState) -> SequenceState:
  match s:
    case S{one, two, clicks, first, outcome}: S{one, two, 0, Miss{}, Running{}}

def sequence_record_count(zero: Bool, one: Rect, two: Rect, clicks: U32, +first: Hit, hit: Hit) -> SequenceState:
  match zero:
    case True{}: S{one, two, 1, hit, Running{}}
    case False{}: S{one, two, 2, first, terminal(first_correct(first, hit))}

def sequence_record(s: SequenceState, hit: Hit) -> SequenceState:
  match s:
    case S{one, two, +clicks, first, Running{}}:
      sequence_record_count(U32.is_eq(clicks, 0), one, two, clicks, first, hit)
    case S{one, two, clicks, first, outcome}: S{one, two, clicks, first, outcome}

def sequence_record_hit(s: SequenceState, hit: Hit) -> SequenceState:
  match hit:
    case Miss{}: s
    case One{}: sequence_record(s, One{})
    case Two{}: sequence_record(s, Two{})

def sequence_click(+s: SequenceState, +x: U32, +y: U32) -> SequenceState:
  match s:
    case S{+one, +two, clicks, first, Running{}}:
      sequence_record_hit(s, hit_at(one, two, x, y))
    case S{one, two, clicks, first, outcome}: s

def sequence_timeout(s: SequenceState) -> SequenceState:
  match s:
    case S{one, two, clicks, first, Running{}}: S{one, two, clicks, first, TimedOut{}}
    case S{one, two, clicks, first, outcome}: s

def sequence_active(s: SequenceState, action: SequenceAction) -> SequenceState:
  match action:
    case Wait{}: s
    case Click{x, y}: sequence_click(s, x, y)
    case Timeout{}: sequence_timeout(s)
    case Reset{}: sequence_reset(s)

def sequence_deadline(expired: Bool, s: SequenceState, action: SequenceAction) -> SequenceState:
  match expired s:
    case True{} S{one, two, clicks, first, Running{}}: S{one, two, clicks, first, TimedOut{}}
    case False{} S{one, two, clicks, first, Running{}}: sequence_active(s, action)
    case _ S{one, two, clicks, first, outcome}: s

def sequence_step(s: SequenceState, action: SequenceAction, elapsed: U32) -> SequenceState:
  sequence_deadline(U32.is_ge(elapsed, 10000), s, action)

def sequence_status(s: SequenceState) -> Outcome:
  match s:
    case S{one, two, clicks, first, outcome}: outcome

def sequence_clicks(s: SequenceState) -> U32:
  match s:
    case S{one, two, clicks, first, outcome}: clicks

def sequence_first(s: SequenceState) -> Hit:
  match s:
    case S{one, two, clicks, first, outcome}: first

# ---------------------------------------------------------------------------
# choose-list

type ChooseAction is Data:
  ChooseWait{}
  Select{index: Nat}
  Submit{}
  ChooseTimeout{}
  ChooseReset{}

type ChooseState is Data:
  C{options: +List<+List<U32>>, target: +List<U32>, selected: Nat, outcome: Outcome}

def text_equal(a: +List<U32>, b: +List<U32>) -> Bool:
  match a b:
    case Nil{} Nil{}: True{}
    case Con{x, xs} Con{y, ys}: U32.is_eq(x, y) && text_equal(xs, ys)
    case _ _: False{}

def option_at(options: +List<+List<U32>>, index: Nat) -> +List<U32>:
  match options index:
    case Nil{} _: Nil{}
    case Con{label, rest} 0n: label
    case Con{label, rest} 1n+p: option_at(rest, p)

def choose_options(s: ChooseState) -> +List<+List<U32>>:
  match s:
    case C{options, target, selected, outcome}: options

def choose_target(s: ChooseState) -> +List<U32>:
  match s:
    case C{options, target, selected, outcome}: target

def choose_selected(s: ChooseState) -> Nat:
  match s:
    case C{options, target, selected, outcome}: selected

def choose_outcome(s: ChooseState) -> Outcome:
  match s:
    case C{options, target, selected, outcome}: outcome

def choose_reset(s: ChooseState) -> ChooseState:
  match s:
    case C{options, target, selected, outcome}: C{options, target, 0n, Running{}}

def choose_select_valid(valid: Bool, options: +List<+List<U32>>, target: +List<U32>, selected: Nat, index: Nat) -> ChooseState:
  match valid:
    case True{}: C{options, target, index, Running{}}
    case False{}: C{options, target, selected, Running{}}

def choose_select(s: ChooseState, +index: Nat) -> ChooseState:
  match s:
    case C{+options, +target, selected, Running{}}:
      choose_select_valid(Nat.is_lt(index, List.length(&2, +List<U32>, options)), options, target, selected, index)
    case C{options, target, selected, outcome}: s

def choose_submit(s: ChooseState) -> ChooseState:
  match s:
    case C{+options, +target, +selected, Running{}}:
      C{options, target, selected, terminal(text_equal(option_at(options, selected), target))}
    case C{options, target, selected, outcome}: s

def choose_timeout(s: ChooseState) -> ChooseState:
  match s:
    case C{options, target, selected, Running{}}: C{options, target, selected, TimedOut{}}
    case C{options, target, selected, outcome}: s

def choose_active(s: ChooseState, action: ChooseAction) -> ChooseState:
  match action:
    case ChooseWait{}: s
    case Select{index}: choose_select(s, index)
    case Submit{}: choose_submit(s)
    case ChooseTimeout{}: choose_timeout(s)
    case ChooseReset{}: choose_reset(s)

def choose_deadline(expired: Bool, s: ChooseState, action: ChooseAction) -> ChooseState:
  match expired s:
    case True{} C{options, target, selected, Running{}}: C{options, target, selected, TimedOut{}}
    case False{} C{options, target, selected, Running{}}: choose_active(s, action)
    case _ C{options, target, selected, outcome}: s

def choose_step(s: ChooseState, action: ChooseAction, elapsed: U32) -> ChooseState:
  choose_deadline(U32.is_ge(elapsed, 10000), s, action)
