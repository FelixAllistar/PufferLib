import Base

# A settled-event preset: browser animation completes before the next action.
# Animation-in-flight geometry is outside this model, not silently instantaneous
# wall-clock browser equivalence. Wire retains which presets animate.
type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{active: U32, panels: U32, accordion: Bool, target_tab: U32, medium: U32, outcome: Outcome}

type Action is Data:
  Wait{}
  Header{panel: U32}
  Link{panel: U32, correct: Bool}
  Submit{}

def valid_panel(+panel: U32, count: U32) -> Bool:
  U32.is_gt(panel, 0) && U32.is_le(panel, count)

def visible(+active: U32, panel: U32) -> Bool:
  U32.is_gt(active, 0) && U32.is_eq(active, panel)

def switch(accordion: Bool, +active: U32, +panel: U32) -> U32:
  Bool.pick(U32, accordion && U32.is_eq(active, panel), 0, panel)

def finish(s: State, success: Bool) -> State:
  S{active, panels, accordion, target, medium, outcome} = s
  S{active, panels, accordion, target, medium, Finished{success}}

def header_result(targeted: Bool, fallback: Bool, s: State, target_correct: Bool, fallback_correct: Bool) -> State:
  match targeted fallback:
    case True{} _: finish(s, target_correct)
    case False{} True{}: finish(s, fallback_correct)
    case False{} False{}: s

def header_valid(valid: Bool, s: State, +panel: U32) -> State:
  match valid s:
    case False{} _: s
    case True{} S{+active, panels, +accordion, +target, +medium, outcome}:
      next = {S{switch(accordion, active, panel), panels, accordion, target, medium, outcome} : State}
      header_result(U32.is_gt(target, 0), U32.is_eq(panel, 2) && U32.is_gt(medium, 0), next, U32.is_eq(panel, target), U32.is_eq(medium, 1))

def link_visible(valid: Bool, s: State, correct: Bool) -> State:
  match valid:
    case True{}: finish(s, correct)
    case False{}: s

def act(s: State, action: Action) -> State:
  match s action:
    case S{active, panels, accordion, target, medium, outcome} Wait{}:
      S{active, panels, accordion, target, medium, outcome}
    case S{active, +panels, accordion, target, medium, outcome} Header{+panel}:
      header_valid(valid_panel(panel, panels), S{active, panels, accordion, target, medium, outcome}, panel)
    case S{+active, panels, accordion, target, medium, outcome} Link{panel, correct}:
      link_visible(visible(active, panel), S{active, panels, accordion, target, medium, outcome}, correct)
    case S{+active, panels, accordion, target, medium, outcome} Submit{}:
      finish(S{2, panels, accordion, target, medium, outcome}, U32.is_eq(active, 1))

def guarded(expired: Bool, s: State, action: Action) -> State:
  match expired s:
    case True{} S{active, panels, accordion, target, medium, outcome}:
      S{active, panels, accordion, target, medium, TimedOut{}}
    case False{} _: act(s, action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{active, panels, accordion, target, medium, Running{}}:
      guarded(U32.is_ge(elapsed, deadline), S{active, panels, accordion, target, medium, Running{}}, action)
    case S{active, panels, accordion, target, medium, Finished{success}}:
      S{active, panels, accordion, target, medium, Finished{success}}
    case S{active, panels, accordion, target, medium, TimedOut{}}:
      S{active, panels, accordion, target, medium, TimedOut{}}

def raw(outcome: Outcome) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{True{}}: 1.0
    case Finished{False{}}: (0.0 - 1.0 : F32)
    case TimedOut{}: (0.0 - 1.0 : F32)

def timed(outcome: Outcome, elapsed: U32, deadline: U32) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{True{}}: (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32)
    case Finished{False{}}: (0.0 - 1.0 : F32)
    case TimedOut{}: (0.0 - 1.0 : F32)
