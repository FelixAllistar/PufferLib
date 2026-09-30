import Base

# Source shapes.js attributes. kind: 1 letter, 2 digit, 3 geometric shape.
# glyph is ASCII for text or 201 circle, 202 rectangle, 203 triangle.
type Shape is Data:
  Item{x: U32,y: U32,color: U32,size: U32,kind: U32,glyph: U32}

# size: 0 any, 1 small, 2 large. color: 0 any, 1..7 palette.
# token: 0 item, 1..3 broad kind, otherwise exact glyph.
type Desc is Data:
  D{size: U32,color: U32,token: U32}

type Outcome is Data:
  Running{}
  Finished{success: Bool}
  TimedOut{}

type State is Data:
  S{count_mode: Bool,items: +List<Shape>,desc: Desc,
    buttons: +List<U32>,outcome: Outcome}

type Action is Data:
  Wait{}
  ClickShape{index: Nat}
  ClickBlank{}
  ChooseCount{index: Nat}

def matches(item: Shape, desc: Desc) -> Bool:
  Item{x,y,+color,+size,+kind,+glyph}=item
  D{+want_size,+want_color,+want_token}=desc
  +size_ok = U32.is_eq(want_size,0) ||
    (U32.is_eq(want_size,1) && U32.is_eq(size,0)) ||
    (U32.is_eq(want_size,2) && U32.is_eq(size,1))
  +color_ok = U32.is_eq(want_color,0) ||
    U32.is_eq(want_color,(color + 1 : U32))
  +token_ok = U32.is_eq(want_token,0) ||
    U32.is_eq(want_token,kind) || U32.is_eq(want_token,glyph)
  size_ok && color_ok && token_ok

def matches_at(items: +List<Shape>, desc: Desc, index: Nat) -> Bool:
  match items index:
    case Nil{} _: False{}
    case Con{item,rest} 0n: matches(item,desc)
    case Con{item,rest} 1n+p: matches_at(rest,desc,p)

def count_matches(items: +List<Shape>, +desc: Desc) -> U32:
  match items:
    case Nil{}: 0
    case Con{item,rest}:
      (Bool.pick(U32,matches(item,desc),1,0) + count_matches(rest,desc) : U32)

def button_at(values: +List<U32>,index: Nat) -> U32:
  match values index:
    case Nil{} _: 99
    case Con{value,rest} 0n: value
    case Con{value,rest} 1n+p: button_at(rest,p)

def clicked(enabled: Bool,s: State,success: Bool) -> State:
  match enabled s:
    case False{} _: s
    case True{} S{mode,items,desc,buttons,outcome}:
      S{mode,items,desc,buttons,Finished{success}}

def active(+s: State,action: Action) -> State:
  match s action:
    case S{mode,items,desc,buttons,outcome} Wait{}:
      S{mode,items,desc,buttons,outcome}
    case S{mode,items,desc,buttons,outcome} ClickBlank{}:
      S{mode,items,desc,buttons,Finished{False{}}}
    case S{+mode,+items,+desc,buttons,outcome} ClickShape{index}:
      clicked(Bool.not(mode),S{mode,items,desc,buttons,outcome},
        matches_at(items,desc,index))
    case S{+mode,+items,+desc,+buttons,outcome} ChooseCount{index}:
      clicked(mode,S{mode,items,desc,buttons,outcome},
        U32.is_eq(button_at(buttons,index),count_matches(items,desc)))

def expire(s: State) -> State:
  S{mode,items,desc,buttons,outcome}=s
  S{mode,items,desc,buttons,TimedOut{}}

def guard(expired: Bool,s: State,action: Action) -> State:
  match expired:
    case True{}: expire(s)
    case False{}: active(s,action)

def step(s: State,action: Action,elapsed: U32,deadline: U32) -> State:
  match s:
    case S{mode,items,desc,buttons,Running{}}:
      guard(U32.is_ge(elapsed,deadline),
        S{mode,items,desc,buttons,Running{}},action)
    case S{mode,items,desc,buttons,Finished{success}}:
      S{mode,items,desc,buttons,Finished{success}}
    case S{mode,items,desc,buttons,TimedOut{}}:
      S{mode,items,desc,buttons,TimedOut{}}

def raw(result: Outcome) -> F32:
  match result:
    case Running{}: 0.0
    case Finished{True{}}: 1.0
    case _: (0.0 - 1.0 : F32)

def timed(result: Outcome,elapsed: U32,deadline: U32) -> F32:
  match result:
    case Running{}: 0.0
    case Finished{True{}}:
      F32.max(0.0,(1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32))
    case _: (0.0 - 1.0 : F32)
