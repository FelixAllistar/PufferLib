import Base

# The source uses false for no candidate and 0..8 for a cell. Here 9 is none.
def bit(index: U32) -> U32:
  (1 << U32.to_nat(index) : U32)

def has(mask: U32, index: U32) -> Bool:
  U32.is_ne((mask .&. bit(index) : U32), 0)

def free(occupied: U32, index: U32) -> Bool:
  U32.is_eq((occupied .&. bit(index) : U32), 0)

def candidate(occupied: U32, +mark: U32, target: U32,
              first: U32, second: U32) -> Bool:
  free(occupied, target) && has(mark, first) && has(mark, second)

# Each horizontal helper scans rows from top to bottom and returns its first.
def horizontal(n: Nat, +row: U32, +occupied: U32, +mark: U32,
               +target_col: U32, +first_col: U32, +second_col: U32) -> U32:
  match n:
    case 0n: 9
    case 1n+ +p:
      +base = (row * 3 : U32)
      +target = (base + target_col : U32)
      Bool.pick(U32,
        candidate(occupied, mark, target, (base + first_col : U32),
                  (base + second_col : U32)),
        target,
        horizontal(p, (row + 1 : U32), occupied, mark,
                   target_col, first_col, second_col))

def first_available(+current: U32, next: U32) -> U32:
  Bool.pick(U32, U32.is_lt(current, 9), current, next)

def diagonal(+occupied: U32, +mark: U32) -> U32:
  +a = Bool.pick(U32, candidate(occupied, mark, 0, 4, 8), 0, 9)
  +b = Bool.pick(U32, candidate(occupied, mark, 4, 0, 8), 4, 9)
  +c = Bool.pick(U32, candidate(occupied, mark, 8, 0, 4), 8, 9)
  +d = Bool.pick(U32, candidate(occupied, mark, 2, 4, 6), 2, 9)
  +e = Bool.pick(U32, candidate(occupied, mark, 4, 2, 6), 4, 9)
  +f = Bool.pick(U32, candidate(occupied, mark, 6, 2, 4), 6, 9)
  first_available(a, first_available(b, first_available(c,
    first_available(d, first_available(e, f)))))

def vertical(+occupied: U32, +mark: U32, +col: U32) -> U32:
  +top = col
  +middle = (col + 3 : U32)
  +bottom = (col + 6 : U32)
  +a = Bool.pick(U32, candidate(occupied, mark, top, middle, bottom), top, 9)
  +b = Bool.pick(U32, candidate(occupied, mark, middle, top, bottom), middle, 9)
  +c = Bool.pick(U32, candidate(occupied, mark, bottom, top, middle), bottom, 9)
  first_available(a, first_available(b, c))

# Source builds seven candidates, filters false, then pop()s: last helper wins.
def last_available(old: U32, +next: U32) -> U32:
  Bool.pick(U32, U32.is_lt(next, 9), next, old)

def directions(+occupied: U32, +mark: U32) -> U32:
  +left_h = horizontal(3n, 0, occupied, mark, 0, 1, 2)
  +middle_h = horizontal(3n, 0, occupied, mark, 1, 0, 2)
  +right_h = horizontal(3n, 0, occupied, mark, 2, 0, 1)
  +diag = diagonal(occupied, mark)
  +left_v = vertical(occupied, mark, 0)
  +middle_v = vertical(occupied, mark, 1)
  +right_v = vertical(occupied, mark, 2)
  last_available(last_available(last_available(last_available(
    last_available(last_available(left_h, middle_h), right_h), diag),
    left_v), middle_v), right_v)

def determine(+occupied: U32, omask: U32, xmask: U32) -> U32:
  +winning = directions(occupied, omask)
  +blocking = directions(occupied, xmask)
  Bool.pick(U32, U32.is_lt(winning, 9), winning, blocking)
