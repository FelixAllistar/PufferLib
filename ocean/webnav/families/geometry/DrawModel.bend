import Base

type Task is Data:
  Bisect{}
  Center{}
  Midpoint{}
  Right{}

type Point is Data:
  P{x: F32, y: F32}

type Outcome is Data:
  Running{}
  Finished{reward: F32, scaled: Bool}
  TimedOut{}

# Circle: a=center, radius=source radius. Midpoint/right: a,b are the
# initially drawn points (b is the right-angle vertex). Bisect: a,b are
# black endpoints, c is the initially blue vertex. All coordinates are SVG
# circle centers; the original getBBox offsets enter only the score helpers.
type State is Data:
  S{task: Task, a: Point, b: Point, c: Point, radius: F32,
    has_user: Bool, user: Point, outcome: Outcome}

type Action is Data:
  Wait{}
  Click{point: Point}
  Submit{}

def make_point(+x: U32, +y: U32) -> Point:
  P{U32.to_f32(x), U32.to_f32(y)}

def fixed_point(x: U32, y: U32) -> Point:
  # Chromium's source MouseEvent.pageX/pageY truncate dispatched subpixels.
  P{U32.to_f32((x / 256 : U32)),
    U32.to_f32((y / 256 : U32))}

def distance(p: Point, q: Point) -> F32:
  P{px, py} = p
  P{qx, qy} = q
  F32.hypot((px - qx : F32), (py - qy : F32))

def shift(p: Point, +offset: F32) -> Point:
  P{x, y} = p
  P{(x + offset : F32), (y + offset : F32)}

def midpoint(p: Point, q: Point) -> Point:
  P{px, py} = p
  P{qx, qy} = q
  P{((px + qx : F32) / 2.0 : F32),
    ((py + qy : F32) / 2.0 : F32)}

# The pinned JavaScript uses law-of-cosines on SVG getBBox top-left points.
# Equal radii cancel for bisect/midpoint; right-angle's user r3 versus source
# r3.5 shifts the effective user point by +0.5 in both axes.
def angle(+a: Point, +b: Point, +vertex: Point) -> F32:
  +av = distance(a, vertex)
  +bv = distance(b, vertex)
  ab = distance(a, b)
  +ratio = ((F32.square(bv) + F32.square(av) - F32.square(ab) : F32) /
    (2.0 * bv * av : F32) : F32)
  ((F32.acos(ratio) * 180.0 : F32) / 3.141592653589793 : F32)

def finish_near(within: Bool, dist: F32, +limit: F32) -> Outcome:
  match within:
    case True{}: Finished{((limit - dist : F32) / limit : F32), True{}}
    case False{}: Finished{(0.0 - 1.0 : F32), False{}}

def center_score(center: Point, +radius: F32, user: Point) -> Outcome:
  # The page compares blue getBBox (user center−3.5) to center−3.
  +dist = distance(user, shift(center, 0.5))
  finish_near(F32.is_lt(dist, radius), dist, radius)

def midpoint_score(a: Point, b: Point, user: Point) -> Outcome:
  +dist = distance(user, midpoint(a, b))
  finish_near(F32.is_lt(dist, 30.0), dist, 30.0)

def right_result(within: Bool, diff: F32) -> Outcome:
  match within:
    case True{}: Finished{((90.0 - diff : F32) / 90.0 : F32), True{}}
    case False{}: Finished{(0.0 - 1.0 : F32), False{}}

def right_score(a: Point, vertex: Point, user: Point) -> Outcome:
  +degrees = angle(a, shift(user, 0.5), vertex)
  +diff = F32.abs((90.0 - degrees : F32))
  right_result(F32.is_lt(diff, 45.0), diff)

def bisect_result(outside: Bool, +total: F32, larger: F32) -> Outcome:
  match outside:
    case True{}: Finished{(0.0 - 1.0 : F32), False{}}
    case False{}:
      Finished{((total - larger : F32) / (total / 2.0 : F32) : F32), True{}}

def bisect_score(+a: Point, +b: Point, +vertex: Point,
    +user: Point) -> Outcome:
  +total = angle(a, b, vertex)
  +angle_a = angle(a, user, vertex)
  +angle_b = angle(b, user, vertex)
  +outside = F32.is_gt(angle_a, total) || F32.is_gt(angle_b, total)
  bisect_result(outside, total, F32.max(angle_a, angle_b))

def score(task: Task, a: Point, b: Point, c: Point,
    radius: F32, user: Point) -> Outcome:
  match task:
    case Center{}: center_score(a, radius, user)
    case Midpoint{}: midpoint_score(a, b, user)
    case Right{}: right_score(a, b, user)
    case Bisect{}: bisect_score(a, b, c, user)

def submitted(has_user: Bool, task: Task, a: Point, b: Point,
    c: Point, radius: F32, user: Point) -> Outcome:
  match has_user:
    case False{}: Finished{(0.0 - 1.0 : F32), False{}}
    case True{}: score(task, a, b, c, radius, user)

def active(action: Action, s: State) -> State:
  match action s:
    case Wait{} _: s
    case Click{point} S{task, a, b, c, radius, has_user, user, outcome}:
      S{task, a, b, c, radius, True{}, point, outcome}
    case Submit{} S{+task, +a, +b, +c, +radius, +has_user, +user, outcome}:
      S{task, a, b, c, radius, has_user, user,
        submitted(has_user, task, a, b, c, radius, user)}

def expire(s: State) -> State:
  S{task, a, b, c, radius, has_user, user, outcome} = s
  S{task, a, b, c, radius, has_user, user, TimedOut{}}

def guard(expired: Bool, s: State, action: Action) -> State:
  match expired:
    case True{}: expire(s)
    case False{}: active(action, s)

def step(s: State, action: Action, elapsed: U32, deadline: U32) -> State:
  match s:
    case S{task, a, b, c, radius, has_user, user, Running{}}:
      guard(U32.is_ge(elapsed, deadline),
        S{task, a, b, c, radius, has_user, user, Running{}}, action)
    case S{task, a, b, c, radius, has_user, user, Finished{reward, scaled}}:
      S{task, a, b, c, radius, has_user, user, Finished{reward, scaled}}
    case S{task, a, b, c, radius, has_user, user, TimedOut{}}:
      S{task, a, b, c, radius, has_user, user, TimedOut{}}

def raw(outcome: Outcome) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{reward, scaled}: reward
    case TimedOut{}: (0.0 - 1.0 : F32)

def timed_finished(scaled: Bool, reward: F32,
    elapsed: U32, deadline: U32) -> F32:
  match scaled:
    case True{}:
      (reward * F32.max(0.0,
        (1.0 - U32.to_f32(elapsed) / U32.to_f32(deadline) : F32)) : F32)
    case False{}: reward

def timed(outcome: Outcome, elapsed: U32, deadline: U32) -> F32:
  match outcome:
    case Running{}: 0.0
    case Finished{reward, scaled}: timed_finished(scaled, reward, elapsed, deadline)
    case TimedOut{}: (0.0 - 1.0 : F32)
