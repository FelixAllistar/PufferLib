import Base

type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

# Slider values use an offset of ten: 0 means -10, 20 means +10.
type State is Data:
  S{slider: U32, checked: U32, goal_slider: U32, goal_checkbox: U32, outcome: Outcome}

type Action is Data:
  Wait{}
  SetSlider{value: U32}
  Toggle{index: U32}
  Submit{}

def toggle_mask(index: U32) -> U32:
  match index:
    case 1: 1
    case 2: 2
    case 3: 4
    case _: 0

def set_slider(valid: Bool, s: State, value: U32) -> State:
  match valid s:
    case False{} _: s
    case True{} S{slider, checked, goal_slider, goal_checkbox, outcome}:
      S{value, checked, goal_slider, goal_checkbox, outcome}

def toggle(s: State, index: U32) -> State:
  S{slider, +checked, goal_slider, goal_checkbox, outcome} = s
  S{slider, (checked .^. toggle_mask(index) : U32), goal_slider, goal_checkbox, outcome}

def submit(s: State) -> State:
  S{+slider, +checked, +goal_slider, +goal_checkbox, outcome} = s
  +success = U32.is_eq(slider, goal_slider) && U32.is_eq(checked, toggle_mask(goal_checkbox))
  S{slider, checked, goal_slider, goal_checkbox, Finished{success}}

def active(s: State, action: Action) -> State:
  match action:
    case Wait{}: s
    case SetSlider{+value}: set_slider(U32.is_le(value, 20), s, value)
    case Toggle{index}: toggle(s, index)
    case Submit{}: submit(s)

def guarded(expired: Bool, s: State, action: Action) -> State:
  match expired s:
    case True{} S{slider, checked, goal_slider, goal_checkbox, outcome}:
      S{slider, checked, goal_slider, goal_checkbox, TimedOut{}}
    case False{} _: active(s, action)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{slider, checked, goal_slider, goal_checkbox, Running{}}:
      guarded(U32.is_ge(elapsed, deadline),
        S{slider, checked, goal_slider, goal_checkbox, Running{}}, action)
    case S{slider, checked, goal_slider, goal_checkbox, Finished{success}}:
      S{slider, checked, goal_slider, goal_checkbox, Finished{success}}
    case S{slider, checked, goal_slider, goal_checkbox, TimedOut{}}:
      S{slider, checked, goal_slider, goal_checkbox, TimedOut{}}

def raw(outcome: Outcome) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{True{}}: 1.0
    case Finished{False{}}: (0.0 - 1.0 : F32)
    case TimedOut{}: (0.0 - 1.0 : F32)

def timed(outcome: Outcome, elapsed: U32, deadline: U32) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{True{}}:
      F32.max(0.0, (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32))
    case Finished{False{}}: (0.0 - 1.0 : F32)
    case TimedOut{}: (0.0 - 1.0 : F32)
