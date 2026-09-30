import Base

# SVG gridCoords coordinates are in tenths of a pixel plus 2560. Each shape
# retains its source type/color, while D3 drag updates its transformed bbox.
type Shape is Data:
  Item{kind: U32, color: U32, x: U32, y: U32}

type Outcome is Data:
  Running{}
  Finished{correct: U32}
  TimedOut{}

type State is Data:
  S{shapes: +List<Shape>, box_x: U32, box_y: U32,
    goal_mode: U32, goal: U32, held: U32, outcome: Outcome}

type Action is Data:
  Wait{}
  Down{ref: U32}
  Move{x: U32, y: U32}
  Up{}
  Submit{}

def svg_offset(kind: Nat) -> U32:
  match kind:
    case 0n: 102 # circle: 17-pixel bbox times 0.6, in tenths
    case 1n: 85  # rectangle: 17 pixels times 0.5
    case _: 68   # triangle: 17 pixels times 0.4

def svg_quantize(coordinate: U32, +offset: U32) -> U32:
  ((((coordinate + offset + 5 : U32) / 10 : U32) * 10 : U32) - offset : U32)

def moved(shape: Shape, x: U32, y: U32) -> Shape:
  Item{+kind, color, old_x, old_y} = shape
  +offset = svg_offset(U32.to_nat(kind))
  Item{kind, color, svg_quantize(x, offset), svg_quantize(y, offset)}

def choose_update(found: Bool, shape: Shape, rest: +List<Shape>,
                  x: U32, y: U32) -> +List<Shape>:
  match found:
    case True{}: moved(shape, x, y) <> rest
    case False{}: shape <> rest

def update(xs: +List<Shape>, +ref: U32, +at: U32,
           +x: U32, +y: U32) -> +List<Shape>:
  match xs:
    case Nil{}: Nil{}
    case Con{shape, rest}:
      choose_update(U32.is_eq(ref, at), shape,
        update(rest, ref, (at + 1 : U32), x, y), x, y)

def inside(+x: U32, +y: U32, +box_x: U32, +box_y: U32,
           +width: U32, +height: U32) -> Bool:
  U32.is_gt(x, (2560 + box_x * 10 : U32)) &&
  U32.is_lt(x, (2560 + (box_x + width : U32) * 10 : U32)) &&
  U32.is_gt(y, (2560 + box_y * 10 : U32)) &&
  U32.is_lt(y, (2560 + (box_y + height : U32) * 10 : U32))

def is_target(mode: U32, kind: U32, color: U32, +goal: U32) -> Bool:
  match mode:
    case 0: U32.is_eq(kind, goal)
    case _: U32.is_eq(color, goal)

def shape_one_result(target: Bool, inside_box: Bool) -> Bool:
  match target:
    case True{}: inside_box
    case False{}: Bool.not(inside_box)

def shape_one(shape: Shape, target: Bool, box_x: U32, box_y: U32) -> Bool:
  Item{kind, color, x, y} = shape
  shape_one_result(target, inside(x, y, box_x, box_y, 80, 45))

def all_one(xs: +List<Shape>, +goal: U32, +box_x: U32, +box_y: U32) -> Bool:
  match xs:
    case Nil{}: True{}
    case Con{Item{+kind, color, x, y}, rest}:
      shape_one(Item{kind, color, x, y}, U32.is_eq(kind, goal), box_x, box_y) &&
        all_one(rest, goal, box_x, box_y)

def shape_two(shape: Shape, target: Bool) -> Bool:
  Item{kind, color, x, y} = shape
  match target:
    case True{}: inside(x, y, 2, 5, 70, 50)
    case False{}: inside(x, y, 84, 5, 70, 50)

def count_two(xs: +List<Shape>, +mode: U32, +goal: U32) -> U32:
  match xs:
    case Nil{}: 0
    case Con{Item{+kind, +color, x, y}, rest}:
      +target = is_target(mode, kind, color, goal)
      (Bool.to_u32(shape_two(Item{kind, color, x, y}, target)) +
        count_two(rest, mode, goal) : U32)

def score_index(task: Nat, s: State) -> U32:
  match task:
    case 5n:
      S{shapes, box_x, box_y, goal_mode, goal, held, outcome} = s
      Bool.pick(U32, all_one(shapes, goal, box_x, box_y), 5, 0)
    case _:
      S{shapes, box_x, box_y, goal_mode, goal, held, outcome} = s
      count_two(shapes, goal_mode, goal)

def finish(s: State, correct: U32) -> State:
  S{shapes, box_x, box_y, goal_mode, goal, held, outcome} = s
  S{shapes, box_x, box_y, goal_mode, goal, held, Finished{correct}}

def move_held(held: U32, s: State, x: U32, y: U32) -> State:
  match held:
    case 0: s
    case _:
      S{shapes, box_x, box_y, mode, goal, +holding, outcome} = s
      S{update(shapes, holding, 1, x, y), box_x, box_y,
        mode, goal, holding, Running{}}

def active(+task: U32, s: State, action: Action) -> State:
  S{+shapes, +box_x, +box_y, +mode, +goal, +held, outcome} = s
  match action:
    case Wait{}: S{shapes, box_x, box_y, mode, goal, held, Running{}}
    case Down{ref}: S{shapes, box_x, box_y, mode, goal, ref, Running{}}
    case Move{x, y}:
      move_held(held, S{shapes, box_x, box_y, mode, goal, held,
        Running{}}, x, y)
    case Up{}: S{shapes, box_x, box_y, mode, goal, 0, Running{}}
    case Submit{}:
      +state = {S{shapes, box_x, box_y, mode, goal, held, Running{}} : State}
      finish(state, score_index(U32.to_nat(task), state))

def active_or_timeout(dead: Bool, task: U32, s: State, action: Action) -> State:
  match dead:
    case False{}: active(task, s, action)
    case True{}:
      S{shapes, box_x, box_y, mode, goal, held, outcome} = s
      S{shapes, box_x, box_y, mode, goal, held, TimedOut{}}

def step(task: U32, s: State, action: Action, now: U32, deadline: U32) -> State:
  match s:
    case S{shapes, box_x, box_y, mode, goal, held, Running{}}:
      active_or_timeout(U32.is_ge(now, deadline), task,
        S{shapes, box_x, box_y, mode, goal, held, Running{}}, action)
    case S{shapes, box_x, box_y, mode, goal, held, Finished{correct}}:
      S{shapes, box_x, box_y, mode, goal, held, Finished{correct}}
    case S{shapes, box_x, box_y, mode, goal, held, TimedOut{}}:
      S{shapes, box_x, box_y, mode, goal, held, TimedOut{}}

def partial(correct: Nat) -> F32:
  match correct:
    case 0n: (0.0 - 1.0 : F32)
    case 1n: (0.0 - 0.54 : F32)
    case 2n: (0.0 - 0.08 : F32)
    case 3n: 0.38
    case 4n: 0.84
    case _: 1.0

def raw(task: U32, outcome: Outcome) -> F32:
  match outcome:
    case Running{}: 0.0
    case TimedOut{}: (0.0 - 1.0 : F32)
    case Finished{+correct}:
      Bool.pick(F32, U32.is_eq(task, 5),
        Bool.pick(F32, U32.is_eq(correct, 5), 1.0, (0.0 - 1.0 : F32)),
        partial(U32.to_nat(correct)))

def timed(task: U32, outcome: Outcome, elapsed: U32, deadline: U32) -> F32:
  match outcome:
    case Running{}: 0.0
    case TimedOut{}: (0.0 - 1.0 : F32)
    case Finished{+correct}:
      +score = raw(task, Finished{correct})
      Bool.pick(F32, U32.is_gt(correct, 2),
        (score * (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32) : F32),
        score)
