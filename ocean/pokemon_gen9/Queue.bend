import Base
import ./Mem.bend as S
import ./Lists.bend as L
import ./Catalog.bend as C
import ./GenRng.bend as R

# Shared ordering kernel for actions and effect listeners. Keys are:
# order ascending (zero is unspecified/last), priority descending,
# speed descending, subOrder ascending, effectOrder ascending.
# Priority is signed tenths +32768; speed is signed halves +32768;
# subOrder is signed integer +32768. Payload fields survive swaps unchanged.
type Entry is Data:
  E{id: U32, order: U32, priority: U32, speed: U32, sub_order: U32, effect_order: U32}

def row(index: U32) -> U32:
  (2176 + index * 8 : U32)

def slot(base: U32, index: U32) -> U32:
  (base + index * 8 : U32)

def decode(+xs: +List<U32>) -> Entry:
  E{L.at(xs, 0n), L.at(xs, 1n), L.at(xs, 2n), L.at(xs, 3n), L.at(xs, 4n), L.at(xs, 5n)}

def load(base: U32, index: U32) -> S.Mem<Entry>:
  do S.Mem<Entry>:
    xs : +List<U32> <- C.read_list(6n, slot(base,index))
    return decode(xs)

def compare(a: Entry, b: Entry) -> Cmp:
  E{_, +ao, +ap, +as_, +au, ae} = a
  E{_, +bo, +bp, +bs_, +bu, be} = b
  Bool.pick(Cmp, U32.is_ne(ao, bo),
    Bool.pick(Cmp, U32.is_eq(ao, 0), GT{},
      Bool.pick(Cmp, U32.is_eq(bo, 0), LT{}, U32.cmp(ao, bo))),
    Bool.pick(Cmp, U32.is_ne(ap, bp), U32.cmp(bp, ap),
      Bool.pick(Cmp, U32.is_ne(as_, bs_), U32.cmp(bs_, as_),
        Bool.pick(Cmp, U32.is_ne(au, bu), U32.cmp(au, bu), U32.cmp(ae, be)))))

# Source fast-exit and hit/hazard comparators use stable Array.sort, with no
# shuffle. Extra field 6 is holder ability effectOrder+1 (zero absent), while
# field 7 is the event target index. Normal speedSort ignores both fields.
def compare_special(mode: Nat, a: Entry, b: Entry, +ak: U32, +bk: U32, ai: U32, bi: U32) -> Cmp:
  match mode:
    case 1n:
      E{_, _, +ap, +as_, _, _} = a
      E{_, _, +bp, +bs_, _, _} = b
      Bool.pick(Cmp, U32.is_ne(ap, bp), U32.cmp(bp, ap),
        Bool.pick(Cmp, U32.is_ne(as_, bs_), U32.cmp(bs_, as_),
          Bool.pick(Cmp, U32.is_ne(ak, 0) && U32.is_ne(bk, 0), U32.cmp(ak, bk), EQ{})))
    case 2n:
      E{_, +ao, +ap, _, _, _} = a
      E{_, +bo, +bp, _, _, _} = b
      Bool.pick(Cmp, U32.is_ne(ao, bo),
        Bool.pick(Cmp, U32.is_eq(ao, 0), GT{},
          Bool.pick(Cmp, U32.is_eq(bo, 0), LT{}, U32.cmp(ao, bo))),
        Bool.pick(Cmp, U32.is_ne(ap, bp), U32.cmp(bp, ap), U32.cmp(ai, bi)))
    case _: compare(a, b)

def compare_rows(mode: Nat, +base: U32, +a: U32, +b: U32) -> S.Mem<Cmp>:
  do S.Mem<Cmp>:
    left : Entry <- load(base,a)
    right : Entry <- load(base,b)
    ak : U32 <- S.get((slot(base,a) + 6 : U32))
    bk : U32 <- S.get((slot(base,b) + 6 : U32))
    ai : U32 <- S.get((slot(base,a) + 7 : U32))
    bi : U32 <- S.get((slot(base,b) + 7 : U32))
    return compare_special(mode, left, right, ak, bk, ai, bi)

def store(xs: +List<U32>, +at: U32) -> S.Mem<Unit>:
  match xs:
    case Nil{}: S.Mem.pure(Unit, Unit{})
    case Con{x, rest}:
      do S.Mem<Unit>:
        S.put(at, x)
        store(rest, (at + 1 : U32))

def swap(+base: U32, +a: U32, +b: U32) -> S.Mem<Unit>:
  S.when(U32.is_ne(a, b),
    (do S.Mem<Unit>:
      left : +List<U32> <- C.read_list(8n, slot(base,a))
      right : +List<U32> <- C.read_list(8n, slot(base,b))
      store(right, slot(base,a))
      store(left, slot(base,b))))

def scan(n: Nat, +base: U32, +index: U32, +best: Entry, +indexes: +List<U32>) -> S.Mem<+List<U32>>:
  match n:
    case 0n: S.Mem.pure(+List<U32>, indexes)
    case 1n+p:
      do S.Mem<+List<U32>>:
        +candidate : Entry <- load(base,index)
        +cmp : Cmp = compare(best, candidate)
        next : Entry = Bool.pick(Entry, Cmp.is_gt(cmp), candidate, best)
        next_indexes : +List<U32> = L.pick_list(Cmp.is_gt(cmp), [index],
          L.pick_list(Cmp.is_eq(cmp), L.append(indexes, [index]), indexes))
        scan(p,base, (index + 1 : U32), next, next_indexes)

def gather(indexes: +List<U32>, +base: U32, +at: U32) -> S.Mem<Unit>:
  match indexes:
    case Nil{}: S.Mem.pure(Unit, Unit{})
    case Con{index, rest}:
      do S.Mem<Unit>:
        swap(base,at, index)
        gather(rest,base, (at + 1 : U32))

def shuffle(n: Nat, +base: U32, +at: U32, +end: U32) -> S.Mem<Unit>:
  match n:
    case 0n: S.Mem.pure(Unit, Unit{})
    case 1n+p:
      do S.Mem<Unit>:
        draw : U32 <- R.random((end - at : U32))
        swap(base,at, (at + draw : U32))
        shuffle(p,base, (at + 1 : U32), end)

def sort_fuel(n: Nat, +base: U32, +pos: U32, +size: U32) -> S.Mem<Unit>:
  match n:
    case 0n: S.Mem.pure(Unit, Unit{})
    case 1n+p:
      S.when(U32.is_lt((pos + 1 : U32), size),
        (do S.Mem<Unit>:
          best : Entry <- load(base,pos)
          +indexes : +List<U32> <- scan(U32.to_nat((size - pos - 1 : U32)),
            base,(pos + 1 : U32), best, [pos])
          +count : U32 = L.length(indexes)
          gather(indexes,base, pos)
          S.when(U32.is_gt(count, 1), shuffle(U32.to_nat((count - 1 : U32)),base, pos, (pos + count : U32)))
          sort_fuel(p,base, (pos + count : U32), size)))

def sort_buffer(+base: U32, +size: U32) -> S.Mem<Unit>:
  S.branch(Unit, U32.is_le(size,64),sort_fuel(U32.to_nat(size),base,0,size),S.raise(Unit,3))

def insert(n: Nat, +base: U32, +at: U32, +mode: Nat) -> S.Mem<Unit>:
  match n:
    case 0n: S.Mem.pure(Unit, Unit{})
    case 1n+p:
      do S.Mem<Unit>:
        cmp : Cmp <- compare_rows(mode,base, (at - 1 : U32), at)
        S.when(Cmp.is_gt(cmp),
          (do S.Mem<Unit>:
            swap(base,(at - 1 : U32), at)
            insert(p,base, (at - 1 : U32), mode)))

def stable_fuel(n: Nat, +base: U32, +at: U32, +mode: Nat) -> S.Mem<Unit>:
  match n:
    case 0n: S.Mem.pure(Unit, Unit{})
    case 1n+p:
      do S.Mem<Unit>:
        insert(U32.to_nat(at),base, at, mode)
        stable_fuel(p,base, (at + 1 : U32), mode)

def sort_mode_valid(mode: Nat, base: U32, +size: U32) -> S.Mem<Unit>:
  match mode:
    case 0n: sort_buffer(base,size)
    case 1n: stable_fuel(U32.to_nat((U32.max(size, 1) - 1 : U32)),base, 1, 1n)
    case 2n: stable_fuel(U32.to_nat((U32.max(size, 1) - 1 : U32)),base, 1, 2n)
    case _: S.raise(Unit, 3)

def sort_mode_buffer(mode: Nat, +base: U32, +size: U32) -> S.Mem<Unit>:
  S.branch(Unit,U32.is_le(size,64) && U32.is_ge(base,2176) && U32.is_le(base,15872) &&
    U32.is_eq((base % 8 : U32),0),sort_mode_valid(mode,base,size),S.raise(Unit,3))

def sort(+size: U32) -> S.Mem<Unit>:
  sort_mode_buffer(0n,2176,size)

def sort_mode(mode: Nat, size: U32) -> S.Mem<Unit>:
  sort_mode_buffer(mode,2176,size)
