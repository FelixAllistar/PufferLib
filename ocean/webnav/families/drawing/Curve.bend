import Base
import ./Points.bend as P

# D3 v4 curveBasis, including the source's >10 px sampling filter.
type Span is Data:
  R{lo: F32, hi: F32}

type Box is Data:
  B{x: Span, y: Span}

def extend(s: Span, +x: F32) -> Span:
  R{lo, hi} = s
  R{F32.min(lo, x), F32.max(hi, x)}

def merge(s: Span, t: Span) -> Span:
  R{lo, hi} = t
  extend(extend(s, lo), hi)

def blend(p: P.Point, q: P.Point, +a: F32, +b: F32, +d: F32) -> P.Point:
  P.P{px, py} = p
  P.P{qx, qy} = q
  P.P{((a * px + b * qx) / d : F32), ((a * py + b * qy) / d : F32)}

def endpoint(p: P.Point, q: P.Point, r: P.Point) -> P.Point:
  P.P{px, py} = p
  P.P{qx, qy} = q
  P.P{rx, ry} = r
  P.P{((px + 4.0 * qx + rx) / 6.0 : F32),
      ((py + 4.0 * qy + ry) / 6.0 : F32)}

def value(p0: F32, p1: F32, p2: F32, p3: F32, +t: F32) -> F32:
  +u = (1.0 - t : F32)
  (u * u * u * p0 + 3.0 * u * u * t * p1 +
   3.0 * u * t * t * p2 + t * t * t * p3 : F32)

def at(valid: Bool, s: Span, p0: F32, p1: F32, p2: F32, p3: F32, t: F32) -> Span:
  match valid:
    case True{}: extend(s, value(p0, p1, p2, p3, t))
    case False{}: s

def root(s: Span, p0: F32, p1: F32, p2: F32, p3: F32, +t: F32) -> Span:
  at(F32.is_gt(t, 0.0) && F32.is_lt(t, 1.0), s, p0, p1, p2, p3, t)

def quadratic(real: Bool, +a: F32, +b: F32, d: F32,
    s: Span, +p0: F32, +p1: F32, +p2: F32, +p3: F32) -> Span:
  match real:
    case False{}: s
    case True{}:
      +radical = F32.sqrt(d)
      root(root(s, p0, p1, p2, p3, ((0.0 - b - radical) / (2.0 * a) : F32)),
        p0, p1, p2, p3, ((0.0 - b + radical) / (2.0 * a) : F32))

def linear(nonzero: Bool, b: F32, c: F32, s: Span,
    p0: F32, p1: F32, p2: F32, p3: F32) -> Span:
  match nonzero:
    case True{}: root(s, p0, p1, p2, p3, ((0.0 - c) / b : F32))
    case False{}: s

def derivative(flat: Bool, +a: F32, +b: F32, +c: F32,
    s: Span, p0: F32, p1: F32, p2: F32, p3: F32) -> Span:
  match flat:
    case True{}: linear(F32.is_gt(F32.abs(b), 0.000001), b, c, s, p0, p1, p2, p3)
    case False{}:
      +d = (b * b - 4.0 * a * c : F32)
      quadratic(F32.is_ge(d, 0.0), a, b, d, s, p0, p1, p2, p3)

def cubic(+p0: F32, +p1: F32, +p2: F32, +p3: F32) -> Span:
  +a = (0.0 - p0 + 3.0 * p1 - 3.0 * p2 + p3 : F32)
  b = (2.0 * (p0 - 2.0 * p1 + p2) : F32)
  c = (p1 - p0 : F32)
  derivative(F32.is_lt(F32.abs(a), 0.000001), a, b, c,
    R{F32.min(p0, p3), F32.max(p0, p3)}, p0, p1, p2, p3)

def segment(box: Box, +start: P.Point, +p: P.Point, +q: P.Point, +r: P.Point) -> Box:
  B{bx, by} = box
  +c1 = blend(p, q, 2.0, 1.0, 3.0)
  +c2 = blend(p, q, 1.0, 2.0, 3.0)
  +end = endpoint(p, q, r)
  B{merge(bx, cubic(P.x(start), P.x(c1), P.x(c2), P.x(end))),
    merge(by, cubic(P.y(start), P.y(c1), P.y(c2), P.y(end)))}

def include(box: Box, point: P.Point) -> Box:
  B{x, y} = box
  P.P{px, py} = point
  B{extend(x, px), extend(y, py)}

def alone(+point: P.Point) -> Box:
  B{R{P.x(point), P.x(point)}, R{P.y(point), P.y(point)}}

def tail(xs: +List<P.Point>, +p: P.Point, +q: P.Point, start: P.Point, box: Box) -> Box:
  match xs:
    case Nil{}: include(segment(box, start, p, q, q), q)
    case Con{+r, rest}:
      tail(rest, q, r, endpoint(p, q, r), segment(box, start, p, q, r))

def bounds(xs: +List<P.Point>) -> Box:
  match xs:
    case Nil{}: alone(P.P{0.0, 0.0})
    case Con{+p, Nil{}}: alone(p)
    case Con{+p, Con{+q, Nil{}}}: include(alone(p), q)
    case Con{+p, Con{+q, rest}}:
      +start = blend(p, q, 5.0, 1.0, 6.0)
      tail(rest, p, q, start, include(alone(p), start))

def replace(xs: +List<P.Point>, point: P.Point) -> +List<P.Point>:
  match xs:
    case Nil{}: [point]
    case Con{old, rest}: point <> rest

def filter_step(far: Bool, +point: P.Point, xs: +List<P.Point>, anchor: P.Point) -> +List<P.Point> & P.Point:
  match far:
    case True{}: (point <> xs, point)
    case False{}: (replace(xs, point), anchor)

def filter(xs: +List<P.Point>, pair: +List<P.Point> & P.Point) -> +List<P.Point>:
  match xs:
    case Nil{}:
      (curve, anchor) = pair
      List.reverse(&2, P.Point, curve)
    case Con{+point, rest}:
      (curve, +anchor) = pair
      filter(rest, filter_step(F32.is_gt(P.squared(point, anchor), 100.0), point, curve, anchor))

def filtered(xs: +List<P.Point>) -> +List<P.Point>:
  match xs:
    case Nil{}: Nil{}
    case Con{+p, rest}: filter(rest, ([p, p], p))

def path_bounds(reversed: +List<P.Point>) -> Box:
  bounds(filtered(List.reverse(&2, P.Point, reversed)))

def width(box: Box) -> F32:
  B{R{lo, hi}, y} = box
  (hi - lo : F32)

def height(box: Box) -> F32:
  B{x, R{lo, hi}} = box
  (hi - lo : F32)
