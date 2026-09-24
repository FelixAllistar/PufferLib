import Base
import ../../miniwob/Widgets.bend as W
import ../../miniwob/Text.bend as T

# Nine small arithmetic and numeric-control tasks. The row carries only a
# bounded generator preset; all transitions and rewards live here.
type Task is Data:
  Ascending{}
  Greatest{}
  GenerateNumber{}
  GuessNumber{}
  HotCold{}
  NumberCheckboxes{}
  OddOrEven{}
  SimpleAlgebra{}
  SimpleArithmetic{}

type Outcome is Data:
  Running{}
  Finished{raw: F32, scaled: Bool}
  TimedOut{}

type State is Data:
  S{task: Task, phase: U32, nodes: +List<W.Node>, cursor: U32,
    selected: U32, secret: U32, limit: U32, mode: U32,
    expected: U32, feedback: U32, candidate: U32, rng: U32,
    field: T.Field, outcome: Outcome}

type Action is Data:
  Wait{}
  Click{index: U32, x: U32, y: U32}
  Edit{edit: T.Edit}
  PointerMove{x: U32, y: U32}
  Submit{}
  Timeout{}

type GuessParse is Data:
  InvalidGuess{}
  ParsedGuess{number: U32, negative: Bool}

def make(task: Task, phase: U32, nodes: +List<W.Node>, cursor: U32,
         selected: U32, secret: U32, limit: U32, mode: U32,
         expected: U32, feedback: U32, candidate: U32, rng: U32,
         field: T.Field, outcome: Outcome) -> State:
  S{task, phase, nodes, cursor, selected, secret, limit, mode,
    expected, feedback, candidate, rng, field, outcome}

def finish(s: State, raw: F32, scaled: Bool) -> State:
  S{task, phase, nodes, cursor, selected, secret, limit, mode,
    expected, feedback, candidate, rng, field, outcome} = s
  make(task, phase, nodes, cursor, selected, secret, limit, mode,
       expected, feedback, candidate, rng, field, Finished{raw, scaled})

def with_nodes(s: State, +nodes: +List<W.Node>) -> State:
  S{task, phase, old, cursor, selected, secret, limit, mode,
    expected, feedback, candidate, rng, field, outcome} = s
  make(task, phase, nodes, cursor, selected, secret, limit, mode,
       expected, feedback, candidate, rng, field, outcome)

def with_cursor(s: State, phase: U32, cursor: U32) -> State:
  S{task, old_phase, nodes, old_cursor, selected, secret, limit, mode,
    expected, feedback, candidate, rng, field, outcome} = s
  make(task, phase, nodes, cursor, selected, secret, limit, mode,
       expected, feedback, candidate, rng, field, outcome)

def with_selection(s: State, selected: U32, +nodes: +List<W.Node>) -> State:
  S{task, phase, old_nodes, cursor, old_selected, secret, limit, mode,
    expected, feedback, candidate, rng, field, outcome} = s
  make(task, phase, nodes, cursor, selected, secret, limit, mode,
       expected, feedback, candidate, rng, field, outcome)

def with_feedback(s: State, feedback: U32, value: U32) -> State:
  S{task, phase, nodes, old_cursor, selected, secret, limit, mode,
    expected, old_feedback, candidate, rng, field, outcome} = s
  make(task, phase, nodes, value, selected, secret, limit, mode,
       expected, feedback, candidate, rng, field, outcome)

def with_candidate(s: State, candidate: U32, rng: U32) -> State:
  S{task, phase, nodes, cursor, selected, secret, limit, mode,
    expected, feedback, old_candidate, old_rng, field, outcome} = s
  make(task, 1, nodes, cursor, selected, secret, limit, mode,
       expected, feedback, candidate, rng, field, outcome)

def with_field(s: State, field: T.Field) -> State:
  S{task, phase, nodes, cursor, selected, secret, limit, mode,
    expected, feedback, candidate, rng, old_field, outcome} = s
  make(task, phase, nodes, cursor, selected, secret, limit, mode,
       expected, feedback, candidate, rng, field, outcome)

def clear_selection(nodes: +List<W.Node>, +index: Nat, +selected_index: Nat) -> +List<W.Node>:
  match nodes:
    case Nil{}: Nil{}
    case Con{W.N{kind, selected, target}, rest}:
      W.N{kind, Nat.is_eq(index, selected_index), target} <> clear_selection(rest, 1n+index, selected_index)

def clear_pair(nodes: +List<W.Node>, +index: Nat, +clicked: Nat, +group: Nat) -> +List<W.Node>:
  match nodes:
    case Nil{}: Nil{}
    case Con{W.N{kind, old, target}, rest}:
      selected = Bool.pick(Bool, Nat.is_eq(index / 2n, group), Nat.is_eq(index, clicked), old)
      W.N{kind, selected, target} <> clear_pair(rest, 1n+index, clicked, group)

def matches(a: Bool, b: Bool) -> U32:
  match a b:
    case False{} False{}: 1
    case True{} True{}: 1
    case _ _: 0

def checkbox_score(nodes: +List<W.Node>, correct: U32) -> U32:
  match nodes:
    case Nil{}: correct
    case Con{W.N{W.Checkbox{}, selected, target}, rest}:
      checkbox_score(rest, (correct + matches(selected, target) : U32))
    case Con{_, rest}: checkbox_score(rest, correct)

def odd_row_score(odd_selected: Bool, even_selected: Bool,
                  odd_target: Bool, even_target: Bool) -> F32:
  match odd_selected even_selected:
    case True{} _: (Bool.pick(F32, odd_target, 1.0, (0.0 - 1.0 : F32)) / 3.0 : F32)
    case False{} True{}: (Bool.pick(F32, even_target, 1.0, (0.0 - 1.0 : F32)) / 3.0 : F32)
    case False{} False{}: ((0.0 - 1.0 : F32) / 3.0 : F32)

def odd_rows(nodes: +List<W.Node>) -> F32:
  match nodes:
    case Nil{}: 0.0
    case Con{W.N{W.Button{}, odd_selected, odd_target}, Con{W.N{W.Button{}, even_selected, even_target}, rest}}:
      ((odd_row_score(odd_selected, even_selected, odd_target, even_target) + odd_rows(rest)) : F32)
    case Con{_, rest}: odd_rows(rest)

def update_input(+s: State, edit: T.Edit) -> State:
  S{task, phase, nodes, cursor, selected, secret, limit, mode,
    expected, feedback, candidate, rng, field, outcome} = s
  with_field(s, T.edit(edit, field))

def signed_text(+n: U32) -> String:
  +negative = U32.is_eq((n >> 31n : U32), 1)
  +magnitude = Bool.pick(U32, negative, (0 - n : U32), n)
  Bool.pick(String, negative, "-" ++ U32.show(magnitude), U32.show(magnitude))

def chars(s: String) -> +List<U32>:
  match s:
    case SNil{}: Nil{}
    case SCon{ch, rest}: Char.to_u32(ch) <> chars(rest)

def nat_units(xs: +List<U32>) -> +List<Nat>:
  match xs:
    case Nil{}: Nil{}
    case Con{head, rest}: U32.to_nat(head) <> nat_units(rest)

def digits_end(any: Bool, value: U32) -> Maybe<U32>:
  match any:
    case True{}: Some{value}
    case False{}: None{}

def digits_prefix(any: Bool, value: U32) -> Maybe<U32>:
  match any:
    case True{}: Some{value}
    case False{}: None{}

def decimal_digits(xs: +List<Nat>, value: U32, any: Bool) -> Maybe<U32>:
  match xs:
    case Nil{}: digits_end(any, value)
    case Con{head, rest}:
      match head:
        case 48n: decimal_digits(rest, (value * 10 : U32), True{})
        case 49n: decimal_digits(rest, ((value * 10 : U32) + 1 : U32), True{})
        case 50n: decimal_digits(rest, ((value * 10 : U32) + 2 : U32), True{})
        case 51n: decimal_digits(rest, ((value * 10 : U32) + 3 : U32), True{})
        case 52n: decimal_digits(rest, ((value * 10 : U32) + 4 : U32), True{})
        case 53n: decimal_digits(rest, ((value * 10 : U32) + 5 : U32), True{})
        case 54n: decimal_digits(rest, ((value * 10 : U32) + 6 : U32), True{})
        case 55n: decimal_digits(rest, ((value * 10 : U32) + 7 : U32), True{})
        case 56n: decimal_digits(rest, ((value * 10 : U32) + 8 : U32), True{})
        case 57n: decimal_digits(rest, ((value * 10 : U32) + 9 : U32), True{})
        case _: digits_prefix(any, value)

def parsed_suffix(result: Maybe<U32>, negative: Bool) -> GuessParse:
  match result:
    case None{}: InvalidGuess{}
    case Some{number}: ParsedGuess{number, negative}

def positive_head(head: Nat, rest: +List<Nat>) -> GuessParse:
  match head:
    case 48n: parsed_suffix(decimal_digits(rest, 0, True{}), False{})
    case 49n: parsed_suffix(decimal_digits(rest, 1, True{}), False{})
    case 50n: parsed_suffix(decimal_digits(rest, 2, True{}), False{})
    case 51n: parsed_suffix(decimal_digits(rest, 3, True{}), False{})
    case 52n: parsed_suffix(decimal_digits(rest, 4, True{}), False{})
    case 53n: parsed_suffix(decimal_digits(rest, 5, True{}), False{})
    case 54n: parsed_suffix(decimal_digits(rest, 6, True{}), False{})
    case 55n: parsed_suffix(decimal_digits(rest, 7, True{}), False{})
    case 56n: parsed_suffix(decimal_digits(rest, 8, True{}), False{})
    case 57n: parsed_suffix(decimal_digits(rest, 9, True{}), False{})
    case _: InvalidGuess{}

def parse_head(is_minus: Bool, head: Nat, rest: +List<Nat>) -> GuessParse:
  match is_minus:
    case True{}: parsed_suffix(decimal_digits(rest, 0, False{}), True{})
    case False{}: positive_head(head, rest)

def parsed(xs: +List<Nat>) -> GuessParse:
  match xs:
    case Nil{}: InvalidGuess{}
    case Con{+head, rest}: parse_head(Nat.is_eq(head, 45n), head, rest)

def guess_feedback(equal: Bool, lower: Bool, guess: U32, s: State) -> State:
  match equal lower:
    case True{} _: finish(with_feedback(s, 3, guess), 1.0, True{})
    case False{} True{}: with_feedback(s, 1, guess)
    case False{} False{}: with_feedback(s, 2, guess)

def guess_value(+number: U32, +negative: Bool, +s: State) -> State:
  S{task, phase, nodes, cursor, selected, +secret, limit, mode,
    expected, feedback, candidate, rng, field, outcome} = s
  +is_negative = negative && U32.is_gt(number, 0)
  +guess = Bool.pick(U32, is_negative, (0 - number : U32), number)
  guess_feedback(U32.is_eq(number, secret), Bool.pick(Bool, is_negative, True{}, U32.is_lt(number, secret)), guess, s)

def guess_result(result: GuessParse, +s: State) -> State:
  match result:
    case InvalidGuess{}: with_feedback(s, 4, 0)
    case ParsedGuess{+number, negative}: guess_value(number, negative, s)

def guess(+s: State) -> State:
  S{task, phase, nodes, cursor, selected, secret, limit, mode,
    expected, feedback, candidate, rng, field, outcome} = s
  +value = T.value(field)
  guess_result(parsed(nat_units(value)), s)

def greatest_submit(selected: Bool, correct: Bool, s: State) -> State:
  match selected correct:
    case False{} _: finish(s, (0.0 - 1.0 : F32), False{})
    case True{} True{}: finish(s, 1.0, True{})
    case True{} False{}: finish(s, 0.1, True{})

def find_selected(s: State, nodes: +List<W.Node>, index: Nat) -> State:
  match nodes:
    case Nil{}: finish(s, (0.0 - 1.0 : F32), False{})
    case Con{node, rest}:
      match index:
        case 0n: greatest_submit(True{}, W.goal(node), s)
        case 1n+p: find_selected(s, rest, p)

def allowed_candidate_mode(mode: Nat, limit: U32, candidate: U32) -> Bool:
  match mode:
    case 0n: U32.is_lt(candidate, limit)
    case 1n: U32.is_gt(candidate, limit)
    case 2n: U32.is_eq((candidate % 2 : U32), 1)
    case _: U32.is_eq((candidate % 2 : U32), 0)

def allowed_candidate(mode: U32, limit: U32, candidate: U32) -> Bool:
  allowed_candidate_mode(U32.to_nat(mode), limit, candidate)

def generated_result(correct: Bool, s: State) -> State:
  match correct:
    case True{}: finish(s, 1.0, True{})
    case False{}: finish(s, (0.0 - 1.0 : F32), False{})

def generated_empty_result(mode: Nat, s: State) -> State:
  match mode:
    case 3n: finish(s, 1.0, True{})
    case _: finish(s, (0.0 - 1.0 : F32), False{})

def generate_submit(phase: Nat, mode: U32, limit: U32, candidate: U32, s: State) -> State:
  match phase:
    case 0n: generated_empty_result(U32.to_nat(mode), s)
    case _: generated_result(allowed_candidate(mode, limit, candidate), s)

def checkbox_submit(score: Nat, s: State) -> State:
  match score:
    case 28n: finish(s, 1.0, True{})
    case _: finish(s, (0.0 - 1.0 : F32), False{})

def field_submit(correct: Bool, s: State) -> State:
  match correct:
    case True{}: finish(s, 1.0, True{})
    case False{}: finish(s, (0.0 - 1.0 : F32), False{})

def selected_submit(selected: Nat, nodes: +List<W.Node>, s: State) -> State:
  match selected:
    case 0n: finish(s, (0.0 - 1.0 : F32), False{})
    case 1n+p: find_selected(s, nodes, p)

def submit(+s: State) -> State:
  match s:
    case S{Greatest{}, phase, nodes, cursor, selected, secret, limit, mode, expected, feedback, candidate, rng, field, Running{}}:
      selected_submit(U32.to_nat(selected), nodes, s)
    case S{GenerateNumber{}, phase, nodes, cursor, selected, secret, limit, mode, expected, feedback, candidate, rng, field, Running{}}:
      generate_submit(U32.to_nat(phase), mode, limit, candidate, s)
    case S{GuessNumber{}, phase, nodes, cursor, selected, secret, limit, mode, expected, feedback, candidate, rng, field, Running{}}:
      guess(s)
    case S{NumberCheckboxes{}, phase, nodes, cursor, selected, secret, limit, mode, expected, feedback, candidate, rng, field, Running{}}:
      checkbox_submit(U32.to_nat(checkbox_score(nodes, 0)), s)
    case S{OddOrEven{}, phase, nodes, cursor, selected, secret, limit, mode, expected, feedback, candidate, rng, field, Running{}}:
      +raw = odd_rows(nodes)
      finish(s, raw, F32.is_gt(raw, 0.0))
    case S{SimpleAlgebra{}, phase, nodes, cursor, selected, secret, limit, mode, expected, feedback, candidate, rng, field, Running{}}:
      field_submit(T.equal(T.value(field), chars(signed_text(expected))), s)
    case S{SimpleArithmetic{}, phase, nodes, cursor, selected, secret, limit, mode, expected, feedback, candidate, rng, field, Running{}}:
      field_submit(T.equal(T.value(field), chars(signed_text(expected))), s)
    case _: s

def ascending_initial(is_one: Bool, s: State) -> State:
  match is_one:
    case True{}: with_cursor(s, 1, 1)
    case False{}: finish(s, (0.0 - 1.0 : F32), False{})

def ascending_advance_index(cursor: Nat, s: State) -> State:
  match cursor:
    case 4n: finish(s, 1.0, True{})
    case _: with_cursor(s, 1, U32.from_nat((cursor + 1n : Nat)))

def ascending_later(is_next: Bool, cursor: U32, s: State) -> State:
  match is_next:
    case True{}: ascending_advance_index(U32.to_nat(cursor), s)
    case False{}: finish(s, (U32.to_f32(cursor) / 5.0 : F32), True{})

def ascending_click(phase: Nat, index: U32, +cursor: U32, s: State) -> State:
  match phase:
    case 0n: ascending_initial(U32.is_eq(index, 0), s)
    case _: ascending_later(U32.is_eq(index, cursor), cursor, s)

def greatest_click(is_card: Bool, +index: U32, nodes: +List<W.Node>, s: State) -> State:
  match is_card:
    case True{}: with_selection(s, (index + 1 : U32), clear_selection(nodes, 0n, U32.to_nat(index)))
    case False{}: submit(s)

def mix(+x: U32) -> U32:
  +a = (x .^. (x >> 16n) : U32)
  +b = (a * 2146121005 : U32)
  +c = (b .^. (b >> 15n) : U32)
  +d = (c * 2221713035 : U32)
  (d .^. (d >> 16n) : U32)

def generate_click(is_generate: Bool, rng: U32, s: State) -> State:
  match is_generate:
    case True{}:
      +next_rng = mix(rng)
      with_candidate(s, (next_rng % 11 : U32), next_rng)
    case False{}: submit(s)

def guess_click(is_submit: Bool, s: State) -> State:
  match is_submit:
    case True{}: submit(s)
    case False{}: s

def checkbox_click(is_checkbox: Bool, +index: U32, nodes: +List<W.Node>, s: State) -> State:
  match is_checkbox:
    case True{}: with_nodes(s, W.toggle_at(nodes, U32.to_nat(index)))
    case False{}: submit(s)

def odd_click(is_choice: Bool, +index: U32, nodes: +List<W.Node>, s: State) -> State:
  match is_choice:
    case True{}:
      +clicked = U32.to_nat(index)
      with_nodes(s, clear_pair(nodes, 0n, clicked, clicked / 2n))
    case False{}: submit(s)

def math_click(is_submit: Bool, s: State) -> State:
  match is_submit:
    case True{}: submit(s)
    case False{}: s

def difference(+a: U32, +b: U32) -> U32:
  Bool.pick(U32, U32.is_ge(a, b), (a - b : U32), (b - a : U32))

def heat_far(is_cold: Bool) -> U32:
  match is_cold:
    case True{}: 3
    case False{}: 4

def heat_medium(is_warm: Bool, +d2: U32) -> U32:
  match is_warm:
    case True{}: 2
    case False{}: heat_far(U32.is_lt(d2, 1600))

def heat_near(is_hot: Bool, +d2: U32) -> U32:
  match is_hot:
    case True{}: 1
    case False{}: heat_medium(U32.is_lt(d2, 100), d2)

def heat(secret_x: U32, secret_y: U32, x: U32, y: U32) -> U32:
  +dx = difference(secret_x, x)
  +dy = difference(secret_y, y)
  +d2 = ((dx * dx : U32) + (dy * dy : U32) : U32)
  heat_near(U32.is_lt(d2, 25), d2)

def heat_result_kind(kind: Nat, s: State) -> State:
  match kind:
    case 1n: finish(s, 1.0, True{})
    case 2n: finish(s, 0.5, True{})
    case 3n: finish(s, 0.25, True{})
    case _: finish(s, (0.0 - 1.0 : F32), False{})

def heat_result(kind: U32, s: State) -> State:
  heat_result_kind(U32.to_nat(kind), s)

def clear_heat(+s: State) -> State:
  S{task, phase, nodes, +cursor, selected, secret, limit, mode,
    expected, feedback, candidate, rng, field, outcome} = s
  with_feedback(s, 0, cursor)

def pointer_move(+s: State, x: U32, y: U32) -> State:
  S{task, phase, nodes, +cursor, selected, +secret, +limit, mode,
    expected, feedback, candidate, rng, field, outcome} = s
  +kind = heat(secret, limit, x, y)
  with_feedback(s, kind, cursor)

def pointer_click(+s: State, x: U32, y: U32) -> State:
  S{task, phase, nodes, +cursor, selected, +secret, +limit, mode,
    expected, feedback, candidate, rng, field, outcome} = s
  +kind = heat(secret, limit, x, y)
  heat_result(kind, with_feedback(s, 0, cursor))

def hot_click(is_area: Bool, s: State, x: U32, y: U32) -> State:
  match is_area:
    case True{}: pointer_click(s, x, y)
    case False{}: finish(clear_heat(s), (0.0 - 1.0 : F32), False{})

def click(+s: State, +index: U32, x: U32, y: U32) -> State:
  match s:
    case S{Ascending{}, phase, nodes, cursor, selected, secret, limit, mode, expected, feedback, candidate, rng, field, Running{}}:
      ascending_click(U32.to_nat(phase), index, cursor, s)
    case S{Greatest{}, phase, nodes, cursor, selected, secret, limit, mode, expected, feedback, candidate, rng, field, Running{}}:
      greatest_click(U32.is_lt(index, 3), index, nodes, s)
    case S{GenerateNumber{}, phase, nodes, cursor, selected, secret, limit, mode, expected, feedback, candidate, rng, field, Running{}}:
      generate_click(U32.is_eq(index, 0), rng, s)
    case S{GuessNumber{}, phase, nodes, cursor, selected, secret, limit, mode, expected, feedback, candidate, rng, field, Running{}}:
      guess_click(U32.is_eq(index, 1), s)
    case S{HotCold{}, phase, nodes, cursor, selected, secret, limit, mode, expected, feedback, candidate, rng, field, Running{}}:
      hot_click(U32.is_eq(index, 0), s, x, y)
    case S{NumberCheckboxes{}, phase, nodes, cursor, selected, secret, limit, mode, expected, feedback, candidate, rng, field, Running{}}:
      checkbox_click(U32.is_lt(index, 28), index, nodes, s)
    case S{OddOrEven{}, phase, nodes, cursor, selected, secret, limit, mode, expected, feedback, candidate, rng, field, Running{}}:
      odd_click(U32.is_lt(index, 6), index, nodes, s)
    case S{SimpleAlgebra{}, phase, nodes, cursor, selected, secret, limit, mode, expected, feedback, candidate, rng, field, Running{}}:
      math_click(U32.is_eq(index, 1), s)
    case S{SimpleArithmetic{}, phase, nodes, cursor, selected, secret, limit, mode, expected, feedback, candidate, rng, field, Running{}}:
      math_click(U32.is_eq(index, 1), s)
    case _: s

def timeout_state(+s: State) -> State:
  S{task, phase, nodes, cursor, selected, secret, limit, mode,
    expected, feedback, candidate, rng, field, outcome} = s
  make(task, phase, nodes, cursor, selected, secret, limit, mode,
       expected, feedback, candidate, rng, field, TimedOut{})

def active(s: State, action: Action) -> State:
  match action:
    case Wait{}: s
    case Timeout{}: timeout_state(s)
    case Click{index, x, y}: click(s, index, x, y)
    case Edit{edit}: update_input(s, edit)
    case PointerMove{x, y}: pointer_move(s, x, y)
    case Submit{}: submit(s)

def deadline_step(expired: Bool, +s: State, action: Action) -> State:
  match expired:
    case True{}: active(s, Timeout{})
    case False{}: active(s, action)

def step(+s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{task, phase, nodes, cursor, selected, secret, limit, mode,
           expected, feedback, candidate, rng, field, Running{}}:
      deadline_step(U32.is_ge(elapsed, deadline),
                    make(task, phase, nodes, cursor, selected, secret, limit, mode,
                         expected, feedback, candidate, rng, field, Running{}), action)
    case S{task, phase, nodes, cursor, selected, secret, limit, mode,
           expected, feedback, candidate, rng, field, outcome}:
      make(task, phase, nodes, cursor, selected, secret, limit, mode,
           expected, feedback, candidate, rng, field, outcome)

def raw(outcome: Outcome) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{score, scaled}: score
    case TimedOut{}: (0.0 - 1.0 : F32)

def timed(outcome: Outcome, elapsed: U32, deadline: U32) -> F32:
  match outcome:
    case Running{}: 0.0
    case TimedOut{}: (0.0 - 1.0 : F32)
    case Finished{score, False{}}: score
    case Finished{score, True{}}:
      (score * F32.max(0.0, (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32)) : F32)
