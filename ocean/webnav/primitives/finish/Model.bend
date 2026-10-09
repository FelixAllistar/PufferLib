import Base

# A submission is the policy's claim, never a success judgment. Task grading
# belongs to the application/evaluator. JSON/UTF-8 validation is transport.
type TaskType is Data:
  Navigate{}
  Mutate{}
  Retrieve{}

type Status is Data:
  Success{}
  ActionNotAllowed{}
  PermissionDenied{}
  NotFound{}
  DataValidation{}
  UnknownError{}

type Reply is Data:
  R{kind: TaskType, status: Status, data: +List<U32>, detail: +List<U32>}

type State is Data:
  Open{}
  Submitted{reply: Reply}
  Expired{}

type Action is Data:
  Wait{}
  Submit{reply: Reply}
  Expire{}

def step(s: State, action: Action) -> State:
  match s action:
    case Open{} Wait{}: Open{}
    case Open{} Submit{reply}: Submitted{reply}
    case Open{} Expire{}: Expired{}
    case Submitted{reply} _: Submitted{reply}
    case Expired{} _: Expired{}

def response(s: State) -> Maybe<&2, Reply>:
  match s:
    case Submitted{reply}: Some{reply}
    case _: None{}

def terminal(s: State) -> Bool:
  match s:
    case Open{}: False{}
    case _: True{}
