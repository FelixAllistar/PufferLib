import Base
import ../text/Model.bend as E
import ../finish/Model.bend as F

# A host-provided response form, shared by synthetic applications and browser
# evaluation. It never reads an instruction, reward, task ID or expected answer.
type Focus is Data:
  None{}
  DataField{}
  DetailField{}

type Error is Data:
  Clean{}
  InvalidData{}
  InvalidDetail{}

type State is Data:
  S{completion: F.State, kind: F.TaskType, status: F.Status,
    data: E.Field, detail: E.Field, focus: Focus, error: Error}

type Action is Data:
  Wait{}
  ChooseKind{kind: F.TaskType}
  ChooseStatus{status: F.Status}
  FocusData{}
  FocusDetail{}
  Blur{}
  Editing{edit: E.Edit}
  # The transport parses JSON and encodes these fields from the current draft.
  # Type/status always come from this state, never from a hidden task field.
  Finish{data: +List<U32>, detail: +List<U32>}
  Reject{error: Error}
  Expire{}

def initial() -> State:
  S{F.Open{}, F.Navigate{}, F.Success{}, E.F{[110, 117, 108, 108], 0n, 0n},
    E.F{[110, 117, 108, 108], 0n, 0n}, None{}, Clean{}}

def edit(focus: Focus, s: State, command: E.Edit) -> State:
  match focus s:
    case None{} _: s
    case DataField{} S{completion, kind, status, data, detail, old, error}:
      S{completion, kind, status, E.edit(command, data), detail, DataField{}, Clean{}}
    case DetailField{} S{completion, kind, status, data, detail, old, error}:
      S{completion, kind, status, data, E.edit(command, detail), DetailField{}, Clean{}}

def act(action: Action, s: State) -> State:
  match action s:
    case ChooseKind{next} S{completion, kind, status, data, detail, focus, error}:
      S{completion, next, status, data, detail, None{}, Clean{}}
    case ChooseStatus{next} S{completion, kind, status, data, detail, focus, error}:
      S{completion, kind, next, data, detail, None{}, Clean{}}
    case FocusData{} S{completion, kind, status, data, detail, focus, error}:
      S{completion, kind, status, E.end(data), detail, DataField{}, error}
    case FocusDetail{} S{completion, kind, status, data, detail, focus, error}:
      S{completion, kind, status, data, E.end(detail), DetailField{}, error}
    case Blur{} S{completion, kind, status, data, detail, focus, error}:
      S{completion, kind, status, data, detail, None{}, error}
    case Editing{command} S{completion, kind, status, data, detail, +focus, error}:
      edit(focus, S{completion, kind, status, data, detail, focus, error}, command)
    case Finish{bytes, explanation} S{completion, +kind, +status, data, detail, focus, error}:
      S{F.step(completion, F.Submit{F.R{kind, status, bytes, explanation}}), kind, status, data, detail, None{}, Clean{}}
    case Reject{why} S{completion, kind, status, data, detail, focus, error}:
      S{completion, kind, status, data, detail, focus, why}
    case Expire{} S{completion, kind, status, data, detail, focus, error}:
      S{F.step(completion, F.Expire{}), kind, status, data, detail, None{}, error}
    case _ _: s

def step(s: State, action: Action) -> State:
  match s:
    case S{F.Open{}, kind, status, data, detail, focus, error}:
      act(action, S{F.Open{}, kind, status, data, detail, focus, error})
    case _: s
