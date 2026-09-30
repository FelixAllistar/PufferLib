import Base

type Point is Data:
  P{x: F32, y: F32}

def x(p: Point) -> F32:
  P{a, b} = p
  a

def y(p: Point) -> F32:
  P{a, b} = p
  b

def squared(p: Point, q: Point) -> F32:
  P{px, py} = p
  P{qx, qy} = q
  +dx = (px - qx : F32)
  +dy = (py - qy : F32)
  (dx * dx + dy * dy : F32)
