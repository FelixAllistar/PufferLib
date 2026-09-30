import Base
import ./Points.bend as P
import ./Curve.bend as C

# Preserve the original reward formulas, including draw-line's last-point bug.
def sum_x(xs: +List<P.Point>, acc: F32) -> F32:
  match xs:
    case Nil{}: acc
    case Con{p, rest}: sum_x(rest, (acc + P.x(p) : F32))

def sum_y(xs: +List<P.Point>, acc: F32) -> F32:
  match xs:
    case Nil{}: acc
    case Con{p, rest}: sum_y(rest, (acc + P.y(p) : F32))

def sum_distance(xs: +List<P.Point>, +center: P.Point, acc: F32) -> F32:
  match xs:
    case Nil{}: acc
    case Con{p, rest}:
      sum_distance(rest, center, (acc + F32.sqrt(P.squared(p, center)) : F32))

def sum_variance(xs: +List<P.Point>, +center: P.Point, +mean: F32, acc: F32) -> F32:
  match xs:
    case Nil{}: acc
    case Con{p, rest}:
      +delta = (F32.sqrt(P.squared(p, center)) - mean : F32)
      sum_variance(rest, center, mean, (acc + delta * delta : F32))

def circle_quality(+deviation: F32) -> F32:
  Bool.pick(F32, F32.is_lt(deviation, 2.0), 1.0,
  Bool.pick(F32, F32.is_lt(deviation, 4.0), 0.9,
  Bool.pick(F32, F32.is_lt(deviation, 7.0), 0.7,
  Bool.pick(F32, F32.is_lt(deviation, 10.0), 0.5,
  Bool.pick(F32, F32.is_lt(deviation, 13.0), 0.3,
  Bool.pick(F32, F32.is_lt(deviation, 16.0), 0.1,
  Bool.pick(F32, F32.is_lt(deviation, 19.0), (0.0 - 0.1 : F32),
  Bool.pick(F32, F32.is_lt(deviation, 22.0), (0.0 - 0.3 : F32),
  Bool.pick(F32, F32.is_lt(deviation, 25.0), (0.0 - 0.5 : F32),
  Bool.pick(F32, F32.is_lt(deviation, 28.0), (0.0 - 0.7 : F32),
  Bool.pick(F32, F32.is_lt(deviation, 31.0), (0.0 - 0.9 : F32),
    (0.0 - 1.0 : F32))))))))))))

def size_quality(+box: C.Box) -> F32:
  +small = F32.min(C.width(box), C.height(box))
  Bool.pick(F32, F32.is_lt(small, 5.0), (0.0 - 0.25 : F32),
  Bool.pick(F32, F32.is_lt(small, 10.0), 0.25,
  Bool.pick(F32, F32.is_lt(small, 20.0), 0.5, 1.0)))

def circle_result(+size: F32, quality: F32) -> F32:
  Bool.pick(F32, F32.is_lt(size, 0.0), size, (size * quality : F32))

def circle(+points: +List<P.Point>, +center: P.Point, +n: F32) -> F32:
  mean = (sum_distance(points, center, 0.0) / n : F32)
  deviation = F32.sqrt((sum_variance(points, center, mean, 0.0) / n : F32))
  circle_result(size_quality(C.path_bounds(points)), circle_quality(deviation))

def transverse(+sd: F32) -> F32:
  Bool.pick(F32, F32.is_lt(sd, 1.5), 1.0,
  Bool.pick(F32, F32.is_lt(sd, 2.5), 0.5,
  Bool.pick(F32, F32.is_lt(sd, 4.5), (0.0 - 0.5 : F32),
  Bool.pick(F32, F32.is_lt(sd, 6.0), (0.0 - 0.75 : F32), (0.0 - 1.0 : F32)))))

def longitudinal(+sd: F32) -> F32:
  Bool.pick(F32, F32.is_gt(sd, 5.0), 1.0,
  Bool.pick(F32, F32.is_gt(sd, 4.0), 0.75,
  Bool.pick(F32, F32.is_gt(sd, 2.5), 0.5,
  Bool.pick(F32, F32.is_gt(sd, 1.0), 0.25, 0.1))))

def direction(horizontal: Bool, x: F32, y: F32) -> F32:
  match horizontal:
    case True{}: (longitudinal(x) * transverse(y) : F32)
    case False{}: (longitudinal(y) * transverse(x) : F32)

def nearest(xs: +List<P.Point>, +center: P.Point, acc: F32) -> F32:
  match xs:
    case Nil{}: acc
    case Con{p, rest}: nearest(rest, center, F32.min(acc, F32.sqrt(P.squared(p, center))))

def contact(+d: F32) -> F32:
  Bool.pick(F32, F32.is_lt(d, 6.0), 1.0,
  Bool.pick(F32, F32.is_lt(d, 10.0), 0.75,
  Bool.pick(F32, F32.is_lt(d, 15.0), 0.5,
  Bool.pick(F32, F32.is_lt(d, 20.0), 0.25,
  Bool.pick(F32, F32.is_lt(d, 25.0), 0.1,
  Bool.pick(F32, F32.is_lt(d, 35.0), 0.0,
  Bool.pick(F32, F32.is_lt(d, 45.0), (0.0 - 0.25 : F32),
  Bool.pick(F32, F32.is_lt(d, 55.0), (0.0 - 0.5 : F32), (0.0 - 1.0 : F32)))))))))

def line(+points: +List<P.Point>, last: P.Point, center: P.Point, horizontal: Bool, +n: F32) -> F32:
  P.P{lx, ly} = last
  P.P{cx, cy} = center
  +dx = (lx - sum_x(points, 0.0) / n : F32)
  +dy = (ly - sum_y(points, 0.0) / n : F32)
  d = direction(horizontal, F32.sqrt((dx * dx / n : F32)), F32.sqrt((dy * dy / n : F32)))
  distance = nearest(points, P.P{(cx - 4.0 : F32), (cy - 4.0 : F32)}, 100.0)
  (d * 0.8 + contact(distance) * 0.2 : F32)

def kind(circle_task: Bool, points: +List<P.Point>, last: P.Point, center: P.Point, horizontal: Bool, n: F32) -> F32:
  match circle_task:
    case True{}: circle(points, center, n)
    case False{}: line(points, last, center, horizontal, n)

def score(points: +List<P.Point>, task: U32, center: P.Point, horizontal: Bool) -> F32:
  match points:
    case Nil{}: (0.0 - 1.0 : F32)
    case Con{+last, +rest}:
      +all = {last <> rest : +List<P.Point>}
      kind(U32.is_eq(task, 0), all, last, center, horizontal,
        U32.to_f32(U32.from_nat(List.length(&2, P.Point, all))))
