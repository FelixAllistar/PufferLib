import Base
import ./Terminal.bend as T
import ./TerminalGenerate.bend as G

def prepend(pair: Array<U32> & U32, xs: +List<U32>) -> Array<U32> & +List<U32>:
  (a, x) = pair
  (a, x <> xs)

def read_units(n: Nat, +at: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32> & +List<U32>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, xs) = pair
      read_units(p, at, prepend(a[(at + U32.from_nat(p) : U32)], xs))

def zero(n: Nat, +at: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: zero(p,at,a[(at + U32.from_nat(p) : U32)] <- 0)

def write_units(xs: +List<U32>, +at: U32,
    a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{x,rest}: write_units(rest,(at + 1 : U32),a[at] <- x)

def outcome_index(status: Nat, raw: U32) -> T.Outcome:
  match status:
    case 0n: T.Running{}
    case 1n: T.Finished{U32.is_eq(raw,1065353216)}
    case _: T.TimedOut{}

def outcome(status: U32, raw: U32) -> T.Outcome:
  outcome_index(U32.to_nat(status),raw)

def save_outcome(o: T.Outcome, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case T.Running{}: a
    case T.Finished{+success}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(T.raw(T.Finished{success}))
      a[(base + 11 : U32)] <- F32.bits(T.timed(T.Finished{success},elapsed,deadline))
    case T.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(s: T.State, elapsed: U32, deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  T.S{files,active,expected,+command,focused,output,o} = s
  +size = U32.from_nat(List.length(&2,U32,command))
  a = a[(base + 2 : U32)] <- 0
  a = a[(base + 34 : U32)] <- Bool.to_u32(focused)
  a = a[(base + 35 : U32)] <- size
  a = a[(base + 36 : U32)] <- output
  a = a[(base + 40 : U32)] <- active
  a = write_units(command,(base + 2048 : U32),
    zero(128n,(base + 2048 : U32),a))
  save_outcome(o,elapsed,deadline,base,a)

def decoded(kind: Nat, payload: +List<U32>) -> T.Action:
  match kind:
    case 1n: T.Focus{}
    case 2n: T.Insert{payload}
    case 3n: T.Backspace{}
    case 10n: T.Enter{}
    case _: T.Wait{}

def after_payload(pair: Array<U32> & +List<U32>,
    files: +List<T.File>, active: U32, expected: U32,
    command: +List<U32>, focused: U32, output: U32,
    kind: U32, status: U32, raw: U32,
    +elapsed: U32, +deadline: U32,
    +base: U32) -> Array<U32>:
  (a,payload) = pair
  s = {T.S{files,active,expected,command,U32.is_eq(focused,1),
    output,outcome(status,raw)} : T.State}
  save(T.step(s,decoded(U32.to_nat(kind),payload),elapsed,deadline),
    elapsed,deadline,base,a)

def after_command(pair: Array<U32> & +List<U32>,
    files: +List<T.File>, active: U32, expected: U32,
    focused: U32, output: U32, kind: U32,
    payload_len: U32, status: U32, raw: U32,
    elapsed: U32, deadline: U32,
    +base: U32) -> Array<U32>:
  (a,command) = pair
  after_payload(read_units(U32.to_nat(payload_len),
    (base + 4096 : U32),(a,Nil{})),files,active,expected,
    command,focused,output,kind,status,raw,elapsed,deadline,base)

def after_files(pair: Array<U32> & +List<T.File>,
    active: U32, expected: U32, focused: U32,
    command_len: U32, output: U32, kind: U32,
    payload_len: U32, status: U32, raw: U32,
    elapsed: U32, deadline: U32,
    +base: U32) -> Array<U32>:
  (a,files) = pair
  after_command(read_units(U32.to_nat(command_len),
    (base + 2048 : U32),(a,Nil{})),files,active,expected,
    focused,output,kind,payload_len,status,raw,
    elapsed,deadline,base)

def file_name(pair: Array<U32> & +List<U32>,
    ext: U32, index: U32,
    files: +List<T.File>) -> Array<U32> & +List<T.File>:
  (a,name) = pair
  (a,T.F{name,ext,index} <> files)

def file_length(pair: Array<U32> & U32,
    ext: U32, +index: U32, files: +List<T.File>,
    +base: U32) -> Array<U32> & +List<T.File>:
  (a,size) = pair
  file_name(read_units(U32.to_nat(size),
    (base + 512 + index * 128 : U32),(a,Nil{})),
    ext,index,files)

def file_ext(pair: Array<U32> & U32,
    +index: U32, files: +List<T.File>,
    +base: U32) -> Array<U32> & +List<T.File>:
  (a,ext) = pair
  file_length(a[(base + 56 + index : U32)],
    ext,index,files,base)

def load_files(n: Nat, +base: U32,
    pair: Array<U32> & +List<T.File>) -> Array<U32> & +List<T.File>:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a,files) = pair
      +index = U32.from_nat(p)
      load_files(p,base,
        file_ext(a[(base + 48 + index : U32)],index,files,base))

def metadata(pair: Array<U32> & +List<U32>,
    kind: U32, payload_len: U32, status: U32,
    raw: U32, elapsed: U32, deadline: U32,
    +base: U32) -> Array<U32>:
  (a,fields) = pair
  match fields:
    case Con{expected,Con{count,Con{focused,
         Con{command_len,Con{output,Con{unused0,
         Con{unused1,Con{unused2,Con{active,Nil{}}}}}}}}}}:
      after_files(load_files(U32.to_nat(count),base,
        (a,Nil{})),active,expected,focused,command_len,
        output,kind,payload_len,status,raw,
        elapsed,deadline,base)
    case _: a

def header(pair: Array<U32> & +List<U32>,
    +base: U32) -> Array<U32>:
  (a,fields) = pair
  match fields:
    case Con{kind,Con{ref,Con{payload_len,Con{arg1,
         Con{elapsed,Con{status,Con{raw,Con{timed,
         Con{deadline,Nil{}}}}}}}}}}:
      metadata(read_units(9n,(base + 32 : U32),
        (a,Nil{})),kind,payload_len,status,raw,
        elapsed,deadline,base)
    case _: a

def run(+base: U32, a: Array<U32>) -> Array<U32>:
  header(read_units(9n,(base + 4 : U32),(a,Nil{})),base)

def reset_seed(pair: Array<U32> & U32,
    +base: U32) -> Array<U32>:
  (a,seed) = pair
  G.generate(4,seed,base,a)

def reset(+base: U32,a: Array<U32>) -> Array<U32>:
  reset_seed(a[(base + 3 : U32)],base)
