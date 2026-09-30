import Base

# The page uses separate inbox, search, read, reply and forward elements.
# A read view is always closed back to the inbox, even when opened from search.
type Screen is Data:
  Inbox{}
  Search{}
  Read{}
  Reply{}
  Forward{}

type Outcome is Data:
  Running{}
  Finished{success: Bool, timed: Bool}
  TimedOut{}

# 0 reply, 1 forward, 2 delete, 3 important.
type Goal is Data:
  ReplyGoal{}
  ForwardGoal{}
  DeleteGoal{}
  ImportantGoal{}

type State is Data:
  S{screen: Screen, selected: U32, focus: U32, goal: Goal,
    outcome: Outcome}

type Action is Data:
  Wait{}
  OpenSearch{}
  CancelSearch{}
  OpenEmail{index: U32}
  CloseEmail{}
  OpenReply{}
  CloseReply{}
  OpenForward{}
  CloseForward{}
  Focus{field: U32}
  Star{same_name: Bool}
  Trash{same_name: Bool}
  SendReply{same_name: Bool, text_equal: Bool}
  SendForward{recipient_equal: Bool, body_equal: Bool}

def goal_index(code: Nat) -> Goal:
  match code:
    case 0n: ReplyGoal{}
    case 1n: ForwardGoal{}
    case 2n: DeleteGoal{}
    case _: ImportantGoal{}

def decode_goal(code: U32) -> Goal:
  goal_index(U32.to_nat(code))

def screen_index(code: Nat) -> Screen:
  match code:
    case 0n: Inbox{}
    case 1n: Search{}
    case 2n: Read{}
    case 3n: Reply{}
    case _: Forward{}

def decode_screen(code: U32) -> Screen:
  screen_index(U32.to_nat(code))

def screen_code(screen: Screen) -> U32:
  match screen:
    case Inbox{}: 0
    case Search{}: 1
    case Read{}: 2
    case Reply{}: 3
    case Forward{}: 4

def goal_code(goal: Goal) -> U32:
  match goal:
    case ReplyGoal{}: 0
    case ForwardGoal{}: 1
    case DeleteGoal{}: 2
    case ImportantGoal{}: 3

def outcome_index(status: Nat, +raw_bits: U32) -> Outcome:
  match status:
    case 0n: Running{}
    case 1n: Finished{U32.is_eq(raw_bits, 1065353216), U32.is_ne(raw_bits, 3212836864)}
    case _: TimedOut{}

def decode_outcome(status: U32, raw_bits: U32) -> Outcome:
  outcome_index(U32.to_nat(status), raw_bits)

def terminate(+s: State, success: Bool, timed: Bool) -> State:
  S{screen, selected, focus, goal, outcome} = s
  S{screen, selected, focus, goal, Finished{success, timed}}

def timeout(+s: State) -> State:
  S{screen, selected, focus, goal, outcome} = s
  S{screen, selected, focus, goal, TimedOut{}}

def star_result(s: State, +same: Bool) -> State:
  match s:
    case S{screen, selected, focus, ImportantGoal{}, outcome}:
      S{screen, selected, focus, ImportantGoal{}, Finished{same, same}}
    case S{screen, selected, focus, goal, outcome}:
      S{screen, selected, focus, goal, outcome}

def trash_result(s: State, +same: Bool) -> State:
  match s:
    case S{screen, selected, focus, DeleteGoal{}, outcome}:
      S{screen, selected, focus, DeleteGoal{}, Finished{same, same}}
    case S{screen, selected, focus, goal, outcome}:
      S{screen, selected, focus, goal, outcome}

def reply_result(s: State, +same: Bool, +equal: Bool) -> State:
  match s:
    case S{screen, selected, focus, ReplyGoal{}, outcome}:
      S{screen, selected, focus, ReplyGoal{}, Finished{same && equal, same && equal}}
    case S{screen, selected, focus, goal, outcome}:
      S{screen, selected, focus, goal, Finished{False{}, False{}}}

# Original forward checks recipient and body only, not sender identity.
def forward_result(s: State, +recipient: Bool, +body: Bool) -> State:
  match s:
    case S{screen, selected, focus, ForwardGoal{}, outcome}:
      S{screen, selected, focus, ForwardGoal{}, Finished{recipient && body, recipient && body}}
    case S{screen, selected, focus, goal, outcome}:
      S{screen, selected, focus, goal, Finished{False{}, False{}}}

def act_action(action: Action, s: State) -> State:
  match action:
    case Wait{}: s
    case OpenSearch{}:
      match s:
        case S{Inbox{}, selected, focus, goal, outcome}: S{Search{}, selected, 1, goal, outcome}
        case _: s
    case CancelSearch{}:
      match s:
        case S{Search{}, selected, focus, goal, outcome}: S{Inbox{}, selected, 0, goal, outcome}
        case _: s
    case OpenEmail{index}:
      match s:
        case S{Inbox{}, selected, focus, goal, outcome}: S{Read{}, index, 0, goal, outcome}
        case S{Search{}, selected, focus, goal, outcome}: S{Read{}, index, 0, goal, outcome}
        case _: s
    case CloseEmail{}:
      match s:
        case S{Read{}, selected, focus, goal, outcome}: S{Inbox{}, 0, 0, goal, outcome}
        case _: s
    case OpenReply{}:
      match s:
        case S{Read{}, selected, focus, goal, outcome}: S{Reply{}, selected, 0, goal, outcome}
        case _: s
    case CloseReply{}:
      match s:
        case S{Reply{}, selected, focus, goal, outcome}: S{Read{}, selected, 0, goal, outcome}
        case _: s
    case OpenForward{}:
      match s:
        case S{Read{}, selected, focus, goal, outcome}: S{Forward{}, selected, 0, goal, outcome}
        case _: s
    case CloseForward{}:
      match s:
        case S{Forward{}, selected, focus, goal, outcome}: S{Read{}, selected, 0, goal, outcome}
        case _: s
    case Focus{field}:
      match s:
        case S{Search{}, selected, focus, goal, outcome}:
          S{Search{}, selected, field, goal, outcome}
        case S{Reply{}, selected, focus, goal, outcome}:
          S{Reply{}, selected, field, goal, outcome}
        case S{Forward{}, selected, focus, goal, outcome}:
          S{Forward{}, selected, field, goal, outcome}
        case _: s
    case Star{same_name}: star_result(s, same_name)
    case Trash{same_name}: trash_result(s, same_name)
    case SendReply{same_name, text_equal}: reply_result(s, same_name, text_equal)
    case SendForward{recipient_equal, body_equal}: forward_result(s, recipient_equal, body_equal)

def act(s: State, action: Action) -> State:
  act_action(action, s)

def running(expired: Bool, s: State, action: Action) -> State:
  match expired:
    case True{}: timeout(s)
    case False{}: act(s, action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{screen, selected, focus, goal, Running{}}:
      running(U32.is_ge(elapsed, deadline), S{screen, selected, focus, goal, Running{}}, action)
    case _: s

def raw(outcome: Outcome) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{True{}, timed}: 1.0
    case _: (0.0 - 1.0 : F32)

def reward(outcome: Outcome, elapsed: U32, deadline: U32) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{True{}, True{}}:
      (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32)
    case Finished{True{}, False{}}: 1.0
    case _: (0.0 - 1.0 : F32)
