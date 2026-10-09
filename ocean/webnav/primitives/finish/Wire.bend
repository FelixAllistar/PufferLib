import Base
import ./Model.bend as M
import ../transport/Words.bend as W

# Private v1 transport; never a policy observation.
# 0 version,1 phase,2 kind,3 status,4 data length,5 detail length,8 command.
# Candidate metadata at16..19. Committed data/detail at64/16448;
# candidate data/detail at32768/49152. Limits are16384/2048 UTF-8 JSON bytes.
# Commands:0 wait,1 submit,2 expire,3 reset. Save clears staging and padding.
def kind(code: Nat) -> M.TaskType:
  match code:
    case 1n: M.Mutate{}
    case 2n: M.Retrieve{}
    case _: M.Navigate{}

def status(code: Nat) -> M.Status:
  match code:
    case 1n: M.ActionNotAllowed{}
    case 2n: M.PermissionDenied{}
    case 3n: M.NotFound{}
    case 4n: M.DataValidation{}
    case 5n: M.UnknownError{}
    case _: M.Success{}

def kind_code(kind_: M.TaskType) -> U32:
  match kind_:
    case M.Navigate{}: 0
    case M.Mutate{}: 1
    case M.Retrieve{}: 2

def status_code(status_: M.Status) -> U32:
  match status_:
    case M.Success{}: 0
    case M.ActionNotAllowed{}: 1
    case M.PermissionDenied{}: 2
    case M.NotFound{}: 3
    case M.DataValidation{}: 4
    case M.UnknownError{}: 5

def read_detail(kind_: U32, status_: U32, data: +List<U32>, pair: Array<U32> & +List<U32>) -> Array<U32> & M.Reply:
  (a, detail) = pair
  (a, M.R{kind(U32.to_nat(kind_)), status(U32.to_nat(status_)), data, detail})

def read_data(kind_: U32, status_: U32, detail_len: U32, detail_at: U32,
    pair: Array<U32> & +List<U32>) -> Array<U32> & M.Reply:
  (a, data) = pair
  read_detail(kind_, status_, data, W.read(U32.to_nat(detail_len), detail_at, (a, Nil{})))

def read_reply(+header: +List<U32>, +base: Nat, data_at: U32, detail_at: U32,
    a: Array<U32>) -> Array<U32> & M.Reply:
  read_data(W.at(header, base), W.at(header, (base + 1n : Nat)),
    W.at(header, (base + 3n : Nat)), detail_at,
    W.read(U32.to_nat(W.at(header, (base + 2n : Nat))), data_at, (a, Nil{})))

def clear_blocks(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+p: clear_blocks(p, (base + 512 : U32), W.clear(512n, base, a))

def save_reply(reply_: M.Reply, a: Array<U32>) -> Array<U32>:
  M.R{kind_, status_, +data, +detail} = reply_
  a = a[1] <- 1
  a = a[2] <- kind_code(kind_)
  a = a[3] <- status_code(status_)
  a = a[4] <- U32.from_nat(List.length(&2, U32, data))
  a = a[5] <- U32.from_nat(List.length(&2, U32, detail))
  W.write(detail, 16448, W.write(data, 64, a))

def save_clean(s: M.State, a: Array<U32>) -> Array<U32>:
  match s:
    case M.Open{}: a
    case M.Submitted{reply_}: save_reply(reply_, a)
    case M.Expired{}: a[1] <- 2

def save(s: M.State, a: Array<U32>) -> Array<U32>:
  a = clear_blocks(128n, 0, a)
  a = a[0] <- 1
  save_clean(s, a)

def submit(s: M.State, pair: Array<U32> & M.Reply) -> Array<U32>:
  (a, reply_) = pair
  save(M.step(s, M.Submit{reply_}), a)

def apply(code: Nat, s: M.State, header: +List<U32>, a: Array<U32>) -> Array<U32>:
  match code:
    case 1n: submit(s, read_reply(header, 16n, 32768, 49152, a))
    case 2n: save(M.step(s, M.Expire{}), a)
    case _: save(M.step(s, M.Wait{}), a)

def with_old(+header: +List<U32>, pair: Array<U32> & M.Reply) -> Array<U32>:
  (a, reply_) = pair
  apply(U32.to_nat(W.at(header, 8n)), M.Submitted{reply_}, header, a)

def load_state(phase: Nat, +header: +List<U32>, a: Array<U32>) -> Array<U32>:
  match phase:
    case 1n: with_old(header, read_reply(header, 2n, 64, 16448, a))
    case 2n: apply(U32.to_nat(W.at(header, 8n)), M.Expired{}, header, a)
    case _: apply(U32.to_nat(W.at(header, 8n)), M.Open{}, header, a)

def dispatch(code: Nat, +header: +List<U32>, a: Array<U32>) -> Array<U32>:
  match code:
    case 3n: save(M.Open{}, a)
    case _: load_state(U32.to_nat(W.at(header, 1n)), header, a)

def loaded(pair: Array<U32> & +List<U32>) -> Array<U32>:
  (a, +header) = pair
  dispatch(U32.to_nat(W.at(header, 8n)), header, a)

def run(a: Array<U32>) -> Array<U32>:
  loaded(W.read(20n, 0, (a, Nil{})))
